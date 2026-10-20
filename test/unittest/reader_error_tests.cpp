/*
 * Reader error path tests.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 */

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <vector>

#include "jzip/memory_segment.hpp"
#include "jzip/zip_anomaly.hpp"
#include "jzip/zip_io.hpp"

using namespace jzip;

namespace {

void put_u16(std::vector<ca::u8>& bytes, ca::usize offset, ca::u16 value)
{
    for (int i = 0; i < 2; ++i) {
        bytes[offset + static_cast<ca::usize>(i)] =
            static_cast<ca::u8>((value >> (i * 8)) & 0xFFu);
    }
}

void put_u32(std::vector<ca::u8>& bytes, ca::usize offset, ca::u32 value)
{
    for (int i = 0; i < 4; ++i) {
        bytes[offset + static_cast<ca::usize>(i)] =
            static_cast<ca::u8>((value >> (i * 8)) & 0xFFu);
    }
}

/// LOC(31) + CEN(47) + EOCD(22)；EOCD 声明的 CEN 偏移比实际大 1，
/// 使 baseOffset = -1。CEN 的 relativeOffset 为 0，旧实现把它映射成
/// usize 的 SIZE_MAX，触发越界读。
std::vector<ca::u8> makeNegativeBaseOffsetArchive()
{
    std::vector<ca::u8> bytes(100, 0);
    // LOC @ 0
    put_u32(bytes, 0, 0x04034b50u);
    put_u16(bytes, 4, 20);
    put_u16(bytes, 26, 1);
    bytes[30] = 'a';
    // CEN @ 31
    put_u32(bytes, 31, 0x02014b50u);
    put_u16(bytes, 35, 20);
    put_u16(bytes, 37, 20);
    put_u16(bytes, 59, 1);        // file name length（CEN + 28）
    put_u32(bytes, 31 + 42, 0);   // relative offset of local header
    bytes[77] = 'a';
    // EOCD @ 78
    put_u32(bytes, 78, 0x06054b50u);
    put_u16(bytes, 86, 1);   // entries on this disk
    put_u16(bytes, 88, 1);   // total entries
    put_u32(bytes, 90, 47);  // central directory size
    put_u32(bytes, 94, 32);  // 声明的 central directory offset（实际 31）
    return bytes;
}

}   // namespace

TEST(LibJzipReaderErrorTest, MissingEocdReturnsError)
{
    const std::vector<ca::u8> bad_bytes = {0x01, 0x02, 0x03, 0x04, 0x05};
    EXPECT_THROW(ZipIO::readJvm(MemorySegment(bad_bytes)), ZipParseException);
}

TEST(LibJzipReaderErrorTest, NegativeBaseOffsetSkipsLocalInsteadOfReadingOutOfBounds)
{
    const auto bytes = makeNegativeBaseOffsetArchive();

    ZipArchive standard;
    ASSERT_NO_THROW(standard = ZipIO::readStandard(MemorySegment(bytes)));
    EXPECT_TRUE(standard.getLocalFiles().empty());
    EXPECT_EQ(standard.getReadReport().countOf(ZipAnomalyKind::EocdOffsetMismatch), 1u);
    ASSERT_EQ(standard.getReadReport().countOf(ZipAnomalyKind::LocalHeaderSignatureMiss), 1u);
    // 异常定位到 CEN 记录本身；旧实现会报告回绕后的 usize 伪偏移
    ca::u64 miss_offset = 0;
    for (const auto& anomaly : standard.getReadReport().anomalies()) {
        if (anomaly.kind == ZipAnomalyKind::LocalHeaderSignatureMiss) {
            miss_offset = anomaly.offset;
        }
    }
    EXPECT_EQ(miss_offset, 31u);

    // Jvm 读取器同样不接受负 baseOffset 的映射，但回退扫描应恢复条目
    ZipArchive jvm;
    ASSERT_NO_THROW(jvm = ZipIO::readJvm(MemorySegment(bytes)));
    EXPECT_EQ(jvm.getLocalFiles().size(), 1u);
    EXPECT_EQ(jvm.getReadReport().countOf(ZipAnomalyKind::BaseOffsetFallback), 1u);
}

TEST(LibJzipReaderErrorTest, CentralDirectorySizeBeyondEocdThrows)
{
    std::vector<ca::u8> bytes(22, 0);
    put_u32(bytes, 0, 0x06054b50u);
    put_u32(bytes, 12, 0x7FFFFFFFu);   // cdSize > EOCD 偏移

    EXPECT_THROW(ZipIO::readStandard(MemorySegment(bytes)), ZipParseException);
    EXPECT_THROW(ZipIO::readJvm(MemorySegment(bytes)), ZipParseException);
}

TEST(LibJzipMemorySegmentTest, ReadsNearSizeMaxThrowInsteadOfWrapping)
{
    const std::vector<ca::u8> bytes(8, 0xAB);
    const MemorySegment        segment(bytes);
    const auto                 near_max = std::numeric_limits<ca::usize>::max() - 1;

    EXPECT_THROW(segment.get(ca::u16{}, near_max), std::out_of_range);
    EXPECT_THROW(segment.get(ca::u32{}, near_max), std::out_of_range);
    EXPECT_THROW(segment.get(ca::u64{}, near_max), std::out_of_range);
    EXPECT_THROW(segment.asSlice(near_max, 2), std::out_of_range);
}
