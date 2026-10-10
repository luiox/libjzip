#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "test_utils.hpp"

using namespace jzip;
using namespace jzip::test;



TEST(LargeZipTests, TestLargeStrings)
{
    const auto path  = test_resource_path("sample-long-name.zip");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());
    EXPECT_NO_THROW({ ZipIO::readStandard(MemorySegment(bytes)); });
}

// Left this test out for obvious reasons, all you need to run it is a zip file containing a single
// file >2.1GB TEST(LargeZipTests, TestLargeFiles) {
//     try {
//         ZipArchive zip = ZipIO.readStandard(Paths.get("src/test/resources/large.zip"));
//         // Just need a file bigger than 2.1GB to validate this test works
//         Assertions.assertTrue(zip.getLocalFiles().get(0).getFileData().length() > 0x80000000L);
//     } catch (IOException error) {
//         Assertions.fail(error);
//     }
// }
