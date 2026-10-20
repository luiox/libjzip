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

TEST(LibJzipMapperTest, CopyingMapperReturnsIndependentCopies)
{
    ZipArchive archive{};
    auto       local = std::make_unique<LocalFileHeader>();
    local->setFileName("a.txt");
    local->setOffset(5);
    local->setCrc32(0x11111111u);
    local->setFileData(std::vector<ca::u8>{'a', 'b', 'c'});
    LocalFileHeader* original_local = local.get();
    archive.addPart(std::move(local));

    auto central = std::make_unique<CentralDirectoryFileHeader>();
    central->setFileName("a.txt");
    central->setCrc32(0xABCDEF01u);
    central->setRelativeOffsetOfLocalHeader(5);
    CentralDirectoryFileHeader* original_central = central.get();
    archive.addPart(std::move(central));

    auto eocd = std::make_unique<EndOfCentralDirectory>();
    eocd->setNumEntries(1);
    eocd->setCentralDirectorySize(46);
    eocd->setCentralDirectoryOffset(8);
    EndOfCentralDirectory* original_eocd = eocd.get();
    archive.addPart(std::move(eocd));

    CopyingZipPartMapper    mapper;
    std::unique_ptr<ZipPart> copied_local{mapper.map(archive, original_local)};
    std::unique_ptr<ZipPart> copied_central{mapper.map(archive, original_central)};
    std::unique_ptr<ZipPart> copied_eocd{mapper.map(archive, original_eocd)};

    ASSERT_NE(copied_local, nullptr);
    ASSERT_NE(copied_central, nullptr);
    ASSERT_NE(copied_eocd, nullptr);
    EXPECT_NE(copied_local.get(), original_local);
    EXPECT_NE(copied_central.get(), original_central);
    EXPECT_NE(copied_eocd.get(), original_eocd);
    EXPECT_EQ(copied_local->type(), PartType::LOCAL_FILE_HEADER);
    EXPECT_EQ(copied_central->type(), PartType::CENTRAL_DIRECTORY_FILE_HEADER);
    EXPECT_EQ(copied_eocd->type(), PartType::END_OF_CENTRAL_DIRECTORY);

    auto* local_copy = static_cast<LocalFileHeader*>(copied_local.get());
    EXPECT_EQ(local_copy->getFileName(), "a.txt");
    EXPECT_EQ(local_copy->offset(), 5u);
    EXPECT_EQ(local_copy->getCrc32(), 0x11111111u);
    EXPECT_EQ(local_copy->getFileData(), original_local->getFileData());

    /// 修改副本不得影响原部件
    local_copy->setFileName("changed.txt");
    local_copy->setFileData(std::vector<ca::u8>{});
    EXPECT_EQ(original_local->getFileName(), "a.txt");
    EXPECT_FALSE(original_local->getFileData().empty());

    auto* central_copy = static_cast<CentralDirectoryFileHeader*>(copied_central.get());
    EXPECT_EQ(central_copy->getCrc32(), 0xABCDEF01u);
    EXPECT_EQ(central_copy->getRelativeOffsetOfLocalHeader(), 5);
    auto* eocd_copy = static_cast<EndOfCentralDirectory*>(copied_eocd.get());
    EXPECT_EQ(eocd_copy->getNumEntries(), 1);
    EXPECT_EQ(eocd_copy->getCentralDirectoryOffset(), 8);
}

TEST(LibJzipMapperTest, CopyingMapperDropsLinksButKeepsContent)
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

    auto adopted = ZipIO::mapArchive(std::move(archive), CentralAdoptingMapper{});
    ASSERT_EQ(adopted.getLocalFiles().size(), 1u);
    ASSERT_NE(adopted.getLocalFiles()[0]->getLinkedDirectoryFileHeader(), nullptr);

    auto copied = ZipIO::mapArchive(std::move(adopted), CopyingZipPartMapper{});
    ASSERT_EQ(copied.getLocalFiles().size(), 1u);
    ASSERT_EQ(copied.getCentralDirectories().size(), 1u);

    /// 深拷贝切断 link，内容保留采纳后的 CEN 值
    const auto* mapped_local = copied.getLocalFiles()[0];
    EXPECT_EQ(mapped_local->getLinkedDirectoryFileHeader(), nullptr);
    EXPECT_EQ(mapped_local->getFileName(), "Hello.class");
    EXPECT_EQ(mapped_local->getCrc32(), 0x22222222u);
    EXPECT_EQ(mapped_local->getFileData(), (std::vector<ca::u8>{'x', 'y', 'z'}));
    EXPECT_EQ(copied.getCentralDirectories()[0]->getLinkedFileHeader(), nullptr);
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
