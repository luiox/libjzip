/*
 * 1:1 mapping to LL-Java-Zip test: LargeZipTests.java
 * - testLargeStrings()
 * - testLargeFiles() [commented out - requires >2.1GB file]
 */

#include <gtest/gtest.h>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"

using namespace jzip;

TEST(LargeZipTests, TestLargeStrings)
{
    auto bytes = ::test::getResource("sample-long-name.zip");

    EXPECT_NO_THROW({ ZipIO::readStandard(MemorySegment(bytes)); });
}

// Left this test out for obvious reasons, all you need to run it is a zip file containing a single
// file >2.1GB
/*
TEST(LargeZipTests, DISABLED_TestLargeFiles) {
    auto bytes = ::test::getResource("large.zip");
    auto archive = ZipIO::readStandard(MemorySegment(bytes));
    // Just need a file bigger than 2.1GB to validate this test works
    EXPECT_TRUE(archive.getLocalFiles().at(0)->getFileData().size() > 0x80000000ULL);
}
 */
