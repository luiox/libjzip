.. _algorithm:

libjzip 读写算法分析
====================

状态：定稿。libjzip 各 reader/writer/mapper 的算法流程与语义约定，按 C++ 实现 描述（与 LL-Java-Zip 的差异处显式标注）。类关系与字段模型见 :doc:`model`。

1. ZIP 格式概述
---------------

一个标准 ZIP 文件的布局：

.. code-block:: text

   [Local File Header 1]
   [File Data 1]
   [Local File Header 2]
   [File Data 2]
   ...
   [Central Directory Header 1]
   [Central Directory Header 2]
   ...
   [End of Central Directory]
   [ZIP64 End of Central Directory Locator]
   [ZIP64 End of Central Directory]

每个段的签名（LE uint32）：

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 段
     - 签名
     - 十六进制
   * - Local File Header (LOC)
     - ``PK\x03\x04``
     - ``0x04034b50``
   * - Central Directory Header (CEN)
     - ``PK\x01\x02``
     - ``0x02014b50``
   * - End of Central Directory (EOCD)
     - ``PK\x05\x06``
     - ``0x06054b50``
   * - ZIP64 EOCD Locator
     - ``PK\x06\x07``
     - ``0x07064b50``
   * - ZIP64 EOCD Record
     - ``PK\x06\x06``
     - ``0x06064b50``

EOCD 结构（固定 22 字节 + 注释）：

.. code-block:: text

   偏移  大小  字段
   0     4    签名 (0x06054b50)
   4     2    所在磁盘编号
   6     2    中央目录起始磁盘
   8     2    本磁盘上 CEN 条目数
   10    2    CEN 总条目数
   12    4    CEN 总大小
   16    4    中央目录偏移量
   20    2    注释长度
   22    ?    注释

.. _algorithm-readers:

2. 三种读取策略
---------------

``ZipIO`` 门面按入口分发：``readStandard`` → ``ForwardScanZipReader``， ``readJvm`` → ``JvmZipReader``，``readNaive`` → ``NaiveLocalFileZipReader``； 三者均产出 ``ZipArchive``。

.. list-table:: 策略对比
   :header-rows: 1
   :widths: auto

   * - 特征
     - ForwardScanZipReader
     - JvmZipReader
     - NaiveLocalFileZipReader
   * - EOCD 搜索方向
     - **正向**\ 从文件头
     - **反向**\ 从文件尾
     - 不需要 EOCD
   * - EOCD 验证
     - 无（找到第一个即用）
     - **3 层结构验证链**
     - 不需要
   * - CEN 解析
     - 受 EOCD 声明范围约束
     - 受**验证后** CEN 边界严格约束
     - 不需要
   * - LOC 数据读取
     - 使用 LOC 字段的 compressed_size
     - 用扫描到下一个头方式 + CEN 覆盖
     - 仅 LOC
   * - baseOffset 回退
     - 不需要
     - 有（扫描 PK 签名）
     - 不需要
   * - 前缀数据
     - 支持
     - 支持
     - 支持
   * - CEN↔LOC 链接
     - 双向
     - 双向
     - 无 CEN
   * - ZIP64
     - 是
     - 是
     - 否
   * - 抗伪造
     - 低
     - 高
     - 极低

.. _algorithm-forward-scan:

2.1 ForwardScanZipReader: 正向扫描读取器
-----------------------------------------

**定位**：标准 ZIP 文件解析，按 ZIP 规范布局顺序（LOC→CEN→END）正向扫描。

算法流程：

