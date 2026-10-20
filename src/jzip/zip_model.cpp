#include "zip_model.hpp"

#include <algorithm>
#include <optional>
#include <unordered_set>
#include <utility>

#include "decompressor.hpp"
#include "zip64_support.hpp"
#include "zip_io.hpp"

using namespace ca;

namespace jzip {
namespace {

constexpr u32 kLocalFileHeaderSignature            = 0x04034b50u;
constexpr u32 kCentralDirectoryFileHeaderSignature = 0x02014b50u;
constexpr u32 kEndOfCentralDirectorySignature      = 0x06054b50u;

constexpr usize kLocalFileHeaderMinSize            = 30;
constexpr usize kCentralDirectoryFileHeaderMinSize = 46;
constexpr usize kEndOfCentralDirectoryMinSize      = 22;

u16 read_u16_le(const MemorySegment& data, usize offset)
{
    return data.get(u16{}, offset);
}

u32 read_u32_le(const MemorySegment& data, usize offset)
{
    return data.get(u32{}, offset);
}

std::string read_string(const MemorySegment& data, usize offset, usize length)
{
    auto slice = data.asSlice(offset, length).toByteArray();
    return std::string(reinterpret_cast<const char*>(slice.data()), slice.size());
}

// 将 Extra Field 列表串行化为 Tag+Size+Data 格式
std::vector<u8> serialize_extra_fields(const std::vector<ExtraFieldEntry>& entries)
{
    std::vector<u8> out;
    out.reserve(entries.size() * 4);
    for (const auto& entry : entries) {
        out.push_back(static_cast<u8>(entry.tag & 0xFFu));
        out.push_back(static_cast<u8>((entry.tag >> 8) & 0xFFu));
        out.push_back(static_cast<u8>(entry.data.size() & 0xFFu));
        out.push_back(static_cast<u8>((entry.data.size() >> 8) & 0xFFu));
        out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    return out;
}

}   // namespace

ZipParseException::ZipParseException(const std::string& message, usize offset)
    : std::runtime_error(message)
    , offset_(offset)
{}

usize ZipParseException::getOffset() const noexcept
{
    return offset_;
}

// 解析 Extra Field 字节流为结构化条目列表
std::vector<ExtraFieldEntry> ExtraFieldParser::parse_entries(const std::vector<u8>& extra_bytes)
{
    std::vector<ExtraFieldEntry> entries;
    usize                        offset = 0;

    while (offset + 4 <= extra_bytes.size()) {
        const u16 tag =
            static_cast<u16>(extra_bytes[offset]) | static_cast<u16>(extra_bytes[offset + 1] << 8);
        const u16 size = static_cast<u16>(extra_bytes[offset + 2]) |
                         static_cast<u16>(extra_bytes[offset + 3] << 8);
        offset += 4;

        if (offset + static_cast<usize>(size) > extra_bytes.size()) {
            break;
        }

        ExtraFieldEntry entry;
        entry.tag  = tag;
        entry.size = size;
        entry.data.assign(extra_bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          extra_bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
        entries.push_back(std::move(entry));

        offset += size;
    }

    return entries;
}

// 从 Extra Field 中提取 NTFS 时间戳（Tag 0x000A），回退到 Unix 时间（Tag 0x5455）
std::optional<NtfsExtraTime> ExtraFieldParser::parse_ntfs_time(
    const std::vector<ExtraFieldEntry>& entries)
{
    constexpr u16 kNtfsExtraFieldTag    = 0x000Au;
    constexpr u16 kNtfsAttributeTagTime = 0x0001u;
    constexpr u64 kWindowsEpochDiffMs   = 11644473600000ull;
    constexpr u16 kUnixExtraFieldTag    = 0x5455u;

    auto read_u16 = [](const u8* data) {
        return static_cast<u16>(data[0]) | (static_cast<u16>(data[1]) << 8);
    };
    auto read_u32 = [](const u8* data) {
        return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
               (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
    };
    auto read_u64 = [&](const u8* data) {
        return static_cast<u64>(read_u32(data)) | (static_cast<u64>(read_u32(data + 4)) << 32);
    };
    auto filetime_to_unix_ms = [&](u64 file_time_100ns) {
        const u64 ms = file_time_100ns / 10000ull;
        if (ms < kWindowsEpochDiffMs) {
            return static_cast<u64>(0);
        }
        return ms - kWindowsEpochDiffMs;
    };

    for (const auto& entry : entries) {
        if (entry.tag != kNtfsExtraFieldTag || entry.data.size() < 4) {
            continue;
        }

        usize offset = 4;
        while (offset + 4 <= entry.data.size()) {
            const u16 attr_tag  = read_u16(entry.data.data() + offset);
            const u16 attr_size = read_u16(entry.data.data() + offset + 2);
            offset += 4;

            if (offset + static_cast<usize>(attr_size) > entry.data.size()) {
                break;
            }

            if (attr_tag == kNtfsAttributeTagTime && attr_size >= 24) {
                NtfsExtraTime parsed;
                parsed.modify_ms   = filetime_to_unix_ms(read_u64(entry.data.data() + offset));
                parsed.access_ms   = filetime_to_unix_ms(read_u64(entry.data.data() + offset + 8));
                parsed.creation_ms = filetime_to_unix_ms(read_u64(entry.data.data() + offset + 16));
                return parsed;
            }

            offset += static_cast<usize>(attr_size);
        }
    }

    // 回退：尝试 Unix 时间格式（Tag 0x5455）
    for (const auto& entry : entries) {
        if (entry.tag != kUnixExtraFieldTag || entry.data.empty()) {
            continue;
        }

        const u8      flags  = entry.data[0];
        usize         offset = 1;
        NtfsExtraTime parsed;

        if ((flags & 0x01u) != 0u && offset + 4 <= entry.data.size()) {
            parsed.modify_ms = static_cast<u64>(read_u32(entry.data.data() + offset)) * 1000ull;
            offset += 4;
        }
        if ((flags & 0x02u) != 0u && offset + 4 <= entry.data.size()) {
            parsed.access_ms = static_cast<u64>(read_u32(entry.data.data() + offset)) * 1000ull;
            offset += 4;
        }
        if ((flags & 0x04u) != 0u && offset + 4 <= entry.data.size()) {
            parsed.creation_ms = static_cast<u64>(read_u32(entry.data.data() + offset)) * 1000ull;
            offset += 4;
        }

        // 至少有一个时间戳，否则 fall through
        if (parsed.modify_ms != 0 || parsed.access_ms != 0 || parsed.creation_ms != 0)
            return parsed;
    }

    return std::nullopt;
}

// 从 Extra Field 中提取 Unix 时间戳（Tag 0x5455）
std::optional<UnixExtraTime> ExtraFieldParser::parse_unix_time(
    const std::vector<ExtraFieldEntry>& entries)
{
    constexpr u16 kUnixExtraFieldTag = 0x5455u;

    auto read_u32 = [](const u8* data) {
        return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8) |
               (static_cast<u32>(data[2]) << 16) | (static_cast<u32>(data[3]) << 24);
    };

    for (const auto& entry : entries) {
        if (entry.tag != kUnixExtraFieldTag || entry.data.empty()) {
            continue;
        }

        const u8      flags  = entry.data[0];
        usize         offset = 1;
        UnixExtraTime parsed;

        if ((flags & 0x01u) != 0u && offset + 4 <= entry.data.size()) {
            parsed.modify_ms = static_cast<u64>(read_u32(entry.data.data() + offset)) * 1000ull;
            offset += 4;
        }
        if ((flags & 0x02u) != 0u && offset + 4 <= entry.data.size()) {
            parsed.access_ms = static_cast<u64>(read_u32(entry.data.data() + offset)) * 1000ull;
            offset += 4;
        }
        if ((flags & 0x04u) != 0u && offset + 4 <= entry.data.size()) {
            parsed.creation_ms = static_cast<u64>(read_u32(entry.data.data() + offset)) * 1000ull;
            offset += 4;
        }

        return parsed;
    }

    return std::nullopt;
}

void AbstractZipFileHeader::read(const MemorySegment& data, usize offset)
{
    if (data.byteSize() < offset + kLocalFileHeaderMinSize) {
        throw ZipParseException("local file header truncated", offset);
    }

    const u32 signature = read_u32_le(data, offset);
    if (signature != kLocalFileHeaderSignature) {
        throw ZipParseException("invalid local file header signature", offset);
    }

    data_   = data;
    offset_ = offset;

    versionNeededToExtract_ = read_u16_le(data, offset + 4);
    generalPurposeBitFlag_  = read_u16_le(data, offset + 6);
    compressionMethod_      = read_u16_le(data, offset + 8);
    lastModFileTime_        = read_u16_le(data, offset + 10);
    lastModFileDate_        = read_u16_le(data, offset + 12);
    crc32_                  = read_u32_le(data, offset + 14);
    compressedSize_   = static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 18));
    uncompressedSize_ = static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 22));
    fileNameLength_   = read_u16_le(data, offset + 26);
    extraFieldLength_ = read_u16_le(data, offset + 28);

    const usize name_offset  = offset + kLocalFileHeaderMinSize;
    const usize extra_offset = name_offset + static_cast<usize>(fileNameLength_);

    if (name_offset + fileNameLength_ > data.byteSize() ||
        extra_offset + extraFieldLength_ > data.byteSize()) {
        throw ZipParseException("local file header name/extra out of range", offset);
    }

    fileName_   = read_string(data, name_offset, fileNameLength_);
    extraField_ = data.asSlice(extra_offset, extraFieldLength_).toByteArray();
}

