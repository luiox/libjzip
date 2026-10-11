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

// 对齐 Java LargeZipTests.testLargeFiles（Java 侧同样以注释块保留）：
// 需要一个包含单个 >2.1GB 文件的 large.zip 样本才有意义，故不参与编译。
/*
TEST(LargeZipTests, DISABLED_TestLargeFiles)
{
    const auto path  = test_resource_path("large.zip");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    auto archive = ZipIO::readStandard(MemorySegment(bytes));
    // Just need a file bigger than 2.1GB to validate this test works
    EXPECT_TRUE(archive.getLocalFiles().at(0)->getFileData().size() > 0x80000000ULL);
}
*/