1. 正向扫描 ``0x06054b50`` 签名（``indexOfQuad`` 从偏移 0 开始）；找不到则抛 ``EOCD signature not found``。
2. 解码 ``EndOfCentralDirectory``；``Zip64Support::resolveEndOfCentralDirectory`` 获取 ``centralDirectoryStart`` / ``centralDirectoryEnd``。
3. 在 ``[start, end)`` 内循环读取 CEN：签名必须为 ``0x02014b50``，解码 ``CentralDirectoryFileHeader`` 并加入 archive。
4. 遍历所有 CEN：计算 ``baseOffset + relativeOffsetOfLocalHeader`` 得到 LOC 偏移； 集合去重后验证 LOC 签名 ``0x04034b50``，不匹配则记录异常、跳过 LOC 链接； 匹配则解码 ``LocalFileHeader``，建立 CEN-LOC 双向链接，加入 archive。
5. 首个 LOC 偏移 > 0 时记录前缀数据。
6. ``OffsetComparator`` 排序。

关键设计点：

1. **只找第一个 EOCD**：对 concat/merged ZIP（多个 ZIP 拼接），只能看到**第一个** ZIP 的 EOCD。
2. **CEN 边界由 EOCD 声明**：EOCD 声明的 ``centralDirectoryOffset`` 和 ``centralDirectorySize`` 直接决定 CEN 扫描范围，不验证其真实性。
3. **LOC 字段原样使用**：LOC 的 ``compressedSize``/``uncompressedSize`` 直接用于 确定文件数据边界。LOC 的 size 字段被篡改时读取会出错。
4. **CEN→LOC 映射靠 CEN 的 offset 字段**：``relativeOffsetOfLocalHeader`` 加上 baseOffset（通常为 0）决定 LOC 的绝对位置。
5. **前缀数据**：首个 LOC 偏移之前的字节被保留为 ``prefixData``\（如 exe 启动引导 程序等）。

适用场景：标准未篡改的 ZIP/JAR 文件；需要快速读取且不关心容错的场景。

.. _algorithm-jvm-reader:

2.2 JvmZipReader: JVM 语义读取器
---------------------------------

**定位**：模拟 HotSpot JVM 的 ``java.util.zip.ZipFile`` 行为，是最复杂、最健壮的 读取器。设计目标是：在**不信任**的/伪造的 ZIP 文件上尽可能恢复数据。

算法全流程（``JvmZipReader.read`` 分 5 个阶段，细节算法见下方各小节）：

1. **Phase 1: EOCD 查找与验证**：从尾部 ``65535+22`` 字节窗口反向扫描 ``0x06054b50``，收集所有候选 EOCD；对每个候选执行 ``isValidEndHeader`` （comment_length 对齐文件尾则直接接受，否则做结构验证：CEN 签名 / LOC 签名 / NameLen 一致性），选第一个通过的候选；全部失败则抛 ``No valid End-Of-Central-Directory found!``。
2. **Phase 2: CEN 严格解析**：在 ``[centralDirectoryStart, centralDirectoryEnd)`` 内循环解码 ``CentralDirectoryFileHeader``；消耗后必须精确匹配 ``centralDirectoryEnd``，否则抛 ``CEN 边界不一致``。
3. **Phase 3: baseOffset 确定与回退**：计算 ``jvmBaseFileOffset = endInfo.baseOffset``；若 ``hasAnyLinkedLocalHeader`` 失败，先试偏移 0 处 LOC 签名（快速路径），再前向扫描 PK 签名（慢速路径）以更新 baseOffset。
4. **Phase 4: LOC 解析**：填充 ``entryOffsets`` 排序集（含所有签名校验通过的 CEN→LOC 映射偏移、``earliestCdfh``、EOCD 偏移），遍历 CEN 计算绝对 LOC 偏移； 去重 + LOC 签名校验；通过 ``JvmZipPartAllocator`` 分配 ``JvmLocalFileHeader`` 并 ``setOffsets(entryOffsets)``；从字节读取 LOC（异常静默捕获）→ CEN-LOC 双向 链接 → ``adoptLinkedCentralDirectoryValues`` → 加入 archive。
5. **Phase 5: 收尾**：记录 ``prefixData``，``OffsetComparator`` 排序。

