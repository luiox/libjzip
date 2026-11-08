.. _model:

libjzip 数据模型与类关系
========================

状态：定稿。libjzip（C++）的类型体系与类关系，对照 LL-Java-Zip v2.9.0 设计。 算法流程见 :doc:`algorithm`。

1. 整体模块结构
---------------

.. code-block:: text

   zip_io.hpp          门面
   zip_reader.hpp      结构化读取器
   zip_model.hpp       ZIP 结构模型
   zip_writer.hpp      结构化写入器
   zip_mapper.hpp      结构映射器
   zip_adapting_reader.hpp  委托 ca::zip 的适配读取器
   decompressor.hpp    解压与压缩
   memory_segment.hpp  只读字节视图
   zip_anomaly.hpp     结构异常报告（ZipAnomalySink）
   zip64_support.hpp   ZIP64 字段解析
   zip_corruptor.hpp   归档损坏注入器（测试造数）
   zip_type_traits.hpp 无 RTTI 类型分派

.. _model-interfaces:

2. 接口层次体系
---------------

核心接口五族：读取（``ZipReader``、``ZipPartAllocator``）、模型（``ZipPart``）、 写入（``ZipWriter``）、变换（``ZipPartMapper``）、压缩（``Decompressor``）。

.. _model-zip-part:

2.1 ZipPart 接口
^^^^^^^^^^^^^^^^

所有 ZIP 结构体的根抽象，派生三类实体：

.. code-block:: text

   ZipPart (interface)
   ├── AbstractZipFileHeader
   │   ├── LocalFileHeader
   │   │   └── JvmLocalFileHeader
   │   └── CentralDirectoryFileHeader
   └── EndOfCentralDirectory

字段（``AbstractZipFileHeader``）与 Java 原版的类型对照：

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 字段
     - Java ``AbstractZipFileHeader``
     - C++ ``AbstractZipFileHeader``
   * - ``versionNeededToExtract``
     - ``int``
     - ``ca::u16``
   * - ``generalPurposeBitFlag``
     - ``int``
     - ``ca::u16``
   * - ``compressionMethod``
     - ``int``
     - ``ca::u16``
   * - ``lastModFileTime``
     - ``int``
     - ``ca::u16``
   * - ``lastModFileDate``
     - ``int``
     - ``ca::u16``
   * - ``crc32``
     - ``int``
     - ``ca::u32``
   * - ``compressedSize``
     - ``long`` (4-byte LE→``int``)
     - ``ca::i64``
   * - ``uncompressedSize``
     - ``long``
     - ``ca::i64``
   * - ``fileNameLength``
     - ``int``
     - ``ca::u16``
   * - ``extraFieldLength``
     - ``int``
     - ``ca::u16``
   * - ``fileName``
     - ``StringData``
     - ``std::string``
   * - ``extraField``
     - ``MemorySegmentData``
     - ``std::vector<ca::u8>``
   * - ``offset``
     - ``long`` (transient)
     - ``ca::usize``

.. _model-zip-reader:

2.2 ZipReader 接口
^^^^^^^^^^^^^^^^^^

C++ 简化了继承链，三个 reader 均直接实现 ``ZipReader`` 接口（Java 原版有 ``AbstractZipReader`` 中间层及依赖 ``java.util.zip.ZipFile`` 的 ``AdaptingZipReader``）：

.. code-block:: text

   ZipReader (interface)
   ├── ForwardScanZipReader
   ├── JvmZipReader
   └── NaiveLocalFileZipReader

各 reader 的读取算法见 :ref:`algorithm-readers`。

.. _model-allocator:

2.3 ZipPartAllocator 接口（工厂模式）
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

控制 ``ZipPart`` 子类型的实例化策略：

.. code-block:: text

   ZipPartAllocator (interface)
   ├── SimpleZipPartAllocator      生产 LocalFileHeader
   ├── JvmZipPartAllocator         生产 JvmLocalFileHeader
   └── DelegatingZipPartAllocator

``JvmZipPartAllocator`` 与 ``JvmZipReader`` 配对——它生产 ``JvmLocalFileHeader``， 后者重写了 ``readFileData()`` 方法以实现「扫描到下一个头」语义。