u16 AbstractZipFileHeader::getVersionNeededToExtract() const
{
    return versionNeededToExtract_;
}

void AbstractZipFileHeader::setVersionNeededToExtract(u16 value)
{
    versionNeededToExtract_ = value;
}

u16 AbstractZipFileHeader::getGeneralPurposeBitFlag() const
{
    return generalPurposeBitFlag_;
}

void AbstractZipFileHeader::setGeneralPurposeBitFlag(u16 value)
{
    generalPurposeBitFlag_ = value;
}

u16 AbstractZipFileHeader::getCompressionMethod() const
{
    return compressionMethod_;
}

void AbstractZipFileHeader::setCompressionMethod(u16 value)
{
    compressionMethod_ = value;
}

u16 AbstractZipFileHeader::getLastModFileTime() const
{
    return lastModFileTime_;
}

void AbstractZipFileHeader::setLastModFileTime(u16 value)
{
    lastModFileTime_ = value;
}

u16 AbstractZipFileHeader::getLastModFileDate() const
{
    return lastModFileDate_;
}

void AbstractZipFileHeader::setLastModFileDate(u16 value)
{
    lastModFileDate_ = value;
}

u32 AbstractZipFileHeader::getCrc32() const
{
    return crc32_;
}

void AbstractZipFileHeader::setCrc32(u32 value)
{
    crc32_ = value;
}

i64 AbstractZipFileHeader::getCompressedSize() const
{
    return compressedSize_;
}

void AbstractZipFileHeader::setCompressedSize(i64 value)
{
    compressedSize_ = value;
}

i64 AbstractZipFileHeader::getUncompressedSize() const
{
    return uncompressedSize_;
}

void AbstractZipFileHeader::setUncompressedSize(i64 value)
{
    uncompressedSize_ = value;
}

bool AbstractZipFileHeader::hasZip64CompressedSize() const
{
    return zip64CompressedSize_;
}

bool AbstractZipFileHeader::hasZip64UncompressedSize() const
{
    return zip64UncompressedSize_;
}

u16 AbstractZipFileHeader::getFileNameLength() const
{
    return fileNameLength_;
}

u16 AbstractZipFileHeader::getExtraFieldLength() const
{
    return extraFieldLength_;
}

const std::string& AbstractZipFileHeader::getFileName() const
{
    return fileName_;
}

void AbstractZipFileHeader::setFileName(const std::string& name)
{
    fileName_       = name;
    fileNameLength_ = static_cast<u16>(fileName_.size());
}