.. _algorithm-eocd-validation:

2.2.1 3 层 EOCD 验证链
^^^^^^^^^^^^^^^^^^^^^^

1. **候选定位**：从尾部反向扫描 EOCD 签名，限制窗口 ``65535 + 22`` 字节，收集所有 候选位置。
2. **解码与快速检查**：解码 ``EndOfCentralDirectory``；若 ``commentLength`` 自然 延伸到文件尾则直接接受，跳过结构验证。
3. **结构验证（``isValidEndHeader``）**：由 ``endPos - centralSize`` 推出 ``centralDirectoryStart``；依次检查该处 CEN 签名、首个条目 offset 处的 LOC 签名、CEN/LOC ``fileNameLength`` 一致性；全部通过才接受此候选。

``isValidEndHeader`` 是 JVM 行为的逆向工程产物：

- 它防止嵌入在注释/数据/尾部填充中的**虚假 EOCD 签名**被误接受
- JVM 不会接受一个声称的 CEN 范围与其头部内容不匹配的 EOCD
- 这解释了为何某些样本（如 ``hello-end-declares-0-entries.jar``）在 JVM 读取器下 能恢复数据

.. _algorithm-base-offset:

2.2.2 CEN→LOC 映射回退
^^^^^^^^^^^^^^^^^^^^^^

``hasAnyLinkedLocalHeader`` 失败时按序回退：

1. 快速路径：检查偏移 0 处是否有 LOC 签名；成功则 ``baseOffset = 0``。
2. 慢速路径：从预计位置正向扫描 PK 签名，找到 LOC 或 CEN 签名则把 baseOffset 更新为扫描结果。
3. 都失败则保持原 baseOffset 继续。

.. _algorithm-scan-ceiling:

2.2.3 扫描到下一个头文件数据读取
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

``JvmLocalFileHeader`` 的核心设计，不信任 LOC 的 ``compressedSize``/``uncompressedSize``。数据边界按 C++ 实现的契约执行：

1. 用**原始 LOC 字段**长度计算数据起始（``30 + fileNameLength + extraFieldLength``， 保存于 LOC 解析时，避免被后续 CEN 覆盖污染）：``dataStart = headerOffset + localDataOffset``；越界即抛 ``local file data offset out of range``。
2. 在 ``entryOffsets`` 排序集中查找**首个不早于** ``dataStart`` 的偏移（ceiling， 对齐 Java ``TreeSet.ceiling``；不能用严格大于，零长度条目的数据起点恰好等于 下一个头部偏移时会跳过它，把后续结构算进本条目数据）。找不到则到文件尾。 ``dataEnd`` 即该 ceiling 值。
3. bit 3（data descriptor）置位且显式大小可用（stored 取 ``uncompressedSize``， 其余取 ``compressedSize``；要求 > 0）时，若显式大小落在 ``[dataStart, dataEnd]`` 内，把 ``dataEnd`` 收敛为 ``dataStart + explicitSize``，DD 字节与后续结构留在 数据区外。
4. 切片 ``data[dataStart, dataEnd]`` 作为 fileData；STORED 按实际数据长度回填 ``compressedSize``/``uncompressedSize``，其余按实际数据长度回填 ``compressedSize``。

.. _algorithm-adopt-cen:

2.2.4 CEN 值覆盖（adoptLinkedCentralDirectoryValues）
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