.. _model-mapper:

2.4 ZipPartMapper 接口（装饰器模式）
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

C++ 的映射入口是单一 ``map(archive, part)``（Java 原版为 ``mapLocal`` / ``mapCentral`` 两个方法，C++ 按 ``PartType`` 在实现内区分）：

.. code-block:: text

   ZipPartMapper (interface)      map(archive, part)
   ├── IdentityZipPartMapper
   ├── CopyingZipPartMapper
   ├── DelegatingZipPartMapper    先委派再自身；无委派等价 Identity
   │   ├── CentralAdoptingMapper
   │   └── JvmClassDirectoryMapper

委派映射器只存 delegate 指针：delegate 必须活得比实例久，临时对象仅在同一完整 表达式内安全。

.. _model-zip-writer:

2.5 ZipWriter 接口
^^^^^^^^^^^^^^^^^^

.. code-block:: text

   ZipWriter (interface)          write(archive, stream)
   ├── DirectZipWriter
   ├── RecompressingZipWriter
   └── DelegatingZipWriter

.. _model-archive:

3. 核心数据模型
---------------

3.1 ZipArchive（容器）
^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

   ZipArchive
   ├── List<ZipPart>
   │   ├── LocalFileHeader (n 个)
   │   ├── CentralDirectoryFileHeader (n 个)
   │   └── EndOfCentralDirectory (0/1 个)
   └── MemorySegment prefixData

   LOC --getLinkedDirectoryFileHeader()--> CEN
   CEN --getLinkedFileHeader()--> LOC

关键约定：

- Parts 列表未排序时是无序的（由读取器添加顺序决定）
- ``getLocalFiles()`` 和 ``getCentralDirectories()`` 每次遍历全部 parts，按 ``PartType`` 过滤
- 排序后：``LOC(0) < LOC(1) < ... < CEN(0) < CEN(1) < ... < END``
- C++ 版模型不持有文件句柄：输入是 ``MemorySegment``，条目数据在解析时复制， ``prefixData()`` 是输入段的切片视图；输入由裸指针构造时，caller 需保证它比 archive 活得久

.. _model-link:

3.2 双向链接（CEN↔LOC）
^^^^^^^^^^^^^^^^^^^^^^^

CEN 与 LOC 各持对方指针，链接生命周期：

.. code-block:: text

   ZipIO::readJvm(data)
     → JvmZipReader.read()
       → 解码 CEN
       → 创建 LOC (JvmLocalFileHeader)
       → cen.link(loc)      // CEN 持有 LOC 指针
       → loc.link(cen)      // LOC 持有 CEN 指针
       → loc.adoptLinkedCentralDirectoryValues()  // 用 CEN 覆盖 LOC

CEN 侧的 ``getRelativeOffsetOfLocalHeader()`` 决定 LOC 的位置；LOC 侧以 ``getFileData()`` 承载条目数据。链接的使用方（CentralAdoptingMapper、 ``hasDifferentValuesThanCentralDirectoryHeader()``、 ``adoptLinkedCentralDirectoryValues()``）见 :ref:`algorithm-link`。

3.3 数据流的层级
^^^^^^^^^^^^^^^^

.. code-block:: text

   byte[] / MemorySegment
     → ZipIO::read → ZipArchive
       → getLocalFiles() → LocalFileHeader → getFileData() → fileData
       → ZipIO::decompressLocalFile → 解压后字节数组

``MemorySegment`` 是只读视图，不拥有外部缓冲区；从 ``std::vector`` 构造时会复制 一份并共享所有权。解析时每个条目的 ``fileData`` 通过 ``toByteArray()`` 复制进 ``std::vector``，所以条目数据不依赖输入缓冲区；``prefixData()`` 仍是输入段的切片。 ``decompress()`` 与写回各自产生新的字节数组。

.. _model-classes:

4. 核心类详析
-------------

.. _model-lfh-vs-jvm:

4.1 LocalFileHeader vs JvmLocalFileHeader
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

   LocalFileHeader（standard 语义）
   ├── readFileData(): 用 LOC 的 compressedSize 切片
   └── fileData = data[offset, offset + compressedSize]

   JvmLocalFileHeader（JVM 语义）
   ├── setOffsets(entryOffsets): 传入所有已知偏移
   ├── readFileData(): 扫描到下一个已知偏移确定数据边界（ceiling）
   ├── bit 3 且显式大小可用时把数据结束收敛为 dataStart + explicitSize
   └── adoptLinkedCentralDirectoryValues: CEN 权威重对齐
       fileData = 按 CEN 的权威大小（stored 取 uncompressedSize，其余取
       compressedSize）切片；显式大小为 0 时强制对齐为空数据

边界与重对齐的完整规则见 :ref:`algorithm-scan-ceiling` 与 :ref:`algorithm-adopt-cen`。

4.2 CentralDirectoryFileHeader 独有字段
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 字段
     - 类型
     - 用途
   * - ``versionMadeBy``
     - ``ca::u16``
     - 创建该 ZIP 的软件版本
   * - ``fileCommentLength``
     - ``ca::u16``
     - 文件注释长度
   * - ``fileComment``
     - ``std::string``
     - 文件注释
   * - ``diskNumberStart``
     - ``ca::u16``
     - 条目起始磁盘 (0xFFFF=ZIP64)
   * - ``internalFileAttributes``
     - ``ca::u16``
     - 内部属性 (ASCII/binary)
   * - ``externalFileAttributes``
     - ``ca::u32``
     - 外部属性 (MS-DOS 权限)
   * - ``relativeOffsetOfLocalHeader``
     - ``ca::u32``
     - LOC 在文件中的偏移 (0xFFFFFFFF=ZIP64)

4.3 EndOfCentralDirectory 独有字段
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 字段
     - 类型
     - 用途
   * - ``diskNumber``
     - ``ca::u16``
     - 所在磁盘编号
   * - ``centralDirectoryStartDisk``
     - ``ca::u16``
     - CEN 起始磁盘
   * - ``centralDirectoryStartOffset``
     - ``ca::u16``
     - 本磁盘 CEN 起始偏移
   * - ``numEntries``
     - ``ca::u16``
     - 本磁盘 CEN 条目数
   * - ``centralDirectorySize``
     - ``ca::u32``
     - CEN 总大小
   * - ``centralDirectoryOffset``
     - ``ca::u32``
     - CEN 偏移量 (0xFFFFFFFF=ZIP64)
   * - ``zipCommentLength``
     - ``ca::u16``
     - ZIP 注释长度
   * - ``zipComment``
     - ``std::string``
     - ZIP 注释

5. 核心调用关系
---------------

读取
   ``ZipIO::readJvm(data)`` → ``JvmZipReader.read(archive, data)``。本库不直接读 文件：路径入口 ``ZipIO::readAdaptingIO(path)`` 委托 libca 的 ``ca::zip::ZipFile``， 再把结果转成 ``ZipArchive``。

   ``JvmZipReader.read`` 内部按 EOCD 查找/验证 → CEN 解析 → baseOffset 回退 → LOC 解析 → 双向链接 → ``adoptLinkedCentralDirectoryValues`` 的顺序推进，最终 ``archive.sort(OffsetComparator)`` 后返回。详细流程见 :ref:`algorithm-jvm-reader`。

写入
   ``DirectZipWriter.write`` 遍历 ``getLocalFiles()`` 写 LOC 块、遍历 ``getCentralDirectories()`` 写 CEN 块、最后写 EOCD（若存在）。字段写出顺序为： 签名 → version/gpFlag/method/crc32/sizes → fileNameLength/extraFieldLength → fileName/extra → fileData。``RecompressingZipWriter`` 则解压后重新压缩。 详见 :ref:`algorithm-writers`。

withMapping
   .. code-block:: text

      ZipArchive::withMapping(ZipPartMapper mapper) └── 委托 ZipIO::mapArchive：两阶段执行（先算全部映射结果，再统一替换） ├── 对每个 part 调 mapper.map(archive, part) ├── 返回自身的 part 原样保留，返回新 part 的接管所有权 └── 返回 nullptr 的 part 被丢弃

      CentralAdoptingMapper.map(part) ├── part 是 LocalFileHeader 且已链接 CEN? │   ├── 是: 复制 → adoptLinkedCentralDirectoryValues → 返回副本 │   └── 否: 委托给下一个 mapper (delegate.map()) └── 其余 part 类型: 委托