const std::vector<u8>& AbstractZipFileHeader::getExtraField() const
{
    return extraField_;
}

void AbstractZipFileHeader::setExtraField(const std::vector<u8>& data)
{
    extraField_       = data;
    extraFieldLength_ = static_cast<u16>(extraField_.size());
}

void AbstractZipFileHeader::setDataSegment(const MemorySegment& data)
{
    data_ = data;
}

void AbstractZipFileHeader::setOffset(usize offset)
{
    offset_ = offset;
}

void LocalFileHeader::read(const MemorySegment& data, usize offset)
{
    AbstractZipFileHeader::read(data, offset);

    // 对标 Java：在 read() 中直接处理 Zip64 扩展字段并覆盖基础字段值
    // readZip64ExtendedInfo(false, false) - Local 不读取 relativeOffset 和 diskStart
    zip64CompressedSize_   = false;
    zip64UncompressedSize_ = false;

    if (extraFieldLength_ > 0) {
        usize pos = 0;
        while (pos + 4 <= extraField_.size()) {
            const u16 tag =
                static_cast<u16>(extraField_[pos]) | (static_cast<u16>(extraField_[pos + 1]) << 8);
            const u16 size = static_cast<u16>(extraField_[pos + 2]) |
                             (static_cast<u16>(extraField_[pos + 3]) << 8);
            pos += 4;
            if (pos + static_cast<usize>(size) > extraField_.size()) break;
            if (tag == 0x0001) {
                // Zip64 Extended Information Extra Field
                usize       dataPos = pos;
                const usize dataEnd = pos + static_cast<usize>(size);

                if (uncompressedSize_ == static_cast<i64>(0xFFFFFFFF)) {
                    if (dataPos + 8 > dataEnd) break;
                    u64 val = 0;
                    for (int b = 0; b < 8; ++b)
                        val |= static_cast<u64>(extraField_[dataPos + b]) << (b * 8);
                    uncompressedSize_      = static_cast<i64>(val);
                    zip64UncompressedSize_ = true;
                    dataPos += 8;
                }
                if (compressedSize_ == static_cast<i64>(0xFFFFFFFF)) {
                    if (dataPos + 8 > dataEnd) break;
                    u64 val = 0;
                    for (int b = 0; b < 8; ++b)
                        val |= static_cast<u64>(extraField_[dataPos + b]) << (b * 8);
                    compressedSize_      = static_cast<i64>(val);
                    zip64CompressedSize_ = true;
                    dataPos += 8;
                }
                // Local 不读取 relativeOffset 和 diskStart
                break;
            }
            pos += static_cast<usize>(size);
        }
    }

    readFileData(data, offset);
}

// 按压缩大小从原始数据中切片文件内容
void LocalFileHeader::readFileData(const MemorySegment& data, usize headerOffset)
{
    const usize data_offset = headerOffset + kLocalFileHeaderMinSize +
                              static_cast<usize>(getFileNameLength()) +
                              static_cast<usize>(getExtraFieldLength());

    const usize data_end = data_offset + static_cast<usize>(std::max<i64>(0, getCompressedSize()));
    if (data_end > data.byteSize()) {
        throw ZipParseException("local file data out of range", headerOffset);
    }

    fileData_ = data.asSlice(data_offset, data_end - data_offset).toByteArray();
}

PartType LocalFileHeader::type() const
{
    return PartType::LOCAL_FILE_HEADER;
}

usize LocalFileHeader::length() const
{
    return kLocalFileHeaderMinSize + static_cast<usize>(getFileNameLength()) +
           static_cast<usize>(getExtraFieldLength()) + fileData_.size() +
           static_cast<usize>(dataDescriptorSize_);
}

usize LocalFileHeader::offset() const
{
    return offset_;
}

// 用中央目录的值覆盖本地头，并在必要时重新读取文件数据
// 对标 Java LocalFileHeader.adoptLinkedCentralDirectoryValues()
void LocalFileHeader::adoptLinkedCentralDirectoryValues()
{
    if (linkedDirectory_ == nullptr) {
        return;
    }

    // 对标 Java：以下字段全部从 CEN 复制到 LOC
    setVersionNeededToExtract(linkedDirectory_->getVersionNeededToExtract());
    setGeneralPurposeBitFlag(linkedDirectory_->getGeneralPurposeBitFlag());
    setCompressionMethod(linkedDirectory_->getCompressionMethod());
    setLastModFileTime(linkedDirectory_->getLastModFileTime());
    setLastModFileDate(linkedDirectory_->getLastModFileDate());
    setCrc32(linkedDirectory_->getCrc32());
    setCompressedSize(linkedDirectory_->getCompressedSize());
    setUncompressedSize(linkedDirectory_->getUncompressedSize());
    setFileName(linkedDirectory_->getFileName());
    // Java adoptLinkedCentralDirectoryValues() copies the CEN extra bytes but does not
    // change the LOC-recorded extra-field length used to locate file data.
    extraField_ = linkedDirectory_->getExtraField();

    if (data_.byteSize() > 0) {
        readFileData(data_, offset_);
    }
}

// 对标 Java LocalFileHeader.hasDifferentValuesThanCentralDirectoryHeader()
bool LocalFileHeader::hasDifferentValuesThanCentralDirectoryHeader() const
{
    if (linkedDirectory_ == nullptr) {
        return false;
    }

    return getVersionNeededToExtract() != linkedDirectory_->getVersionNeededToExtract() ||
           getGeneralPurposeBitFlag() != linkedDirectory_->getGeneralPurposeBitFlag() ||
           getCompressionMethod() != linkedDirectory_->getCompressionMethod() ||
           getLastModFileTime() != linkedDirectory_->getLastModFileTime() ||
           getLastModFileDate() != linkedDirectory_->getLastModFileDate() ||
           getCrc32() != linkedDirectory_->getCrc32() ||
           getCompressedSize() != linkedDirectory_->getCompressedSize() ||
           getUncompressedSize() != linkedDirectory_->getUncompressedSize() ||
           getFileNameLength() != linkedDirectory_->getFileNameLength() ||
           getFileName() != linkedDirectory_->getFileName();
}

