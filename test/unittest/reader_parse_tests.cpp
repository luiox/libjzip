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

constexpr ca::u32 kLfhSignature  = 0x04034b50u;
constexpr ca::u32 kCenSignature  = 0x02014b50u;
constexpr ca::u32 kEocdSignature = 0x06054b50u;
constexpr ca::u32 kDdSignature   = 0x08074b50u;

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

void append_lfh(std::vector<ca::u8>& out, ca::u16 flags, ca::u16 method, ca::u32 csize,
                ca::u32 usize, const std::string& name)
{
    jzip::test::append_u32_le(out, kLfhSignature);
    jzip::test::append_u16_le(out, 20);
    jzip::test::append_u16_le(out, flags);
    jzip::test::append_u16_le(out, method);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u32_le(out, 0);
    jzip::test::append_u32_le(out, csize);
    jzip::test::append_u32_le(out, usize);
    jzip::test::append_u16_le(out, static_cast<ca::u16>(name.size()));
    jzip::test::append_u16_le(out, 0);
    out.insert(out.end(), name.begin(), name.end());
}

void append_cen(std::vector<ca::u8>& out, ca::u16 flags, ca::u16 method, ca::u32 csize,
                ca::u32 usize, const std::string& name, ca::u32 local_offset)
{
    jzip::test::append_u32_le(out, kCenSignature);
    jzip::test::append_u16_le(out, 20);
    jzip::test::append_u16_le(out, 20);
    jzip::test::append_u16_le(out, flags);
    jzip::test::append_u16_le(out, method);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u32_le(out, 0);
    jzip::test::append_u32_le(out, csize);
    jzip::test::append_u32_le(out, usize);
    jzip::test::append_u16_le(out, static_cast<ca::u16>(name.size()));
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u32_le(out, 0);
    jzip::test::append_u32_le(out, local_offset);
    out.insert(out.end(), name.begin(), name.end());
}

void append_eocd(std::vector<ca::u8>& out, ca::u16 num_entries, ca::u32 cd_size, ca::u32 cd_offset)
{
    jzip::test::append_u32_le(out, kEocdSignature);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, 0);
    jzip::test::append_u16_le(out, num_entries);
    jzip::test::append_u16_le(out, num_entries);
    jzip::test::append_u32_le(out, cd_size);
    jzip::test::append_u32_le(out, cd_offset);
    jzip::test::append_u16_le(out, 0);
}

// 打包两个条目：第一个为零大小（method/flags 指定），其数据区与第二个 LFH 之间
// 塞 gap 字节；CEN 按权威声明第一个条目 0 大小。前向扫描会把 gap 算进第一个
// 条目的数据。
std::vector<ca::u8> build_zero_size_gap_archive(ca::u16                method,
                                                ca::u16                flags,
                                                const std::vector<ca::u8>& gap)
{
    const std::string first_name  = "gap.bin";
    const std::string second_name = "b.txt";

    std::vector<ca::u8> out;
    append_lfh(out, flags, method, 0, 0, first_name);
    out.insert(out.end(), gap.begin(), gap.end());

    const ca::u32 second_offset = static_cast<ca::u32>(out.size());
    append_lfh(out, 0, 0, 2, 2, second_name);
    out.push_back('x');
    out.push_back('y');

    const ca::u32 cd_offset = static_cast<ca::u32>(out.size());
    append_cen(out, flags, method, 0, 0, first_name, 0);
    append_cen(out, 0, 0, 2, 2, second_name, second_offset);
    const ca::u32 cd_size = static_cast<ca::u32>(out.size()) - cd_offset;
    append_eocd(out, 2, cd_size, cd_offset);
    return out;
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

// 敌意形态：零大小 STORED 条目与下一个 LOC 之间塞垃圾字节。前向扫描会把垃圾
// 算进条目数据，adopt 必须按 CEN 权威清空，不能留下与 CEN 大小矛盾的内容。
TEST(LibJzipReaderParseTest, JvmZeroSizeStoredEntryWithInterHeaderGarbageStaysEmpty)
{
    const std::vector<ca::u8> garbage = {'P', 'K', 0x03, 0x04, 1, 2, 3, 4};
    const auto                bytes   = build_zero_size_gap_archive(0, 0, garbage);
    const auto                parsed  = ZipIO::readJvm(MemorySegment(bytes));
    const auto                locals  = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_EQ(locals[0]->getFileName(), "gap.bin");
    EXPECT_TRUE(locals[0]->getFileData().empty());
    EXPECT_EQ(locals[0]->getCompressedSize(), 0);
    EXPECT_EQ(locals[0]->getUncompressedSize(), 0);
    EXPECT_EQ(locals[1]->getFileName(), "b.txt");
    ASSERT_EQ(locals[1]->getFileData().size(), 2u);
}

// 敌意形态：零大小条目带 bit3 数据描述符。LOC 无显式大小可用，前向扫描无法
// 裁剪尾随 DD，会把整个 DD 算进数据；CEN 权威 0 大小时 adopt 必须清空。
TEST(LibJzipReaderParseTest, JvmZeroSizeDataDescriptorEntryStaysEmpty)
{
    const std::string first_name  = "gap.bin";
    const std::string second_name = "b.txt";

    std::vector<ca::u8> out;
    append_lfh(out, 0x0008u, 0, 0, 0, first_name);
    // 16 字节数据描述符（带签名）：零大小条目流式写法的尾随结构
    jzip::test::append_u32_le(out, kDdSignature);
    jzip::test::append_u32_le(out, 0);
    jzip::test::append_u32_le(out, 0);
    jzip::test::append_u32_le(out, 0);

    const ca::u32 second_offset = static_cast<ca::u32>(out.size());
    append_lfh(out, 0, 0, 2, 2, second_name);
    out.push_back('x');
    out.push_back('y');

    const ca::u32 cd_offset = static_cast<ca::u32>(out.size());
    append_cen(out, 0x0008u, 0, 0, 0, first_name, 0);
    append_cen(out, 0, 0, 2, 2, second_name, second_offset);
    const ca::u32 cd_size = static_cast<ca::u32>(out.size()) - cd_offset;
    append_eocd(out, 2, cd_size, cd_offset);

    const auto parsed = ZipIO::readJvm(MemorySegment(out));
    const auto locals = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_TRUE(locals[0]->getFileData().empty());
    EXPECT_EQ(locals[0]->getCompressedSize(), 0);
    EXPECT_EQ(locals[0]->getUncompressedSize(), 0);
    ASSERT_EQ(locals[1]->getFileData().size(), 2u);
}

// 敌意形态：csize=0 的 DEFLATED 条目没有任何流字节，数据区只有垃圾。adopt 按
// CEN 权威清空后，解压应得到空内容而不是把垃圾喂给 zlib。
TEST(LibJzipReaderParseTest, JvmZeroSizeDeflatedEntryDecompressesEmpty)
{
    const std::vector<ca::u8> garbage = {'P', 'K', 0x01, 0x02, 9, 9};
    const auto                bytes   = build_zero_size_gap_archive(8, 0, garbage);
    const auto                parsed  = ZipIO::readJvm(MemorySegment(bytes));
    const auto                locals  = parsed.getLocalFiles();
    ASSERT_EQ(locals.size(), 2u);
    EXPECT_EQ(locals[0]->getCompressionMethod(), 8);
    EXPECT_TRUE(locals[0]->getFileData().empty());
    EXPECT_EQ(locals[0]->getCompressedSize(), 0);
    EXPECT_TRUE(ZipIO::decompressLocalFile(*locals[0]).empty());
}
