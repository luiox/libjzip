#pragma once

#include <memory>
#include <optional>

#include "memory_segment.hpp"
#include "zip64_support.hpp"
#include "zip_model.hpp"

namespace jzip {

/// @brief ZipPart 分配器。
class ZipPartAllocator
{
public:
    /// @brief 析构函数。
    virtual ~ZipPartAllocator() = default;

    /// @brief 创建 LocalFileHeader。
    virtual std::unique_ptr<LocalFileHeader> newLocalFileHeader() = 0;

    /// @brief 创建 CentralDirectoryFileHeader。
    virtual std::unique_ptr<CentralDirectoryFileHeader> newCentralDirectoryFileHeader() = 0;

    /// @brief 创建 EndOfCentralDirectory。
    virtual std::unique_ptr<EndOfCentralDirectory> newEndOfCentralDirectory() = 0;
};

/// @brief 默认 ZipPart 分配器。
class SimpleZipPartAllocator final : public ZipPartAllocator
{
public:
    /// @brief 创建 LocalFileHeader。
    std::unique_ptr<LocalFileHeader> newLocalFileHeader() override;

    /// @brief 创建 CentralDirectoryFileHeader。
    std::unique_ptr<CentralDirectoryFileHeader> newCentralDirectoryFileHeader() override;

    /// @brief 创建 EndOfCentralDirectory。
    std::unique_ptr<EndOfCentralDirectory> newEndOfCentralDirectory() override;
};

/// @brief JVM 风格 ZipPart 分配器。
class JvmZipPartAllocator final : public ZipPartAllocator
{
public:
    /// @brief 创建 LocalFileHeader。
    std::unique_ptr<LocalFileHeader> newLocalFileHeader() override;

    /// @brief 创建 CentralDirectoryFileHeader。
    std::unique_ptr<CentralDirectoryFileHeader> newCentralDirectoryFileHeader() override;

    /// @brief 创建 EndOfCentralDirectory。
    std::unique_ptr<EndOfCentralDirectory> newEndOfCentralDirectory() override;
};

/// @brief ZIP 读取器接口。
class ZipReader
{
public:
    /// @brief 析构函数。
    virtual ~ZipReader() = default;

    /// @brief 读取 ZIP。
    /// @param archive 归档输出
    /// @param data ZIP 数据
    virtual void read(ZipArchive& archive, const MemorySegment& data) = 0;

    /// @brief 本地文件头后处理。
    /// @param header 本地文件头
    virtual void postProcessLocalFileHeader(LocalFileHeader& header) { (void)header; }

    /// @brief 注入异常事件 sink（可选；不注入时仅归档内报告生效）。
    /// @param sink sink 指针，不持有所有权
    void setAnomalySink(ZipAnomalySink* sink) { anomalySink_ = sink; }

protected:
    /// @brief 向归档报告与 sink 记录一条结构异常。
    /// @param archive 归档（取其报告）
    /// @param kind 异常类型
    /// @param offset 相关偏移
    /// @param entry 关联条目名
    /// @param detail 附加说明
    void emit(ZipArchive& archive, ZipAnomalyKind kind, ca::u64 offset, std::string entry,
              std::string detail)
    {
        jzip::emitAnomaly(archive.getReadReport(), anomalySink_, kind, offset, std::move(entry),
                          std::move(detail));
    }

    ZipAnomalySink* anomalySink_ = nullptr;
};

/// @brief 顺序扫描读取器。
class ForwardScanZipReader : public ZipReader
{
public:
    /// @brief 构造读取器。
    /// @param allocator 分配器
    explicit ForwardScanZipReader(
        std::unique_ptr<ZipPartAllocator> allocator = std::make_unique<SimpleZipPartAllocator>());