// 对标 Java LocalFileHeader.copy()
std::unique_ptr<LocalFileHeader> LocalFileHeader::copy() const
{
    auto c     = std::make_unique<LocalFileHeader>();
    c->data_   = data_;
    c->offset_ = offset_;
    // linkedDirectory 不复制，副本中为 nullptr
    c->versionNeededToExtract_ = versionNeededToExtract_;
    c->generalPurposeBitFlag_  = generalPurposeBitFlag_;
    c->compressionMethod_      = compressionMethod_;
    c->lastModFileTime_        = lastModFileTime_;
    c->lastModFileDate_        = lastModFileDate_;
    c->crc32_                  = crc32_;
    c->compressedSize_         = compressedSize_;
    c->uncompressedSize_       = uncompressedSize_;
    c->fileNameLength_         = fileNameLength_;
    c->extraFieldLength_       = extraFieldLength_;
    c->fileName_               = fileName_;
    c->extraField_             = extraField_;
    c->zip64CompressedSize_    = zip64CompressedSize_;
    c->zip64UncompressedSize_  = zip64UncompressedSize_;
    c->fileData_               = fileData_;
    c->hasDataDescriptor_      = hasDataDescriptor_;
    c->dataDescriptorSize_     = dataDescriptorSize_;
    c->extraFields_            = extraFields_;
    return c;
}

const std::vector<u8>& LocalFileHeader::getFileData() const
{
    return fileData_;
}

void LocalFileHeader::setFileData(const std::vector<u8>& data)
{
    fileData_ = data;
}

// 对标 Java LocalFileHeader.setFileDataEndOffset()
void LocalFileHeader::setFileDataEndOffset(usize endOffset)
{
    const usize fileDataStartOffset = offset_ + kMinFixedSize +
                                      static_cast<usize>(getFileNameLength()) +
                                      static_cast<usize>(getExtraFieldLength());
    const usize length = endOffset > fileDataStartOffset ? endOffset - fileDataStartOffset : 0;
    setFileDataLength(length);
}

// 对标 Java LocalFileHeader.setFileDataLength()
// 注：当请求长度超出 data_ 边界时，会静默截断到可用范围（与 Java
// 行为一致）。调用方应确保传入的 newLength 合理；如需严格校验，
// 改用 getFileData().size() 比较实际切片结果。
void LocalFileHeader::setFileDataLength(usize newLength)
{
    if (data_.byteSize() == 0) {
        // 没有原始数据，无法切片
        fileData_.resize(newLength);
        return;
    }
    const usize dataOffset = offset_ + kMinFixedSize + static_cast<usize>(getFileNameLength()) +
                             static_cast<usize>(getExtraFieldLength());
    if (dataOffset + newLength > data_.byteSize()) {
        // 超出数据范围，截断到可用大小（不抛异常，与 Java 行为一致）
        newLength = data_.byteSize() > dataOffset ? data_.byteSize() - dataOffset : 0;
    }
    fileData_ = data_.asSlice(dataOffset, newLength).toByteArray();
}

void LocalFileHeader::link(CentralDirectoryFileHeader* cen)
{
    linkedDirectory_ = cen;
}

CentralDirectoryFileHeader* LocalFileHeader::getLinkedDirectoryFileHeader() const
{
    return linkedDirectory_;
}

std::vector<u8> LocalFileHeader::decompress(Decompressor& decompressor) const
{
    MemorySegment segment(fileData_);
    return decompressor.decompress(*this, segment);
}

void LocalFileHeader::setDataDescriptor(bool hasDescriptor, u32 size)
{
    hasDataDescriptor_  = hasDescriptor;
    dataDescriptorSize_ = size;
}

bool LocalFileHeader::hasDataDescriptor() const
{
    return hasDataDescriptor_;
}

u32 LocalFileHeader::getDataDescriptorSize() const
{
    return dataDescriptorSize_;
}

const std::vector<ExtraFieldEntry>& LocalFileHeader::getExtraFields() const
{
    return extraFields_;
}

// 设置解析后的 Extra Fields，同时序列化到原始字节字段
void LocalFileHeader::setExtraFields(const std::vector<ExtraFieldEntry>& entries)
{
    extraFields_ = entries;
    setExtraField(serialize_extra_fields(entries));
}

void LocalFileHeader::appendExtraFieldsFromCen()
{
    if (linkedDirectory_ == nullptr || linkedDirectory_->getExtraFieldLength() == 0) return;

    auto cen_entries = ExtraFieldParser::parse_entries(linkedDirectory_->getExtraField());

    std::unordered_set<u16> existing_tags;
    for (const auto& e : extraFields_) existing_tags.insert(e.tag);

    for (auto& e : cen_entries) {
        if (!existing_tags.count(e.tag)) {
            extraFields_.push_back(std::move(e));
            existing_tags.insert(e.tag);
        }
    }
}

void JvmLocalFileHeader::setOffsets(std::shared_ptr<const std::set<usize>> offsets)
{
    offsets_ = std::move(offsets);
}

// 保存原始 LOC 字段长度，避免 adoptLinkedCentralDirectoryValues 后使用 CEN 值计算偏移
void JvmLocalFileHeader::read(const MemorySegment& data, usize offset)
{
    locFileNameLength_   = data.get(u16{}, offset + 26);
    locExtraFieldLength_ = data.get(u16{}, offset + 28);
    LocalFileHeader::read(data, offset);
}

