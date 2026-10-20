/*
 * RecompressingZipWriter tests.
 * 覆盖三类问题：无法解压的 method 不能静默丢条目、零字节文件不能当目录丢弃、
 * 解压输出受默认配额限制（压缩炸弹）。
 */

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "jzip/decompressor.hpp"
#include "jzip/zip_io.hpp"
#include "jzip/zip_writer.hpp"

using namespace jzip;

namespace {

std::unique_ptr<LocalFileHeader> make_local(const std::string& name, ca::u16 method,
                                            const std::vector<ca::u8>& data,
                                            ca::i64 uncompressedSize)
{
    auto local = std::make_unique<LocalFileHeader>();
    local->setFileName(name);
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(method);
    local->setCrc32(0);
    local->setCompressedSize(static_cast<ca::i64>(data.size()));
    local->setUncompressedSize(uncompressedSize);
    local->setFileData(data);
    return local;
}

std::unique_ptr<LocalFileHeader> make_stored_local(const std::string& name,
                                                   const std::string& content)
{
    return make_local(name, 0, std::vector<ca::u8>(content.begin(), content.end()),
                      static_cast<ca::i64>(content.size()));
}

std::vector<ca::u8> write_recompressed(const ZipArchive& archive, bool createDirectoryEntries,
                                       ca::u16 targetMethod)
{
    std::ostringstream oss(std::ios::binary | std::ios::out);
    RecompressingZipWriter(createDirectoryEntries, targetMethod).write(archive, oss);
    const auto&         str = oss.str();
    return std::vector<ca::u8>(str.begin(), str.end());
}

}   // namespace

TEST(LibJzipRecompressingWriterTest, UnsupportedSourceMethodThrowsInsteadOfDroppingEntry)
{
    ZipArchive               archive;
    std::vector<ca::u8>      payload = {'x'};
    archive.addPart(make_local("kept.txt", 0, std::vector<ca::u8>{'a'}, 1));
    archive.addPart(make_local("bzip2.txt", 12, payload, 1));

    std::ostringstream oss(std::ios::binary | std::ios::out);
    try {
        RecompressingZipWriter(false, 0).write(archive, oss);
        FAIL() << "unsupported method was silently dropped";
    }
    catch (const ZipParseException& ex) {
        EXPECT_NE(std::string(ex.what()).find("bzip2.txt"), std::string::npos);
    }
}

TEST(LibJzipRecompressingWriterTest, RejectsUnsupportedTargetMethod)
{
    ZipArchive archive;
    archive.addPart(make_stored_local("a.txt", "a"));

    std::ostringstream oss(std::ios::binary | std::ios::out);
    EXPECT_THROW(RecompressingZipWriter(false, 12).write(archive, oss), ZipParseException);
}

TEST(LibJzipRecompressingWriterTest, KeepsZeroLengthFileWhenDirectoryEntriesDisabled)
{
    ZipArchive archive;
    archive.addPart(make_stored_local("empty.txt", ""));

    // 用 readStandard 验证数据长度：readJvm 把零长度条目按"扫到下一个结构"处理，
    // 会把这之后的内容算进 fileData（见 JvmLocalFileHeader::readFileData）。
    const auto bytes  = write_recompressed(archive, false, 0);
    auto       parsed = ZipIO::readStandard(MemorySegment(bytes));
    const auto locals = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(locals[0]->getFileName(), "empty.txt");
    EXPECT_EQ(locals[0]->getCompressionMethod(), 0);
    EXPECT_EQ(locals[0]->getCompressedSize(), 0);
    EXPECT_EQ(locals[0]->getUncompressedSize(), 0);
    EXPECT_EQ(locals[0]->getFileData().size(), 0u);
    EXPECT_TRUE(ZipIO::decompressLocalFile(*locals[0]).empty());
}

TEST(LibJzipRecompressingWriterTest, DirectoryEntriesFollowCreateFlag)
{
    ZipArchive archive;
    archive.addPart(make_stored_local("dir/", ""));
    archive.addPart(make_stored_local("dir/a.txt", "a"));

    {
        const auto bytes  = write_recompressed(archive, false, 0);
        auto       parsed = ZipIO::readJvm(MemorySegment(bytes));
        const auto locals = parsed.getLocalFiles();
        ASSERT_EQ(locals.size(), 1u);
        EXPECT_EQ(locals[0]->getFileName(), "dir/a.txt");
    }
    {
        const auto bytes  = write_recompressed(archive, true, 0);
        auto       parsed = ZipIO::readJvm(MemorySegment(bytes));
        EXPECT_EQ(parsed.getLocalFiles().size(), 2u);
    }
}

TEST(LibJzipRecompressingWriterTest, EmptyArchiveWritesValidZip)
{
    ZipArchive archive;

    const auto bytes = write_recompressed(archive, false, 0);
    EXPECT_EQ(bytes.size(), 22u);
    auto parsed = ZipIO::readJvm(MemorySegment(bytes));
    EXPECT_TRUE(parsed.getLocalFiles().empty());
}

TEST(LibJzipRecompressingWriterTest, RejectsEntryNameBeyondU16)
{
    ZipArchive archive;
    archive.addPart(make_stored_local(std::string(70000, 'n'), ""));

    std::ostringstream oss(std::ios::binary | std::ios::out);
    EXPECT_THROW(RecompressingZipWriter(false, 0).write(archive, oss), ZipParseException);
}

TEST(LibJzipRecompressingWriterTest, DecompressionIsCapped)
{
    // deflate 最大膨胀比约 1032:1；16 MiB 零数据压到约 16 KB，
    // 默认配额 compressedSize*1000 略小于 16 MiB，解压必须在上限处失败。
    const std::vector<ca::u8> zeros(16ULL << 20, 0);
    const auto                compressed = DeflateCompressor().compress(zeros);
    ASSERT_LT(defaultEntryOutputCap(compressed.size()), zeros.size());

    ZipArchive archive;
    archive.addPart(make_local("bomb.bin", 8, compressed,
                               static_cast<ca::i64>(zeros.size())));

    std::ostringstream oss(std::ios::binary | std::ios::out);
    EXPECT_THROW(RecompressingZipWriter(false, 0).write(archive, oss), ZipParseException);
}
