#include "zip_reader.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace ca;

namespace jzip {
namespace {

constexpr u32   kEndOfCentralDirectorySignature      = 0x06054b50u;
constexpr u32   kCentralDirectoryFileHeaderSignature = 0x02014b50u;
constexpr u32   kLocalFileHeaderSignature            = 0x04034b50u;
constexpr usize kEndOfCentralDirectoryMinSize        = 22;
constexpr usize kCentralDirectoryFileHeaderMinSize   = 46;
constexpr usize kLocalFileHeaderMinSize              = 30;

// 从字节数组小端序读取 u16
u16 read_u16_le(const std::vector<u8>& bytes, usize offset)
{
    return static_cast<u16>(bytes[offset]) | static_cast<u16>(bytes[offset + 1] << 8);
}

// 从字节数组小端序读取 u32
u32 read_u32_le(const std::vector<u8>& bytes, usize offset)
{
    return static_cast<u32>(bytes[offset]) | static_cast<u32>(bytes[offset + 1] << 8) |
           static_cast<u32>(bytes[offset + 2] << 16) | static_cast<u32>(bytes[offset + 3] << 24);
}

// 当中央目录不可用时，从本地文件头反向推导中央目录记录
void recover_central_from_local_headers(const std::vector<u8>& bytes, usize scan_end,
                                        ZipArchive& archive, ZipPartAllocator& allocator)
{
    usize cursor = 0;
    while (cursor + kLocalFileHeaderMinSize <= scan_end) {
        if (read_u32_le(bytes, cursor) != kLocalFileHeaderSignature) {
            ++cursor;
            continue;
        }

        const u16 general_purpose_bit_flag = read_u16_le(bytes, cursor + 6);
        const u16 compression_method       = read_u16_le(bytes, cursor + 8);
        const u32 crc32                    = read_u32_le(bytes, cursor + 14);
        const u32 compressed_size          = read_u32_le(bytes, cursor + 18);
        const u32 uncompressed_size        = read_u32_le(bytes, cursor + 22);
        const u16 file_name_length         = read_u16_le(bytes, cursor + 26);
        const u16 extra_field_length       = read_u16_le(bytes, cursor + 28);

        const usize header_size = kLocalFileHeaderMinSize + static_cast<usize>(file_name_length) +
                                  static_cast<usize>(extra_field_length);
        if (cursor + header_size > scan_end) {
            break;
        }

        const usize name_offset = cursor + kLocalFileHeaderMinSize;
        std::string name(reinterpret_cast<const char*>(&bytes[name_offset]),
                         static_cast<usize>(file_name_length));

        auto central = allocator.newCentralDirectoryFileHeader();
        central->setFileName(name);
        central->setGeneralPurposeBitFlag(general_purpose_bit_flag);
        central->setCompressionMethod(compression_method);
        central->setCrc32(crc32);
        central->setCompressedSize(compressed_size);
        central->setUncompressedSize(uncompressed_size);
        central->setRelativeOffsetOfLocalHeader(static_cast<i64>(cursor));
        central->setOffset(cursor);

        archive.addPart(std::move(central));

        usize       next_cursor = cursor + 1;
        const usize data_start  = cursor + header_size;
        // 用 compressed_size 跳过文件数据区
        if (compressed_size != 0 && data_start + static_cast<usize>(compressed_size) <= scan_end) {
            next_cursor = data_start + static_cast<usize>(compressed_size);
        }
        if (next_cursor <= cursor) {
            next_cursor = cursor + 1;
        }
        cursor = next_cursor;
    }
}

}   // namespace

// 创建 LocalFileHeader 实例
std::unique_ptr<LocalFileHeader> SimpleZipPartAllocator::newLocalFileHeader()
{
    return std::make_unique<LocalFileHeader>();
}

std::unique_ptr<CentralDirectoryFileHeader> SimpleZipPartAllocator::newCentralDirectoryFileHeader()
{
    return std::make_unique<CentralDirectoryFileHeader>();
}

std::unique_ptr<EndOfCentralDirectory> SimpleZipPartAllocator::newEndOfCentralDirectory()
{
    return std::make_unique<EndOfCentralDirectory>();
}

std::unique_ptr<LocalFileHeader> JvmZipPartAllocator::newLocalFileHeader()
{
    return std::make_unique<JvmLocalFileHeader>();
}

std::unique_ptr<CentralDirectoryFileHeader> JvmZipPartAllocator::newCentralDirectoryFileHeader()
{
    return std::make_unique<CentralDirectoryFileHeader>();
}

std::unique_ptr<EndOfCentralDirectory> JvmZipPartAllocator::newEndOfCentralDirectory()
{
    return std::make_unique<EndOfCentralDirectory>();
}

ForwardScanZipReader::ForwardScanZipReader(std::unique_ptr<ZipPartAllocator> allocator)
    : allocator_(std::move(allocator))
{}

// 前向扫描策略：从头寻找 EOCD，然后读取中央目录和 LFH
void ForwardScanZipReader::read(ZipArchive& archive, const MemorySegment& data)
{
    const auto data_size = data.byteSize();
    // 前向查找 EOCD 签名（匹配 Java ForwardScanZipReader 行为）
    auto eocd_offset = MemorySegmentUtil::indexOfQuad(data, 0, kEndOfCentralDirectorySignature);
    if (eocd_offset >= data_size) {
        throw ZipParseException("EOCD signature not found", 0);
    }

    auto end = allocator_->newEndOfCentralDirectory();
    end->read(data, eocd_offset);

    // 捕获声明值（addPart move 之后不可再读）
    const auto declaredCdOffset  = static_cast<ca::u64>(end->getCentralDirectoryOffset());
    const auto archiveEndForward = eocd_offset + kEndOfCentralDirectoryMinSize +
                                   static_cast<usize>(end->getZipCommentLength());
    const auto trailingBytes =
        data_size > archiveEndForward ? data_size - archiveEndForward : 0;

    auto resolved = Zip64Support::resolveEndOfCentralDirectory(data, *end);
    archive.addPart(std::move(end));

    // 结构异常：声明的 CEN 偏移与推导值（EOCD−cdSize）不一致
    if (declaredCdOffset != static_cast<ca::u64>(resolved.centralDirectoryStart)) {
        emit(archive, ZipAnomalyKind::EocdOffsetMismatch, eocd_offset, "",
             "declared=" + std::to_string(declaredCdOffset) +
                 "; derived=" + std::to_string(resolved.centralDirectoryStart));
    }
    // 结构异常：EOCD 之后仍有残余数据
    if (trailingBytes != 0) {
        emit(archive, ZipAnomalyKind::TrailingGarbageAfterEocd, eocd_offset, "",
             "bytes=" + std::to_string(trailingBytes));
    }

    auto cen_offset = resolved.centralDirectoryStart;
    auto cen_end    = resolved.centralDirectoryEnd;

    std::vector<CentralDirectoryFileHeader*> centrals;
    while (cen_offset < cen_end) {
        // 对标 Java：remaining 必须足以容纳最小 CEN header，否则报错
        const auto remaining = cen_end - cen_offset;
        if (remaining < kCentralDirectoryFileHeaderMinSize) {
            throw ZipParseException(
                "Invalid central directory: remaining bytes less than minimum header size",
                cen_offset);
        }
        if (MemorySegmentUtil::readQuad(data, cen_offset) != kCentralDirectoryFileHeaderSignature) {
            // 对标 Java：签名不匹配必须报错（之前 C++ 静默退出循环会丢条目）
            throw ZipParseException("Invalid central directory header signature", cen_offset);
        }

        auto directory = std::make_unique<CentralDirectoryFileHeader>();
        try {
            directory->read(data, cen_offset);
        }
        catch (const ZipParseException& ex) {
            throw ZipParseException(std::string("Failed to read central directory: ") + ex.what(),
                                    cen_offset);
        }

        // 对标 Java：下一个 CEN 偏移必须在 CEN 块内
        const auto nextOffset = cen_offset + directory->length();
        if (nextOffset > cen_end) {
            throw ZipParseException("Invalid central directory length", cen_offset);
        }

        cen_offset    = nextOffset;
        auto* dir_ptr = directory.get();
        archive.addPart(std::move(directory));
        centrals.push_back(dir_ptr);
    }

    // 按中央目录记录的偏移读取 LFH，遇重复偏移则跳过
    std::unordered_set<usize> used_offsets_set;
    usize                     min_local_offset = data_size;
    for (auto* dir : centrals) {
        auto local_offset = static_cast<usize>(
            resolved.baseOffset +
            static_cast<std::ptrdiff_t>(Zip64Support::resolveLocalHeaderOffset(*dir)));

        if (used_offsets_set.count(local_offset)) {
            emit(archive, ZipAnomalyKind::DuplicateLocalOffset, local_offset, dir->getFileName(),
                 "multiple CEN entries map to the same LOC offset");
            continue;
        }
        used_offsets_set.insert(local_offset);

        // 对标 Java：offset 必须保证能容纳完整 LFH 最小尺寸（之前只检查 >= data_size 太宽松）
        if (local_offset + kLocalFileHeaderMinSize > data_size ||
            MemorySegmentUtil::readQuad(data, local_offset) != kLocalFileHeaderSignature) {
            emit(archive, ZipAnomalyKind::LocalHeaderSignatureMiss, local_offset,
                 dir->getFileName(), "no LOC signature at CEN-resolved offset");
            continue;
        }

        auto local = allocator_->newLocalFileHeader();
        try {
            local->AbstractZipFileHeader::read(data, local_offset);
            Zip64Support::resolveLocalFileHeaderSizes(*local);
            local->readFileData(data, local_offset);
        }
        catch (const ZipParseException& ex) {
            // 对标 Java：保留内层异常信息（Java 是 chained IOException）
            throw ZipParseException(std::string("Failed to read local file header: ") + ex.what(),
                                    local_offset);
        }
        // Parse extra fields from raw bytes
        if (local->getExtraFieldLength() > 0) {
            local->setExtraFields(ExtraFieldParser::parse_entries(local->getExtraField()));
        }
        local->link(dir);
        dir->link(local.get());
        postProcessLocalFileHeader(*local);
        local->appendExtraFieldsFromCen();
        archive.addPart(std::move(local));
        if (local_offset < min_local_offset) {
            min_local_offset = local_offset;
        }
    }

    // 记录 ZIP 条目前的非 ZIP 前缀数据
    if (!used_offsets_set.empty() && min_local_offset > 0) {
        archive.setPrefixData(data.asSlice(0, min_local_offset));
        emit(archive, ZipAnomalyKind::PrefixJunkDetected, 0, "",
             "prefix bytes=" + std::to_string(min_local_offset));
    }

    // 按偏移量排序各部分
    archive.sortParts(OffsetComparator{});
}

JvmZipReader::JvmZipReader(bool skipRevisitedCenToLocalLinks, bool allowBasicJvmBaseOffsetZeroCheck,
                           std::unique_ptr<ZipPartAllocator> allocator)
    : skipRevisitedCenToLocalLinks_(skipRevisitedCenToLocalLinks)
    , allowBasicJvmBaseOffsetZeroCheck_(allowBasicJvmBaseOffsetZeroCheck)
    , allocator_(std::move(allocator))
{}

// ── EOCD 验证链 ──────────────────────────────────────────────

std::unique_ptr<JvmZipReader::EndInfo> JvmZipReader::findEndOfCentralDirectory(
    const MemorySegment& data, ZipArchive& zip)
{
    const auto fileLength = data.byteSize();
    if (fileLength < kEndOfCentralDirectoryMinSize) {
        throw ZipParseException("zip bytes smaller than EOCD minimum size", 0);
    }

    // 从文件末尾的 MAX_END_SEARCH 窗口内反向扫描 EOCD 签名
    // 拦截虚假 EOCD 签名（在注释、文件数据、尾部垃圾数据中嵌入的）
    const usize kMaxEndSearch = kEndOfCentralDirectoryMinSize + 0xFFFFu;
    const auto  minOffset     = (fileLength > kMaxEndSearch) ? fileLength - kMaxEndSearch : 0u;

    auto offset =
        MemorySegmentUtil::lastIndexOfQuad(data, fileLength - 4, kEndOfCentralDirectorySignature);

    // 对标 Java：当所有候选均被拒绝时，保留最精确的失败信息
    // （如 split/multi-disk ZIP64、entry count 超 i32 范围等）以便最终抛出
    std::exception_ptr deferredFailure;

    while (offset < fileLength) {
        // 超出搜索窗口
        if (offset < minOffset) break;

        bool rejectedByValidation = false;
        try {
            auto result = tryReadEndOfCentralDirectory(data, offset);
            if (result) {
                return result;
            }
            // 返回 nullptr：结构验证未通过
            rejectedByValidation = true;
        }
        catch (...) {
            if (!deferredFailure) {
                deferredFailure = std::current_exception();
            }
        }
        // 结构异常：被拒绝的虚假 EOCD 候选（验证失败或解析异常）
        emit(zip, ZipAnomalyKind::RejectedEocdCandidate, offset, "",
             rejectedByValidation ? "EOCD candidate failed structural validation"
                                  : "EOCD candidate raised during parse");

        // 回退到上一个 EOCD 候选（lastIndexOfQuad 未找到时返回 fileLength）
        if (offset == 0) break;
        offset =
            MemorySegmentUtil::lastIndexOfQuad(data, offset - 1, kEndOfCentralDirectorySignature);
    }

    if (deferredFailure) {
        std::rethrow_exception(deferredFailure);
    }
    throw ZipParseException("No valid End-Of-Central-Directory found!", 0);
}

std::unique_ptr<JvmZipReader::EndInfo> JvmZipReader::tryReadEndOfCentralDirectory(
    const MemorySegment& data, usize offset)
{
    auto end = allocator_->newEndOfCentralDirectory();
    try {
        end->read(data, offset);
    }
    catch (const ZipParseException&) {
        return nullptr;
    }

    const auto fileLength = data.byteSize();
    const auto archiveEnd =
        offset + kEndOfCentralDirectoryMinSize + static_cast<usize>(end->getZipCommentLength());
    const bool exactEndMatch = (archiveEnd == fileLength);

    // 对标 Java：先解析 ZIP64 拿到真实 CEN 边界，再做结构验证
    // 注意：Java 把 isValidEndHeader 的 endPos 传为 resolvedEnd.centralDirectoryEnd()，
    // 不是 EOCD 起始位置 — 这在 Zip64 下两者不同（ZIP64 EOCD 记录介于传统 EOCD 与 CEN 之间）
    auto resolved = Zip64Support::resolveEndOfCentralDirectory(data, *end);

    // 对标 Java：检查 ZIP64 entry count 是否超出 i32 范围
    if (resolved.zip64Info.has_value()) {
        constexpr u64 kI32Max = static_cast<u64>(std::numeric_limits<i32>::max());
        if (resolved.zip64Info->entriesOnThisDisk > kI32Max ||
            resolved.zip64Info->entryCount > kI32Max) {
            throw ZipParseException("Unsupported ZIP64 entry count range", offset);
        }
    }

    // 对标 Java：centralDirectoryStart 必须非负且不超过 centralDirectoryEnd
    if (static_cast<std::ptrdiff_t>(resolved.centralDirectoryStart) < 0 ||
        resolved.centralDirectoryStart > resolved.centralDirectoryEnd) {
        return nullptr;
    }

    // 如果 comment 长度未对齐文件尾，做结构性回退验证
    // 关键差异：传 resolved.centralDirectoryEnd（Zip64 下 != EOCD 起始）
    if (!exactEndMatch && !isValidEndHeader(data,
                                            resolved.centralDirectoryEnd,
                                            static_cast<u32>(end->getCentralDirectorySize()),
                                            static_cast<u32>(end->getCentralDirectoryOffset()),
                                            static_cast<u16>(end->getNumEntries()))) {
        return nullptr;
    }

    return std::make_unique<EndInfo>(EndInfo{std::move(end),
                                             resolved.centralDirectoryStart,
                                             resolved.centralDirectoryEnd,
                                             resolved.baseOffset});
}

bool JvmZipReader::isValidEndHeader(const MemorySegment& data, usize endPos,
                                    u32 centralDirectorySize, u32 centralDirectoryOffset,
                                    u16 entryCount)
{
    (void)entryCount;   // reserved for ZIP64-aware validation

    const auto centralDirSize   = static_cast<usize>(centralDirectorySize);
    const auto centralDirOffset = static_cast<usize>(centralDirectoryOffset);

    // CEN 块必须完全在 EOCD 之前
    if (endPos < centralDirSize) return false;
    const auto centralDirectoryStart = endPos - centralDirSize;

    // 空 CEN 块：必须紧贴 EOCD 起始
    if (centralDirSize == 0) {
        return centralDirectoryStart == endPos;
    }

    // CEN 块必须有足够空间容纳 header
    const auto fileLength = data.byteSize();
    if (centralDirectoryStart + kCentralDirectoryFileHeaderMinSize > fileLength) {
        return false;
    }

    // CEN 块起始必须有合法 CEN 签名
    if (MemorySegmentUtil::readQuad(data, centralDirectoryStart) !=
        kCentralDirectoryFileHeaderSignature) {
        return false;
    }

    // 验证第一个 CEN 能映射到真实 LOC
    // CEN[42..46] = relative offset of local header
    const auto localOffsetRelative = MemorySegmentUtil::readQuad(data, centralDirectoryStart + 42);
    const auto baseOffset          = static_cast<std::ptrdiff_t>(centralDirectoryStart) -
                            static_cast<std::ptrdiff_t>(centralDirOffset);
    const auto localOffset = baseOffset + static_cast<std::ptrdiff_t>(localOffsetRelative);

    if (localOffset < 0 || static_cast<usize>(localOffset) + kLocalFileHeaderMinSize > fileLength) {
        return false;
    }

    if (MemorySegmentUtil::readQuad(data, static_cast<usize>(localOffset)) !=
        kLocalFileHeaderSignature) {
        return false;
    }

    // 验证 CEN/LOC name_length 一致（CEN[+28] vs LOC[+26]）
    const auto cenNameLen = MemorySegmentUtil::readWord(data, centralDirectoryStart + 28);
    const auto locNameLen = MemorySegmentUtil::readWord(data, static_cast<usize>(localOffset) + 26);

    return cenNameLen == locNameLen;
}

// ── 有界 CEN 解析 ────────────────────────────────────────────

void JvmZipReader::readCentralDirectories(ZipArchive& zip, const MemorySegment& data,
                                          usize centralDirectoryStart, usize endOffset)
{
    auto offset = centralDirectoryStart;

    while (offset < endOffset) {
        const auto remaining = endOffset - offset;
        if (remaining < kCentralDirectoryFileHeaderMinSize) {
            throw ZipParseException(
                "Invalid central directory: trailing bytes before End-Of-Central-Directory",
                offset);
        }
        if (MemorySegmentUtil::readQuad(data, offset) != kCentralDirectoryFileHeaderSignature) {
            throw ZipParseException("Invalid central directory header signature", offset);
        }

        auto directory = allocator_->newCentralDirectoryFileHeader();
        try {
            directory->read(data, offset);
        }
        catch (const ZipParseException& ex) {
            throw ZipParseException(std::string("Failed to read central directory: ") + ex.what(),
                                    offset);
        }

        const auto nextOffset = offset + directory->length();
        if (nextOffset > endOffset) {
            throw ZipParseException("Invalid central directory length", offset);
        }

        zip.addPart(std::move(directory));
        offset = nextOffset;
    }

    // CEN 块必须被干净地消耗完
    if (offset != endOffset) {
        throw ZipParseException("Invalid central directory bounds", offset);
    }
}

// ── BaseOffset 可用性检查 ────────────────────────────────────

bool JvmZipReader::hasAnyLinkedLocalHeader(const MemorySegment& data, ZipArchive& zip,
                                           std::ptrdiff_t baseOffset)
{
    const auto fileLength = data.byteSize();
    for (const auto* cen : zip.getCentralDirectories()) {
        const auto resolvedOffset = Zip64Support::resolveLocalHeaderOffset(*cen);
        const auto signedOffset =
            static_cast<std::ptrdiff_t>(baseOffset) + static_cast<std::ptrdiff_t>(resolvedOffset);
        if (signedOffset < 0) continue;
        const auto offset = static_cast<usize>(signedOffset);
        if (offset + kLocalFileHeaderMinSize <= fileLength &&
            MemorySegmentUtil::readQuad(data, offset) == kLocalFileHeaderSignature) {
            return true;
        }
    }
    return false;
}

std::optional<usize> JvmZipReader::scanForLocalHeaderBaseOffset(const MemorySegment&         data,
                                                                const EndOfCentralDirectory& end)
{
    const auto fileLength = data.byteSize();

    // 快速路径：文件以 LOC 开头，且后面还有第二个 LOC
    if (allowBasicJvmBaseOffsetZeroCheck_ && fileLength >= 4 &&
        MemorySegmentUtil::readQuad(data, 0) == kLocalFileHeaderSignature) {
        const auto nextLoc = MemorySegmentUtil::indexOfQuad(data, 1, kLocalFileHeaderSignature);
        // 对标 Java：只要 nextLoc > MIN_FIXED_SIZE 即认为文件以 LOC 开头，
        // 不需要 < fileLength 限制（单条目归档也是合法场景）
        if (nextLoc > kLocalFileHeaderMinSize) {
            return 0u;
        }
    }

    // 从 EOCD 推导的 baseOffset 附近前向扫描 PK 前缀
    const auto centralDirSize   = static_cast<usize>(end.getCentralDirectorySize());
    const auto centralDirOffset = static_cast<usize>(end.getCentralDirectoryOffset());

    std::ptrdiff_t baseOffsetCalc = 0;
    if (end.offset() >= centralDirSize) {
        baseOffsetCalc = static_cast<std::ptrdiff_t>(end.offset() - centralDirSize) -
                         static_cast<std::ptrdiff_t>(centralDirOffset);
    }

    auto scanPos = static_cast<usize>(std::max<std::ptrdiff_t>(baseOffsetCalc, 0));
    while (scanPos + 4 <= fileLength) {
        const auto sig = MemorySegmentUtil::readQuad(data, scanPos);
        if (sig == kLocalFileHeaderSignature || sig == kCentralDirectoryFileHeaderSignature) {
            return scanPos;
        }
        ++scanPos;
    }

    return std::nullopt;
}

// ── 主读取方法 ────────────────────────────────────────────────

void JvmZipReader::read(ZipArchive& archive, const MemorySegment& data)
{
    // 1. 从文件尾反向扫描并验证 EOCD
    auto  endInfo = findEndOfCentralDirectory(data, archive);
    auto* end     = endInfo->end.get();

    // 捕获声明值（addPart move 之后不可再读）
    const auto declaredCdOffset  = static_cast<ca::u64>(end->getCentralDirectoryOffset());
    const auto archiveEnd        = end->offset() + kEndOfCentralDirectoryMinSize +
                        static_cast<usize>(end->getZipCommentLength());
    const auto trailingBytes =
        data.byteSize() > archiveEnd ? data.byteSize() - archiveEnd : 0;

    const auto endOfCentralDirectoryOffset = end->offset();
    archive.addPart(std::move(endInfo->end));

    // 结构异常：声明的 CEN 偏移与推导值（EOCD−cdSize）不一致
    if (declaredCdOffset != static_cast<ca::u64>(endInfo->centralDirectoryStart)) {
        emit(archive, ZipAnomalyKind::EocdOffsetMismatch, endOfCentralDirectoryOffset, "",
             "declared=" + std::to_string(declaredCdOffset) +
                 "; derived=" + std::to_string(endInfo->centralDirectoryStart));
    }
    // 结构异常：EOCD 之后仍有残余数据
    if (trailingBytes != 0) {
        emit(archive, ZipAnomalyKind::TrailingGarbageAfterEocd, endOfCentralDirectoryOffset, "",
             "bytes=" + std::to_string(trailingBytes));
    }

    const auto fileLength = data.byteSize();

    // 2. 在验证后的 EOCD 边界内有界解析 CEN
    readCentralDirectories(
        archive, data, endInfo->centralDirectoryStart, endInfo->centralDirectoryEnd);

    // 3. 确定 jvmBaseFileOffset（将 CEN 的相对偏移转为文件绝对偏移）
    auto jvmBaseFileOffset = endInfo->baseOffset;

    // 4. 如果 baseOffset 无法映射任何 CEN 到真实 LOC，尝试回退扫描
    if (!hasAnyLinkedLocalHeader(data, archive, jvmBaseFileOffset) &&
        allowBasicJvmBaseOffsetZeroCheck_) {
        auto fallback = scanForLocalHeaderBaseOffset(data, *end);
        if (fallback.has_value() &&
            hasAnyLinkedLocalHeader(data, archive, static_cast<std::ptrdiff_t>(*fallback))) {
            emit(archive, ZipAnomalyKind::BaseOffsetFallback, endOfCentralDirectoryOffset, "",
                 "base " + std::to_string(static_cast<long long>(jvmBaseFileOffset)) + " -> " +
                     std::to_string(*fallback));
            jvmBaseFileOffset = static_cast<std::ptrdiff_t>(*fallback);
        }
    }

    // 5. 构建 entryOffset 排序集（用于确定数据段上下界）
    // 共享 ptr 避免值拷贝到每个 JvmLocalFileHeader（70000 条目时内存暴涨）
    auto  entryOffsets = std::make_shared<std::set<usize>>();
    usize earliestCdfh = std::numeric_limits<usize>::max();
    for (const auto* cen : archive.getCentralDirectories()) {
        if (cen->offset() < earliestCdfh) earliestCdfh = cen->offset();

        const auto resolvedLhOffset = Zip64Support::resolveLocalHeaderOffset(*cen);
        const auto signedOffset     = static_cast<std::ptrdiff_t>(jvmBaseFileOffset) +
                                  static_cast<std::ptrdiff_t>(resolvedLhOffset);
        if (signedOffset >= 0) {
            const auto offset = static_cast<usize>(signedOffset);
            if (offset + 4 <= fileLength &&
                MemorySegmentUtil::readQuad(data, offset) == kLocalFileHeaderSignature) {
                entryOffsets->insert(offset);
            }
        }
    }
    entryOffsets->insert(earliestCdfh);
    entryOffsets->insert(endOfCentralDirectoryOffset);

    // 6. 读取 Local File Header（用 ceiling 边界 + CEN 采用代替内联 DD 检测）
    std::unordered_set<usize>         visitedOffsets;
    std::map<usize, LocalFileHeader*> localFilesByOffset;
    for (const auto* cen : archive.getCentralDirectories()) {
        const auto& entryName = cen->getFileName();
        // 结构异常：可疑条目名（NUL/HTML 片段/路径穿越/控制字符）
        if (isSuspiciousEntryName(entryName)) {
            emit(archive, ZipAnomalyKind::SuspiciousEntryName, cen->offset(), entryName,
                 "entry name looks like an injection or traversal attempt");
        }

        const auto resolvedLhOffset  = Zip64Support::resolveLocalHeaderOffset(*cen);
        const auto signedLocalOffset = static_cast<std::ptrdiff_t>(jvmBaseFileOffset) +
                                       static_cast<std::ptrdiff_t>(resolvedLhOffset);
        // 跳过无效偏移
        if (signedLocalOffset < 0) continue;
        const auto localOffset = static_cast<usize>(signedLocalOffset);

        // 跳过重复 LOC 偏移
        if (!visitedOffsets.insert(localOffset).second) {
            emit(archive, ZipAnomalyKind::DuplicateLocalOffset, localOffset, entryName,
                 "multiple CEN entries map to the same LOC offset");
            auto existing = localFilesByOffset.find(localOffset);
            if (existing != localFilesByOffset.end() && existing->second != nullptr) {
                auto* existingLocal = existing->second;
                existingLocal->link(const_cast<CentralDirectoryFileHeader*>(cen));
                const_cast<CentralDirectoryFileHeader*>(cen)->link(existingLocal);
                try {
                    existingLocal->adoptLinkedCentralDirectoryValues();
                }
                catch (...) {
                    // Keep the existing local linked even if re-adoption fails.
                }
            }
            if (skipRevisitedCenToLocalLinks_) {
                continue;
            }
        }

        // 签名校验
        if (localOffset + 4 > fileLength) {
            emit(archive, ZipAnomalyKind::LocalHeaderSignatureMiss, localOffset, entryName,
                 "LOC offset out of bounds");
            continue;
        }
        if (MemorySegmentUtil::readQuad(data, localOffset) != kLocalFileHeaderSignature) {
            emit(archive, ZipAnomalyKind::LocalHeaderSignatureMiss, localOffset, entryName,
                 "no LOC signature at CEN-resolved offset");
            continue;
        }

        auto local = allocator_->newLocalFileHeader();

        // 传递 entry offsets 用于 ceiling 边界计算（virtual dispatch）
        local->setOffsets(entryOffsets);   // shared_ptr, no copy

        bool localReadOk = false;
        // bounds check 防止 read 越界
        if (localOffset + kLocalFileHeaderMinSize <= fileLength) {
            // 读出 LOC 头字段 + 文件数据（由 JvmLocalFileHeader::readFileData 处理）
            try {
                local->read(data, localOffset);
                localReadOk = true;
            }
            catch (...) {
                // Suppress — CEN adoption will repair
                emit(archive, ZipAnomalyKind::LocalHeaderReadFailed, localOffset, entryName,
                     "LOC parse failed; repaired via CEN adoption");
            }
        }

        // 结构异常：LOC 与 CEN 尺寸不一致（收养前对比，仅在实际读出 LOC 时判定）
        if (localReadOk &&
            (local->getCompressedSize() != cen->getCompressedSize() ||
             local->getUncompressedSize() != cen->getUncompressedSize())) {
            emit(archive, ZipAnomalyKind::SizesAdoptedFromCen, localOffset, entryName,
                 "loc(csize=" + std::to_string(local->getCompressedSize()) +
                     ",usize=" + std::to_string(local->getUncompressedSize()) +
                     ") vs cen(csize=" + std::to_string(cen->getCompressedSize()) +
                     ",usize=" + std::to_string(cen->getUncompressedSize()) + ")");
        }

        // Parse extra fields from raw LOC bytes
        if (local->getExtraFieldLength() > 0) {
            local->setExtraFields(ExtraFieldParser::parse_entries(local->getExtraField()));
        }

        // Link CEN↔LOC
        local->link(const_cast<CentralDirectoryFileHeader*>(cen));
        const_cast<CentralDirectoryFileHeader*>(cen)->link(local.get());

        // Adopt CEN values (realigns file data if needed via readFileData)
        local->adoptLinkedCentralDirectoryValues();

        local->appendExtraFieldsFromCen();

        // 对标 Java：addPart 之后调用 postProcess
        // postProcess 默认空实现，子类（如 PartParseTests）可覆写
        auto* stored = local.get();
        archive.addPart(std::move(local));
        localFilesByOffset[localOffset] = stored;
        postProcessLocalFileHeader(*stored);
    }

    // 7. 记录 ZIP 条目前的前缀数据
    if (!entryOffsets->empty()) {
        const auto firstOffset = *entryOffsets->begin();
        if (firstOffset > 0) {
            archive.setPrefixData(data.asSlice(0, firstOffset));
            emit(archive, ZipAnomalyKind::PrefixJunkDetected, 0, "",
                 "prefix bytes=" + std::to_string(firstOffset));
        }
    }

    // 8. 按偏移量排序
    archive.sortParts(OffsetComparator{});
}

NaiveLocalFileZipReader::NaiveLocalFileZipReader(std::unique_ptr<ZipPartAllocator> allocator)
    : allocator_(std::move(allocator))
{}

// 仅扫描 LFH，完全不依赖中央目录和 EOCD（最简策略）
void NaiveLocalFileZipReader::read(ZipArchive& archive, const MemorySegment& data)
{
    auto local_offset = MemorySegmentUtil::indexOfQuad(data, 0, kLocalFileHeaderSignature);
    if (local_offset >= data.byteSize()) return;

    // 第一个 LFH 前的数据记为前缀
    if (local_offset > 0) {
        archive.setPrefixData(data.asSlice(0, local_offset));
        emit(archive, ZipAnomalyKind::PrefixJunkDetected, 0, "",
             "prefix bytes=" + std::to_string(local_offset));
    }

    // 顺序遍历所有 LFH
    do {
        auto local = allocator_->newLocalFileHeader();
        try {
            local->read(data, local_offset);
        }
        catch (const ZipParseException&) {
            throw ZipParseException("Failed to read local file header", local_offset);
        }
        if (local->getExtraFieldLength() > 0) {
            local->setExtraFields(ExtraFieldParser::parse_entries(local->getExtraField()));
        }
        archive.addPart(std::move(local));
    } while ((local_offset = MemorySegmentUtil::indexOfQuad(
                  data, local_offset + 1, kLocalFileHeaderSignature)) < data.byteSize());
}

}   // namespace jzip