// Ceiling-based data boundary computation matching Java JvmLocalFileHeader.readFileData()
void JvmLocalFileHeader::readFileData(const MemorySegment& data, usize headerOffset)
{
    // 使用原始 LOC 字段长度（而非可能被 CEN 覆盖后的值）计算数据偏移
    const usize local_offset = headerOffset + kLocalFileHeaderMinSize +
                               static_cast<usize>(locFileNameLength_) +
                               static_cast<usize>(locExtraFieldLength_);

    if (local_offset >= data.byteSize()) {
        throw ZipParseException("local file data offset out of range", headerOffset);
    }

    // Find ceiling: 首个不早于数据起点的头部偏移（对齐 Java offset 的 ceiling）。
    // 不能用 upper_bound：零长度条目的数据起点恰好等于下一个头部偏移，
    // upper_bound 会跳过它，把后续结构算进本条目的 fileData。
    // Shared ptr avoids copying the full set (70k entries) per LocalFileHeader.
    usize data_end = data.byteSize();
    if (offsets_) {
        auto it = offsets_->lower_bound(local_offset);
        if (it != offsets_->end()) {
            data_end = *it;
        }
    }

    // JVM parser normally scans to the next header. When the LOC advertises a data
    // descriptor and also exposes a trustworthy explicit size, clamp the range so the
    // descriptor bytes stay outside file data.
    if ((getGeneralPurposeBitFlag() & 0b1000u) != 0u && data_end < data.byteSize()) {
        const i64 explicitSize = getExplicitFileDataLength();
        if (explicitSize >= 0 && data_end >= local_offset &&
            static_cast<usize>(explicitSize) <= data_end - local_offset) {
            data_end = local_offset + static_cast<usize>(explicitSize);
        }
    }

    relativeDataOffsetStart_ = local_offset - headerOffset;
    relativeDataOffsetEnd_   = data_end - headerOffset;

    const usize data_length = data_end - local_offset;
    if (data_length > 0) {
        setFileData(data.asSlice(local_offset, data_length).toByteArray());
    }
    else {
        setFileData({});
    }

    // Update sizes from actual data length
    if (getCompressionMethod() == 0) {   // stored
        setCompressedSize(static_cast<i64>(data_length));
        setUncompressedSize(static_cast<i64>(data_length));
    }
    else {
        setCompressedSize(static_cast<i64>(data_length));
    }

    foundData_ = data_length != 0;
}

i64 JvmLocalFileHeader::getExplicitFileDataLength() const
{
    const i64 length = getCompressionMethod() == 0 ? getUncompressedSize() : getCompressedSize();
    return length > 0 ? length : -1;
}

void JvmLocalFileHeader::adoptLinkedCentralDirectoryValues()
{
    auto* linkedDirectory = getLinkedDirectoryFileHeader();
    if (linkedDirectory == nullptr) {
        return;
    }

    setVersionNeededToExtract(linkedDirectory->getVersionNeededToExtract());
    setGeneralPurposeBitFlag(linkedDirectory->getGeneralPurposeBitFlag());
    setCompressionMethod(linkedDirectory->getCompressionMethod());
    setLastModFileTime(linkedDirectory->getLastModFileTime());
    setLastModFileDate(linkedDirectory->getLastModFileDate());
    setCrc32(linkedDirectory->getCrc32());
    setCompressedSize(linkedDirectory->getCompressedSize());
    setUncompressedSize(linkedDirectory->getUncompressedSize());
    setFileName(linkedDirectory->getFileName());

    const i64 fileDataLength = getExplicitFileDataLength();
    if (data_.byteSize() == 0 || fileDataLength < 0) {
        return;
    }

    const usize absoluteDataOffsetStart = offset_ + relativeDataOffsetStart_;
    usize       availableDataEnd        = data_.byteSize();
    if (relativeDataOffsetEnd_ > relativeDataOffsetStart_) {
        availableDataEnd = std::min(availableDataEnd, offset_ + relativeDataOffsetEnd_);
    }

    const bool shouldRealignDataRange = !foundData_ ||
                                        (getGeneralPurposeBitFlag() & 0b1000u) != 0u ||
                                        getFileData().size() != static_cast<usize>(fileDataLength);
    if (!shouldRealignDataRange || absoluteDataOffsetStart > availableDataEnd) {
        return;
    }

    if (static_cast<usize>(fileDataLength) > availableDataEnd - absoluteDataOffsetStart) {
        return;
    }
    const usize absoluteDataOffsetEnd =
        absoluteDataOffsetStart + static_cast<usize>(fileDataLength);

    setFileData(
        data_.asSlice(absoluteDataOffsetStart, static_cast<usize>(fileDataLength)).toByteArray());
    relativeDataOffsetEnd_ = relativeDataOffsetStart_ + static_cast<usize>(fileDataLength);
    foundData_             = fileDataLength != 0;
}

