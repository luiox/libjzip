#pragma once

#include <string>
#include <vector>

#include "zip_mapper.hpp"
#include "zip_model.hpp"

namespace jzip {

/// @brief ZIP IO 门面。
class ZipIO final
{
public:
    /// @brief 读取标准 ZIP。
    /// @param data ZIP 数据
    /// @param sink 可选结构异常 sink（不持有所有权；报告始终聚合进归档）
    static ZipArchive readStandard(const MemorySegment& data, ZipAnomalySink* sink = nullptr);

    /// @brief 读取本地头扫描 ZIP。
    /// @param data ZIP 数据
    /// @param sink 可选结构异常 sink
    static ZipArchive readNaive(const MemorySegment& data, ZipAnomalySink* sink = nullptr);

    /// @brief 读取 JVM 语义 ZIP。
    /// @param data ZIP 数据
    /// @param sink 可选结构异常 sink
    static ZipArchive readJvm(const MemorySegment& data, ZipAnomalySink* sink = nullptr);

    /// @brief 读取文件路径。
    /// @param path 文件路径
    static ZipArchive readAdaptingIO(const std::string& path);

    /// @brief 直接写入 ZIP。
    /// @param archive 归档模型
    static std::vector<ca::u8> writeDirect(const ZipArchive& archive);

    /// @brief 解压本地文件。
    /// @param local 本地头
    static std::vector<ca::u8> decompressLocalFile(const LocalFileHeader& local);

    /// @brief 映射归档。
    /// @param archive 归档模型
    /// @param mapper 映射器
    static ZipArchive mapArchive(ZipArchive archive, const ZipPartMapper& mapper);
};

}   // namespace jzip
