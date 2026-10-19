# libjzip

对齐 JVM 容错语义的 ZIP/JAR 结构化读写库：畸形样本容忍、结构保留、可控写回（C++17 / xmake）。

C++17 实现的 [LL-Java-Zip](https://github.com/Col-E/LL-Java-Zip) 结构化 ZIP/JAR 模型，
面向 JVM 字节码工具链的 JAR 读写底层场景，作为独立库长期维护。

## 特性

- **畸形样本容忍**：拼接（concat）、junk header、重复目录项、data descriptor、ZIP64、
  前缀垃圾等 JVM 现实样本均可读取，歧义处以 JVM / CEN 语义为准
- **结构保留**：`LOC / CEN / EOCD / 前缀数据` 全量建模，保留重复 entry、原始压缩方式
  与字节布局——不是只把内容解包出来
- **可控写回**：按模型直接重写原始结构，或解压后重压缩成干净归档
- **三种读取策略**：standard（前向规范扫描）/ jvm（反向 EOCD + CEN 权威收养）/
  naive（只扫 LOC 兜底），读取过程产出结构异常报告（`ZipAnomalySink`）
- **解压防线**：每条目输出配额默认生效，敌意 JAR 批量扫描场景防 zip 炸弹，
  可按需 opt-in 更高上限
- **纯内存结构库**：字节进出、不做文件 IO；文件路径读取委托 libca 的 `ca::zip`

## 快速开始

依赖 [xmake](https://xmake.io)（≥ 3.0）。先注册
[luiox-repo](https://github.com/luiox/luiox-repo)（libjzip 及其依赖 libca 均为该仓库的
git 直连包）：

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
```

在工程 `xmake.lua` 中接入：

```lua
add_requires("libjzip")

target("demo")
    set_kind("binary")
    add_files("src/*.cpp")
    add_packages("libjzip")
```

读取 JAR、遍历条目并写回：

```cpp
#include "jzip/zip_io.hpp"
#include "jzip/zip_model.hpp"

// 归档字节来源自定（文件 / 网络 / 内存均可），本库纯内存解析
std::vector<ca::u8> bytes = load_jar_bytes();

// JVM 语义解析：对前缀垃圾、拼接归档、LOC/CEN 不一致等保持容忍
jzip::ZipArchive archive = jzip::ZipIO::readJvm(jzip::MemorySegment(bytes));

for (const auto* local : archive.getLocalFiles()) {
    const std::string&  name = local->getFileName();   // 原始条目名（目录项保留尾斜杠）
    std::vector<ca::u8> data = jzip::ZipIO::decompressLocalFile(*local);   // stored / deflate
}

// 按当前模型直接序列化；改用 RecompressingZipWriter 可产出重压缩的干净归档
std::vector<ca::u8> out = jzip::ZipIO::writeDirect(archive);
```

## 构建与测试（本仓开发）

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
xmake f --with_tests=y -y        # 配置（gtest 仅测试需要）
xmake build libjzip              # 库本体
xmake build libjzip_unittest     # 单元测试
xmake run libjzip_unittest       # 跑测试（rundir = 仓库根）
xmake build libjzip_origintest   # LL-Java-Zip 对齐测试
xmake run libjzip_origintest
```

## 模块速览

```text
├── src/jzip/        LL-Java-Zip 对齐层：模型、reader、writer、mapper
├── test/origin/     对齐 LL-Java-Zip 的原版行为测试
├── test/unittest/   C++ 侧补充回归测试
├── test/crossval/   Java/C++ 对拍工具
├── test_resource/   测试样本（畸形 ZIP/JAR 集合）
├── test_helper/     测试门面（libca.test 布局机制的薄封装）
├── doc/             设计文档
└── xmake.lua        独立构建根
```

## 依赖说明

| 依赖 | 来源 | 说明 |
|------|------|------|
| libca 0.0.16 | luiox-repo（git 直连包） | 运行依赖 core / zip 模块，测试另需 test 模块；版本锁固定，升级须显式换版 |
| zlib | xrepo | deflate 压缩 / 解压 |
| gtest / gmock | xrepo | 仅 `--with_tests=y` 时拉取 |

## 状态与 License

- 项目**开发中、未定版**（0.x）：公共 API 可能随版本调整，不兼容变更见提交说明与设计文档
- 以 [MIT](LICENSE) 协议发布

## 深入阅读

- [doc/design.md](doc/design.md)：分层架构与 reader / writer / mapper 定位
- [doc/model.md](doc/model.md)：对象模型与 CEN↔LOC 双向链接
- [doc/algorithm.md](doc/algorithm.md)：EOCD 验证链、扫描边界、DD 裁剪、ZIP64 算法
- [doc/tricks.md](doc/tricks.md)：容器 trick 分类法与畸形样本挂载表