.. _model-patterns:

6. 关键设计模式
---------------

6.1 工厂模式：ZipPartAllocator
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

reader 通过 ``ZipPartAllocator`` 决定 LOC 子类型：standard 路径用 ``SimpleZipPartAllocator``（``LocalFileHeader``），JVM 路径用 ``JvmZipPartAllocator``（``JvmLocalFileHeader``）。见 :ref:`model-allocator`。

6.2 装饰器模式：ZipPartMapper 链
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. code-block:: text

   ZipArchive.withMapping()
     → CentralAdoptingMapper: CEN 字段覆盖 LOC
       → JvmClassDirectoryMapper: 去除 .class/ 尾部斜杠
         → IdentityZipPartMapper: 无操作（终止）

执行顺序：外层先执行，然后委托内层。

.. code-block:: text

   mapLocal(lfh)
     → CAM: 检查链接 → 修补 LOC → 委托
       → JCDM: 检查 .class/ → 修改 fileName → 委托
         → Identity: 返回原始

6.3 单例模式：Decompressor
^^^^^^^^^^^^^^^^^^^^^^^^^^

``UnsafeDeflateDecompressor`` 池化 z_stream 实例（最多缓存 64 个）供多线程复用， 池耗尽时回退到按次构造的 ``DeflateDecompressor``。

6.4 适配器模式：AdaptingZipReader
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``AdaptingZipReader`` 内部委托 ``ca::zip::ZipFile``（stdlib 风格随机访问），把 条目解压为 method=stored 的干净结构填充 ``ZipArchive``。见 :ref:`design-zip-layer`。

7. 状态/异常模型
----------------

C++ 中：

.. code-block:: cpp

   class ZipParseException : public std::runtime_error {
       ca::usize getOffset() const noexcept;  // 定位错误字节偏移
   };

触发场景：EOCD 未找到、CEN 签名不匹配、CEN 边界不一致、LOC 解码越界、文件数据 边界检查失败。

**JvmZipReader 的静默捕获**：个别 LOC 解码抛出的 ``std::out_of_range`` 会被静默 捕获，随后用 CEN 字段修复 LOC，使数据成功恢复。

8. 类型映射（数据模型层面）
---------------------------

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Java
     - C++
     - 说明
   * - ``int`` (4-byte LE)
     - ``ca::u32``
     - unsigned
   * - ``long`` (offset, size)
     - ``ca::usize``
     - 平台相关 (64-bit)
   * - ``MemorySegment``
     - ``jzip::MemorySegment``
     - 视图, 非复制
   * - ``MemorySegmentData``
     - ``std::vector<ca::u8>``
     - 连续内存
   * - ``StringData``
     - ``std::string``
     - UTF-8
   * - ``List<ZipPart>``
     - ``std::vector<std::unique_ptr<ZipPart>>``
     - 所有权明确

9. 模块依赖关系
---------------

``zip_io``（门面）→ ``zip_reader`` / ``zip_writer`` / ``zip_mapper`` → ``zip_model``；``zip_reader`` / ``zip_writer`` 还依赖 ``decompressor``；所有模块 依赖 ``memory_segment`` 提供的零拷贝视图。

10. C++ 编码约定
----------------

- 命名空间: ``jzip``
- 无 RTTI：部件类型经虚函数 ``type()`` 返回的 ``PartType`` 枚举区分 （``zip_type_traits.hpp`` 为 ``ca::core::tag_cast`` 提供 ``TypeOf`` 特化备用）
- 异常: ``ZipParseException`` 代替 ``IOException``，携带 ``offset()`` 定位错误
- ``MemorySegment`` 构造: ``MemorySegment(vector)`` 或 ``MemorySegment(ptr, size)``
- ``ZipArchive`` 通过 ``std::unique_ptr`` 管理 ``ZipPart`` 生命周期；条目 ``fileData`` 解析时复制，``prefixData`` 是输入段切片，``decompress()`` 与写回 各产生新字节数组
- 条目查找优先使用 ``CentralDirectoryFileHeader`` 的 ``fileName``\（CEN 为权威来源）
