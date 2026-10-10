/*
 * Mismatch detection tests — C++ extension, no direct Java equivalent.
 * Tests local/central field mismatch detection via hasDifferentValuesThanCentralDirectoryHeader().
 * Core behavior tested in PartParseTests.testLocalHeaderDetectMismatch;
 * these provide additional sample coverage.
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"

using namespace jzip;

TEST(LibJzipMismatchTest, ZeroLengthLocalSampleReportsMismatch)
{
    auto bytes = ::test::getResource("hello-secret-0-length-locals.jar");

    auto       archive = ZipIO::readStandard(MemorySegment(bytes));
    const auto locals  = archive.getLocalFiles();
    ASSERT_FALSE(locals.empty());

    bool has_mismatch = false;
    for (const auto* local : locals) {
        if (local->hasDifferentValuesThanCentralDirectoryHeader()) {
            has_mismatch = true;
            break;
        }
    }
    EXPECT_TRUE(has_mismatch);
}

TEST(LibJzipMismatchTest, WrongLocalCompressionReportsMismatch)
{
    auto bytes = ::test::getResource("hello-wrong-local-compression.jar");

    auto archive      = ZipIO::readStandard(MemorySegment(bytes));
    bool has_mismatch = false;
    for (const auto* local : archive.getLocalFiles()) {
        if (local->hasDifferentValuesThanCentralDirectoryHeader()) {
            has_mismatch = true;
            break;
        }
    }
    EXPECT_TRUE(has_mismatch);
}
