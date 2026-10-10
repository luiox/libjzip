#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "test_utils.hpp"

using namespace jzip;
using namespace jzip::test;



TEST(UncoveredParseTests, DeceptionJar)
{
    const auto path  = test_resource_path("deception.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    // Ensure parsing does not crash
    EXPECT_NO_THROW({ ZipIO::readStandard(MemorySegment(bytes)); });
    EXPECT_NO_THROW({ ZipIO::readJvm(MemorySegment(bytes)); });
    EXPECT_NO_THROW({ ZipIO::readNaive(MemorySegment(bytes)); });
}

TEST(UncoveredParseTests, InvalidNoEnd)
{
    const auto path  = test_resource_path("invalid-no-end.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    // No END signature: standard and jvm strategies require END
    EXPECT_THROW(ZipIO::readStandard(MemorySegment(bytes)), ZipParseException);
    EXPECT_THROW(ZipIO::readJvm(MemorySegment(bytes)), ZipParseException);
    // Naive reader scans LFH only, does not need END
    EXPECT_NO_THROW(ZipIO::readNaive(MemorySegment(bytes)));
}

TEST(UncoveredParseTests, InvalidNoEndNoCen)
{
    const auto path  = test_resource_path("invalid-no-end-no-cen.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    // No END and no CEN: standard and jvm strategies require END
    EXPECT_THROW(ZipIO::readStandard(MemorySegment(bytes)), ZipParseException);
    EXPECT_THROW(ZipIO::readJvm(MemorySegment(bytes)), ZipParseException);
    // Naive reader scans LFH only, does not need END or CEN
    EXPECT_NO_THROW(ZipIO::readNaive(MemorySegment(bytes)));
}
