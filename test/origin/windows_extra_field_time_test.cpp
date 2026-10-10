/*
 * 1:1 mapping to LL-Java-Zip test: WindowsExtraFieldTimeTests.java
 * - validity(String mode) parameterized with {"standard", "naive", "jvm"}
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"

using namespace jzip;

class WindowsExtraFieldTimeTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(WindowsExtraFieldTimeTests, Validity)
{
    const auto& mode  = GetParam();
    auto        bytes = ::test::getResource("content-with-windows-time.jar");

    ZipArchive archive = (mode == "standard") ? ZipIO::readStandard(MemorySegment(bytes))
                         : (mode == "naive")  ? ZipIO::readNaive(MemorySegment(bytes))
                                              : ZipIO::readJvm(MemorySegment(bytes));

    ASSERT_GE(archive.getLocalFiles().size(), 1u) << "mode=" << mode;
    const auto* testing = archive.getLocalFiles()[0];
    ASSERT_NE(testing, nullptr) << "mode=" << mode;
    auto ntfs = ExtraFieldParser::parse_ntfs_time(testing->getExtraFields());
    ASSERT_NE(ntfs, std::nullopt) << "mode=" << mode;
    EXPECT_EQ(ntfs->creation_ms, 1000000000000ull) << "mode=" << mode;
    EXPECT_EQ(ntfs->modify_ms, 1200000000000ull) << "mode=" << mode;
    EXPECT_EQ(ntfs->access_ms, 1400000000000ull) << "mode=" << mode;
}
INSTANTIATE_TEST_SUITE_P(WindowsExtraFieldTimeTests, WindowsExtraFieldTimeTests,
                         ::testing::Values("standard", "naive", "jvm"));