JVM 信任 CEN 而非 LOC。解析 LOC 后，``adoptLinkedCentralDirectoryValues`` 会用 CEN 的以下字段覆盖 LOC：

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 字段
     - 覆盖规则
   * - ``versionNeededToExtract``
     - 从 CEN 复制
   * - ``generalPurposeBitFlag``
     - 从 CEN 复制
   * - ``compressionMethod``
     - 从 CEN 复制
   * - ``crc32``
     - 从 CEN 复制
   * - ``compressedSize``
     - 从 CEN 复制
   * - ``uncompressedSize``
     - 从 CEN 复制
   * - ``fileName``
     - 从 CEN 复制
   * - ``extraField``
     - 不在覆盖之列：``JvmLocalFileHeader`` 保留 LOC 原值；基类
       ``LocalFileHeader``\（``CentralAdoptingMapper`` 场景）整体替换为 CEN
       extra。同名 tag 去重的追加合并走 ForwardScan 读入路径的
       ``appendExtraFieldsFromCen``，只改解析列表、不改 raw 字节
   * - **文件数据切片**
     - 按 CEN 的权威大小重对齐：stored 取 ``uncompressedSize``，其余取
       ``compressedSize``

数据切片重对齐规则：

- CEN 权威大小为 0：**强制对齐为空数据**，fileData 清空、数据区间收敛到起点、 ``foundData_`` 置假。前向扫描可能把邻接的下一个结构（LFH/CEN）或数据描述符字节 算进本条目，权威大小为 0 时不能保留这些垃圾，模型必须与 CEN 大小自洽。
- CEN 权威大小 > 0：满足重对齐条件（未找到数据 / bit 3 置位 / 现有 fileData 长度 与权威大小不符）且数据起点边界可容纳时，按权威大小重切片并收敛数据区间。
- CEN 权威大小不可用或越界放不下：保留扫描结果不动。

适用场景：JAR/WAR/APK 等可能被篡改的归档；concat/merged ZIP（多 ZIP 拼接，取最后 一个有效 ZIP）；需要抗伪造能力的场景。

.. _algorithm-naive:

2.3 NaiveLocalFileZipReader: 朴素读取器
----------------------------------------

**定位**：最简单的读取器，只用 LOC 签名，完全不依赖 EOCD 和 CEN。

算法流程：从偏移 0 前向扫描 ``0x04034b50``；找到则把首个 LOC 之前的字节记录为 ``prefixData``，然后在 do-while 循环里解码 ``LocalFileHeader`` → 加入 archive → ``postProcessLocalFileHeader`` → 扫描下一个 LOC 签名，直到找不到为止。

关键设计点：

- **无结构验证**：只找 LOC 签名，不检查 EOCD/CEN 的任何内容
- **无去重**：同一个字节序列如果有多个 LOC 签名重叠匹配，会被多次读取
- **不返回错误**：找不到 LOC 时返回空 ``ZipArchive``，不抛异常

适用场景：流式 ZIP（数据流只有 LOC 头）；仅需快速预览文件列表；修复严重损坏的 ZIP（EOCD 和 CEN 完全丢失）。

.. _algorithm-writers:

3. 写入策略
-----------

3.1 DirectZipWriter: 直接写入器
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

将 ``ZipArchive`` 的模型字段逐字写出，不做重压缩。流程：遍历 ``LocalFileHeader`` 写 LOC（签名 + 固定字段 + 文件名 + extra + 文件数据）；遍历 ``CentralDirectoryFileHeader`` 写 CEN（签名 + 固定字段 + 文件名 + extra + 注释）； ``End`` 存在时写 EOCD。

**写入顺序**：``LOC block -> CEN block -> END record``

特点：

- 不重压缩，不重新计算 CRC32
- LOC 和 CEN 的 ``relativeOffsetOfLocalHeader`` 必须提前正确
- 常用于调试/测试，或需要逐字复制的场景

3.2 RecompressingZipWriter: 重压缩写入器
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

解压后重新压缩成新 ZIP，始终产出标准结构：遍历条目，fileData 非空时先解压再写入 压缩流；零字节普通文件照常写出；``createDirectoryEntries`` 控制名字以 ``/`` 结尾 的目录条目是否保留。

与 DirectZipWriter 对比：

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 特征
     - DirectZipWriter
     - RecompressingZipWriter
   * - 压缩
     - 保持原始 method
     - 解压→重新压缩
   * - CEN/END
     - 从模型写出
     - 写入器重新生成
   * - 字节级重现
     - 是
     - 否
   * - 输出兼容性
     - 依赖模型准确性
     - 始终生成标准 ZIP

