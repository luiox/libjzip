#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "libca/core/datatype.hpp"
#include "memory_segment.hpp"
#include "zip_anomaly.hpp"

namespace jzip {

class Decompressor;
class CentralDirectoryFileHeader;
class ZipPartMapper;

/// @brief ZIP 解析异常。
class ZipParseException : public std::runtime_error
{
public:
    /// @brief 构造解析异常。
    /// @param message 异常描述
    /// @param offset 相关偏移
    ZipParseException(const std::string& message, ca::usize offset = 0);

    /// @brief 获取异常偏移。
    ca::usize getOffset() const noexcept;

private:
    ca::usize offset_ = 0;
};

/// @brief Extra Field 条目。
struct ExtraFieldEntry
{
    ca::u16             tag  = 0;
    ca::u16             size = 0;
    std::vector<ca::u8> data;
};

/// @brief NTFS 时间扩展字段。
struct NtfsExtraTime
{
    ca::u64 modify_ms   = 0;
    ca::u64 access_ms   = 0;
    ca::u64 creation_ms = 0;
};

/// @brief Unix 时间扩展字段。
struct UnixExtraTime
{
    ca::u64 modify_ms   = 0;
    ca::u64 access_ms   = 0;
    ca::u64 creation_ms = 0;
};

/// @brief Extra Field 解析工具。
class ExtraFieldParser final
{
public:
    /// @brief 解析 Extra Field 字节序列。
    /// @param extra_bytes 原始字节
    static std::vector<ExtraFieldEntry> parse_entries(const std::vector<ca::u8>& extra_bytes);

    /// @brief 解析 NTFS 时间扩展。
    /// @param entries Extra Field 列表
    static std::optional<NtfsExtraTime> parse_ntfs_time(
        const std::vector<ExtraFieldEntry>& entries);

    /// @brief 解析 Unix 时间扩展。
    /// @param entries Extra Field 列表
    static std::optional<UnixExtraTime> parse_unix_time(
        const std::vector<ExtraFieldEntry>& entries);
};

/// @brief ZIP 部件类型。
enum class PartType
{
    LOCAL_FILE_HEADER,
    CENTRAL_DIRECTORY_FILE_HEADER,
    END_OF_CENTRAL_DIRECTORY
};

/// @brief ZIP 部件基类。
class ZipPart
{
public:
    /// @brief 析构函数。
    virtual ~ZipPart() = default;

    /// @brief 获取部件类型。
    virtual PartType type() const = 0;

    /// @brief 获取部件长度。
    virtual ca::usize length() const = 0;

    /// @brief 获取部件偏移。
    virtual ca::usize offset() const = 0;

    /// @brief 从内存段读取部件。
    /// @param data 数据段
    /// @param offset 起始偏移
    virtual void read(const MemorySegment& data, ca::usize offset) = 0;
};

/// @brief ZIP 文件头公共字段。
class AbstractZipFileHeader : public ZipPart
{
public:
    /// @brief 读取公共头字段。
    /// @param data 数据段
    /// @param offset 起始偏移
    void read(const MemorySegment& data, ca::usize offset) override;

    /// @brief 读取文件数据。
    /// @param data 数据段
    /// @param headerOffset 头偏移
    virtual void readFileData(const MemorySegment& data, ca::usize headerOffset) = 0;

    /// @brief 获取解压所需版本。
    ca::u16 getVersionNeededToExtract() const;

    /// @brief 设置解压所需版本。
    /// @param value 版本号
    void setVersionNeededToExtract(ca::u16 value);

    /// @brief 获取通用标志位。
    ca::u16 getGeneralPurposeBitFlag() const;

    /// @brief 设置通用标志位。
    /// @param value 标志位
    void setGeneralPurposeBitFlag(ca::u16 value);

    /// @brief 获取压缩方法。
    ca::u16 getCompressionMethod() const;

    /// @brief 设置压缩方法。
    /// @param value 方法号
    void setCompressionMethod(ca::u16 value);

    /// @brief 获取修改时间。
    ca::u16 getLastModFileTime() const;

