#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_mapper.hpp"
#include "test_helper.hpp"

namespace jzip::test {

/// @brief 读取文件字节。
/// @param path 文件路径
inline std::vector<ca::u8> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }

    in.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    std::vector<ca::u8> data(size);
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    }
    return data;
}

/// @brief 获取测试资源路径。
/// @param file_name 文件名
inline std::filesystem::path test_resource_path(const std::string& file_name)
{
    return ::test::getResourcePath(file_name);
}

/// @brief 判断归档中是否包含指定名称的本地文件。
/// @param zip 归档
/// @param name 文件名
inline bool has_file(const ZipArchive& zip, const std::string& name)
{
    for (const auto* local : zip.getLocalFiles()) {
        if (local->getFileName() == name) {
            return true;
        }
    }
    return false;
}

/// @brief 按名称查找本地文件。
/// @param zip 归档
/// @param name 文件名
/// @return 本地文件指针，未找到返回 nullptr
inline const LocalFileHeader* get_local_file_by_name(const ZipArchive& zip, const std::string& name)
{
    for (const auto* local : zip.getLocalFiles()) {
        if (local->getFileName() == name) {
            return local;
        }
    }
    return nullptr;
}

/// @brief 按名称谓词过滤本地文件。
/// @param zip 归档
/// @param predicate 名称谓词
inline std::vector<const LocalFileHeader*> get_filtered_local_files(
    const ZipArchive& zip, const std::function<bool(const std::string&)>& predicate)
{
    std::vector<const LocalFileHeader*> result;
    for (const auto* local : zip.getLocalFiles()) {
        if (predicate(local->getFileName())) {
            result.push_back(local);
        }
    }
    return result;
}

/// @brief 检查字节序列是否为有效 Java class 文件（以 0xCAFEBABE 开头）。
/// @param code 字节序列
inline bool is_valid_class(const std::vector<ca::u8>& code)
{
    return code.size() >= 4 && code[0] == static_cast<ca::u8>(0xCA) &&
           code[1] == static_cast<ca::u8>(0xFE) && code[2] == static_cast<ca::u8>(0xBA) &&
           code[3] == static_cast<ca::u8>(0xBE);
}

/// @brief 检查 class 字节中是否包含指定字符串。
/// 对应 Java 版本的 Utils.assertDefinesString()，但简化为字符串搜索。
/// @param code class 字节
/// @param target 目标字符串
inline bool class_defines_string(const std::vector<ca::u8>& code, const std::string& target)
{
    if (code.size() < target.size()) {
        return false;
    }
    for (size_t i = 0; i + target.size() <= code.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < target.size(); ++j) {
            if (code[i + j] != static_cast<ca::u8>(target[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

/// @brief 将字节序列转换为字符串。
/// @param data 字节序列
inline std::string bytes_to_string(const std::vector<ca::u8>& data)
{
    return std::string(data.begin(), data.end());
}

/// @brief 尝试解析 class 文件，失败时抛出异常。
/// 对应 Java 版本的 JarInJarUtils.handleClass()。
/// @param name 文件名
/// @param local 本地文件头
inline void handle_class(const std::string& name, const LocalFileHeader& local)
{
    auto entryData = ZipIO::decompressLocalFile(local);
    if (!is_valid_class(entryData)) {
        throw std::runtime_error("Failed to parse class: " + name);
    }
}

/// @brief 递归验证 JAR 内容。
/// 对应 Java 版本的 JarInJarUtils.handleJar()。
/// - .class 条目：验证解压后为有效 class 文件
/// - .jar 条目：递归解析并验证内嵌 JAR
/// @param zip 归档
inline void handle_jar(ZipArchive& zip)
{
    for (const auto* local : zip.getLocalFiles()) {
        const auto& entry_name = local->getFileName();
        if (entry_name.size() >= 6 && entry_name.compare(entry_name.size() - 6, 6, ".class") == 0) {
            handle_class(entry_name, *local);
        }
        else if (entry_name.size() >= 4 &&
                 entry_name.compare(entry_name.size() - 4, 4, ".jar") == 0) {
            auto decompressed = ZipIO::decompressLocalFile(*local);
            try {
                auto inner = ZipIO::readStandard(MemorySegment(decompressed));
                handle_jar(inner);
            }
            catch (const std::exception&) {
                throw std::runtime_error("Failed to read inner jar: " + entry_name);
            }
        }
    }
}

}   // namespace jzip::test
