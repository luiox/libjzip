/*
 * Advanced decompression tests.
 * Moved from unittest/ as a C++-only extension (no direct Java test equivalent).
 * - Stored method round-trip
 * - Deflate decompression from samples
 * - Unknown method error path
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/decompressor.hpp"
#include "jzip/zip_io.hpp"
#include "test_zip_fixtures.hpp"

using namespace jzip;

TEST(LibJzipDecompressionTest, StoredMethodReturnsOriginalBytes)
{
    const std::vector<ca::u8> raw = {'a', 'b', 'c'};
    LocalFileHeader           local;
    local.setFileName("plain.txt");
    local.setGeneralPurposeBitFlag(0);
    local.setCompressionMethod(0);
    local.setCrc32(0);
    local.setCompressedSize(static_cast<ca::u32>(raw.size()));
    local.setUncompressedSize(static_cast<ca::u32>(raw.size()));
    local.setFileData(raw);

    auto result = ZipIO::decompressLocalFile(local);
    EXPECT_EQ(result, raw);
}

TEST(LibJzipDecompressionTest, DeflateMethodCanDecompressClassBytes)
{
    static const std::vector<std::string> candidates = {
        "hello.jar",
        "sample-code-7z.zip",
        "sample-code-windows.zip",
    };

    ZipArchive parsed_archive{};
    bool       found_archive = false;
    for (const auto& candidate : candidates) {
        if (!::test::hasResource(candidate)) {
            continue;
        }

        const auto bytes = ::test::getResource(candidate);
        try {
            parsed_archive = ZipIO::readJvm(MemorySegment(bytes));
        }
        catch (const ZipParseException&) {
            continue;
        }
        found_archive = true;
        break;
    }

    // Fallback to inline deflated ZIP if no file-based archive found
    if (!found_archive) {
        const auto inline_bytes = jzip::test::build_deflated_entry_zip();
        parsed_archive          = ZipIO::readJvm(MemorySegment(inline_bytes));
        found_archive           = true;
    }

    const LocalFileHeader* deflate_file = nullptr;
    for (const auto* local : parsed_archive.getLocalFiles()) {
        if (local->getCompressionMethod() == 8) {
            deflate_file = local;
            break;
        }
    }

    ASSERT_NE(deflate_file, nullptr);

    auto decompressed = ZipIO::decompressLocalFile(*deflate_file);
    ASSERT_GE(decompressed.size(), 1u);

    // The inline deflated data is "hello", real JARs contain class files (0xCAFEBABE magic).
    // Check that decompression produces valid data - first byte is non-zero.
    EXPECT_NE(decompressed[0], 0);
}

TEST(LibJzipDecompressionTest, UnknownMethodReturnsError)
{
    LocalFileHeader local;
    local.setFileName("x.bin");
    local.setGeneralPurposeBitFlag(0);
    local.setCompressionMethod(99);
    local.setCrc32(0);
    local.setCompressedSize(1);
    local.setUncompressedSize(1);
    local.setFileData(std::vector<ca::u8>{0x00});

    EXPECT_THROW(ZipIO::decompressLocalFile(local), ZipParseException);
}

// ---- opt-in 解压输出上限（敌意 JAR 批量扫描场景）----

namespace {

std::vector<ca::u8> compressRawDeflate(const std::vector<ca::u8>& input)
{
    DeflateCompressor compressor;
    return compressor.compress(input);
}

LocalFileHeader makeDeflateHeader(const std::vector<ca::u8>& raw, const std::vector<ca::u8>& packed)
{
    LocalFileHeader local;
    local.setFileName("capped.bin");
    local.setGeneralPurposeBitFlag(0);
    local.setCompressionMethod(8);
    local.setCrc32(0);
    local.setCompressedSize(static_cast<ca::u32>(packed.size()));
    local.setUncompressedSize(static_cast<ca::u32>(raw.size()));
    local.setFileData(packed);
    return local;
}

}   // namespace

// 实际产出超出上限：抛 ZipParseException，不再继续翻倍缓冲。
TEST(LibJzipDecompressionTest, MaxOutputSizeRejectsOversizedPayload)
{
    std::vector<ca::u8> raw(1000, 'A');
    auto                packed = compressRawDeflate(raw);
    auto                local  = makeDeflateHeader(raw, packed);
    MemorySegment       segment(local.getFileData());

    DeflateDecompressor uncapped;
    EXPECT_EQ(uncapped.decompress(local, segment).size(), 1000u);

    DeflateDecompressor capped;
    capped.setMaxOutputSize(512);
    EXPECT_THROW(capped.decompress(local, segment), ZipParseException);
}

// 头部声明虚高（实际产出很小）：上限只钳预分配，产出照常成功——
// 旧实现会按声明值直接预分配，敌意声明即 DoS。
TEST(LibJzipDecompressionTest, MaxOutputSizeClampsLyingDeclaredSizeWithoutFailingSmallOutput)
{
    std::vector<ca::u8> raw(64, 'B');
    auto                packed = compressRawDeflate(raw);
    auto                local  = makeDeflateHeader(raw, packed);
    // 声明 1GB，实际只有 64 字节
    local.setUncompressedSize(static_cast<ca::i64>(1) << 30);
    MemorySegment segment(local.getFileData());

    DeflateDecompressor capped;
    capped.setMaxOutputSize(4096);
    auto out = capped.decompress(local, segment);
    EXPECT_EQ(out.size(), 64u);
    EXPECT_EQ(out.front(), 'B');
}

// 负声明值：旧实现 usize 环绕成天文数字预分配；钳制后按压缩数据大小起步。
TEST(LibJzipDecompressionTest, NegativeDeclaredSizeDoesNotExplodeAllocation)
{
    std::vector<ca::u8> raw(32, 'C');
    auto                packed = compressRawDeflate(raw);
    auto                local  = makeDeflateHeader(raw, packed);
    local.setUncompressedSize(-1);
    MemorySegment segment(local.getFileData());

    DeflateDecompressor capped;
    capped.setMaxOutputSize(4096);
    auto out = capped.decompress(local, segment);
    EXPECT_EQ(out.size(), 32u);

    DeflateDecompressor uncapped;
    auto                out2 = uncapped.decompress(local, segment);
    EXPECT_EQ(out2.size(), 32u);
}

// 池化解压器同样遵守上限。
TEST(LibJzipDecompressionTest, UnsafeDecompressorHonorsMaxOutputSize)
{
    std::vector<ca::u8> raw(1000, 'D');
    auto                packed = compressRawDeflate(raw);
    auto                local  = makeDeflateHeader(raw, packed);
    MemorySegment       segment(local.getFileData());

    UnsafeDeflateDecompressor capped(2);
    capped.setMaxOutputSize(256);
    EXPECT_THROW(capped.decompress(local, segment), ZipParseException);
}


// ZipIO::decompressLocalFile 默认带炸弹防护：压缩比超 1000x 的大膨胀条目
// （64 MiB 零串）应在默认配额处抛 ZipParseException，而非无界产出。
TEST(LibJzipDecompressionTest, DecompressLocalFileAppliesDefaultBombCap)
{
    std::vector<ca::u8> raw(64ULL * 1024 * 1024, 0x00);
    auto                packed = compressRawDeflate(raw);
    auto                local  = makeDeflateHeader(raw, packed);

    // 未显式设上限的便捷路径同样受保护
    EXPECT_THROW(ZipIO::decompressLocalFile(local), ZipParseException);

    // 同一负载在显式高配额下正常解出
    DeflateDecompressor generous;
    generous.setMaxOutputSize(raw.size() + 1);
    MemorySegment segment(local.getFileData());
    auto          out = generous.decompress(local, segment);
    EXPECT_EQ(out.size(), raw.size());
}