// 从原始数据解析中央目录记录
void CentralDirectoryFileHeader::read(const MemorySegment& data, usize offset)
{
    if (data.byteSize() < offset + kCentralDirectoryFileHeaderMinSize) {
        throw ZipParseException("central directory header truncated", offset);
    }

    const u32 signature = read_u32_le(data, offset);
    if (signature != kCentralDirectoryFileHeaderSignature) {
        throw ZipParseException("invalid central directory signature", offset);
    }

    data_   = data;
    offset_ = offset;

    versionMadeBy_          = read_u16_le(data, offset + 4);
    versionNeededToExtract_ = read_u16_le(data, offset + 6);
    generalPurposeBitFlag_  = read_u16_le(data, offset + 8);
    compressionMethod_      = read_u16_le(data, offset + 10);
    lastModFileTime_        = read_u16_le(data, offset + 12);
    lastModFileDate_        = read_u16_le(data, offset + 14);
    crc32_                  = read_u32_le(data, offset + 16);
    compressedSize_    = static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 20));
    uncompressedSize_  = static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 24));
    fileNameLength_    = read_u16_le(data, offset + 28);
    extraFieldLength_  = read_u16_le(data, offset + 30);
    fileCommentLength_ = read_u16_le(data, offset + 32);
    diskNumberStart_   = static_cast<i32>(read_u16_le(data, offset + 34));
    internalFileAttributes_ = static_cast<i32>(read_u16_le(data, offset + 36));
    externalFileAttributes_ = read_u32_le(data, offset + 38);
    relativeOffsetOfLocalHeader_ =
        static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 42));

    const usize name_offset    = offset + kCentralDirectoryFileHeaderMinSize;
    const usize extra_offset   = name_offset + static_cast<usize>(fileNameLength_);
    const usize comment_offset = extra_offset + static_cast<usize>(extraFieldLength_);

    if (comment_offset + fileCommentLength_ > data.byteSize()) {
        throw ZipParseException("central directory name/extra/comment out of range", offset);
    }

    fileName_   = read_string(data, name_offset, fileNameLength_);
    extraField_ = data.asSlice(extra_offset, extraFieldLength_).toByteArray();

    // 对标 Java：在 read() 中直接处理 Zip64 扩展字段并覆盖基础字段值
    // readZip64ExtendedInfo(true, true) - CEN 读取 relativeOffset 和 diskStart
    zip64CompressedSize_              = false;
    zip64UncompressedSize_            = false;
    zip64RelativeOffsetOfLocalHeader_ = false;
    zip64DiskNumberStart_             = -1;

    if (extraFieldLength_ > 0) {
        usize pos = 0;
        while (pos + 4 <= extraField_.size()) {
            const u16 tag =
                static_cast<u16>(extraField_[pos]) | (static_cast<u16>(extraField_[pos + 1]) << 8);
            const u16 size = static_cast<u16>(extraField_[pos + 2]) |
                             (static_cast<u16>(extraField_[pos + 3]) << 8);
            pos += 4;
            if (pos + static_cast<usize>(size) > extraField_.size()) break;
            if (tag == 0x0001) {
                // Zip64 Extended Information Extra Field
                usize       dataPos = pos;
                const usize dataEnd = pos + static_cast<usize>(size);

                if (uncompressedSize_ == static_cast<i64>(0xFFFFFFFF)) {
                    if (dataPos + 8 > dataEnd) break;
                    u64 val = 0;
                    for (int b = 0; b < 8; ++b)
                        val |= static_cast<u64>(extraField_[dataPos + b]) << (b * 8);
                    uncompressedSize_      = static_cast<i64>(val);
                    zip64UncompressedSize_ = true;
                    dataPos += 8;
                }
                if (compressedSize_ == static_cast<i64>(0xFFFFFFFF)) {
                    if (dataPos + 8 > dataEnd) break;
                    u64 val = 0;
                    for (int b = 0; b < 8; ++b)
                        val |= static_cast<u64>(extraField_[dataPos + b]) << (b * 8);
                    compressedSize_      = static_cast<i64>(val);
                    zip64CompressedSize_ = true;
                    dataPos += 8;
                }
                // CEN 读取 relativeOffset 和 diskStart
                if (relativeOffsetOfLocalHeader_ == static_cast<i64>(0xFFFFFFFF)) {
                    if (dataPos + 8 > dataEnd) break;
                    u64 val = 0;
                    for (int b = 0; b < 8; ++b)
                        val |= static_cast<u64>(extraField_[dataPos + b]) << (b * 8);
                    relativeOffsetOfLocalHeader_      = static_cast<i64>(val);
                    zip64RelativeOffsetOfLocalHeader_ = true;
                    dataPos += 8;
                }
                if (diskNumberStart_ == static_cast<i32>(0xFFFF)) {
                    if (dataPos + 4 > dataEnd) break;
                    u32 val = static_cast<u32>(extraField_[dataPos]) |
                              (static_cast<u32>(extraField_[dataPos + 1]) << 8) |
                              (static_cast<u32>(extraField_[dataPos + 2]) << 16) |
                              (static_cast<u32>(extraField_[dataPos + 3]) << 24);
                    zip64DiskNumberStart_ = static_cast<i64>(val);
                    diskNumberStart_      = static_cast<i32>(val);
                }
                break;
            }
            pos += static_cast<usize>(size);
        }
    }

    fileComment_ = read_string(data, comment_offset, fileCommentLength_);
}

void CentralDirectoryFileHeader::readFileData(const MemorySegment&, usize) {}

PartType CentralDirectoryFileHeader::type() const
{
    return PartType::CENTRAL_DIRECTORY_FILE_HEADER;
}

usize CentralDirectoryFileHeader::length() const
{
    return kCentralDirectoryFileHeaderMinSize + static_cast<usize>(fileNameLength_) +
           static_cast<usize>(extraFieldLength_) + static_cast<usize>(fileCommentLength_);
}

usize CentralDirectoryFileHeader::offset() const
{
    return offset_;
}

u16 CentralDirectoryFileHeader::getVersionMadeBy() const
{
    return versionMadeBy_;
}

void CentralDirectoryFileHeader::setVersionMadeBy(u16 value)
{
    versionMadeBy_ = value;
}

u16 CentralDirectoryFileHeader::getFileCommentLength() const
{
    return fileCommentLength_;
}

const std::string& CentralDirectoryFileHeader::getFileComment() const
{
    return fileComment_;
}

void CentralDirectoryFileHeader::setFileComment(const std::string& comment)
{
    fileComment_       = comment;
    fileCommentLength_ = static_cast<u16>(fileComment_.size());
}

i32 CentralDirectoryFileHeader::getDiskNumberStart() const
{
    return diskNumberStart_;
}

void CentralDirectoryFileHeader::setDiskNumberStart(i32 value)
{
    diskNumberStart_ = value;
}

i32 CentralDirectoryFileHeader::getInternalFileAttributes() const
{
    return internalFileAttributes_;
}

void CentralDirectoryFileHeader::setInternalFileAttributes(i32 value)
{
    internalFileAttributes_ = value;
}

u32 CentralDirectoryFileHeader::getExternalFileAttributes() const
{
    return externalFileAttributes_;
}

void CentralDirectoryFileHeader::setExternalFileAttributes(u32 value)
{
    externalFileAttributes_ = value;
}

i64 CentralDirectoryFileHeader::getRelativeOffsetOfLocalHeader() const
{
    return relativeOffsetOfLocalHeader_;
}

void CentralDirectoryFileHeader::setRelativeOffsetOfLocalHeader(i64 value)
{
    relativeOffsetOfLocalHeader_ = value;
}

bool CentralDirectoryFileHeader::hasZip64RelativeOffsetOfLocalHeader() const
{
    return zip64RelativeOffsetOfLocalHeader_;
}

