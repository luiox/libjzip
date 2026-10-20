# 参与贡献

## 库的定位

libjzip 是 C++17 的 ZIP/JAR 结构化读写库，行为对齐 [LL-Java-Zip](https://github.com/Col-E/LL-Java-Zip) 与 JVM 的容错语义，是独立实现而非移植。改动不应复制第三方库代码；对畸形归档的处理要能说明依据（JVM 实际行为或 `doc/algorithm.md` 记录的算法约定）。

## 开始之前

- API 形状、新模块、新的容错策略先开 issue 对齐；小修复和文档修正可以直接提 PR。
- 修改 reader / writer 行为时，同步检查 `doc/algorithm.md`、`doc/model.md`、`doc/tricks.md` 是否需要更新。

## 构建与测试

先注册依赖仓库：

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
```

然后：

```sh
xmake f -y --with_tests=y
xmake build libjzip_unittest
xmake run libjzip_unittest
```

LL-Java-Zip 对齐测试：

```sh
xmake build libjzip_origintest
xmake run libjzip_origintest
```

CI 在 Linux 和 Windows 上跑这两个测试 target；`test/crossval/` 的 Java/C++ 对拍需要额外的 JDK 22 与 Gradle，不在 CI 内。

## 基本要求

- 敌意输入是常态：`test_resource/` 里的畸形样本必须能解析且不崩溃、不泄漏、不死循环。新增容错分支要经 `ZipAnomalySink` 留下可观测记录，不能静默吞掉。
- 头文件 Doxygen 注释是 API 契约的唯一来源；`doc/` 只记录设计与算法取舍。
- 新测试必须在没有本次改动时失败。
- 不要为了让测试通过而放宽断言；断言确实错了就单独修，并在提交说明里写原因。
- stored(0) / deflate(8) 之外的压缩方式不支持是既定边界：解压入口应显式抛 `ZipParseException`，不能静默跳过条目。

## 风格

- 跟随周边文件：4 空格缩进、K&R 花括号；函数与变量 `snake_case`，类型 `CamelCase`，常量 `k` 前缀。
- 注释用中文，写约束来源和取舍原因，不复述代码行为。
- 头文件包含用引号形式 `#include "jzip/xxx.hpp"`。

## 提交信息

一条提交只做一件事，写清可观察的行为变化；仓库惯用 `[libjzip]` 前缀。

## License

贡献代码按仓库的 MIT 协议授权。