    /// @brief 读取 ZIP。
    void read(ZipArchive& archive, const MemorySegment& data) override;

private:
    std::unique_ptr<ZipPartAllocator> allocator_;
};

/// @brief JVM 语义读取器。
class JvmZipReader : public ZipReader
{
public:
    /// @brief 构造 JVM 读取器。
    /// @param skipRevisitedCenToLocalLinks 跳过重复链接
    /// @param allowBasicJvmBaseOffsetZeroCheck 允许零偏移检查
    /// @param allocator 分配器
    JvmZipReader(
        bool skipRevisitedCenToLocalLinks = true, bool allowBasicJvmBaseOffsetZeroCheck = true,
        std::unique_ptr<ZipPartAllocator> allocator = std::make_unique<JvmZipPartAllocator>());

    /// @brief 读取 ZIP。
    void read(ZipArchive& archive, const MemorySegment& data) override;

private:
    /// @brief EOCD 验证结果。
    /// centralDirectoryEnd = CEN 块 exclusive 上界。
    /// 无 ZIP64 时 = endOffset（EOCD 起始），有 ZIP64 时 = ZIP64 EOCD 记录的偏移。
    struct EndInfo
    {
        std::unique_ptr<EndOfCentralDirectory> end;
        ca::usize                              centralDirectoryStart;
        ca::usize                              centralDirectoryEnd;   ///< CEN 块 exclusive 上界
        std::ptrdiff_t baseOffset;   ///< centralDirectoryStart - centralDirectoryOffset
    };

    /// @brief 从文件尾反向扫描并验证 EOCD。
    /// 逐个候选解码验证，返回第一个通过结构验证的 EOCD。
    std::unique_ptr<EndInfo> findEndOfCentralDirectory(const MemorySegment& data, ZipArchive& zip);

    /// @brief 解码并验证单个 EOCD 候选。
    /// @param data ZIP 数据
    /// @param offset 候选 EOCD 签名偏移
    /// @return 验证通过的 EndInfo，或 nullptr
    std::unique_ptr<EndInfo> tryReadEndOfCentralDirectory(const MemorySegment& data,
                                                          ca::usize            offset);

    /// @brief 结构验证：CEN 签名 + LOC 映射 + name_length 一致性。
    /// 当 EOCD 声明的 comment 长度未对齐文件尾时启用此回退验证。
    bool isValidEndHeader(const MemorySegment& data, ca::usize endPos, ca::u32 centralDirectorySize,
                          ca::u32 centralDirectoryOffset, ca::u16 entryCount);

    /// @brief 在验证后的 EOCD 边界内有界解析 CEN 条目。
    /// @param zip 归档输出
    /// @param data ZIP 数据
    /// @param centralDirectoryStart CEN 块起始（绝对偏移）
    /// @param endOffset EOCD 起始（绝对偏移，用作 exclusive 上界）
    void readCentralDirectories(ZipArchive& zip, const MemorySegment& data,
                                ca::usize centralDirectoryStart, ca::usize endOffset);

    /// @brief 检查 baseOffset 能否将至少一个 CEN 映射到真实 LOC。
    bool hasAnyLinkedLocalHeader(const MemorySegment& data, ZipArchive& zip,
                                 std::ptrdiff_t baseOffset);

    /// @brief 回退扫描：在 END 推导位置附近找第一个 PK 签名。
    std::optional<ca::usize> scanForLocalHeaderBaseOffset(const MemorySegment&         data,
                                                          const EndOfCentralDirectory& end);

    bool                              skipRevisitedCenToLocalLinks_     = true;
    bool                              allowBasicJvmBaseOffsetZeroCheck_ = true;
    std::unique_ptr<ZipPartAllocator> allocator_;
};

/// @brief 仅扫描本地头的读取器。
class NaiveLocalFileZipReader : public ZipReader
{
public:
    /// @brief 构造读取器。
    /// @param allocator 分配器
    explicit NaiveLocalFileZipReader(
        std::unique_ptr<ZipPartAllocator> allocator = std::make_unique<SimpleZipPartAllocator>());

    /// @brief 读取 ZIP。
    void read(ZipArchive& archive, const MemorySegment& data) override;

private:
    std::unique_ptr<ZipPartAllocator> allocator_;
};

}   // namespace jzip
