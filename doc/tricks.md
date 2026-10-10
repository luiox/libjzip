# ZIP/JAR 容器 trick 分类法

> 本文件是 **libjzip 的容器威胁知识库**：定义 JAR/ZIP 容器层"可被动手脚的点"如何分类、
> 如何识别、如何防御，并用 libjzip 的 `test_resource/` 样本作为实例。
> 其他基于 libjzip 的上层项目在容器层遇到同类问题时，
> **不要另起一套术语**，直接引用本文件的分类法，把自己项目里遇到的样本挂到对应分类下。
> 类关系见 libjzip [MODEL.md](model.md)，读写算法见 [ALGORITHM.md](algorithm.md)。

---

## 1. 这份文档解决什么

JAR 就是 ZIP 加语义约定。混淆器/恶意 jar 会在**容器元数据**上做手脚：让工具读错数据边界、
看不到条目、把 class 归类错、或在解压/回写时丢数据。不同子项目遇到的是同一批 trick，
只是具体样本不同。

本文档统一回答三类问题：

1. 容器层有哪些可被动手脚的点？（威胁模型，§2）
2. 每种 trick 长什么样、怎么识别、通用防御原则是什么？（分类法，§3）
3. libjzip 的测试样本分别挂在哪个分类下？（实例，§4）

### 1.1 怎么用这份文档

| 角色 | 用法 |
|---|---|
| libjzip 维护者 | §3 是分类定义，§4 是它的回归样本清单；新增样本先归类再补测试 |
| 其他子项目 | 遇到容器异常先查 §3 对号入座；确认是新 trick 再在 §3 追加分类，并把自产样本记到自己的文档（引用 §N） |
| 写回/导出方 | §3 每种 trick 附"对写回的影响"，防止丢数据（见 §6 检查清单） |

---

## 2. 容器层威胁模型

### 2.1 信任边界

ZIP/JAR 有四种元数据载体，可信度完全不同：

| 载体 | 内容 | 可信度 | 理由 |
|---|---|---|---|
| CEN (Central Directory) | 条目的权威元数据：名字、CRC、大小、LOC 偏移 | **最高** | JVM 以 CEN 为准；LOC 字段可被篡改 |
| LOC (Local File Header) | 同样的字段副本 + extra | 低 | 压缩工具经常写错或篡改 |
| EOCD | CEN 偏移/大小/条目数 | 低 | 可被改写成"0 条目"或指向别处 |
| 文件数据区 | 条目内容 | 中 | 边界靠"扫描到下一个头"而非 LOC 字段 |

### 2.2 攻击面总览

| # | 攻击面 | 手段 | 后果 |
|---|---|---|---|
| 1 | 文件头部 | 前面塞 junk / 拼接另一个 ZIP | 标准工具误读 prefix 或只读第一个 ZIP |
| 2 | EOCD | 声明 0 条目 / 0 偏移 / 越界 / 掺入垃圾签名 | 目录枚举被欺骗，条目"消失" |
| 3 | LOC 大小字段 | 写成 0 / 虚假大小 / 与 CEN 不一致 | 数据边界错乱，解压失败或读到垃圾 |
| 4 | 压缩方法 / CRC | LOC 与 CEN 不一致 | 按 LOC 会解错 |
| 5 | Data Descriptor | bit 3 置位，LOC 里 crc/size=0 | 真实值只在 DD，常规解析读不到 |
| 6 | 条目名 | 尾斜杠、内嵌 NUL、非 UTF-8 | 按名字分类/解压的工具被绕过 |
| 7 | CEN→LOC 映射 | 多个 CEN 指向同一 LOC / 偏移越界 | 重复导入或读到错误数据 |
| 8 | ZIP64 | 数量/偏移超 65535/4GiB | 32 位字段溢出 |
| 9 | 结构完整性 | 缺 EOCD / 缺 CEN | 依赖目录的 reader 直接失败 |

---

## 3. 容器 trick 分类法

> 每条一个分类：**特征**（怎么认出它）、**识别方法**（静态可检查的判据）、**通用防御原则**
> （不绑定具体实现）、**对写回的影响**（导出/回写方要注意什么）。
> 分类用 `T##` 编号，新增分类不要重排已有编号。

