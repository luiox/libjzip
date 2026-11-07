# 参与贡献

## 库的定位

libjzip 是 C++17 的 ZIP/JAR 结构化读写库，行为对齐 LL-Java-Zip 与 JVM 的容错语义，是独立实现而非移植。改动不应复制第三方库代码；对畸形归档的处理要能说明依据（JVM 实际行为或 `doc/algorithm.rst` 记录的算法约定）。

## 开始之前

- API 形状、新模块、新的容错策略先开 issue 对齐；小修复和文档修正可以直接提 PR。
- 修改 reader / writer 行为时，同步检查 `doc/algorithm.rst`、`doc/model.rst`、`doc/tricks.rst` 是否需要更新。

## 文档规范

三层按**权威来源**分工，扩展名即角色：

| 载体 | 层 | 写什么 | 谁能引用它 |
|---|---|---|---|
| `///` Doxygen | 微观 | 单个符号的契约：参数、返回、失败模式、不变量；单模块的「背景/定位」（`@defgroup` 组注） | 随代码移动 |
| `doc/*.rst` | 宏观 | 跨模块设计、数据模型、读写算法、容器威胁分类法 | 代码/测试/构建脚本 |
| `*.md` | 人 + AI 方向 | README、CONTRIBUTING 等流程文档 | 只有人和 AI 读 |

理由：markdown 撑不住代码耦合的规格——无 label、无校验引用面，代码改名/行号漂移后引用静默腐烂。RST 白送可校验的引用面，规范才谈得上执行。

硬规则（`doc/check_refs.py` + Sphinx `-W` 在 CI 挡）：

1. 代码/测试/构建脚本只可引用 `doc/*.rst`，路径必须存在；出现 `.md` 设计引用即失败（流程文档白名单除外）。
2. 引用定位用 label，形式 `doc:<label>`；被引用的位置必须有 `.. _<label>:` 声明（不必每节都放），命名 `<doc-slug>[-<section-slug>]`。跨文档的 `§N` 一律换 `:ref:`，编号会随插入章节漂移。
3. RST 里「``X`` 头注」指向微观层：`X` 必须是某个 `@defgroup` 组名或头文件名。
4. RST 站点以 `sphinx-build -b html -E -W --keep-going doc doc/_build` 构建，警告即失败；本地验证先 `pip install "sphinx==9.1.1"`（与 CI 钉同版）。
5. 微观层以 `doxygen doc/Doxyfile` 构建，`WARN_AS_ERROR` 生效；`///` 注释是 API 契约的唯一来源，与代码同仓同改。

RST 写作约定（中文正文的坑）：表格一律 `.. list-table::`（禁 grid table，全角字形会破坏边框校验）；行内代码紧邻中文时在闭定界符后补转义空格（docutils 要求 end-string 后接空白或 ASCII 标点）；标题层级 `=` / `-` / `^`；跨仓出处带仓名标记（LL-Java-Zip / libca），带标记视为 prose 不校验存在性。

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