    /// @brief 设置修改时间。
    /// @param value 时间
    void setLastModFileTime(ca::u16 value);

    /// @brief 获取修改日期。
    ca::u16 getLastModFileDate() const;

    /// @brief 设置修改日期。
    /// @param value 日期
    void setLastModFileDate(ca::u16 value);

    /// @brief 获取 CRC32。
    ca::u32 getCrc32() const;

    /// @brief 设置 CRC32。
    /// @param value CRC32
    void setCrc32(ca::u32 value);

    /// @brief 获取压缩后大小。
    /// @note 对标 Java long，Zip64 扩展值需要 64 位。
    ca::i64 getCompressedSize() const;

    /// @brief 设置压缩后大小。
    /// @param value 大小
    void setCompressedSize(ca::i64 value);

    /// @brief 获取解压后大小。
    /// @note 对标 Java long，Zip64 扩展值需要 64 位。
    ca::i64 getUncompressedSize() const;

    /// @brief 设置解压后大小。
    /// @param value 大小
    void setUncompressedSize(ca::i64 value);

    /// @brief 压缩大小是否来自 Zip64 扩展字段。
    bool hasZip64CompressedSize() const;

    /// @brief 解压大小是否来自 Zip64 扩展字段。
    bool hasZip64UncompressedSize() const;

    /// @brief 获取文件名长度。
    ca::u16 getFileNameLength() const;

    /// @brief 获取 Extra Field 长度。
    ca::u16 getExtraFieldLength() const;

    /// @brief 获取文件名。
    const std::string& getFileName() const;

    /// @brief 设置文件名。
    /// @param name 文件名
    void setFileName(const std::string& name);

    /// @brief 获取 Extra Field 原始字节。
    const std::vector<ca::u8>& getExtraField() const;

    /// @brief 设置 Extra Field 原始字节。
    /// @param data 字节数组
    void setExtraField(const std::vector<ca::u8>& data);

    /// @brief 设置数据段引用。
    /// @param data 数据段
    void setDataSegment(const MemorySegment& data);

    /// @brief 设置头偏移。
    /// @param offset 偏移
    void setOffset(ca::usize offset);

protected:
    MemorySegment       data_{};
    ca::usize           offset_                 = static_cast<ca::usize>(-1);
    ca::u16             versionNeededToExtract_ = 0;
    ca::u16             generalPurposeBitFlag_  = 0;
    ca::u16             compressionMethod_      = 0;
    ca::u16             lastModFileTime_        = 0;
    ca::u16             lastModFileDate_        = 0;
    ca::u32             crc32_                  = 0;
    ca::i64             compressedSize_         = 0;
    ca::i64             uncompressedSize_       = 0;
    ca::u16             fileNameLength_         = 0;
    ca::u16             extraFieldLength_       = 0;
    std::string         fileName_;
    std::vector<ca::u8> extraField_;
    bool                zip64CompressedSize_   = false;
    bool                zip64UncompressedSize_ = false;
};

/// @brief 本地文件头。
class LocalFileHeader : public AbstractZipFileHeader
{
public:
    /// @brief 本地文件头最小固定长度（签名 + 2+2+2+2+2+4+4+4+2+2 = 30）。
    static constexpr ca::usize kMinFixedSize = 30;

    /// @brief 读取本地文件头。
    /// @param data 数据段
    /// @param offset 起始偏移
    void read(const MemorySegment& data, ca::usize offset) override;

    /// @brief 读取文件数据。
    /// @param data 数据段
    /// @param headerOffset 头偏移
    void readFileData(const MemorySegment& data, ca::usize headerOffset) override;

    /// @brief 获取部件类型。
    PartType type() const override;

    /// @brief 获取部件长度。
    ca::usize length() const override;

    /// @brief 获取部件偏移。
    ca::usize offset() const override;

    /// @brief 采用关联 Central 的字段值。
    virtual void adoptLinkedCentralDirectoryValues();

    /// @brief 获取文件数据。
    const std::vector<ca::u8>& getFileData() const;