### T01 前缀垃圾与拼接（leading junk / concat / merged）

- **特征**：首个 LOC 之前有任意字节（自解压引导、嵌包、防分析），或文件里拼了多个 ZIP。
- **识别**：`prefixData` 非空；反向找到的 EOCD 与正向找到的不是同一个；CEN 偏移不在文件头。
- **防御**：
  - reader 要保留 prefix，并在 CEN→LOC 映射时用 baseOffset 回退（不假设 LOC 在偏移 0）。
  - 拼接归档以**最后一个有效 EOCD** 为准（JVM 行为），正向扫描只能看到第一个。
- **对写回的影响**：回写前先决定要不要保留 prefix；重压缩成干净归档时 prefix 会被丢弃（预期）。

### T02 EOCD 欺骗

- **特征**：EOCD 声称 0 条目、0 偏移、越界偏移，或垃圾区里藏着伪造 EOCD 签名。
- **识别**：`centralDirectoryStart/End` 与 CEN 内容对不上；EOCD 之前有不在 CEN 范围内的数据。
- **防御**：不能直接信 EOCD 字段。JVM 做法是 3 层验证链——候选定位 → comment 对齐检查 →
  结构验证（CEN 签名 / 首个 LOC 签名 / fileNameLength 一致性），全过才接受。
- **对写回的影响**：EOCD 声称 0 条目不代表归档是空的；按 EOCD 条目数跳过会导致整包丢失。

### T03 LOC 大小字段造假（0-length / 越界）

- **特征**：LOC 的 `compressedSize`/`uncompressedSize` 是 0 或虚假值，CEN 却有真实数据；
  或数据区越界到别的头。
- **识别**：LOC 与 CEN 的 size 字段不一致（`hasDifferentValuesThanCentralDirectoryHeader`）；
  按 LOC size 切片读出来是垃圾或越界。
- **防御**：不信任 LOC 的 size。JVM 语义用"扫描到下一个头"确定数据边界（LOC 之后下一个
  已知偏移作为 ceiling），再用 CEN 值覆盖；个别字段解码失败时从 CEN 修复。
- **对写回的影响**：模型里存的是 CEN 修正后的数据切片，直接回写即可；不要回写 LOC 的假 size。

### T04 LOC/CEN 不一致（crc / compression / 字段）

- **特征**：LOC 的 CRC、压缩方法或任意字段与 CEN 不同，按 LOC 解必然出错。
- **识别**：逐字段比较 LOC 与 CEN（`hasDifferentValuesThanCentralDirectoryHeader`）。
- **防御**：**CEN 权威**——解析后让 LOC 采用 CEN 值（`adoptLinkedCentralDirectoryValues`）；
  保留"LOC 与 CEN 是否不一致"的可查询标记，供上层决定要不要警告/修复。
- **对写回的影响**：CEN 覆盖后 LOC 字段已被修正，回写是自洽的。

### T05 Data Descriptor（bit 3）

- **特征**：`generalPurposeBitFlag` bit 3 置位，LOC 的 crc/size=0，真实值在条目后的 DD 里。
- **识别**：LOC bit 3 == 1；按 CEN size 读数据后紧跟 12/16 字节 DD（有/无 `0x08074b50` 签名）。
- **防御**：
  - 按 CEN 的 size 切片（standard 路径）；
  - 或扫描到下一头后裁剪 DD 字节（JVM 路径，DD 有签名试 16、无签名试 12）。
- **对写回的影响**：重压缩时不再需要 DD，正常写出即可；直接回写时模型已把 DD 裁掉。

### T06 条目名攻击（尾斜杠 / 内嵌 NUL / 非 UTF-8）

- **特征**：条目名以 `/` 结尾（看着像目录，内容却是文件）；名字里塞 NUL；或非 UTF-8 字节。
- **识别**：
  - 尾斜杠：`fileName` 末尾是 `/`，但条目有非空数据 / 是 `.class` 内容。
  - NUL：`fileName` 含 `\0`，C 风格字符串会在该处截断，多个名字解压后碰撞叠成一个文件。
