/*
 * Reader error path tests.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>

#include <vector>

#include "jzip/zip_io.hpp"

using namespace jzip;

TEST(LibJzipReaderErrorTest, MissingEocdReturnsError)
{
    const std::vector<ca::u8> bad_bytes = {0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_THROW(ZipIO::readJvm(MemorySegment(bad_bytes)), ZipParseException);
}