    /// @brief 设置文件数据。
    /// @param data 文件数据
    void setFileData(const std::vector<ca::u8>& data);

    /// @brief 设置文件数据结束偏移，自动计算长度并更新文件数据。
    /// 对标 Java LocalFileHeader.setFileDataEndOffset()。
    /// @param endOffset 文件数据的绝对结束偏移
    void setFileDataEndOffset(ca::usize endOffset);

    /// @brief 设置文件数据长度，从原始数据中重新切片。
    /// 对标 Java LocalFileHeader.setFileDataLength()。
    /// @param newLength 新的文件数据长度
    void setFileDataLength(ca::usize newLength);

    /// @brief 关联 Central 头。
    /// @param cen 目录项
    void link(CentralDirectoryFileHeader* cen);

    /// @brief 获取关联的 Central 头。
    CentralDirectoryFileHeader* getLinkedDirectoryFileHeader() const;

    /// @brief 解压文件数据。
    /// @param decompressor 解压器
    std::vector<ca::u8> decompress(Decompressor& decompressor) const;

    /// @brief 设置是否存在数据描述符。
    /// @param hasDescriptor 是否存在
    /// @param size 描述符长度
    void setDataDescriptor(bool hasDescriptor, ca::u32 size);

    /// @brief 是否存在数据描述符。
    bool hasDataDescriptor() const;

    /// @brief 获取数据描述符长度。
    ca::u32 getDataDescriptorSize() const;

    /// @brief 设置 JVM 偏移列表（JvmLocalFileHeader 覆写，基类空实现）。
    /// @param offsets 偏移集合
    virtual void setOffsets(std::shared_ptr<const std::set<ca::usize>> offsets) { (void)offsets; }

    /// @brief 获取解析后的 Extra Field 列表。
    const std::vector<ExtraFieldEntry>& getExtraFields() const;

    /// @brief 设置解析后的 Extra Field 列表。
    /// @param entries Extra Field 列表
    void setExtraFields(const std::vector<ExtraFieldEntry>& entries);

    /// @brief 从关联的 Central Directory 合并 Extra Field（仅追加到 parsed 列表，不影响 raw 字段）。
    void appendExtraFieldsFromCen();

    /// @brief 检查本地文件头与关联的中央目录是否有字段值不一致。
    /// 注意：此方法比较的是当前 LocalFileHeader 中实际存储的字段值，
    /// 而非原始 LOC 字节中的值。JVM 读取器会覆盖 LOC 字段为 CEN 值，
    /// 因此对于 JVM 读取器产生的条目，此方法会返回 false。
    bool hasDifferentValuesThanCentralDirectoryHeader() const;

    /// @brief 创建当前对象的深拷贝。
    /// 对标 Java LocalFileHeader.copy()，返回独立副本。
    /// 注意：linkedDirectory 指针不复制，副本中为 nullptr；
    /// 对 JvmLocalFileHeader 派生对象调用会退化为基类副本（JVM 扫描边界
    /// 等扩展状态不保留），需要保留 JVM 语义时不要走此拷贝。
    std::unique_ptr<LocalFileHeader> copy() const;

private:
    std::vector<ca::u8>          fileData_;
    CentralDirectoryFileHeader*  linkedDirectory_    = nullptr;
    bool                         hasDataDescriptor_  = false;
    ca::u32                      dataDescriptorSize_ = 0;
    std::vector<ExtraFieldEntry> extraFields_;
};

/// @brief JVM 语义本地文件头。
class JvmLocalFileHeader : public LocalFileHeader
{
public:
    /// @brief 读取本地文件头（保存原始 LOC 字段长度供 readFileData 使用）。
    void read(const MemorySegment& data, ca::usize offset) override;

    /// @brief 设置 JVM 偏移列表。
    /// @param offsets 偏移列表
    void setOffsets(std::shared_ptr<const std::set<ca::usize>> offsets) override;

    /// @brief 读取文件数据。
    void readFileData(const MemorySegment& data, ca::usize headerOffset) override;