- **防御**：
  - 名字按 `fileNameLength` **显式截取**存入 `std::string`，NUL 原样保留，不做 C 截断。
  - 尾斜杠条目原样承载；需要按名归类时用 mapper 规范化（如 `.class/` → `.class`）。
  - 按名判定类别时不能只靠"以 `.class` 结尾"（尾斜杠、NUL 都能绕过），要以内容 magic 兜底。
- **对写回的影响**：任何"名字以 `/` 结尾就当目录跳过数据"的回写逻辑都会**静默丢数据**；
  必须区分"真目录"与"带数据的尾斜杠条目"。

### T07 CEN→LOC 映射异常（N-to-1 / 偏移越界）

- **特征**：多个 CEN 指向同一个 LOC；或 CEN 的 `relativeOffsetOfLocalHeader` 越界/指向垃圾。
- **识别**：按偏移计算 LOC 位置后签名不是 `0x04034b50`；同一 LOC 偏移被多个 CEN 引用。
- **防御**：CEN→LOC 映射用 TreeSet 去重（同一偏移只链接一次）；LOC 签名校验失败的
  CEN 记录警告并跳过链接。
- **对写回的影响**：`DirectZipWriter` 必须按 CEN→LOC 实际链接写回，不能按名字找第一个 LOC
  （重名/重复偏移会写错）。

### T08 ZIP64 溢出

- **特征**：条目数 ≥ 65535 或大小/偏移 ≥ 4GiB，字段用 `0xFFFF`/`0xFFFFFFFF` 占位。
- **识别**：EOCD 条目数或 CEN 的 size/offset 是占位魔法值；存在 `PK\x06\x07` Locator。
- **防御**：解析 ZIP64 EOCD Locator/Record 取真实值；把占位魔法值替换为真实 64 位偏移；
  超 `i32` 上限的条目数直接拒绝（防溢出）。
- **对写回的影响**：模型里存的是解析后的真实值，回写时重新判断是否需要 ZIP64。

### T09 结构缺失（无 END / 无 CEN）

- **特征**：整个文件没有 EOCD 签名，或 EOCD 存在但 CEN 全缺。
- **识别**：反向窗口扫不到 `0x06054b50`；或扫到但 CEN 范围内无条目。
- **防御**：依赖目录的 reader（forward / jvm）应抛明确异常；只扫 LOC 的 reader
  （naive）可作兜底读取。上层按需求选择策略。
- **对写回的影响**：从这类归档读出的模型没有完整 CEN，回写前需重建目录。

---

## 4. 实例：libjzip 样本挂载表

> libjzip 的 `test_resource/` 样本按 §3 分类挂载。测试在 `test/origin/`（继承 LL-Java-Zip）
> 和 `test/unittest/`（C++ 补充回归）两处；`zip_comparison_showcase_tests` 是跨三种 reader
> 的打印对比（默认 DISABLED）。

### 4.0 正常 / 兼容性样本（无 trick，回归基线）

| 样本 | 用途 | 覆盖测试 |
|---|---|---|
| `hello.jar` / `hello-secret.jar` | 最普通 jar：`META-INF/MANIFEST.MF` + `Hello.class`（secret 版内藏 `ROSE`） | part_parse / zip_file / zip_input_stream / decompression |
| `hello.png` | 非归档二进制，验证 reader 不误读普通文件 | zip_comparison_showcase |
| `sample-code-windows.zip` / `sample-code-7z.zip` | Windows/7-Zip 生成的代码包，跨工具兼容 | part_parse (StandardCodeZip) / decompression |
| `sample-long-name.zip` | 超长文件名 | large_zip |
| `hello-txt-stored.jar` / `hello-txt-type-0.jar` | `Hello.txt` 用 STORED / type-0 两种压缩方式存储的变体 | part_parse (JvmCanRecoverData) / zip_file |

