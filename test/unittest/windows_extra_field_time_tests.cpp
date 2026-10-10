#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "test_utils.hpp"

using namespace jzip;
using namespace jzip::test;



class WindowsExtraFieldTimeTests : public ::testing::TestWithParam<std::string>
{};

TEST_P(WindowsExtraFieldTimeTests, Validity)
{
    const auto& mode  = GetParam();
    const auto  path  = test_resource_path("content-with-windows-time.jar");
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    const ca::u64 timeCreate = 1000000000000ull;
    const ca::u64 timeModify = 1200000000000ull;
    const ca::u64 timeAccess = 1400000000000ull;

    ZipArchive archive{};
    if (mode == "standard") {
        archive = ZipIO::readStandard(MemorySegment(bytes));
    }
    else if (mode == "naive") {
        archive = ZipIO::readNaive(MemorySegment(bytes));
    }
    else if (mode == "jvm") {
        archive = ZipIO::readJvm(MemorySegment(bytes));
    }
    else {
        FAIL() << "IllegalStateException";
    }

    const auto locals = archive.getLocalFiles();
    ASSERT_FALSE(locals.empty());
    const auto* header = locals[0];
    ASSERT_NE(nullptr, header);

    const auto ntfs_time = ExtraFieldParser::parse_ntfs_time(header->getExtraFields());
    ASSERT_TRUE(ntfs_time.has_value()) << "Missing time data";

    EXPECT_EQ(ntfs_time->creation_ms, timeCreate);
    EXPECT_EQ(ntfs_time->modify_ms, timeModify);
    EXPECT_EQ(ntfs_time->access_ms, timeAccess);
}

INSTANTIATE_TEST_SUITE_P(WindowsExtraFieldTimeTests, WindowsExtraFieldTimeTests,
                         ::testing::Values("standard", "naive", "jvm"));