    /// @brief 采用关联 Central 的字段值，并按 JVM 语义必要时重对齐数据区。
    void adoptLinkedCentralDirectoryValues() override;

private:
    ca::i64 getExplicitFileDataLength() const;

    std::shared_ptr<const std::set<ca::usize>> offsets_;
    ca::usize                                  relativeDataOffsetStart_ = 0;
    ca::usize                                  relativeDataOffsetEnd_   = 0;
    bool                                       foundData_               = false;
    ca::u16                                    locFileNameLength_       = 0;
    ca::u16                                    locExtraFieldLength_     = 0;
};

/// @brief Central Directory 文件头。
class CentralDirectoryFileHeader : public AbstractZipFileHeader
{
public:
    /// @brief 读取 Central Directory 文件头。
    /// @param data 数据段
    /// @param offset 起始偏移
    void read(const MemorySegment& data, ca::usize offset) override;

    /// @brief 读取文件数据（Central 为空实现）。
    /// @param data 数据段
    /// @param headerOffset 头偏移
    void readFileData(const MemorySegment& data, ca::usize headerOffset) override;

    /// @brief 获取部件类型。
    PartType type() const override;

    /// @brief 获取部件长度。
    ca::usize length() const override;

    /// @brief 获取部件偏移。
    ca::usize offset() const override;

    /// @brief 获取创建版本。
    ca::u16 getVersionMadeBy() const;

    /// @brief 设置创建版本。
    /// @param value 版本
    void setVersionMadeBy(ca::u16 value);

    /// @brief 获取文件注释长度。
    ca::u16 getFileCommentLength() const;

    /// @brief 获取文件注释。
    const std::string& getFileComment() const;

    /// @brief 设置文件注释。
    /// @param comment 注释
    void setFileComment(const std::string& comment);

    /// @brief 获取起始磁盘号。
    /// @note 对标 Java int（readWord 零扩展）。
    ca::i32 getDiskNumberStart() const;

    /// @brief 设置起始磁盘号。
    /// @param value 磁盘号
    void setDiskNumberStart(ca::i32 value);

    /// @brief 获取内部属性。
    /// @note 对标 Java int（readWord 零扩展）。
    ca::i32 getInternalFileAttributes() const;

    /// @brief 设置内部属性。
    /// @param value 属性
    void setInternalFileAttributes(ca::i32 value);

    /// @brief 获取外部属性。
    ca::u32 getExternalFileAttributes() const;

    /// @brief 设置外部属性。
    /// @param value 属性
    void setExternalFileAttributes(ca::u32 value);

    /// @brief 获取关联本地头偏移。
    /// @note 对标 Java long，Zip64 扩展值需要 64 位。
    ca::i64 getRelativeOffsetOfLocalHeader() const;

    /// @brief 设置关联本地头偏移。
    /// @param value 偏移
    void setRelativeOffsetOfLocalHeader(ca::i64 value);

    /// @brief 本地头偏移是否来自 Zip64 扩展字段。
    bool hasZip64RelativeOffsetOfLocalHeader() const;

    /// @brief 获取 Zip64 磁盘号起始值，-1 表示无。
    ca::i64 getZip64DiskNumberStart() const;

    /// @brief 关联 Local 头。
    /// @param local 本地头
    void link(LocalFileHeader* local);

    /// @brief 获取关联 Local 头。
    LocalFileHeader* getLinkedFileHeader() const;

    /// @brief 创建当前对象的深拷贝。
    /// 对标 Java CentralDirectoryFileHeader.copy()，返回独立副本。
    /// 注意：linkedFile 指针不复制，副本中为 nullptr。
    std::unique_ptr<CentralDirectoryFileHeader> copy() const;

private:
    ca::u16          versionMadeBy_     = 0;
    ca::u16          fileCommentLength_ = 0;
    std::string      fileComment_;
    ca::i32          diskNumberStart_                  = 0;
    ca::i32          internalFileAttributes_           = 0;
    ca::u32          externalFileAttributes_           = 0;
    ca::i64          relativeOffsetOfLocalHeader_      = 0;
    bool             zip64RelativeOffsetOfLocalHeader_ = false;
    ca::i64          zip64DiskNumberStart_             = -1;
    LocalFileHeader* linkedFile_                       = nullptr;
};

/// @brief Central Directory 结束记录。
class EndOfCentralDirectory : public ZipPart
{
public:
    /// @brief 读取 EOCD。
    /// @param data 数据段
    /// @param offset 起始偏移
    void read(const MemorySegment& data, ca::usize offset) override;

