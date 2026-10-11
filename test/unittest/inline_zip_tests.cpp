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

// 回归：EOCD 之前的字节窗口不足以容纳合法 ZIP64 结构（locator 20 + 最小记录 56）
// 时，伪造的 locator 必须被忽略。旧 tryReadZip64EndInfo 中
// `endOffset - 56` 无符号下溢会放行该伪造结构，进而在紧贴文件尾的
// zip64EndOffset 处 readLong 越界，抛出裸 std::out_of_range，
// 破坏"解析失败一律抛 ZipParseException"的契约。
TEST(InlineZipTest, forgedZip64LocatorNearTinyEocdThrowsParseError)
{
    std::vector<ca::u8> bytes(64, 0x00);
    auto                put32 = [&bytes](ca::usize offset, ca::u32 value) {
        for (int i = 0; i < 4; ++i) {
            bytes[offset + static_cast<ca::usize>(i)] =
                static_cast<ca::u8>((value >> (i * 8)) & 0xFFu);
        }
    };
    auto put64 = [&bytes](ca::usize offset, ca::u64 value) {
        for (int i = 0; i < 8; ++i) {
            bytes[offset + static_cast<ca::usize>(i)] =
                static_cast<ca::u8>((value >> (i * 8)) & 0xFFu);
        }
    };

    put32(22, 0x07064b50);   // 伪造 zip64 locator 签名（EOCD 位于 42，locator 落在 22）
    put64(30, 56);           // zip64EndOffset 指向 fileLen-8，紧贴文件尾
    put32(56, 0x06064b50);   // zip64 记录签名：骗过旧代码的记录头检查后即越界
    put32(42, 0x06054b50);   // 传统 EOCD（其余字段为零；cdSize 高位来自记录签名字节）

    EXPECT_THROW(ZipIO::readJvm(MemorySegment(bytes)), ZipParseException);
    // naive 只扫 LOC，不读 EOCD/locator，不受影响
    EXPECT_NO_THROW(ZipIO::readNaive(MemorySegment(bytes)));
}
