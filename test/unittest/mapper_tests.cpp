/*
 * Advanced mapper tests using inline data.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_mapper.hpp"

using namespace jzip;

TEST(LibJzipMapperTest, IdentityMapperKeepsArchiveUnchanged)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("a.txt");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0x11111111u);
    local->setCompressedSize(3);
    local->setUncompressedSize(3);
    local->setFileData(std::vector<ca::u8>{'a', 'b', 'c'});
    archive.addPart(std::move(local));

    const auto mapped = ZipIO::mapArchive(std::move(archive), IdentityZipPartMapper{});
    const auto locals = mapped.getLocalFiles();
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(locals[0]->getFileName(), "a.txt");
    EXPECT_EQ(locals[0]->getCrc32(), 0x11111111u);
}

TEST(LibJzipMapperTest, CentralAdoptingMapperUsesCentralMetadata)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("Hello.class/");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0x11111111u);
    local->setCompressedSize(0);
    local->setUncompressedSize(0);
    local->setFileData(std::vector<ca::u8>{'x', 'y', 'z'});
    local->setOffset(7);
    archive.addPart(std::move(local));

    auto central = std::make_unique<CentralDirectoryFileHeader>();
    central->setFileName("Hello.class");
    central->setGeneralPurposeBitFlag(0x0008u);
    central->setCompressionMethod(8);
    central->setCrc32(0x22222222u);
    central->setCompressedSize(30);
    central->setUncompressedSize(99);
    central->setRelativeOffsetOfLocalHeader(7);
    central->setOffset(12);
    archive.addPart(std::move(central));

    const auto mapped = ZipIO::mapArchive(std::move(archive), CentralAdoptingMapper{});

    const auto locals = mapped.getLocalFiles();
    ASSERT_EQ(locals.size(), 1u);
    const auto* mapped_local = locals[0];
    EXPECT_EQ(mapped_local->getFileName(), "Hello.class");
    EXPECT_EQ(mapped_local->getGeneralPurposeBitFlag(), 0x0008u);
    EXPECT_EQ(mapped_local->getCompressionMethod(), 8u);
    EXPECT_EQ(mapped_local->getCrc32(), 0x22222222u);
    EXPECT_EQ(mapped_local->getCompressedSize(), 30u);
    EXPECT_EQ(mapped_local->getUncompressedSize(), 99u);
}

TEST(LibJzipMapperTest, JvmClassDirectoryMapperNormalizesTrailingSlash)
{
    ZipArchive archive{};
    auto       local1 = std::make_unique<LocalFileHeader>();
    local1->setFileName("Hello.class/");
    local1->setGeneralPurposeBitFlag(0);
    local1->setCompressionMethod(0);
    local1->setCrc32(0x11111111u);
    local1->setCompressedSize(10);
    local1->setUncompressedSize(10);
    local1->setFileData(std::vector<ca::u8>{'a', 'b', 'c'});
    archive.addPart(std::move(local1));

    auto local2 = std::make_unique<LocalFileHeader>();
    local2->setFileName("World.txt");
    local2->setGeneralPurposeBitFlag(0);
    local2->setCompressionMethod(0);
    local2->setCrc32(0x22222222u);
    local2->setCompressedSize(5);
    local2->setUncompressedSize(5);
    local2->setFileData(std::vector<ca::u8>{'d', 'e'});
    archive.addPart(std::move(local2));

    auto central1 = std::make_unique<CentralDirectoryFileHeader>();
    central1->setFileName("Hello.class/");
    central1->setGeneralPurposeBitFlag(0);
    central1->setCompressionMethod(0);
    central1->setCrc32(0x11111111u);
    central1->setCompressedSize(10);
    central1->setUncompressedSize(10);
    central1->setRelativeOffsetOfLocalHeader(0);
    archive.addPart(std::move(central1));

    auto central2 = std::make_unique<CentralDirectoryFileHeader>();
    central2->setFileName("World.txt");
    central2->setGeneralPurposeBitFlag(0);
    central2->setCompressionMethod(0);
    central2->setCrc32(0x22222222u);
    central2->setCompressedSize(5);
    central2->setUncompressedSize(5);
    central2->setRelativeOffsetOfLocalHeader(10);
    archive.addPart(std::move(central2));

    const auto mapped = ZipIO::mapArchive(std::move(archive), JvmClassDirectoryMapper{});

    const auto locals = mapped.getLocalFiles();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_EQ(locals[0]->getFileName(), "Hello.class");
    EXPECT_EQ(locals[1]->getFileName(), "World.txt");
    const auto centrals = mapped.getCentralDirectories();
    ASSERT_EQ(centrals.size(), 2u);
    EXPECT_EQ(centrals[0]->getFileName(), "Hello.class");
    EXPECT_EQ(centrals[1]->getFileName(), "World.txt");
}

TEST(LibJzipMapperTest, MapperChainNormalizesThenAdoptsMetadata)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("Hello.class/");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0x11111111u);
    local->setCompressedSize(0);
    local->setUncompressedSize(0);
    local->setFileData(std::vector<ca::u8>{'x', 'y', 'z'});
    local->setOffset(5);
    archive.addPart(std::move(local));

    auto central = std::make_unique<CentralDirectoryFileHeader>();
    central->setFileName("Hello.class");
    central->setGeneralPurposeBitFlag(0x0008u);
    central->setCompressionMethod(8);
    central->setCrc32(0x22222222u);
    central->setCompressedSize(30);
    central->setUncompressedSize(99);
    central->setRelativeOffsetOfLocalHeader(5);
    central->setOffset(12);
    archive.addPart(std::move(central));

    auto       normalized = ZipIO::mapArchive(std::move(archive), JvmClassDirectoryMapper{});
    const auto mapped     = ZipIO::mapArchive(std::move(normalized), CentralAdoptingMapper{});
    const auto locals     = mapped.getLocalFiles();
    ASSERT_EQ(locals.size(), 1u);

    const auto* mapped_local = locals[0];
    EXPECT_EQ(mapped_local->getFileName(), "Hello.class");
    EXPECT_EQ(mapped_local->getGeneralPurposeBitFlag(), 0x0008u);
    EXPECT_EQ(mapped_local->getCompressionMethod(), 8u);
    EXPECT_EQ(mapped_local->getCrc32(), 0x22222222u);
    EXPECT_EQ(mapped_local->getCompressedSize(), 30u);
    EXPECT_EQ(mapped_local->getUncompressedSize(), 99u);
}