### 4.1 T01 前缀垃圾与拼接

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `hello-concat.jar` / `hello-merged.jar` | 两个 jar 拼在一起，目录只认其中之一 | `JvmZipReader` 反向 EOCD 取**最后一个**有效 ZIP；首个 LOC 之前记为 `prefixData` | part_parse (ConcatAndMerged, JvmCanRecoverData) |
| `hello-concat-junkheader.jar` / `hello-merged-junkheader.jar` | 拼接 + junk header | 同上 + baseOffset 回退 | part_parse / patching |
| `hello-secret-junkheader.jar` | LOC 前塞垃圾 | prefixData + baseOffset | part_parse (Hello) |
| `hello-total-junk.jar` / `hello-total-junk-large.jar` | 几乎全是垃圾，只有零散有效结构 | `JvmZipReader` EOCD 验证链 + CEN 严格边界 | part_parse (JvmCanRecoverData) |
| `hello-junk-dir-length.jar` / `hello-junk-local-length.jar` | 目录/局部长度字段掺入垃圾 | CEN 权威 + 扫描定位 | part_parse (JvmCanRecoverData) |
| `hello-junk-eocd.jar` | 垃圾区里藏着伪造 EOCD | 3 层 EOCD 验证链拒绝假签名 | part_parse (JvmCanRecoverData) |
| `hello-copyjar-at-head.jar` / `hello-copyjar-at-tail.jar` | 整个 jar 复制品叠在头/尾 | 反向 EOCD 优先最后一个 | jar_in_jar / showcase |
| `hello-jar-in-in-jar-in-jar-in-jar-in-jar.jar` | 多层 jar-in-jar | 同上 | jar_in_jar / showcase |

### 4.2 T02 EOCD 欺骗

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `hello-end-declares-0-entries.jar` | EOCD 声称 0 个条目 | `isValidEndHeader` 结构验证：CEN 签名 / LOC 签名 / NameLen 一致，仍能恢复 | part_parse (JvmCanRecoverData) |
| `hello-end-declares-0-entries-0-offset.jar` | 0 条目 + 0 偏移 | 同上 + baseOffset 回退 | part_parse (JvmCanRecoverData) |
| `sample-eocd-oob.jar` | EOCD 越界（Out Of Bounds），另有 `Hello.class/` | 验证链接受有效候选；尾斜杠条目原样保留 | part_parse (EocdMisleading) |
| `hello-merged-fake-empty.jar` | merged 后接一个"假装空"的 EOCD | 反向扫描跳过假 EOCD，取真实尾部 | part_parse (MergedFakeEmpty, JvmCanRecoverData) |
| `deception.jar` / `hello-deceptive.jar` | 欺骗性结构，常规解析可能崩 | 三 reader 均不崩溃 | uncovered_parse (DeceptionJar) / showcase |

### 4.3 T03 LOC 大小字段造假

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `hello-secret-0-length-locals.jar` | LOC 全零大小，CEN 正常 | `JvmLocalFileHeader` "扫描到下一个头"确定边界 + `adoptLinkedCentralDirectoryValues` 用 CEN 覆盖 | part_parse (Hello, LocalHeaderDetectMismatch) / mismatch |
| `hello-zeroed-locals.jar` | LOC 字段被清零 | 同上 | part_parse (JvmCanRecoverData) |
| `hello-secret-trailing-slash-0-length-locals.jar` | 尾斜杠 + 0 长度 LOC 叠加 | 扫描边界 + 尾斜杠原样保留 | part_parse (JvmCanRecoverData) |
| `hello-no-local-length.jar` / `hello-no-local-names.jar` | LOC 缺少长度/名字字段 | CEN 权威补全 | part_parse / showcase |
| `resource-pack-trick-data-ioobe.zip` | 文件数据越界读取 | `LengthIgnoringAllocator` + ForwardScan/Naive 按真实结构恢复；标准 reader 抛异常属预期 | patching (TestReadIgnoringFileLengths) |

### 4.4 T04 LOC/CEN 不一致

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `hello-wrong-crc-local.jar` / `hello-wrong-crc-central.jar` / `hello-wrong-crc-both.jar` | CRC 在 LOC/CEN/两处被改 | `adoptLinkedCentralDirectoryValues` 用 CEN 覆盖；`hasDifferentValuesThanCentralDirectoryHeader()` 可检测不一致 | part_parse / mismatch / showcase |
| `hello-wrong-local-compression.jar` | LOC 压缩方法 ≠ CEN | 同上 | mismatch (WrongLocalCompression) / part_parse (JvmCanRecoverData) |
| 内联样本 | `build_mismatch_crc_zip()` / `build_mismatch_compression_zip()` | 同上的构造函数 | inline / reader_parse |

