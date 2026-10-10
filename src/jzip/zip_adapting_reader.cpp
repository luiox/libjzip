#include "zip_adapting_reader.hpp"

#include <memory>
#include <utility>

#include "libca/zip/entry.hpp"
#include "libca/zip/file.hpp"

using namespace ca;

namespace jzip {
namespace {

void fillArchiveFromZipFile(ZipArchive& archive, const ca::zip::ZipFile& zipFile)
{
    for (size_t i = 0; i < zipFile.size(); ++i) {
        const auto& entry = zipFile.get_entry_at(i);

        auto local = std::make_unique<LocalFileHeader>();
        local->setFileName(entry.name());
        // libca 0.0.9：ZipFile::read 错误通道由异常改为 Result<T, ZipErrorInfo>。
        // 条目读取失败（结构损坏/CRC 不符/压缩方法不支持等）属归档损坏，按本
        // 模块既有体例（decompressor/zip_io 同类失败）抛 ZipParseException 向上
        // 传播，不静默降级；详情取 ZipErrorInfo.message（含条目名等动态信息）。
        auto bytes = zipFile.read(entry);
        if (bytes.is_err()) {
            throw ZipParseException(
                "failed to read entry '" + entry.name() + "': " + bytes.unwrap_err().message,
                static_cast<ca::usize>(entry.relative_offset()));
        }
        local->setFileData(bytes.unwrap());
        local->setCompressionMethod(0);
        local->setUncompressedSize(static_cast<i64>(local->getFileData().size()));
        local->setCompressedSize(static_cast<i64>(local->getFileData().size()));
        local->setCrc32(entry.crc32());
        local->setExtraField(entry.extra_field());
        local->setOffset(static_cast<usize>(entry.relative_offset()));
        archive.addPart(std::move(local));
    }
}

}   // namespace

// 对标 Java AdaptingZipReader：委托给 stdlib 风格 ZipFile，
// 直接产出已解压、method=stored 的干净条目。
void AdaptingZipReader::read(ZipArchive& archive, const MemorySegment& data)
{
    ca::zip::ZipFile zipFile(data.toByteArray());
    fillArchiveFromZipFile(archive, zipFile);
}

void AdaptingZipReader::readFromPath(ZipArchive& archive, const std::string& path)
{
    ca::zip::ZipFile zipFile(path);
    fillArchiveFromZipFile(archive, zipFile);
}

}   // namespace jzip
