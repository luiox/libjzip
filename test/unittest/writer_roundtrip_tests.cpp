/*
 * Writer round-trip tests using inline data.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_writer.hpp"

using namespace jzip;

TEST(LibJzipWriterRoundTripTest, DelegatingZipWriterDelegates)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("a.txt");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0);
    local->setCompressedSize(3);
    local->setUncompressedSize(3);
    local->setFileData(std::vector<ca::u8>{'a', 'b', 'c'});
    archive.addPart(std::move(local));
    auto archiveBytes = ZipIO::writeDirect(archive);

    // Re-read to get a proper ZipArchive structure
    auto archived = ZipIO::readJvm(MemorySegment(archiveBytes));

    // Write via DelegatingZipWriter wrapping DirectZipWriter
    auto                inner = std::make_unique<DirectZipWriter>();
    DelegatingZipWriter writer(std::move(inner));
    std::ostringstream  oss(std::ios::binary | std::ios::out);
    writer.write(archived, oss);
    auto str = oss.str();
    EXPECT_GT(str.size(), 30u);

    auto result = std::vector<ca::u8>(str.begin(), str.end());
    auto parsed = ZipIO::readJvm(MemorySegment(result));
    EXPECT_EQ(parsed.getLocalFiles().size(), 1u);
    EXPECT_EQ(parsed.getCentralDirectories().size(), 1u);
}

TEST(LibJzipWriterRoundTripTest, WriteDirectRoundTrip)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("a.txt");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0);
    local->setCompressedSize(3);
    local->setUncompressedSize(3);
    local->setFileData(std::vector<ca::u8>{'a', 'b', 'c'});
    archive.addPart(std::move(local));

    auto bytes_result = ZipIO::writeDirect(archive);

    auto parsed = ZipIO::readJvm(MemorySegment(bytes_result));

    const auto centrals = parsed.getCentralDirectories();
    const auto locals   = parsed.getLocalFiles();
    ASSERT_EQ(centrals.size(), 1u);
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(centrals[0]->getFileName(), "a.txt");
    EXPECT_EQ(locals[0]->getFileData().size(), 3u);
    EXPECT_EQ(locals[0]->getFileData()[0], static_cast<ca::u8>('a'));
}

TEST(LibJzipWriterRoundTripTest, RecompressToDeflate)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("a.txt");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0);
    local->setCompressedSize(3);
    local->setUncompressedSize(3);
    local->setFileData(std::vector<ca::u8>{'a', 'b', 'c'});
    archive.addPart(std::move(local));

    std::ostringstream     oss(std::ios::binary | std::ios::out);
    RecompressingZipWriter writer(false, 8);
    writer.write(archive, oss);
    const auto&         str = oss.str();
    std::vector<ca::u8> bytes_result(str.begin(), str.end());

    auto       parsed = ZipIO::readJvm(MemorySegment(bytes_result));
    const auto locals = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(locals[0]->getCompressionMethod(), 8);
    auto decompressed = ZipIO::decompressLocalFile(*locals[0]);
    EXPECT_EQ(decompressed.size(), 3u);
    EXPECT_EQ(decompressed[0], static_cast<ca::u8>('a'));
    EXPECT_EQ(decompressed[1], static_cast<ca::u8>('b'));
    EXPECT_EQ(decompressed[2], static_cast<ca::u8>('c'));
}

TEST(LibJzipWriterRoundTripTest, DirectWriterPreservesDuplicateEntryOffsets)
{
    ZipArchive archive{};

    auto first = std::make_unique<LocalFileHeader>();
    first->setFileName("dup.txt");
    first->setGeneralPurposeBitFlag(0);
    first->setCompressionMethod(0);
    first->setCrc32(0);
    first->setCompressedSize(1);
    first->setUncompressedSize(1);
    first->setFileData(std::vector<ca::u8>{'A'});
    archive.addPart(std::move(first));

    auto second = std::make_unique<LocalFileHeader>();
    second->setFileName("dup.txt");
    second->setGeneralPurposeBitFlag(0);
    second->setCompressionMethod(0);
    second->setCrc32(0);
    second->setCompressedSize(1);
    second->setUncompressedSize(1);
    second->setFileData(std::vector<ca::u8>{'B'});
    archive.addPart(std::move(second));

    auto initialBytes = ZipIO::writeDirect(archive);
    auto parsed       = ZipIO::readJvm(MemorySegment(initialBytes));

    DirectZipWriter    writer;
    std::ostringstream oss(std::ios::binary | std::ios::out);
    writer.write(parsed, oss);
    const auto rewrittenStr = oss.str();
    auto       rewritten    = std::vector<ca::u8>(rewrittenStr.begin(), rewrittenStr.end());

    auto       reparsed = ZipIO::readJvm(MemorySegment(rewritten));
    const auto locals   = reparsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 2u);
    ASSERT_EQ(reparsed.getCentralDirectories().size(), 2u);

    auto firstData  = ZipIO::decompressLocalFile(*locals[0]);
    auto secondData = ZipIO::decompressLocalFile(*locals[1]);
    EXPECT_EQ(firstData, std::vector<ca::u8>({'A'}));
    EXPECT_EQ(secondData, std::vector<ca::u8>({'B'}));
}