安全边界：源 method 只支持 stored(0)/deflate(8)，其他值抛 ``ZipParseException``； deflate 解压受 ``defaultEntryOutputCap``\（min(1 GiB, 压缩大小 × 1000)，下限 1 MiB）限制，防止压缩炸弹。

.. _algorithm-link:

4. CEN↔LOC 链接机制
-------------------

读取器在解码 LOC 后立即建立双向链接（``cdfh.link(lfh)`` / ``lfh.link(cdfh)``）。

链接在以下场景中使用：

1. **CentralAdoptingMapper**：遍历 CEN 找到链接的 LOC，用 CEN 字段覆盖 LOC
2. **``hasDifferentValuesThanCentralDirectoryHeader()``**：逐字段比较 LOC 和 CEN， 检测不一致
3. **``adoptLinkedCentralDirectoryValues()``**：用 CEN 字段修复 LOC

链接的字段语义与生命周期见 :ref:`model-link`。

.. _algorithm-dd:

5. Data Descriptor 处理
-----------------------

Data Descriptor 是 ZIP 规范 §4.3.9 定义的可选结构，位于文件数据之后。当 ``generalPurposeBitFlag`` 的 bit 3 设置时，LOC 的 CRC32/size 字段为 0，真实值在 DD 中。

.. code-block:: text

   LOC: CRC32=0, sizes=0, bit3=1 | 文件数据 | Data Descriptor 12/16 字节 | 下一头

C++ 处理策略：

- **JVM 路径**\（``JvmLocalFileHeader``）：数据结束由 ceiling 定界 （:ref:`algorithm-scan-ceiling`）；bit 3 且显式大小可用时把数据结束收敛为 ``dataStart + explicitSize``，DD 字节留在数据区外；CEN 权威大小随后在 adopt 阶段 重对齐（:ref:`algorithm-adopt-cen`），权威大小为 0 时强制空数据。
- **Standard 路径**\（``ForwardScanZipReader``）：直接使用 CEN 的 compressedSize， 不需要特殊 DD 裁剪。

DD 格式：

.. code-block:: text

   有签名 (16 字节)         无签名 (12 字节)
   0x08074b50               (无签名)
   CRC32 (4 字节)           CRC32 (4 字节)
   compressedSize (4/8)     compressedSize (4/8)
   uncompressedSize (4/8)   uncompressedSize (4/8)

.. _algorithm-mappers:

6. Mapper 体系
--------------

``ZipPartMapper`` 接口提供单一映射点 ``map(archive, part)``（按 ``PartType`` 区分 部件）；实现有 ``CentralAdoptingMapper``、``JvmClassDirectoryMapper``、 ``IdentityZipPartMapper``、``CopyingZipPartMapper``， ``DelegatingZipPartMapper`` 承载委托链（先委派再自身，无委派等价 Identity）。

使用方式（委派链先执行内层再执行外层；临时对象在同一完整表达式内安全）：

.. code-block:: cpp

   ZipArchive mapped = archive.withMapping(
       CentralAdoptingMapper(
           JvmClassDirectoryMapper(
               IdentityZipPartMapper()))));

CentralAdoptingMapper
   先委派，再对已链接 CEN 的 ``LocalFileHeader`` 复制并调用 ``adoptLinkedCentralDirectoryValues``，返回修补后的副本；其余部件原样委托。

JvmClassDirectoryMapper
   修复 JVM 在某些情况下遗留的尾部斜杠：``fileName`` 以 ``.class/`` 结尾时复制并 去掉末尾斜杠、减小 ``fileNameLength``，否则委托。

