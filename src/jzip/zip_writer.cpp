#include "zip_writer.hpp"

#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "libca/zip/checksum.hpp"
#include "decompressor.hpp"

using namespace ca;

namespace jzip {

constexpr u32 kEndOfCentralDirectorySignature      = 0x06054b50u;
constexpr u32 kCentralDirectoryFileHeaderSignature = 0x02014b50u;
constexpr u32 kLocalFileHeaderSignature            = 0x04034b50u;

// 以小端序追加 u16 到输出缓冲区
void append_u16_le(std::vector<u8>& out, u16 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
}

// 以小端序追加 u32 到输出缓冲区
void append_u32_le(std::vector<u8>& out, u32 value)
{
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

std::vector<u8> serialize_extra_fields(const std::vector<ExtraFieldEntry>& entries)
{
    std::vector<u8> out;
    for (const auto& entry : entries) {
        append_u16_le(out, entry.tag);
        append_u16_le(out, static_cast<u16>(entry.data.size()));
        out.insert(out.end(), entry.data.begin(), entry.data.end());
    }
    return out;
}

void append_memory_segment(std::vector<u8>& out, const MemorySegment& segment)
{
    auto bytes = segment.toByteArray();
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// 对标 Java ZipWriter.writeToByteArray()
std::vector<u8> ZipWriter::writeToByteArray(const ZipArchive& archive)
{
    std::ostringstream oss(std::ios::binary | std::ios::out);
    write(archive, oss);
    const auto& str = oss.str();
    return std::vector<u8>(str.begin(), str.end());
}

// 对标 Java DirectZipWriter.writeLocalFile()
// 注：C++ 兼容保留旧行为：versionNeeded=20 硬编码、extraField 通过 serialize_extra_fields
// 从 getExtraFields() 序列化得到（与旧 builder 模式字节完全一致，避免破坏 trick-jar
// 测试期望的"byte-stable roundtrip"）。
void DirectZipWriter::writeLocalFile(const LocalFileHeader& file, std::vector<u8>& out)
{
    append_u32_le(out, kLocalFileHeaderSignature);
    append_u16_le(out, 20);   // versionNeededToExtract（与旧 C++ 行为一致）
    append_u16_le(out, file.getGeneralPurposeBitFlag());
    append_u16_le(out, file.getCompressionMethod());
    append_u16_le(out, file.getLastModFileTime());
    append_u16_le(out, file.getLastModFileDate());
    append_u32_le(out, file.getCrc32());
    append_u32_le(out, static_cast<u32>(file.getCompressedSize()));
    append_u32_le(out, static_cast<u32>(file.getUncompressedSize()));
    const auto extra_bytes = serialize_extra_fields(file.getExtraFields());
    append_u16_le(out, static_cast<u16>(file.getFileName().size()));
    append_u16_le(out, static_cast<u16>(extra_bytes.size()));

    const auto& name = file.getFileName();
    for (size_t k = 0; k < name.size(); ++k) out.push_back(static_cast<u8>(name[k]));
    for (size_t k = 0; k < extra_bytes.size(); ++k) out.push_back(extra_bytes[k]);
    const auto& data = file.getFileData();
    for (size_t k = 0; k < data.size(); ++k) out.push_back(data[k]);
}

// 对标 Java DirectZipWriter.writeCentralDirectory()
void DirectZipWriter::writeCentralDirectory(const CentralDirectoryFileHeader& dir,
                                            std::vector<u8>&                  out)
{
    append_u32_le(out, kCentralDirectoryFileHeaderSignature);
    append_u16_le(out, dir.getVersionMadeBy());
    append_u16_le(out, dir.getVersionNeededToExtract());
    append_u16_le(out, dir.getGeneralPurposeBitFlag());
    append_u16_le(out, dir.getCompressionMethod());
    append_u16_le(out, dir.getLastModFileTime());
    append_u16_le(out, dir.getLastModFileDate());
    append_u32_le(out, dir.getCrc32());
    append_u32_le(out, static_cast<u32>(dir.getCompressedSize()));
    append_u32_le(out, static_cast<u32>(dir.getUncompressedSize()));
    const auto& extra_bytes = dir.getExtraField();
    append_u16_le(out, dir.getFileNameLength());
    append_u16_le(out, static_cast<u16>(extra_bytes.size()));
    append_u16_le(out, dir.getFileCommentLength());
    append_u16_le(out, static_cast<u16>(dir.getDiskNumberStart()));
    append_u16_le(out, static_cast<u16>(dir.getInternalFileAttributes()));
    append_u32_le(out, dir.getExternalFileAttributes());
    append_u32_le(out, static_cast<u32>(dir.getRelativeOffsetOfLocalHeader()));

    const auto& name = dir.getFileName();
    for (size_t k = 0; k < name.size(); ++k) out.push_back(static_cast<u8>(name[k]));
    for (size_t k = 0; k < extra_bytes.size(); ++k) out.push_back(extra_bytes[k]);
    const auto& comment = dir.getFileComment();
    for (size_t k = 0; k < comment.size(); ++k) out.push_back(static_cast<u8>(comment[k]));
}

// 对标 Java DirectZipWriter.writeEnd()
void DirectZipWriter::writeEnd(const EndOfCentralDirectory& end, std::vector<u8>& out)
{
    append_u32_le(out, kEndOfCentralDirectorySignature);
    append_u16_le(out, static_cast<u16>(end.getDiskNumber()));
    append_u16_le(out, static_cast<u16>(end.getCentralDirectoryStartDisk()));
    append_u16_le(out, static_cast<u16>(end.getCentralDirectoryStartOffset()));
    append_u16_le(out, static_cast<u16>(end.getNumEntries()));
    append_u32_le(out, static_cast<u32>(end.getCentralDirectorySize()));
    append_u32_le(out, static_cast<u32>(end.getCentralDirectoryOffset()));
    append_u16_le(out, end.getZipCommentLength());

    const auto& comment = end.getZipComment();
    out.insert(out.end(), comment.begin(), comment.end());
}

// C++ 兼容：当 archive 没有 CEN 时，从 locals 重建一个最小 CEN 块
// localHeaderOffsets[i] = 阶段一写入第 i 个 LFH 时 out 的位置（即 CEN 的
// relativeOffsetOfLocalHeader）。不能用 local->offset()：未读取的 local 默认
// offset_ = static_cast<usize>(-1) (zip_model.hpp:301)。
void DirectZipWriter::writeBuiltCentralDirectories(const ZipArchive&       archive,
                                                   const std::vector<u32>& localHeaderOffsets,
                                                   std::vector<u8>&        out)
{
    const auto& locals = archive.getLocalFiles();
    for (usize i = 0; i < locals.size(); ++i) {
        const auto* local = locals[i];
        if (local->getFileName().size() > static_cast<usize>(std::numeric_limits<u16>::max())) {
            throw ZipParseException("file name too long for ZIP32", local->offset());
        }
        if (local->getExtraField().size() > static_cast<usize>(std::numeric_limits<u16>::max())) {
            throw ZipParseException("extra field too large for ZIP32", local->offset());
        }

        append_u32_le(out, kCentralDirectoryFileHeaderSignature);
        append_u16_le(out, 20);   // versionMadeBy
        append_u16_le(out, 20);   // versionNeededToExtract
        append_u16_le(out, local->getGeneralPurposeBitFlag());
        append_u16_le(out, local->getCompressionMethod());
        append_u16_le(out, local->getLastModFileTime());
        append_u16_le(out, local->getLastModFileDate());
        append_u32_le(out, local->getCrc32());
        append_u32_le(out, static_cast<u32>(local->getCompressedSize()));
        append_u32_le(out, static_cast<u32>(local->getUncompressedSize()));
        append_u16_le(out, static_cast<u16>(local->getFileName().size()));
        append_u16_le(out, static_cast<u16>(local->getExtraField().size()));
        append_u16_le(out, 0);   // fileCommentLength
        append_u16_le(out, 0);   // diskNumberStart
        append_u16_le(out, 0);   // internalFileAttributes
        append_u32_le(out, 0);   // externalFileAttributes
        append_u32_le(out, localHeaderOffsets[i]);

        const auto& name = local->getFileName();
        out.insert(out.end(), name.begin(), name.end());
        append_memory_segment(out, MemorySegment(local->getExtraField()));
    }
}

// C++ 兼容：当 archive 没有 EOCD 时，构造一个最小 EOCD
// EOCD 二进制字段顺序：diskNumber, centralDirectoryStartDisk, numEntriesOnDisk,
// totalNumEntries, centralDirectorySize, centralDirectoryOffset, zipCommentLength
// （getCentralDirectoryStartOffset() / getNumEntries() 在 C++ 类中是误导性命名，
//  实际对应 numEntriesOnDisk / totalNumEntries 二进制字段；reader 写入字段时已对应）
void DirectZipWriter::writeBuiltEnd(usize entryCount, usize cenSize, usize cenOffset,
                                    std::vector<u8>& out)
{
    append_u32_le(out, kEndOfCentralDirectorySignature);
    append_u16_le(out, 0);                              // diskNumber
    append_u16_le(out, 0);                              // centralDirectoryStartDisk
    append_u16_le(out, static_cast<u16>(entryCount));   // numEntriesOnDisk
    append_u16_le(out, static_cast<u16>(entryCount));   // totalNumEntries
    append_u32_le(out, static_cast<u32>(cenSize));
    append_u32_le(out, static_cast<u32>(cenOffset));
    append_u16_le(out, 0);   // zipCommentLength
}

// 对标 Java DirectZipWriter.write()：三阶段 LOC → CEN → EOCD
// C++ 扩展：1) 支持 archive.getPrefixData() 前缀数据；2) 当 archive 缺 CEN/EOCD 时
// 从 locals 重建（保留旧 C++ 测试场景的兼容性）
void DirectZipWriter::write(const ZipArchive& archive, std::ostream& os)
{
    std::vector<u8> out;

    // C++ 扩展：先写前缀数据（Java 没有此概念）
    append_memory_segment(out, archive.getPrefixData());

    // 第一阶段：所有 Local File Header
    // 记录每个 LFH 在输出中的起始偏移量，供阶段二从 locals 重建 CEN 时使用
    std::vector<u32> localHeaderOffsets;
    localHeaderOffsets.reserve(archive.getLocalFiles().size());
    for (const auto* local : archive.getLocalFiles()) {
        if (local->getFileName().size() > static_cast<usize>(std::numeric_limits<u16>::max())) {
            throw ZipParseException("file name too long for ZIP32", local->offset());
        }
        if (local->getExtraField().size() > static_cast<usize>(std::numeric_limits<u16>::max())) {
            throw ZipParseException("extra field too large for ZIP32", local->offset());
        }
        localHeaderOffsets.push_back(static_cast<u32>(out.size()));
        writeLocalFile(*local, out);
    }

    // 第二阶段：所有 Central Directory Header
    // 对标 Java：优先用 archive.getCentralDirectories()（保留 CEN 完整字段）
    // C++ 扩展：archive 缺 CEN 时从 locals 重建
    // 关键修复：写入 CEN 前必须把 relOff 改为输出中的绝对偏移。
    // reader 用 jvmBaseFileOffset = centralDirectoryStart - centralDirectoryOffset
    // （通常为 0）来解码 relOff；如果 relOff 仍是输入文件的相对值（基于输入
    // 的 jvmBaseFileOffset），reader 会把 CEN 映射到错误的 LFH 位置。
    // 另一关键修复：必须按文件名匹配 CEN 到 LFH，不能假设 CEN 顺序 = LFH 顺序
    // （merged jar 中 CEN=[Hello.class, META-INF/...]、LFH=[META-INF/..., Hello.class]）。
    const auto  cenOffset = out.size();
    const auto& centrals  = archive.getCentralDirectories();
    const auto& locals    = archive.getLocalFiles();
    std::vector<std::unique_ptr<CentralDirectoryFileHeader>>  cenCopies;
    std::unordered_map<const LocalFileHeader*, size_t>        localIndexByPtr;
    std::unordered_map<std::string_view, std::vector<size_t>> localIndicesByName;
    std::unordered_map<std::string_view, size_t>              nextLocalIndexByName;
    localIndexByPtr.reserve(locals.size());
    localIndicesByName.reserve(locals.size());
    for (size_t k = 0; k < locals.size(); ++k) {
        localIndexByPtr.emplace(locals[k], k);
        localIndicesByName[locals[k]->getFileName()].push_back(k);
    }
    if (!centrals.empty()) {
        cenCopies.reserve(centrals.size());
        for (const auto* cen : centrals) {
            auto copy  = cen->copy();
            bool found = false;

            if (const auto* linked = cen->getLinkedFileHeader()) {
                auto linkedIt = localIndexByPtr.find(linked);
                if (linkedIt != localIndexByPtr.end()) {
                    copy->setRelativeOffsetOfLocalHeader(localHeaderOffsets[linkedIt->second]);
                    found = true;
                }
            }

            if (!found) {
                auto indicesIt = localIndicesByName.find(cen->getFileName());
                if (indicesIt != localIndicesByName.end()) {
                    auto& nextIndex = nextLocalIndexByName[cen->getFileName()];
                    if (nextIndex < indicesIt->second.size()) {
                        copy->setRelativeOffsetOfLocalHeader(
                            localHeaderOffsets[indicesIt->second[nextIndex]]);
                        ++nextIndex;
                        found = true;
                    }
                }
            }

            if (!found) {
                // 防御性检查：每个 CEN 条目都必须能在 LFH 中找到对应项。
                // 若找不到，说明 archive 处于不一致状态（孤儿 CEN），继续写入
                // 会产生 relOff=0 的损坏 ZIP。直接抛错比静默回退更安全。
                throw ZipParseException(
                    "DirectZipWriter: CEN entry has no matching LFH: " + cen->getFileName(),
                    cen->offset());
            }
            writeCentralDirectory(*copy, out);
            cenCopies.push_back(std::move(copy));
        }
    }
    else {
        writeBuiltCentralDirectories(archive, localHeaderOffsets, out);
    }
    const auto cenSize    = out.size() - cenOffset;
    const auto entryCount = !centrals.empty() ? centrals.size() : locals.size();

    // 第三阶段：EOCD
    // 对标 Java：优先用 archive.getEnd()（保留 diskNumber、zipComment 等字段）
    // C++ 扩展：archive 缺 EOCD 时构造一个最小 EOCD
    if (const auto* end = archive.getEnd()) {
        EndOfCentralDirectory updated = *end;
        updated.setCentralDirectoryOffset(static_cast<i64>(cenOffset));
        updated.setCentralDirectorySize(static_cast<i64>(cenSize));
        updated.setNumEntries(static_cast<i32>(entryCount));
        writeEnd(updated, out);
    }
    else {
        writeBuiltEnd(entryCount, cenSize, cenOffset, out);
    }

    if (!out.empty()) {
        os.write(reinterpret_cast<const char*>(out.data()),
                 static_cast<std::streamsize>(out.size()));
    }
}

RecompressingZipWriter::RecompressingZipWriter(bool createDirectoryEntries, u16 targetMethod)
    : createDirectoryEntries_(createDirectoryEntries)
    , targetMethod_(targetMethod)
{}

// 解压后重新压缩为指定 method（消除压缩差异）
void RecompressingZipWriter::write(const ZipArchive& archive, std::ostream& os)
{
    struct Entry
    {
        std::string name;
        u32         crc32;
        u32         compressed_size;
        u32         uncompressed_size;
        u16         method;
        u32         local_header_offset;
    };
    std::vector<Entry> entries;
    std::vector<u8>    out;

    for (const auto* local : archive.getLocalFiles()) {
        std::vector<u8> decompressed;
        if (local->getCompressionMethod() == 0) {
            decompressed = local->getFileData();
        }
        else if (local->getCompressionMethod() == 8) {
            DeflateDecompressor decomp;
            decompressed = decomp.decompress(*local, MemorySegment(local->getFileData()));
        }
        else {
            continue;
        }

        if (!createDirectoryEntries_ && decompressed.empty()) {
            continue;
        }

        const auto header_offset = static_cast<u32>(out.size());
        const auto name_bytes =
            std::vector<u8>(local->getFileName().begin(), local->getFileName().end());

        u16             method = decompressed.empty() ? 0 : targetMethod_;
        std::vector<u8> filedata;
        if (method == 8) {
            filedata = DeflateCompressor().compress(decompressed);
        }
        else {
            filedata = decompressed;
        }

        ca::zip::Crc32 crcCalc;
        if (!decompressed.empty()) {
            crcCalc.update(decompressed.data(), decompressed.size());
        }
        u32 crc = crcCalc.value();

        append_u32_le(out, kLocalFileHeaderSignature);
        append_u16_le(out, method == 8 ? 20 : 10);
        append_u16_le(out, 0);
        append_u16_le(out, method);
        append_u16_le(out, 0);
        append_u16_le(out, 0);
        append_u32_le(out, crc);
        append_u32_le(out, static_cast<u32>(filedata.size()));
        append_u32_le(out, static_cast<u32>(decompressed.size()));
        append_u16_le(out, static_cast<u16>(name_bytes.size()));
        append_u16_le(out, 0);

        out.insert(out.end(), name_bytes.begin(), name_bytes.end());
        out.insert(out.end(), filedata.begin(), filedata.end());

        entries.push_back(Entry{local->getFileName(),
                                crc,
                                static_cast<u32>(filedata.size()),
                                static_cast<u32>(decompressed.size()),
                                method,
                                header_offset});
    }

    if (entries.empty() && out.empty()) {
        return;
    }

    const auto central_dir_offset = static_cast<u32>(out.size());
    for (const auto& entry : entries) {
        const auto name_bytes = std::vector<u8>(entry.name.begin(), entry.name.end());
        append_u32_le(out, kCentralDirectoryFileHeaderSignature);
        append_u16_le(out, 20);
        append_u16_le(out, entry.method == 8 ? 20 : 10);
        append_u16_le(out, 0);
        append_u16_le(out, entry.method);
        append_u16_le(out, 0);
        append_u16_le(out, 0);
        append_u32_le(out, entry.crc32);
        append_u32_le(out, entry.compressed_size);
        append_u32_le(out, entry.uncompressed_size);
        append_u16_le(out, static_cast<u16>(name_bytes.size()));
        append_u16_le(out, 0);
        append_u16_le(out, 0);
        append_u16_le(out, 0);
        append_u16_le(out, 0);
        append_u32_le(out, 0);
        append_u32_le(out, entry.local_header_offset);
        out.insert(out.end(), name_bytes.begin(), name_bytes.end());
    }

    const auto central_dir_size = static_cast<u32>(out.size()) - central_dir_offset;
    const auto entry_count      = static_cast<u16>(entries.size());

    append_u32_le(out, kEndOfCentralDirectorySignature);
    append_u16_le(out, 0);
    append_u16_le(out, 0);
    append_u16_le(out, entry_count);
    append_u16_le(out, entry_count);
    append_u32_le(out, central_dir_size);
    append_u32_le(out, central_dir_offset);
    append_u16_le(out, 0);

    if (!out.empty()) {
        os.write(reinterpret_cast<const char*>(out.data()),
                 static_cast<std::streamsize>(out.size()));
    }
}

}   // namespace jzip
