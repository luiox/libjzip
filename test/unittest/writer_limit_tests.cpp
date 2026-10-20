/*
 * Writer ZIP32 field limit tests.
 * DirectZipWriter 只写 ZIP32；字段超出 u16/u32 时必须报错，不能截断。
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_writer.hpp"

using namespace jzip;

namespace {

std::unique_ptr<LocalFileHeader> make_local(const std::string& name, ca::i64 compressed,
                                            ca::i64 uncompressed)
{
    auto local = std::make_unique<LocalFileHeader>();
    local->setFileName(name);
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0);
    local->setCompressedSize(compressed);
    local->setUncompressedSize(uncompressed);
    return local;
}

}   // namespace

TEST(LibJzipWriterLimitTest, RejectsEntrySizeBeyondZip32)
{
    ZipArchive archive;
    archive.addPart(make_local("a.txt", 0x100000000LL, 0x100000000LL));

    EXPECT_THROW(ZipIO::writeDirect(archive), ZipParseException);
}

TEST(LibJzipWriterLimitTest, RejectsNegativeEntrySize)
{
    ZipArchive archive;
    archive.addPart(make_local("a.txt", -1, -1));

    EXPECT_THROW(ZipIO::writeDirect(archive), ZipParseException);
}

TEST(LibJzipWriterLimitTest, RejectsCentralExtraFieldBeyondU16)
{
    ZipArchive archive;
    archive.addPart(make_local("a.txt", 0, 0));

    auto central = std::make_unique<CentralDirectoryFileHeader>();
    central->setFileName("a.txt");
    central->setExtraField(std::vector<ca::u8>(70000, 0));
    archive.addPart(std::move(central));

    EXPECT_THROW(ZipIO::writeDirect(archive), ZipParseException);
}

TEST(LibJzipWriterLimitTest, RejectsFileCommentBeyondU16)
{
    ZipArchive archive;
    archive.addPart(make_local("a.txt", 0, 0));

    auto central = std::make_unique<CentralDirectoryFileHeader>();
    central->setFileName("a.txt");
    central->setFileComment(std::string(70000, 'c'));
    archive.addPart(std::move(central));

    EXPECT_THROW(ZipIO::writeDirect(archive), ZipParseException);
}

TEST(LibJzipWriterLimitTest, RejectsEocdFieldBeyondU16)
{
    ZipArchive archive;
    archive.addPart(make_local("a.txt", 0, 0));

    auto end = std::make_unique<EndOfCentralDirectory>();
    end->setDiskNumber(70000);
    archive.addPart(std::move(end));

    EXPECT_THROW(ZipIO::writeDirect(archive), ZipParseException);
}

TEST(LibJzipWriterLimitTest, RejectsEntryCountBeyondU16)
{
    ZipArchive archive;
    for (int i = 0; i < 65536; ++i) {
        archive.addPart(make_local("", 0, 0));
    }

    EXPECT_THROW(ZipIO::writeDirect(archive), ZipParseException);
}
