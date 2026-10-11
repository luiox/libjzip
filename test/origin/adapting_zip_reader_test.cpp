/*
 * 1:1 mapping to LL-Java-Zip test: AdaptingZipReaderTests.java
 * - testReadArchiveExceedingBufferSize()
 *
 * Regression test for the AdaptingZipReader buffer loop (Java PR #32):
 * archives larger than the internal 16KB buffer must not lose entries.
 * The C++ AdaptingZipReader delegates to libca ca::zip::ZipFile, so this
 * guards the same property over the delegation path.
 *
 * C++ notes:
 * - Java builds the sample via ZipOutputStream(STORED); the C++ side builds
 *   the equivalent single stored entry via ZipIO::writeDirect.
 * - CRC32 comes from zlib, mirroring java.util.zip.CRC32.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "jzip/zip_adapting_reader.hpp"
#include "jzip/zip_io.hpp"
#include "jzip/zip_model.hpp"
#include "utils.hpp"

using namespace jzip;

TEST(AdaptingZipReaderTests, TestReadArchiveExceedingBufferSize)
{
    // Single stored entry "large_entry.txt" with 20KB of dummy data.
    constexpr ca::usize kDataSize = 20u * 1024u;
    std::vector<ca::u8> dummyData(kDataSize, 0x00);

    ZipArchive archiveOut;
    auto local = std::make_unique<LocalFileHeader>();
    local->setFileName("large_entry.txt");
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);   // STORED
    local->setCrc32(jzip::test_utils::computeCrc32(dummyData));
    local->setCompressedSize(static_cast<ca::i64>(dummyData.size()));
    local->setUncompressedSize(static_cast<ca::i64>(dummyData.size()));
    local->setFileData(dummyData);
    archiveOut.addPart(std::move(local));
    const auto zipBytes = ZipIO::writeDirect(archiveOut);

    ASSERT_GT(zipBytes.size(), 16384u) << "Test archive size must exceed 16KB";

    ZipArchive archive;
    ASSERT_NO_THROW({
        AdaptingZipReader reader;
        reader.read(archive, MemorySegment(zipBytes));
    }) << "Failed to read with read(adapting) when size exceeds buffer";

    const LocalFileHeader* largeEntry = nullptr;
    for (const auto* lfh : archive.getLocalFiles()) {
        if (lfh->getFileName() == "large_entry.txt") {
            largeEntry = lfh;
            break;
        }
    }
    ASSERT_NE(largeEntry, nullptr) << "Missing 'large_entry.txt' file due to truncation";

    // Adapting reader hands out decompressed (method 0) entries; the 20KB payload must survive.
    EXPECT_EQ(largeEntry->getCompressionMethod(), 0u);
    EXPECT_EQ(largeEntry->getFileData().size(), kDataSize);
}