### 4.5 T05 Data Descriptor

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `jar-in-jar-with-data-descriptor.jar` | jar-in-jar + DD | `JvmLocalFileHeader` 扫描下一头确定边界，裁剪 12/16 字节 DD；std reader 用 CEN size | data_descriptor / jar_in_jar |
| `end-in-local-data-desc.jar` | DD 数据区含 EOCD 签名干扰（639 个 class） | 反向 EOCD 验证链 + 扫描边界 | part_parse (EndInLocalDataDesc) |
| 内联样本 | `build_data_descriptor_zip()` / `zip64DataDescriptorArchive(...)` | 构造函数 + DD 裁剪 | data_descriptor / zip64 |

### 4.6 T06 条目名攻击

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `hello-secret-trailing-slash.jar` | `Hello.class/` 藏 class | `JvmClassDirectoryMapper` 去尾斜杠；未映射时原样保留 | patching (TestTrailingSlashTransform) / part_parse |
| `sample-duplicate-manifest-oob.jar` | 重复 `MANIFEST.MF` + trap class + `Hello.class/` | 全量解析不跳过；trap class 解压失败不阻断 | part_parse (DuplicateManifest) |

> **待补覆盖点**：内嵌 NUL 路径（`a\u0000b.class`）目前只有代码保证（`std::string` 原样
> 承载），`test_resource/` 里暂无样本。需要构造一个含 `\0` 的 jar，断言 `getFileName()`
> 完整含 `\0` 且写回后字节不变。

### 4.7 T07 CEN→LOC 映射异常

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `resource-pack-trick-header-N-to-1-cen-to-loc-mapping.zip` | 多个 CEN 指向同一个 LOC | TreeSet 去重 + CEN↔LOC 双向链接按偏移 | jvm_vs_zipfile_equality / showcase |
| 内联样本 | `build_n_to_one_cen_to_loc_zip()` | 同上去重 | inline |

### 4.8 T08 ZIP64

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `there-is-a-zip64-in-here.zip` | 外层 zip 包一个 70000 条目的 ZIP64 内层 jar | `Zip64Support`：EOCD Locator/Record、真实偏移替换 `0xFFFFFFFF` 魔法值 | origin zip64_test (TestZip64ByEntryCount) |
| 内联样本 | `zip64OffsetArchive()`（2 条目标偏移）/ `zip64DataDescriptorArchive()`（DD + ZIP64） | ZIP64 解析 + DD 裁剪 | origin zip64_test |

### 4.9 T09 结构缺失

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `invalid-no-end.jar` | 整个文件没有 EOCD | std/jvm 抛 `ZipParseException`（预期）；`NaiveLocalFileZipReader` 只扫 LOC 可兜底 | uncovered_parse (InvalidNoEnd) |
| `invalid-no-end-no-cen.jar` | 没有 EOCD 也没有 CEN | 同上，Naive 兜底 | uncovered_parse (InvalidNoEndNoCen) |

### 4.10 元数据兼容（非 trick，跨工具边界）

| 样本 | 手法 | libjzip 应对 | 覆盖测试 |
|---|---|---|---|
| `content-with-windows-time.jar` | Windows NTFS extra field 存创建/修改/访问时间 | `ExtraFieldParser::parse_ntfs_time` 解析 tag `0x000A` | windows_extra_field_time |

---

## 5. 三 reader 覆盖矩阵

