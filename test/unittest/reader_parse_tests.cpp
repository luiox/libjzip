/*
 * Minimal archive parsing tests using inline builders.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>

#include "jzip/zip_io.hpp"
#include "test_zip_fixtures.hpp"

using namespace jzip;

TEST(LibJzipReaderParseTest, ParseMinimalArchive)
{
    const auto zip_bytes = jzip::test::build_minimal_single_entry_zip();
    const auto archive   = ZipIO::readJvm(MemorySegment(zip_bytes));

    const auto centrals = archive.getCentralDirectories();
    const auto locals   = archive.getLocalFiles();
    ASSERT_EQ(centrals.size(), 1u);
    ASSERT_EQ(locals.size(), 0u);
    EXPECT_EQ(centrals[0]->getFileName(), "a.txt");
    EXPECT_EQ(centrals[0]->getCompressedSize(), 3u);
}