    /// @brief 获取部件长度。
    ca::usize length() const override;

    /// @brief 获取部件类型。
    PartType type() const override { return PartType::END_OF_CENTRAL_DIRECTORY; }

    /// @brief 获取部件偏移。
    ca::usize offset() const override { return offset_; }

    /// @brief 获取磁盘号。
    /// @note 对标 Java int（readWord 零扩展）。
    ca::i32 getDiskNumber() const;

    /// @brief 设置磁盘号。
    /// @param value 磁盘号
    void setDiskNumber(ca::i32 value);

    /// @brief 获取 Central Directory 起始磁盘号。
    /// @note 对标 Java int（readWord 零扩展）。
    ca::i32 getCentralDirectoryStartDisk() const;

    /// @brief 设置 Central Directory 起始磁盘号。
    /// @param value 磁盘号
    void setCentralDirectoryStartDisk(ca::i32 value);

    /// @brief 获取 Central Directory 起始偏移。
    /// @note 对标 Java int（readWord 零扩展）。
    ca::i32 getCentralDirectoryStartOffset() const;

    /// @brief 设置 Central Directory 起始偏移。
    /// @param value 偏移
    void setCentralDirectoryStartOffset(ca::i32 value);

    /// @brief 获取条目数量。
    /// @note 对标 Java int（readWord 零扩展）。
    ca::i32 getNumEntries() const;

    /// @brief 设置条目数量。
    /// @param value 条目数
    void setNumEntries(ca::i32 value);

    /// @brief 获取 Central Directory 大小。
    /// @note 对标 Java long。
    ca::i64 getCentralDirectorySize() const;

    /// @brief 设置 Central Directory 大小。
    /// @param value 大小
    void setCentralDirectorySize(ca::i64 value);

    /// @brief 获取 Central Directory 偏移。
    /// @note 对标 Java long。
    ca::i64 getCentralDirectoryOffset() const;

    /// @brief 设置 Central Directory 偏移。
    /// @param value 偏移
    void setCentralDirectoryOffset(ca::i64 value);

    /// @brief 获取注释长度。
    ca::u16 getZipCommentLength() const;

    /// @brief 获取注释。
    const std::string& getZipComment() const;

    /// @brief 创建当前对象的深拷贝。
    /// 对标 Java EndOfCentralDirectory.copy()，返回独立副本。
    std::unique_ptr<EndOfCentralDirectory> copy() const;

private:
    ca::usize   offset_                      = static_cast<ca::usize>(-1);
    ca::i32     diskNumber_                  = 0;
    ca::i32     centralDirectoryStartDisk_   = 0;
    ca::i32     centralDirectoryStartOffset_ = 0;
    ca::i32     numEntries_                  = 0;
    ca::i64     centralDirectorySize_        = 0;
    ca::i64     centralDirectoryOffset_      = 0;
    ca::u16     zipCommentLength_            = 0;
    std::string zipComment_;
};

/// @brief ZIP 归档模型。
class ZipArchive
{
public:
    /// @brief 添加部件。
    /// @param part 部件指针
    void addPart(std::unique_ptr<ZipPart> part);

    /// @brief 移除部件。
    /// @param part 部件指针
    void removePart(ZipPart* part);

    /// @brief 按比较器排序部件。
    /// @param comparator 比较器
    void sortParts(const std::function<bool(const ZipPart*, const ZipPart*)>& comparator);

    /// @brief 获取部件列表。
    std::vector<std::unique_ptr<ZipPart>>& getParts();

    /// @brief 获取部件列表。
    const std::vector<std::unique_ptr<ZipPart>>& getParts() const;