CopyingZipPartMapper
   返回独立深拷贝、原部件由 ``mapArchive`` 释放；深拷贝不保留 LOC/CEN 互链，必须 放在映射链末端（见 ``zip_mapper.hpp`` 头注）。

.. _algorithm-zip64:

7. ZIP64 支持
-------------

ZIP64 用于处理超过 65535 个条目或超过 4GiB 的归档，由 ``zip64_support.hpp`` 处理。

触发条件：

- CEN 条目数 = ``0xFFFF``
- LOC/CEN 的 size 字段 = ``0xFFFFFFFF``
- CEN 的 offset 字段 = ``0xFFFFFFFF``

解析流程（``Zip64Support::resolveEndOfCentralDirectory``）：

1. 检查是否存在 ZIP64 EOCD Locator；存在则读取 Locator 与 ZIP64 EOCD Record。
2. 提取真实 ``centralDirectoryOffset`` / ``centralDirectorySize`` / ``numEntries``。
3. 应用 baseOffset 修正后返回 ``ResolvedEnd``。
4. 不存在则使用 basicEnd 的字段。

CEN→LOC 映射的回退（``Zip64Support::getRelativeOffsetOfLocalHeader``）： ``relativeOffsetOfLocalHeader == 0xFFFFFFFF`` 时从 ZIP64 Extra Field 读真实偏移； Extra 不存在时用 ``directOffset - baseOffset`` 计算相对偏移；非占位值直接用字段 原始值。

.. _algorithm-comparison:

8. 三大策略对比
---------------

.. list-table::
   :header-rows: 1
   :widths: auto

   * - 场景
     - ForwardScanZipReader
     - JvmZipReader
     - NaiveLocalFileZipReader
   * - 标准 ZIP/JAR
     - ✅ 最优
     - ✅ 可用
     - ✅ 可用
   * - Concat/Merged ZIP
     - ❌ 只读到第一个
     - ✅ 读到最后一个
     - ❌ 读到所有混在一起
   * - EOCD 被篡改
     - ❌ 失败
     - ✅ 尝试恢复
     - ❌ (不需要 EOCD)
   * - CEN 被篡改
     - ❌ 失败
     - ✅ CEN 覆盖修复
     - ❌ (不需要 CEN)
   * - LOC size 字段为 0
     - ❌ 读不出数据
     - ✅ 扫描下一个头
     - ❌ 读不出
   * - Data Descriptor
     - ✅ 用 CEN size
     - ✅ 扫描 + 显式大小收敛
     - ❌ 忽略
   * - ZIP64
     - ✅ 是
     - ✅ 是
     - ❌ 否
   * - 抗伪造
     - 低
     - **高**
     - 极低

9. 异常模型
-----------

错误以 ``ZipParseException`` 承载（携带 ``offset()`` 定位错误字节偏移）：

- EOCD 未找到（ForwardScan / Jvm）
- CEN 签名不匹配（ForwardScan）
- CEN 边界不一致（Jvm）
- LOC 解码越界（所有 reader）
- 文件数据边界检查失败

**JvmZipReader 的特殊容错**：个别 LOC 解码失败被静默捕获，随后 ``adoptLinkedCentralDirectoryValues`` 用 CEN 字段覆盖修复，数据成功恢复，LOC 字段损坏不阻断整包读取。

.. _algorithm-principles:

10. 核心原则
------------

1. **CEN 权威**：Central Directory 是元数据的权威来源，LOC 的字段可以有偏差
2. **JVM 行为为准**：遇到歧义时，以 ``java.util.zip.ZipFile`` 的行为为准
3. **反向 EOCD**：从文件末尾反向扫描，而非正向
4. **ceiling 定界**：JVM 路径的数据边界由下一个已知偏移的 ceiling确定， bit 3 且显式大小可用时把 DD 收敛在数据区外；不信任 LOC 的 size 字段
5. **容错性**：容忍 leading/trailing garbage、偏移偏差、zero-length locals、 尾部斜杠类名
