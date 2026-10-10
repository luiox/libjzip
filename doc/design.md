# libjzip 设计文档

## 1. 定位

`libjzip` 是面向 JVM 字节码场景的 ZIP/JAR 基础库，服务两类上层需求：

- `jzip`：保留 ZIP 原始结构，行为对齐 LL-Java-Zip / JVM
- `zip`：提供接近 `java.util.zip` 的简单读写接口

这两层不是重复实现，而是不同抽象层级：

- `jzip` 关心 `LOC / CEN / EOCD / data descriptor / ZIP64 / prefix junk`
- `zip` 关心“按 entry 读写内容”

## 2. 分层结构

```text
libs/libjzip/src/
├── jzip/
│   ├── zip_model.hpp        ZIP 结构模型
│   ├── zip_reader.hpp       结构化读取器
│   ├── zip_writer.hpp       结构化写入器
│   ├── zip_mapper.hpp       结构映射器
│   └── zip_adapting_reader.hpp
└── zip/
    ├── zip_file.hpp         stdlib 风格随机访问读取
    ├── zip_input_stream.hpp stdlib 风格流式读取
    ├── zip_output_stream.hpp stdlib 风格写出
    └── zip_entry.hpp        entry 元数据
```

## 3. 架构图

```mermaid
graph TD
    A["原始 ZIP/JAR 字节"] --> B["jzip::ZipReader"]
    B --> C["jzip::ZipArchive"]
    C --> D["jzip::ZipPartMapper"]
    D --> E["jzip::ZipWriter"]

    A --> F["zip::ZipFile / zip::ZipInputStream"]
    G["zip::ZipOutputStream"] --> H["ZIP 输出文件"]

    C -. 适配/清洗 .-> F
```

## 4. `jzip` 层：结构化模型

`jzip` 的中心对象是 `zip_model.hpp` 里的 `jzip::ZipArchive`。

它持有一组 `ZipPart`，主要有三类：

- `LocalFileHeader`
- `CentralDirectoryFileHeader`
- `EndOfCentralDirectory`

关键点：

- `LocalFileHeader` 和 `CentralDirectoryFileHeader` 可以双向链接。
- `JvmLocalFileHeader` 是 `LocalFileHeader` 的 JVM 语义变体，用于 LOC/CEN 不一致时按 JVM 规则回读。
- `ZipArchive` 保留前缀数据 `prefixData`，因此可以处理前面带 junk 或拼接数据的样本。

## 5. `jzip` 层：读取策略

`zip_reader.hpp` 里有三种 reader：

### `ForwardScanZipReader`

适合标准 ZIP。按常规布局顺序找 EOCD 和 CEN。

特点：

- 行为更接近“规范 ZIP”
- 对 trick 样本容忍度不如 JVM reader

### `JvmZipReader`

这是 `libjzip` 默认最重要的 reader，目标是贴近 JVM/libzip 的现实行为。

特点：

- 从文件尾反向找 EOCD
- 支持 base offset 回退和 CEN/LOC 重新链接
- 遇到字段不一致时更偏向以 CEN 为权威
- 适合 JAR、拼接归档、畸形样本

### `NaiveLocalFileZipReader`

只扫 LOC，不依赖 CEN 和 EOCD。

适合：

- 调试
- 极端损坏样本的兜底读取
- 验证“只看 local headers 能读到什么”

## 6. `jzip` 层：写入策略

`zip_writer.hpp` 里有两条主要写出路径：

### `DirectZipWriter`

直接按 `ZipArchive` 当前结构写回。

适合：

- 尽量保留原结构
- 修改少量元数据后回写
- 需要保留重复 entry、原始 method、原始布局

这类 writer 的正确性依赖 `LOC <-> CEN` 链接关系。最近修复的一个关键问题就是重复文件名时不能只按名字找第一个 LOC，而要按 CEN->LOC 实际链接写回。

### `RecompressingZipWriter`

解压后重写成新 ZIP。

适合：

- 生成干净输出
- 主动规范化结构
- 不需要保留原始压缩布局

## 7. `jzip` 层：mapper

`zip_mapper.hpp` 提供对 `ZipArchive` 中部件的映射。

常用 mapper：

- `IdentityZipPartMapper`：不改
- `CentralAdoptingMapper`：让 LOC 采用 CEN 值
- `JvmClassDirectoryMapper`：修正 `.class/` 目录名这类 JVM 特殊情况
- `CopyingZipPartMapper`：做深拷贝映射

典型链路是：

1. reader 读出 `ZipArchive`
2. mapper 做标准化或复制
3. writer 写回

## 8. `zip` 层：stdlib 风格封装

`zip` 层对标 `java.util.zip`，提供按 entry 的易用读写：

- `zip::ZipFile`（`zip_file.hpp`）：随机访问读取
- `zip::ZipInputStream`（`zip_input_stream.hpp`）：顺序流式读取，对标 `java.util.zip.ZipInputStream`，已修过 data descriptor 处理和空 stored entry 回读
- `zip::ZipOutputStream`（`zip_output_stream.hpp`）：按 entry 顺序写

各类型的方法清单与签名直接查对应头文件（或见 `../readme.md` 的入口速查表）。

## 9. 文档导航

- 想理解对象关系：看 `MODEL.md`
- 想理解 EOCD / CEN / LOC / ZIP64 处理算法：看 `ALGORITHM.md`
- 想理解容器 trick 分类法（仓库通用）与 libjzip 样本挂载：看 `TRICKS.md`
- 想看项目总览、入口速查和选层建议：回到 `../readme.md`