    /// @brief 获取 Local File 列表。
    std::vector<const LocalFileHeader*> getLocalFiles() const;

    /// @brief 获取 Central Directory 列表。
    std::vector<const CentralDirectoryFileHeader*> getCentralDirectories() const;

    /// @brief 获取 EOCD（返回偏移最大的最后一个）。
    /// @note 对标 Java ZipArchive.getEnd()，使用 reversed OffsetComparator。
    const EndOfCentralDirectory* getEnd() const;

    /// @brief 获取 EOCD（返回偏移最大的最后一个）。
    EndOfCentralDirectory* getEnd();

    /// @brief 按名称过滤 Central Directory 条目。
    /// @param nameFilter 名称过滤器，返回 true 表示保留
    std::vector<const CentralDirectoryFileHeader*> getNameFilteredCentralDirectories(
        const std::function<bool(const std::string&)>& nameFilter) const;

    /// @brief 按名称过滤 Local File 条目（优先用 CEN 名称）。
    /// @param nameFilter 名称过滤器，返回 true 表示保留
    std::vector<const LocalFileHeader*> getNameFilteredLocalFiles(
        const std::function<bool(const std::string&)>& nameFilter) const;

    /// @brief 按名称获取单个 Local File（优先用 CEN 名称）。
    /// @param name 文件名
    /// @return 匹配的 LocalFileHeader，未找到返回 nullptr
    const LocalFileHeader* getLocalFileByName(const std::string& name) const;

    /// @brief 按索引添加部件。
    /// @param index 插入位置
    /// @param part 部件指针
    void addPartAt(ca::usize index, std::unique_ptr<ZipPart> part);

    /// @brief 按索引移除部件。
    /// @param index 移除位置
    /// @return 被移除的部件
    std::unique_ptr<ZipPart> removePartAt(ca::usize index);

    /// @brief 设置前缀数据。
    /// @param data 前缀数据
    void setPrefixData(const MemorySegment& data);

    /// @brief 获取前缀数据。
    const MemorySegment& getPrefixData() const;

    /// @brief 获取本次读取的结构异常报告（可变，reader 写入侧）。
    ZipReadReport& getReadReport();

    /// @brief 获取本次读取的结构异常报告。
    const ZipReadReport& getReadReport() const;

    /// @brief 应用映射器返回新归档（move 语义，调用后当前对象为空）。
    /// @param mapper 映射器
    ZipArchive withMapping(const ZipPartMapper& mapper);

private:
    std::vector<std::unique_ptr<ZipPart>> parts_;
    MemorySegment                         prefixData_{};
    ZipReadReport                         readReport_{};
};

/// @brief 按偏移排序的比较器。
struct OffsetComparator
{
    bool operator()(const ZipPart* a, const ZipPart* b) const { return a->offset() < b->offset(); }
};

/// @brief 按文件名排序的比较器。
struct NameComparator
{
    bool operator()(const ZipPart* a, const ZipPart* b) const
    {
        if (a->type() == PartType::LOCAL_FILE_HEADER && b->type() == PartType::LOCAL_FILE_HEADER) {
            auto*       localA = static_cast<const LocalFileHeader*>(a);
            auto*       localB = static_cast<const LocalFileHeader*>(b);
            auto*       cenA   = localA->getLinkedDirectoryFileHeader();
            auto*       cenB   = localB->getLinkedDirectoryFileHeader();
            const auto& nameA  = cenA ? cenA->getFileName() : localA->getFileName();
            const auto& nameB  = cenB ? cenB->getFileName() : localB->getFileName();
            return nameA < nameB;
        }
        if (a->type() == PartType::CENTRAL_DIRECTORY_FILE_HEADER &&
            b->type() == PartType::CENTRAL_DIRECTORY_FILE_HEADER) {
            auto* cenA = static_cast<const CentralDirectoryFileHeader*>(a);
            auto* cenB = static_cast<const CentralDirectoryFileHeader*>(b);
            return cenA->getFileName() < cenB->getFileName();
        }
        return a->offset() < b->offset();
    }
};

}   // namespace jzip