i64 CentralDirectoryFileHeader::getZip64DiskNumberStart() const
{
    return zip64DiskNumberStart_;
}

void CentralDirectoryFileHeader::link(LocalFileHeader* local)
{
    linkedFile_ = local;
}

LocalFileHeader* CentralDirectoryFileHeader::getLinkedFileHeader() const
{
    return linkedFile_;
}

// 对标 Java CentralDirectoryFileHeader.copy()
std::unique_ptr<CentralDirectoryFileHeader> CentralDirectoryFileHeader::copy() const
{
    auto c     = std::make_unique<CentralDirectoryFileHeader>();
    c->data_   = data_;
    c->offset_ = offset_;
    // linkedFile 不复制，副本中为 nullptr
    c->versionMadeBy_                    = versionMadeBy_;
    c->versionNeededToExtract_           = versionNeededToExtract_;
    c->generalPurposeBitFlag_            = generalPurposeBitFlag_;
    c->compressionMethod_                = compressionMethod_;
    c->lastModFileTime_                  = lastModFileTime_;
    c->lastModFileDate_                  = lastModFileDate_;
    c->crc32_                            = crc32_;
    c->compressedSize_                   = compressedSize_;
    c->uncompressedSize_                 = uncompressedSize_;
    c->fileNameLength_                   = fileNameLength_;
    c->extraFieldLength_                 = extraFieldLength_;
    c->fileCommentLength_                = fileCommentLength_;
    c->diskNumberStart_                  = diskNumberStart_;
    c->internalFileAttributes_           = internalFileAttributes_;
    c->externalFileAttributes_           = externalFileAttributes_;
    c->relativeOffsetOfLocalHeader_      = relativeOffsetOfLocalHeader_;
    c->zip64CompressedSize_              = zip64CompressedSize_;
    c->zip64UncompressedSize_            = zip64UncompressedSize_;
    c->zip64RelativeOffsetOfLocalHeader_ = zip64RelativeOffsetOfLocalHeader_;
    c->zip64DiskNumberStart_             = zip64DiskNumberStart_;
    c->fileName_                         = fileName_;
    c->extraField_                       = extraField_;
    c->fileComment_                      = fileComment_;
    return c;
}

// 从原始数据解析 EOCD 记录
void EndOfCentralDirectory::read(const MemorySegment& data, usize offset)
{
    if (data.byteSize() < offset + kEndOfCentralDirectoryMinSize) {
        throw ZipParseException("EOCD truncated", offset);
    }

    const u32 signature = read_u32_le(data, offset);
    if (signature != kEndOfCentralDirectorySignature) {
        throw ZipParseException("invalid EOCD signature", offset);
    }

    offset_                      = offset;
    diskNumber_                  = static_cast<i32>(read_u16_le(data, offset + 4));
    centralDirectoryStartDisk_   = static_cast<i32>(read_u16_le(data, offset + 6));
    centralDirectoryStartOffset_ = static_cast<i32>(read_u16_le(data, offset + 8));
    numEntries_                  = static_cast<i32>(read_u16_le(data, offset + 10));
    centralDirectorySize_ =
        static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 12));
    centralDirectoryOffset_ =
        static_cast<i64>(MemorySegmentUtil::readMaskedLongQuad(data, offset + 16));
    zipCommentLength_ = read_u16_le(data, offset + 20);

    if (offset + kEndOfCentralDirectoryMinSize + zipCommentLength_ > data.byteSize()) {
        throw ZipParseException("EOCD comment out of range", offset);
    }

    zipComment_ = read_string(
        data, offset + kEndOfCentralDirectoryMinSize, static_cast<usize>(zipCommentLength_));
}

usize EndOfCentralDirectory::length() const
{
    return kEndOfCentralDirectoryMinSize + static_cast<usize>(zipCommentLength_);
}

i32 EndOfCentralDirectory::getDiskNumber() const
{
    return diskNumber_;
}

void EndOfCentralDirectory::setDiskNumber(i32 value)
{
    diskNumber_ = value;
}

i32 EndOfCentralDirectory::getCentralDirectoryStartDisk() const
{
    return centralDirectoryStartDisk_;
}

void EndOfCentralDirectory::setCentralDirectoryStartDisk(i32 value)
{
    centralDirectoryStartDisk_ = value;
}

i32 EndOfCentralDirectory::getCentralDirectoryStartOffset() const
{
    return centralDirectoryStartOffset_;
}

void EndOfCentralDirectory::setCentralDirectoryStartOffset(i32 value)
{
    centralDirectoryStartOffset_ = value;
}

i32 EndOfCentralDirectory::getNumEntries() const
{
    return numEntries_;
}

void EndOfCentralDirectory::setNumEntries(i32 value)
{
    numEntries_ = value;
}

i64 EndOfCentralDirectory::getCentralDirectorySize() const
{
    return centralDirectorySize_;
}

void EndOfCentralDirectory::setCentralDirectorySize(i64 value)
{
    centralDirectorySize_ = value;
}

i64 EndOfCentralDirectory::getCentralDirectoryOffset() const
{
    return centralDirectoryOffset_;
}

void EndOfCentralDirectory::setCentralDirectoryOffset(i64 value)
{
    centralDirectoryOffset_ = value;
}

u16 EndOfCentralDirectory::getZipCommentLength() const
{
    return zipCommentLength_;
}

const std::string& EndOfCentralDirectory::getZipComment() const
{
    return zipComment_;
}

// 对标 Java EndOfCentralDirectory.copy()
std::unique_ptr<EndOfCentralDirectory> EndOfCentralDirectory::copy() const
{
    auto c                          = std::make_unique<EndOfCentralDirectory>();
    c->offset_                      = offset_;
    c->diskNumber_                  = diskNumber_;
    c->centralDirectoryStartDisk_   = centralDirectoryStartDisk_;
    c->centralDirectoryStartOffset_ = centralDirectoryStartOffset_;
    c->numEntries_                  = numEntries_;
    c->centralDirectorySize_        = centralDirectorySize_;
    c->centralDirectoryOffset_      = centralDirectoryOffset_;
    c->zipCommentLength_            = zipCommentLength_;
    c->zipComment_                  = zipComment_;
    return c;
}

