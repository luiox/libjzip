#include "zip64_support.hpp"
#include "memory_segment.hpp"

using namespace ca;

namespace jzip {
namespace {
constexpr u64 kZip64LocatorLength   = 20;
constexpr u64 kZip64RecordMinLength = 56;
/// 传统 EOCD 之前需完整容纳 locator + 最小记录，低于该下限必无有效 ZIP64 结构
constexpr u64 kZip64MinUpperBoundary = kZip64LocatorLength + kZip64RecordMinLength;
constexpr u32 kZip64LocatorSig      = 0x07064b50u;
constexpr u32 kZip64RecordSig       = 0x06064b50u;
}   // namespace

std::optional<Zip64EndInfo> Zip64Support::tryReadZip64EndInfo(const MemorySegment& data,
                                                              usize                endOffset)
{

    auto fileLen = data.byteSize();

    // 合法 ZIP64 布局要求 locator 与最小 zip64 记录都在传统 EOCD 之前：低于
    // kZip64MinUpperBoundary 必无有效结构。该早退同时保证下方
    // endOffset - kZip64RecordMinLength 的减法不下溢、后续读取不越界。
    if (endOffset < kZip64MinUpperBoundary) return std::nullopt;

    usize locatorOffset = endOffset - kZip64LocatorLength;
    if (MemorySegmentUtil::readQuad(data, locatorOffset) != kZip64LocatorSig) return std::nullopt;

    u64 locatorStartDisk = MemorySegmentUtil::readMaskedLongQuad(data, locatorOffset + 4);
    u64 zip64EndOffset   = MemorySegmentUtil::readLong(data, locatorOffset + 8);
    u64 totalDisks       = MemorySegmentUtil::readMaskedLongQuad(data, locatorOffset + 16);

    if (zip64EndOffset > endOffset - kZip64RecordMinLength) return std::nullopt;
    if (static_cast<usize>(zip64EndOffset) + 4 > fileLen) return std::nullopt;
    if (MemorySegmentUtil::readQuad(data, static_cast<usize>(zip64EndOffset)) != kZip64RecordSig)
        return std::nullopt;

    u64 zip64RecordSize = MemorySegmentUtil::readLong(data, static_cast<usize>(zip64EndOffset + 4));
    u64 zip64RecordLength = 12 + zip64RecordSize;
    if (zip64RecordSize < 44) return std::nullopt;
    if (static_cast<usize>(zip64EndOffset + zip64RecordLength) > locatorOffset) return std::nullopt;

    auto         recOff = static_cast<usize>(zip64EndOffset);
    Zip64EndInfo info;
    info.recordOffset              = recOff;
    info.locatorStartDisk          = locatorStartDisk;
    info.totalDisks                = totalDisks;
    info.diskNumber                = MemorySegmentUtil::readMaskedLongQuad(data, recOff + 16);
    info.centralDirectoryStartDisk = MemorySegmentUtil::readMaskedLongQuad(data, recOff + 20);
    info.entriesOnThisDisk         = MemorySegmentUtil::readLong(data, recOff + 24);
    info.entryCount                = MemorySegmentUtil::readLong(data, recOff + 32);
    info.centralDirectorySize      = MemorySegmentUtil::readLong(data, recOff + 40);
    info.centralDirectoryOffset    = MemorySegmentUtil::readLong(data, recOff + 48);

    return info;
}

ResolvedEnd Zip64Support::resolveEndOfCentralDirectory(const MemorySegment&   data,
                                                       EndOfCentralDirectory& end)
{

    usize endOffset = end.offset();
    auto  zip64Info = tryReadZip64EndInfo(data, endOffset);

    if (zip64Info.has_value()) {
        auto& z64 = *zip64Info;

        end.setDiskNumber(static_cast<i32>(z64.diskNumber));
        end.setCentralDirectoryStartDisk(static_cast<i32>(z64.centralDirectoryStartDisk));
        end.setNumEntries(static_cast<i32>(z64.entryCount));
        end.setCentralDirectorySize(static_cast<i64>(z64.centralDirectorySize));
        end.setCentralDirectoryOffset(static_cast<i64>(z64.centralDirectoryOffset));

        usize centralDirectoryStart = 0;
        if (z64.recordOffset >= static_cast<usize>(z64.centralDirectorySize)) {
            centralDirectoryStart = z64.recordOffset - static_cast<usize>(z64.centralDirectorySize);
        }
        std::ptrdiff_t baseOffset = static_cast<std::ptrdiff_t>(centralDirectoryStart) -
                                    static_cast<std::ptrdiff_t>(z64.centralDirectoryOffset);

        return ResolvedEnd{
            centralDirectoryStart, baseOffset, z64.recordOffset, std::move(zip64Info)};
    }

    usize centralDirectoryStart = endOffset - static_cast<usize>(end.getCentralDirectorySize());
    std::ptrdiff_t baseOffset   = static_cast<std::ptrdiff_t>(centralDirectoryStart) -
                                static_cast<std::ptrdiff_t>(end.getCentralDirectoryOffset());

    return ResolvedEnd{centralDirectoryStart, baseOffset, endOffset, std::nullopt};
}

namespace {

/// Parse Zip64 extra field (tag 0x0001) from a CEN entry's extra bytes
/// and read the embedded 64-bit value at the position determined by
/// which preceding CEN fields have magic values.
///
/// @param extra the raw extra field bytes
/// @param cenHasUncompSize true if CEN uncompressedSize is 0xFFFFFFFF
/// @param cenHasCompSize true if CEN compressedSize is 0xFFFFFFFF
/// @param cenHasLocalOffset true if CEN relativeOffset is 0xFFFFFFFF
/// @param fieldIndex 0=uncompSize, 1=compSize, 2=localOffset
/// @param outValue output for the parsed 64-bit value
/// @return true if the field was found and parsed
bool readZip64ExtraField(const std::vector<u8>& extra, bool cenHasUncompSize, bool cenHasCompSize,
                         bool cenHasLocalOffset, int fieldIndex, u64& outValue)
{
    usize i = 0;
    while (i + 4 <= extra.size()) {
        u16 tag  = static_cast<u16>(extra[i]) | (static_cast<u16>(extra[i + 1]) << 8);
        u16 size = static_cast<u16>(extra[i + 2]) | (static_cast<u16>(extra[i + 3]) << 8);
        if (tag == 0x0001) {
            usize dataStart = i + 4;
            if (dataStart + static_cast<usize>(size) > extra.size()) return false;
            usize dataEnd = dataStart + size;
            usize cursor  = dataStart;
            for (int f = 0; f <= fieldIndex && cursor + 8 <= dataEnd; ++f) {
                bool skip = false;
                if (f == 0)
                    skip = !cenHasUncompSize;
                else if (f == 1)
                    skip = !cenHasCompSize;
                else if (f == 2)
                    skip = !cenHasLocalOffset;
                if (f == fieldIndex) {
                    if (skip) return false;
                    outValue = static_cast<u64>(extra[cursor]) |
                               (static_cast<u64>(extra[cursor + 1]) << 8) |
                               (static_cast<u64>(extra[cursor + 2]) << 16) |
                               (static_cast<u64>(extra[cursor + 3]) << 24) |
                               (static_cast<u64>(extra[cursor + 4]) << 32) |
                               (static_cast<u64>(extra[cursor + 5]) << 40) |
                               (static_cast<u64>(extra[cursor + 6]) << 48) |
                               (static_cast<u64>(extra[cursor + 7]) << 56);
                    return true;
                }
                if (!skip) cursor += 8;
            }
            return false;
        }
        i += 4 + size;
    }
    return false;
}

}   // namespace

u64 Zip64Support::resolveLocalHeaderOffset(const CentralDirectoryFileHeader& cen)
{
    i64 raw = cen.getRelativeOffsetOfLocalHeader();
    if (raw != 0xFFFFFFFF) return static_cast<u64>(raw);
    u64 result = 0;
    if (readZip64ExtraField(cen.getExtraField(),
                            cen.getUncompressedSize() == 0xFFFFFFFFLL,
                            cen.getCompressedSize() == 0xFFFFFFFFLL,
                            true,
                            2,
                            result))
        return result;
    return static_cast<u64>(raw);
}

u64 Zip64Support::resolveCompressedSize(const CentralDirectoryFileHeader& cen)
{
    i64 raw = cen.getCompressedSize();
    if (raw != 0xFFFFFFFF) return static_cast<u64>(raw);
    u64 result = 0;
    if (readZip64ExtraField(cen.getExtraField(),
                            cen.getUncompressedSize() == 0xFFFFFFFFLL,
                            true,
                            cen.getRelativeOffsetOfLocalHeader() == 0xFFFFFFFFLL,
                            1,
                            result))
        return result;
    return static_cast<u64>(raw);
}

u64 Zip64Support::resolveUncompressedSize(const CentralDirectoryFileHeader& cen)
{
    i64 raw = cen.getUncompressedSize();
    if (raw != 0xFFFFFFFF) return static_cast<u64>(raw);
    u64 result = 0;
    if (readZip64ExtraField(cen.getExtraField(),
                            true,
                            cen.getCompressedSize() == 0xFFFFFFFFLL,
                            cen.getRelativeOffsetOfLocalHeader() == 0xFFFFFFFFLL,
                            0,
                            result))
        return result;
    return static_cast<u64>(raw);
}

void Zip64Support::resolveLocalFileHeaderSizes(LocalFileHeader& local)
{
    if (local.getCompressedSize() != 0xFFFFFFFF && local.getUncompressedSize() != 0xFFFFFFFF)
        return;

    const auto& extra = local.getExtraField();
    usize       i     = 0;
    while (i + 4 <= extra.size()) {
        u16 tag = static_cast<u16>(extra[i]) | (static_cast<u16>(extra[i + 1]) << 8);
        u16 sz  = static_cast<u16>(extra[i + 2]) | (static_cast<u16>(extra[i + 3]) << 8);
        if (tag == 0x0001 && i + 4 + sz <= extra.size() && sz >= 16) {
            if (local.getUncompressedSize() == 0xFFFFFFFFLL) {
                u64 v = 0;
                for (int b = 0; b < 8; ++b) v |= static_cast<u64>(extra[i + 4 + b]) << (b * 8);
                local.setUncompressedSize(static_cast<i64>(v));
            }
            if (local.getCompressedSize() == 0xFFFFFFFFLL) {
                u64 v = 0;
                for (int b = 0; b < 8; ++b) v |= static_cast<u64>(extra[i + 12 + b]) << (b * 8);
                local.setCompressedSize(static_cast<i64>(v));
            }
            return;
        }
        i += 4 + sz;
    }
}

}   // namespace jzip