| Trick | ForwardScanZipReader | JvmZipReader | NaiveLocalFileZipReader |
|---|---|---|---|
| T01 拼接 / merged | ❌ 只读到第一个 | ✅ 取最后一个 | ⚠️ 所有混在一起 |
| T01 junk header / prefix | ✅ prefixData | ✅ prefixData + baseOffset 回退 | ✅ prefixData |
| T02 EOCD 欺骗 | ❌ 按声明边界，易被误导 | ✅ 3 层验证链 | N/A（不需要 EOCD） |
| T03 0-length locals | ❌ LOC size=0 读不出 | ✅ 扫描下一头 + CEN 覆盖 | ❌ 读不出 |
| T04 LOC/CEN 不一致 | ⚠️ 原样用 LOC | ✅ CEN 覆盖 | ⚠️ 仅 LOC |
| T05 Data Descriptor | ✅ 用 CEN size | ✅ 扫描 + DD 裁剪 | ❌ 忽略 |
| T06 尾斜杠类名 | ✅ 原样保留（配合 mapper 规范化） | ✅ 原样保留 | ✅ 原样保留 |
| T07 N-to-1 CEN→LOC | ⚠️ 按偏移去重 | ✅ TreeSet 去重 | N/A（无 CEN） |
| T08 ZIP64 | ✅ | ✅ | ❌ |
| T09 无 END / 无 CEN | ❌ 抛异常 | ❌ 抛异常 | ✅ 兜底 |
| 抗伪造总体 | 低 | **高** | 极低 |

> 完整算法对比见 [ALGORITHM.md](algorithm.md) §8。

---

## 6. 下游消费方检查清单

libjzip 负责承载容器结构，以下分类/判定若由消费方自己做，需配合 libjzip 的映射能力：

1. **按名字判定 class 别只用 `.class` 结尾**：尾斜杠条目（`Hello.class/`）在未过
   `JvmClassDirectoryMapper` 时不满足 `.class` 结尾；内嵌 NUL 条目（`a\u0000b.class`）同样
   会被名字检查漏掉。要按内容 magic（`0xCAFEBABE`）兜底，或先跑 mapper 规范化。
2. **写回别把尾斜杠条目当空目录**：任何按 `entryName.back() == '/'` 就跳过数据的写逻辑
   都会**静默丢弃**"看着像目录、实际带 class 数据"的条目。libjzip 的 `DirectZipWriter`
   不这么做；若消费方自行写回需自行保证。
3. **NUL 形态在按名字过滤的统计里不可见**：`a\u0000b.class` 结尾是 `b.class`，若先按
   `.class` 结尾过滤整个条目会被滤掉，连"NUL 分类"都统计不到。应按内容而非名字补可见性。
4. **EOCD 声称 0 条目不代表 jar 是空的**：解析必须走 `JvmZipReader` 这类带验证链的 reader，
   不能直接信 EOCD 的条目数。
5. **`resource-pack-trick-*` 系列**：标准 reader 抛异常是预期行为；需要恢复时用
   `ForwardScanZipReader` / `NaiveLocalFileZipReader` + 适当 allocator（如
   `LengthIgnoringAllocator`）。

---

## 7. 为其他项目补容器文档的模板

子项目遇到容器问题时，按下面的骨架在**自己的文档**里记录，分类编号引用本文件：

```markdown
## 容器 trick 记录：<项目名>

> 分类法见 libs/libjzip/doc/tricks.md §3；本文只记本项目实际遇到的样本。

### 遇到的 trick（引用分类）

| 分类 | 样本 | 现象 | 本项目处理 | 相关代码 |
|---|---|---|---|---|
| T03 LOC 大小字段造假 | `xx.jar` | LOC size=0，解压为空 | 改用 CEN 值 | `src/xx.cpp:42` |
| T06 条目名攻击 | `yy.jar` | 尾斜杠 class 被当目录 | 先跑 JvmClassDirectoryMapper | `src/yy.cpp:17` |

### 新发现的 trick（若不在 §3，先在本文件 §3 追加分类，再引用）
```

约定：

- **分类编号是全局唯一的事实来源**，放在 `libs/libjzip/doc/tricks.md`；子项目只引用、不复制定义。
- 新增 trick 分类：先在 §3 追加一条（特征/识别/防御/写回影响），再在 §4 挂 libjzip 样本
  （若有），最后更新 §5 矩阵和 §2.2 总览表。
- 子项目的样本清单放在子项目自己的 `doc/`，不塞进 libjzip；libjzip 只持有它自己的回归样本。

---

## 8. 相关文档

- [design.md](design.md)：分层架构、reader / writer / mapper 定位
- [model.md](model.md)：对象模型、CEN↔LOC 双向链接
- [algorithm.md](algorithm.md)：EOCD 验证链、扫描边界、DD 裁剪、ZIP64 算法
- [../readme.md](../readme.md)：项目总览、常用入口、构建测试