void ZipArchive::addPart(std::unique_ptr<ZipPart> part)
{
    if (part) {
        parts_.push_back(std::move(part));
    }
}

void ZipArchive::removePart(ZipPart* part)
{
    if (part == nullptr) {
        return;
    }

    parts_.erase(
        std::remove_if(parts_.begin(),
                       parts_.end(),
                       [&](const std::unique_ptr<ZipPart>& item) { return item.get() == part; }),
        parts_.end());
}

// 按比较器对存档的各个部分排序
void ZipArchive::sortParts(const std::function<bool(const ZipPart*, const ZipPart*)>& comparator)
{
    std::stable_sort(parts_.begin(),
                     parts_.end(),
                     [&](const std::unique_ptr<ZipPart>& a, const std::unique_ptr<ZipPart>& b) {
                         return comparator(a.get(), b.get());
                     });
}

std::vector<std::unique_ptr<ZipPart>>& ZipArchive::getParts()
{
    return parts_;
}

const std::vector<std::unique_ptr<ZipPart>>& ZipArchive::getParts() const
{
    return parts_;
}

std::vector<const LocalFileHeader*> ZipArchive::getLocalFiles() const
{
    std::vector<const LocalFileHeader*> locals;
    for (const auto& part : parts_) {
        if (part->type() == PartType::LOCAL_FILE_HEADER) {
            locals.push_back(static_cast<const LocalFileHeader*>(part.get()));
        }
    }
    return locals;
}

std::vector<const CentralDirectoryFileHeader*> ZipArchive::getCentralDirectories() const
{
    std::vector<const CentralDirectoryFileHeader*> centrals;
    for (const auto& part : parts_) {
        if (part->type() == PartType::CENTRAL_DIRECTORY_FILE_HEADER) {
            centrals.push_back(static_cast<const CentralDirectoryFileHeader*>(part.get()));
        }
    }
    return centrals;
}

const EndOfCentralDirectory* ZipArchive::getEnd() const
{
    const EndOfCentralDirectory* result    = nullptr;
    usize                        maxOffset = 0;
    for (const auto& part : parts_) {
        if (part->type() == PartType::END_OF_CENTRAL_DIRECTORY) {
            auto* eocd = static_cast<const EndOfCentralDirectory*>(part.get());
            if (result == nullptr || eocd->offset() > maxOffset) {
                result    = eocd;
                maxOffset = eocd->offset();
            }
        }
    }
    return result;
}

EndOfCentralDirectory* ZipArchive::getEnd()
{
    EndOfCentralDirectory* result    = nullptr;
    usize                  maxOffset = 0;
    for (auto& part : parts_) {
        if (part->type() == PartType::END_OF_CENTRAL_DIRECTORY) {
            auto* eocd = static_cast<EndOfCentralDirectory*>(part.get());
            if (result == nullptr || eocd->offset() > maxOffset) {
                result    = eocd;
                maxOffset = eocd->offset();
            }
        }
    }
    return result;
}

std::vector<const CentralDirectoryFileHeader*> ZipArchive::getNameFilteredCentralDirectories(
    const std::function<bool(const std::string&)>& nameFilter) const
{
    std::vector<const CentralDirectoryFileHeader*> result;
    for (const auto& part : parts_) {
        if (part->type() == PartType::CENTRAL_DIRECTORY_FILE_HEADER) {
            auto* cen = static_cast<const CentralDirectoryFileHeader*>(part.get());
            if (nameFilter(cen->getFileName())) {
                result.push_back(cen);
            }
        }
    }
    return result;
}

std::vector<const LocalFileHeader*> ZipArchive::getNameFilteredLocalFiles(
    const std::function<bool(const std::string&)>& nameFilter) const
{
    // 优先用 CEN 名称（与 Java 对齐）
    auto centrals = getCentralDirectories();
    if (!centrals.empty()) {
        std::vector<const LocalFileHeader*> result;
        for (const auto* cen : centrals) {
            if (nameFilter(cen->getFileName())) {
                auto* local = cen->getLinkedFileHeader();
                if (local != nullptr) {
                    result.push_back(local);
                }
            }
        }
        return result;
    }
    // 回退到 Local 名称
    std::vector<const LocalFileHeader*> result;
    for (const auto& part : parts_) {
        if (part->type() == PartType::LOCAL_FILE_HEADER) {
            auto* local = static_cast<const LocalFileHeader*>(part.get());
            if (nameFilter(local->getFileName())) {
                result.push_back(local);
            }
        }
    }
    return result;
}

const LocalFileHeader* ZipArchive::getLocalFileByName(const std::string& name) const
{
    auto matches = getNameFilteredLocalFiles([&name](const std::string& n) { return n == name; });
    return matches.empty() ? nullptr : matches.front();
}

void ZipArchive::addPartAt(usize index, std::unique_ptr<ZipPart> part)
{
    if (!part) return;
    if (index >= parts_.size()) {
        parts_.push_back(std::move(part));
    }
    else {
        parts_.insert(parts_.begin() + static_cast<std::ptrdiff_t>(index), std::move(part));
    }
}

std::unique_ptr<ZipPart> ZipArchive::removePartAt(usize index)
{
    if (index >= parts_.size()) return nullptr;
    auto it     = parts_.begin() + static_cast<std::ptrdiff_t>(index);
    auto result = std::move(*it);
    parts_.erase(it);
    return result;
}

void ZipArchive::setPrefixData(const MemorySegment& data)
{
    prefixData_ = data;
}

const MemorySegment& ZipArchive::getPrefixData() const
{
    return prefixData_;
}

ZipReadReport& ZipArchive::getReadReport()
{
    return readReport_;
}

const ZipReadReport& ZipArchive::getReadReport() const
{
    return readReport_;
}

ZipArchive ZipArchive::withMapping(const ZipPartMapper& mapper)
{
    return ZipIO::mapArchive(std::move(*this), mapper);
}

}   // namespace jzip
