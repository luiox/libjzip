/*
 * Minimal archive parsing tests using inline builders.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_writer.hpp"
#include "test_zip_fixtures.hpp"

using namespace jzip;

namespace {

std::unique_ptr<LocalFileHeader> make_stored_local(const std::string& name,
                                                   const std::string& content)
{
    auto local = std::make_unique<LocalFileHeader>();
    local->setFileName(name);
    local->setGeneralPurposeBitFlag(0);
    local->setCompressionMethod(0);
    local->setCrc32(0);
    local->setCompressedSize(static_cast<ca::i64>(content.size()));
    local->setUncompressedSize(static_cast<ca::i64>(content.size()));
    local->setFileData(std::vector<ca::u8>(content.begin(), content.end()));
    return local;
}

}   // namespace

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

// 数据起点恰好等于下一个结构（CEN）的偏移：JVM 语义按 ceiling 取边界，
// 零长度条目不能把 CEN 当成自己的数据。
TEST(LibJzipReaderParseTest, JvmZeroLengthLastEntryKeepsEmptyData)
{
    ZipArchive archive;
    archive.addPart(make_stored_local("empty.txt", ""));

    const auto bytes  = ZipIO::writeDirect(archive);
    const auto parsed = ZipIO::readJvm(MemorySegment(bytes));
    const auto locals = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 1u);
    EXPECT_EQ(locals[0]->getFileName(), "empty.txt");
    EXPECT_EQ(locals[0]->getCompressedSize(), 0);
    EXPECT_EQ(locals[0]->getUncompressedSize(), 0);
    EXPECT_TRUE(locals[0]->getFileData().empty());
}

// 数据起点恰好等于下一个 LOC 的偏移：后续条目也不能被算进前一个空条目。
TEST(LibJzipReaderParseTest, JvmZeroLengthEntryBeforeAnotherEntry)
{
    ZipArchive archive;
    archive.addPart(make_stored_local("empty.txt", ""));
    archive.addPart(make_stored_local("b.txt", "xy"));

    const auto bytes  = ZipIO::writeDirect(archive);
    const auto parsed = ZipIO::readJvm(MemorySegment(bytes));
    const auto locals = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_TRUE(locals[0]->getFileData().empty());
    EXPECT_EQ(locals[1]->getFileName(), "b.txt");
    ASSERT_EQ(locals[1]->getFileData().size(), 2u);
    EXPECT_EQ(locals[1]->getFileData()[0], static_cast<ca::u8>('x'));
}
