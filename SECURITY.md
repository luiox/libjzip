# 安全策略

## 支持的版本

项目处于 0.x 开发期，只有最新的 `main` 与最新 tag 接受安全修复。

## 报告漏洞

请使用 GitHub 的私密漏洞报告（**Security → Report a vulnerability**），不要开公开 issue，也可以直接联系维护者。

报告请包含：

- 受影响的版本、tag 或 commit
- 平台与工具链（如 Windows/MSVC 2022、Linux/GCC 13、xmake 版本）
- 能复现问题的最小归档（可脱敏，但保留触发所需的 ZIP 结构）
- 观察到的后果（崩溃、越界读写、内存无界增长、死循环等）

## 范围

库直接解析不可信的 ZIP/JAR 字节，以下问题在范围内：

- 越界读写、未定义行为或崩溃
- 绕过 `defaultEntryOutputCap` 的 zip 炸弹或无界内存分配
- 单个恶意归档触发的死循环
- 写路径把恶意输入转成越界访问或损坏的输出

以下不算漏洞：

- `doc/algorithm.md` 记录的容错边界：reader 按 JVM / CEN 语义接受某些畸形结构，这是有意的设计取舍
- stored(0) / deflate(8) 之外的压缩方式不支持，入口直接抛 `ZipParseException`
- 解压配额默认值（min(1 GiB, 压缩大小 × 1000)，下限 1 MiB）本身；除非能证明配额计算可被绕过
