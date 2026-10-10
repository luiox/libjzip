/// @file anomaly_report_tests.cpp
/// @brief ZIP 结构异常侦测（zip_anomaly）与破坏注入器（zip_corruptor）回归测试。
///
/// 用 ZipCorruptor 制造已知损坏形态，断言 reader 的 ZipReadReport /
/// ZipAnomalySink 按预期记录事件，且容错语义（条目仍可解析）不变。

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "jzip/zip_anomaly.hpp"
#include "jzip/zip_corruptor.hpp"
#include "jzip/zip_io.hpp"
#include "jzip/zip_writer.hpp"

using namespace jzip;

namespace {

/// 构造干净的三条目归档字节（全部 store，writer 自动重建 CEN/EOCD）。
std::vector<ca::u8> makeCleanArchive()
{
    ZipArchive archive;
    auto       makeLocal = [](const std::string& name, const std::string& content) {
        auto local = std::make_unique<LocalFileHeader>();
        local->setFileName(name);
        local->setGeneralPurposeBitFlag(0);
        local->setCompressionMethod(0);
        local->setCrc32(0);
        local->setCompressedSize(static_cast<ca::i64>(content.size()));
        local->setUncompressedSize(static_cast<ca::i64>(content.size()));
        local->setFileData(std::vector<ca::u8>(content.begin(), content.end()));
        return local;
    };
    archive.addPart(makeLocal("a.txt", "hello"));
    archive.addPart(makeLocal("dir/b.txt", "world"));
    archive.addPart(makeLocal("META-INF/MANIFEST.MF", "Manifest-Version: 1.0"));
    return ZipIO::writeDirect(archive);
}

/// 统计报告中某类事件的条数。
ca::usize countKind(const ZipReadReport& report, ZipAnomalyKind kind)
{
    return report.countOf(kind);
}

}   // namespace

TEST(ZipAnomalyReportTest, CleanArchiveYieldsEmptyReport)
{
    auto        clean   = makeCleanArchive();
    const auto  archive = ZipIO::readJvm(MemorySegment(clean));
    EXPECT_TRUE(archive.getReadReport().empty());
    EXPECT_EQ(archive.getLocalFiles().size(), 3u);
}

TEST(ZipAnomalyReportTest, PrefixJunkWithStaleCdOffsetDetected)
{
    // OS3.0.0H 形态：CAFEBABE 前缀垃圾 + 陈旧 cdOffset
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.prependJunk(1024).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::PrefixJunkDetected), 1u);
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::EocdOffsetMismatch), 1u);
    // 容错语义不变：条目全部恢复，前缀被识别
    EXPECT_EQ(archive.getLocalFiles().size(), 3u);
    EXPECT_EQ(archive.getPrefixData().byteSize(), 1024u);
}

TEST(ZipAnomalyReportTest, LieAboutCdOffsetDetected)
{
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.lieAboutCdOffset(0x7FFFFFFFu).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::EocdOffsetMismatch), 1u);
    EXPECT_EQ(archive.getLocalFiles().size(), 3u);
}

TEST(ZipAnomalyReportTest, BrokenLocalOffsetReported)
{
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.breakEntryLocalOffset(0).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::LocalHeaderSignatureMiss), 1u);
    // 其余两条不受影响
    EXPECT_EQ(archive.getLocalFiles().size(), 2u);
}

TEST(ZipAnomalyReportTest, AliasedLocalOffsetReported)
{
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.aliasEntryLocalOffset(1, 0).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::DuplicateLocalOffset), 1u);
}

TEST(ZipAnomalyReportTest, ShrunkLocalSizesAdopted)
{
    // 注意：store 条目的 LOC 尺寸会被 reader 按数据段长度重算（readFileData），
    // 谎报存活不到收养对比；deflate 条目的 uncompressedSize 保留头部原值。
    auto        clean   = makeCleanArchive();
    const auto  reparsed = ZipIO::readJvm(MemorySegment(clean));
    std::ostringstream oss(std::ios::binary | std::ios::out);
    RecompressingZipWriter(true, 8).write(reparsed, oss);
    const std::string str = oss.str();
    ZipCorruptor      corruptor(std::vector<ca::u8>(str.begin(), str.end()));
    const auto        damaged = corruptor.shrinkLocalSizes(0).build();

    const auto  archive = ZipIO::readJvm(MemorySegment(damaged));
    const auto& locals  = archive.getLocalFiles();
    ASSERT_EQ(locals.size(), 3u);
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::SizesAdoptedFromCen), 1u);
    // 收养后尺寸恢复为 CEN 权威值
    EXPECT_EQ(locals[0]->getUncompressedSize(), 5);
}

TEST(ZipAnomalyReportTest, TrailingGarbageDetected)
{
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.appendTrailingGarbage(64).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::TrailingGarbageAfterEocd), 1u);
    EXPECT_EQ(archive.getLocalFiles().size(), 3u);
}

TEST(ZipAnomalyReportTest, FakeEocdRejectedAndGarbageDetected)
{
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.appendGarbageWithFakeEocd(64).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_GE(countKind(archive.getReadReport(), ZipAnomalyKind::RejectedEocdCandidate), 1u);
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::TrailingGarbageAfterEocd), 1u);
    // 回退到真实 EOCD 后条目完整
    EXPECT_EQ(archive.getLocalFiles().size(), 3u);
}

TEST(ZipAnomalyReportTest, SuspiciousEntryNameFlagged)
{
    std::string evil = "\x00";
    evil += "<html><img src=\"https:0.class/";
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.renameEntry(0, evil).build();

    const auto archive = ZipIO::readJvm(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::SuspiciousEntryName), 1u);
}

TEST(ZipAnomalyReportTest, SinkReceivesSameEventsAsReport)
{
    class CaptureSink final : public ZipAnomalySink
    {
    public:
        void onAnomaly(const ZipAnomaly& anomaly) override { seen.push_back(anomaly.kind); }
        std::vector<ZipAnomalyKind> seen;
    };

    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.prependJunk(256).appendTrailingGarbage(16).build();

    CaptureSink sink;
    const auto  archive = ZipIO::readJvm(MemorySegment(damaged), &sink);
    ASSERT_FALSE(sink.seen.empty());
    EXPECT_EQ(sink.seen.size(), archive.getReadReport().anomalies().size());
}

TEST(ZipAnomalyReportTest, NaiveReaderReportsPrefixJunk)
{
    ZipCorruptor corruptor(makeCleanArchive());
    const auto   damaged = corruptor.prependJunk(512).build();

    const auto archive = ZipIO::readNaive(MemorySegment(damaged));
    EXPECT_EQ(countKind(archive.getReadReport(), ZipAnomalyKind::PrefixJunkDetected), 1u);
}

TEST(ZipAnomalyNameHeuristicTest, FlagsKnownPatterns)
{
    std::string nulHtml = "\x00";
    nulHtml += "<html><img src=x";
    EXPECT_TRUE(isSuspiciousEntryName(nulHtml));
    EXPECT_TRUE(isSuspiciousEntryName("../../etc/passwd"));
    EXPECT_TRUE(isSuspiciousEntryName("a<script>b"));
    std::string ctrl = "ctrl";
    ctrl += char(0x01);
    ctrl += "char";
    EXPECT_TRUE(isSuspiciousEntryName(ctrl));
    EXPECT_FALSE(isSuspiciousEntryName("com/example/Main.class"));
    EXPECT_FALSE(isSuspiciousEntryName("META-INF/MANIFEST.MF"));
}
