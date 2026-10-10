#pragma once

#include <functional>
#include <string>
#include <vector>

#include "jzip/zip_io.hpp"

namespace jzip::test_utils {

// 检查字节序列中是否包含指定字符串
inline bool containsString(const std::vector<ca::u8>& data, const std::string& target)
{
    if (data.size() < target.size()) return false;
    for (size_t i = 0; i <= data.size() - target.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < target.size(); ++j) {
            if (data[i + j] != static_cast<ca::u8>(target[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// 验证 .class 文件的 magic (0xCAFEBABE)
inline bool isValidClassMagic(const std::vector<ca::u8>& data)
{
    return data.size() >= 4 && data[0] == 0xCA && data[1] == 0xFE && data[2] == 0xBA &&
           data[3] == 0xBE;
}

// 近似 Java JarInJarUtils.handleJar(): 递归处理 .class 和 .jar 条目
inline void handleJar(ZipArchive& archive)
{
    // Java 的 handleJar 会递归检查 .class 的 ClassReader 和 .jar 的解压再解析。
    // 在 C++ 中简化为验证 .class 的 magic 完整性。
    for (const auto* local : archive.getLocalFiles()) {
        const auto& name = local->getFileName();
        if (name.size() >= 6 && name.compare(name.size() - 6, 6, ".class") == 0) {
            auto decompressed = ZipIO::decompressLocalFile(*local);
            if (decompressed.empty() || !isValidClassMagic(decompressed)) {
                throw ZipParseException("invalid class: " + name, local->offset());
            }
        }
        else if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".jar") == 0) {
            auto decompressed = ZipIO::decompressLocalFile(*local);
            auto inner        = ZipIO::readStandard(MemorySegment(decompressed));
            handleJar(inner);
        }
    }
}

// 近似 Java JarInJarUtils.handleJar() 的 Supplier 重载
inline void handleJar(std::function<ZipArchive()> supplier)
{
    auto archive = supplier();
    handleJar(archive);
}

// 近似 Java Utils.assertDefinesString()
// 验证 decompressed class 中包含目标字符串
inline void assertDefinesString(const std::vector<ca::u8>& data, const std::string& target)
{
    if (!containsString(data, target)) {
        throw std::runtime_error("Expected string not found: " + target);
    }
}

// MemorySegment overload (mirrors Java assertDefinesString(MemorySegment, String))
inline void assertDefinesString(const MemorySegment& code, const std::string& target)
{
    assertDefinesString(code.toByteArray(), target);
}

}   // namespace jzip::test_utils
