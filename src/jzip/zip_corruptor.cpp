#include "jzip/zip_corruptor.hpp"

#include <cstring>

#include "jzip/zip_io.hpp"
#include "jzip/zip_model.hpp"

namespace jzip {

namespace {

constexpr ca::u32 kLocSignature = 0x04034b50u;
constexpr ca::u32 kCenSignature = 0x02014b50u;
constexpr ca::u32 kEocdSignature = 0x06054b50u;

ca::u32 readU32(const std::vector<ca::u8>& bytes, ca::usize pos)
{
    return static_cast<ca::u32>(bytes[pos]) | (static_cast<ca::u32>(bytes[pos + 1]) << 8) |
           (static_cast<ca::u32>(bytes[pos + 2]) << 16) |
           (static_cast<ca::u32>(bytes[pos + 3]) << 24);
}

void patchU32(std::vector<ca::u8>& bytes, ca::usize pos, ca::u32 value)
{
    bytes[pos]     = static_cast<ca::u8>(value & 0xFF);
    bytes[pos + 1] = static_cast<ca::u8>((value >> 8) & 0xFF);
    bytes[pos + 2] = static_cast<ca::u8>((value >> 16) & 0xFF);
    bytes[pos + 3] = static_cast<ca::u8>((value >> 24) & 0xFF);
}

/// 反向查找 EOCD 签名（限定尾部窗口，与 reader 行为一致）。
ca::usize findEocdPos(const std::vector<ca::u8>& bytes)
{
    if (bytes.size() < 22) return static_cast<ca::usize>(-1);
    const ca::usize window = bytes.size() > (22 + 0xFFFFu) ? (22 + 0xFFFFu) : bytes.size();
    for (ca::usize i = bytes.size() - 22 + 1; i-- > bytes.size() - window;) {
        if (readU32(bytes, i) == kEocdSignature) return i;
    }
    return static_cast<ca::usize>(-1);
}

struct CenRecord
{
    ca::usize pos;   ///< 记录起始偏移
    ca::u16   nameLength;
    ca::u16   extraLength;
    ca::u16   commentLength;
};

std::vector<CenRecord> collectCenRecords(const std::vector<ca::u8>& bytes)
{
    std::vector<CenRecord> records;
    const auto             eocd = findEocdPos(bytes);
    if (eocd == static_cast<ca::usize>(-1)) return records;
    const auto cdSize  = readU32(bytes, eocd + 12);
    const auto cdStart = eocd - cdSize;
    auto       pos     = cdStart;
    while (pos + 46 <= eocd && readU32(bytes, pos) == kCenSignature) {
        CenRecord rec;
        rec.pos           = pos;
        rec.nameLength    = static_cast<ca::u16>(bytes[pos + 28] | (bytes[pos + 29] << 8));
        rec.extraLength   = static_cast<ca::u16>(bytes[pos + 30] | (bytes[pos + 31] << 8));
        rec.commentLength = static_cast<ca::u16>(bytes[pos + 32] | (bytes[pos + 33] << 8));
        records.push_back(rec);
        pos += 46 + rec.nameLength + rec.extraLength + rec.commentLength;
    }
    return records;
}

}   // namespace

ZipCorruptor::ZipCorruptor(std::vector<ca::u8> cleanBytes)
    : cleanBytes_(std::move(cleanBytes))
{}

// ── 模型级 ────────────────────────────────────────────────────

ZipCorruptor& ZipCorruptor::renameEntry(ca::usize index, const std::string& name)
{
    auto archive = ZipIO::readJvm(MemorySegment(cleanBytes_));
    ca::usize seen = 0;
    for (auto& part : archive.getParts()) {
        if (part->type() != PartType::LOCAL_FILE_HEADER) continue;
        if (seen++ != index) continue;
        auto* local = static_cast<LocalFileHeader*>(part.get());
        local->setFileName(name);
        auto* cen = local->getLinkedDirectoryFileHeader();
        if (cen != nullptr) cen->setFileName(name);
        break;
    }
    cleanBytes_ = ZipIO::writeDirect(archive);
    return *this;
}

ZipCorruptor& ZipCorruptor::shrinkLocalSizes(ca::usize index)
{
    auto archive = ZipIO::readJvm(MemorySegment(cleanBytes_));
    ca::usize seen = 0;
    for (auto& part : archive.getParts()) {
        if (part->type() != PartType::LOCAL_FILE_HEADER) continue;
        if (seen++ != index) continue;
        auto* local = static_cast<LocalFileHeader*>(part.get());
        // 只改未压缩尺寸：compressedSize 必须与数据段长度一致（否则 LOC 数据
        // 切片阶段直接抛异常，走 LocalHeaderReadFailed 而非收养路径）。
        const auto usize_ = local->getUncompressedSize();
        local->setUncompressedSize(usize_ / 2);
        break;
    }
    cleanBytes_ = ZipIO::writeDirect(archive);
    return *this;
}

// ── 字节级 ────────────────────────────────────────────────────

ZipCorruptor& ZipCorruptor::prependJunk(ca::usize size, const std::string& magic)
{
    byteOps_.push_back([size, magic](std::vector<ca::u8> bytes) {
        const auto eocd = findEocdPos(bytes);
        const auto staleCdOffset = eocd == static_cast<ca::usize>(-1) ? ca::u32{0}
                                                                     : readU32(bytes, eocd + 16);

        std::vector<ca::u8> junk(size);
        const auto          period = magic.empty() ? 1 : magic.size();
        for (ca::usize i = 0; i < size; ++i) {
            junk[i] = magic.empty() ? ca::u8{'A'} : static_cast<ca::u8>(magic[i % period]);
        }
        bytes.insert(bytes.begin(), junk.begin(), junk.end());

        // 陈旧 cdOffset：恢复加垃圾前的声明值（OS3.0.0H 形态）。
        // 前缀垃圾把全部内容后移，推导值（EOCD−cdSize）仍正确，
        // 而声明值落后恰好一个垃圾长度 → baseOffset = 垃圾大小。
        if (eocd != static_cast<ca::usize>(-1)) {
            patchU32(bytes, findEocdPos(bytes) + 16, staleCdOffset);
        }
        return bytes;
    });
    return *this;
}

ZipCorruptor& ZipCorruptor::lieAboutCdOffset(ca::u32 fakeOffset)
{
    byteOps_.push_back([fakeOffset](std::vector<ca::u8> bytes) {
        const auto eocd = findEocdPos(bytes);
        if (eocd != static_cast<ca::usize>(-1)) patchU32(bytes, eocd + 16, fakeOffset);
        return bytes;
    });
    return *this;
}

ZipCorruptor& ZipCorruptor::breakEntryLocalOffset(ca::usize index)
{
    byteOps_.push_back([index](std::vector<ca::u8> bytes) {
        auto records = collectCenRecords(bytes);
        if (index >= records.size()) return bytes;
        const auto eocd    = findEocdPos(bytes);
        const auto cdStart = eocd - readU32(bytes, eocd + 12);
        // 相对偏移指到 CEN 块自身 → LOC 签名校验必然失败
        patchU32(bytes, records[index].pos + 42, static_cast<ca::u32>(cdStart));
        return bytes;
    });
    return *this;
}

ZipCorruptor& ZipCorruptor::aliasEntryLocalOffset(ca::usize fromIndex, ca::usize toIndex)
{
    byteOps_.push_back([fromIndex, toIndex](std::vector<ca::u8> bytes) {
        auto records = collectCenRecords(bytes);
        if (fromIndex >= records.size() || toIndex >= records.size()) return bytes;
        const auto aliasOffset = readU32(bytes, records[toIndex].pos + 42);
        patchU32(bytes, records[fromIndex].pos + 42, aliasOffset);
        return bytes;
    });
    return *this;
}

ZipCorruptor& ZipCorruptor::appendTrailingGarbage(ca::usize size)
{
    byteOps_.push_back([size](std::vector<ca::u8> bytes) {
        bytes.insert(bytes.end(), size, ca::u8{'G'});
        return bytes;
    });
    return *this;
}

ZipCorruptor& ZipCorruptor::appendGarbageWithFakeEocd(ca::usize size)
{
    byteOps_.push_back([size](std::vector<ca::u8> bytes) {
        bytes.insert(bytes.end(), size, ca::u8{'G'});
        // 虚假 EOCD：签名合法但 cdSize/cdOffset 远超文件范围，结构验证必败
        std::vector<ca::u8> fake(22);
        patchU32(fake, 0, kEocdSignature);
        fake[10] = 0;   // entries this disk
        fake[11] = 0;
        fake[12] = 0;   // total entries
        fake[13] = 0;
        patchU32(fake, 12, 999999u);    // cdSize
        patchU32(fake, 16, 999999u);    // cdOffset
        fake[20] = 0;                   // comment length
        fake[21] = 0;
        bytes.insert(bytes.end(), fake.begin(), fake.end());
        return bytes;
    });
    return *this;
}

std::vector<ca::u8> ZipCorruptor::build()
{
    auto bytes = cleanBytes_;
    for (const auto& op : byteOps_) {
        bytes = op(std::move(bytes));
    }
    return bytes;
}

}   // namespace jzip
