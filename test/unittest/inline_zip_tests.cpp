/*
 * Advanced inline ZIP parsing tests.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/memory_segment.hpp"
#include "jzip/zip_model.hpp"

using namespace jzip;

TEST(InlineZipTest, truncatedZipThrows)
{
    std::vector<ca::u8> bad = {0x50, 0x4B, 0x03, 0x04};
    EXPECT_THROW(jzip::ZipIO::readJvm(jzip::MemorySegment(bad.data(), bad.size())),
                 jzip::ZipParseException);
}

TEST(InlineZipTest, emptyDataThrows)
{
    std::vector<ca::u8> empty;
    EXPECT_THROW(jzip::ZipIO::readJvm(jzip::MemorySegment(empty.data(), empty.size())),
                 jzip::ZipParseException);
}

TEST(InlineZipTest, invalidMagicThrows)
{
    std::vector<ca::u8> bad = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_THROW(jzip::ZipIO::readJvm(jzip::MemorySegment(bad.data(), bad.size())),
                 jzip::ZipParseException);
}
