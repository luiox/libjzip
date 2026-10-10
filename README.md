# libjzip

对齐 JVM 错误容忍行为的 ZIP/JAR 读写库（C++17 / xmake）。

- `src/jzip/`：对齐 [LL-Java-Zip](https://github.com/Col-E/LL-Java-Zip) 的结构化
  ZIP/JAR 模型，强调 JVM 语义、畸形样本容忍和可控写回。

## 这个库解决什么问题

- 读取普通 ZIP/JAR，也能处理 concat、junk header、重复目录项、data descriptor、
  ZIP64 这类 JVM 现实样本。
- 保留 ZIP 的结构信息，而不是只把文件内容解包出来。
- 在需要时直接重写原始结构，或者重压缩成干净归档。
- 面向 JVM 字节码工具链的 JAR 读写底层场景，作为独立库长期维护。

## 构建

xmake ≥ 3.0。依赖经 [luiox-repo](https://github.com/luiox/luiox-repo) 解析
（libca git 直连包），zlib/gtest 走 xrepo。首次构建先注册包仓库：

```sh
xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git
xmake f --with_tests=y -y     # 配置（gtest 仅测试需要）
xmake build libjzip           # 库本体
xmake build libjzip_unittest  # 单元测试
xmake run libjzip_unittest    # 跑测试（rundir = 仓库根）
```

## 目录

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

设计文档：分层架构与 reader/writer/mapper 见 [doc/design.md](doc/design.md)；
对象关系见 [doc/model.md](doc/model.md)；EOCD/CEN/LOC/ZIP64 处理算法见
[doc/algorithm.md](doc/algorithm.md)；容器 trick 分类法见 [doc/tricks.md](doc/tricks.md)。

## License

[MIT](LICENSE)
