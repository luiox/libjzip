#pragma once

#include <string>

#include "memory_segment.hpp"
#include "zip_model.hpp"
#include "zip_reader.hpp"

namespace jzip {

/// @brief 适配读取器，委托给 stdlib 风格 ZipFile，
/// 并将所有条目解压为 stored method（method 0），得到干净的归档模型。
///
/// 对应 LL-Java-Zip 的 AdaptingZipReader，Java 版委托给 java.util.zip.ZipFile，
/// C++ 版委托给 libca/zip 的 ca::zip::ZipFile。
class AdaptingZipReader final : public ZipReader
{
public:
    /// @brief 读取 ZIP。
    /// @param archive 归档输出
    /// @param data ZIP 数据
    void read(ZipArchive& archive, const MemorySegment& data) override;

    /// @brief 从文件路径读取 ZIP。
    /// @param archive 归档输出
    /// @param path ZIP 文件路径
    void readFromPath(ZipArchive& archive, const std::string& path);
};

}   // namespace jzip
