#pragma once

#include <ostream>
#include <vector>

#include "zip_model.hpp"

namespace jzip {

/// @brief ZIP 写入器接口。
class ZipWriter
{
public:
    /// @brief 析构函数。
    virtual ~ZipWriter() = default;

    /// @brief 写入 ZIP。
    /// @param archive 归档模型
    /// @param os 输出流
    virtual void write(const ZipArchive& archive, std::ostream& os) = 0;

    /// @brief 便捷方法：直接写入字节数组。
    /// 对标 Java ZipWriter.writeToByteArray()。
    std::vector<ca::u8> writeToByteArray(const ZipArchive& archive);
};

/// @brief 直接写入器。
class DirectZipWriter : public ZipWriter
{
public:
    /// @brief 写入 ZIP。
    void write(const ZipArchive& archive, std::ostream& os) override;

private:
    /// @brief 写入单个 Local File Header。
    /// 对标 Java DirectZipWriter.writeLocalFile()。
    void writeLocalFile(const LocalFileHeader& file, std::vector<ca::u8>& out);

    /// @brief 写入单个 Central Directory Header。
    /// 对标 Java DirectZipWriter.writeCentralDirectory()。
    void writeCentralDirectory(const CentralDirectoryFileHeader& dir, std::vector<ca::u8>& out);

    /// @brief 写入 End Of Central Directory 记录。
    /// 对标 Java DirectZipWriter.writeEnd()。
    void writeEnd(const EndOfCentralDirectory& end, std::vector<ca::u8>& out);

    /// @brief 当 archive 没有 CEN 时，从 locals 重建 CEN（C++ 内部兼容）。
    /// @param localHeaderOffsets 阶段一写入 LFH 时记录的每个 LFH 起始偏移量
    void writeBuiltCentralDirectories(const ZipArchive&           archive,
                                      const std::vector<ca::u32>& localHeaderOffsets,
                                      std::vector<ca::u8>&        out);

    /// @brief 当 archive 没有 EOCD 时，从 CEN 块构造一个最小 EOCD（C++ 内部兼容）。
    void writeBuiltEnd(ca::usize entryCount, ca::usize cenSize, ca::usize cenOffset,
                       std::vector<ca::u8>& out);
};

/// @brief 重新压缩写入器。
class RecompressingZipWriter : public ZipWriter
{
public:
    /// @brief 构造写入器，默认 target 为 0（stored）。
    /// @param createDirectoryEntries 是否生成目录条目
    /// @param targetMethod 目标压缩方式：0=stored, 8=deflated
    explicit RecompressingZipWriter(bool createDirectoryEntries = false, ca::u16 targetMethod = 0);

    /// @brief 写入 ZIP。
    void write(const ZipArchive& archive, std::ostream& os) override;

private:
    bool    createDirectoryEntries_ = false;
    ca::u16 targetMethod_           = 0;
};

/// @brief 委派写入器，包装另一个 ZipWriter。
/// 匹配 Java DelegatingZipWriter。
class DelegatingZipWriter : public ZipWriter
{
public:
    explicit DelegatingZipWriter(std::unique_ptr<ZipWriter> delegate)
        : delegate_(std::move(delegate))
    {}

    void write(const ZipArchive& archive, std::ostream& os) override
    {
        delegate_->write(archive, os);
    }

private:
    std::unique_ptr<ZipWriter> delegate_;
};

}   // namespace jzip
