#include "zip_io.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

#include "decompressor.hpp"
#include "zip_adapting_reader.hpp"
#include "zip_reader.hpp"
#include "zip_writer.hpp"

using namespace ca;

namespace jzip {

namespace {

// 生产便捷路径的默认防线（Recaf 5b63ec87f 同类问题）：敌意 JAR 的 zip 炸弹
// 条目此前可经本入口无界产出。配额 = min(每条目 1 GiB, compressedSize*1000
// 饱和积)，下限 1 MiB（极小条目的合法膨胀不受 1000x 之外额约束）。需要更高
// 配额的调用方自行构造 Decompressor 并 setMaxOutputSize。
constexpr usize kDefaultEntryOutputCap = 1024ULL * 1024 * 1024;
constexpr usize kMaxExpansionFactor    = 1000;
constexpr usize kMinExpansionQuota     = 1024ULL * 1024;

usize effectiveOutputCap(usize compressedSize)
{
    if (compressedSize > std::numeric_limits<usize>::max() / kMaxExpansionFactor) {
        return kDefaultEntryOutputCap;
    }
    const usize scaled = compressedSize * kMaxExpansionFactor;
    const usize quota =
        std::min(std::max(scaled, kMinExpansionQuota), kDefaultEntryOutputCap);
    return quota;
}

}   // namespace

// 委托 ForwardScanZipReader（全文件前向扫描）
ZipArchive ZipIO::readStandard(const MemorySegment& data, ZipAnomalySink* sink)
{
    ZipArchive           archive;
    ForwardScanZipReader reader;
    reader.setAnomalySink(sink);
    reader.read(archive, data);
    return archive;
}

// 仅扫描 LFH，忽略中央目录和 EOCD
ZipArchive ZipIO::readNaive(const MemorySegment& data, ZipAnomalySink* sink)
{
    ZipArchive              archive;
    NaiveLocalFileZipReader reader;
    reader.setAnomalySink(sink);
    reader.read(archive, data);
    return archive;
}

// 模拟 Java ZipInputStream 的向后扫描 EOCD 策略
ZipArchive ZipIO::readJvm(const MemorySegment& data, ZipAnomalySink* sink)
{
    ZipArchive   archive;
    JvmZipReader reader;
    reader.setAnomalySink(sink);
    reader.read(archive, data);
    return archive;
}

// 对标 Java AdaptingZipReader：委托 stdlib 风格 ZipFile，从路径读取归档
ZipArchive ZipIO::readAdaptingIO(const std::string& path)
{
    ZipArchive        archive;
    AdaptingZipReader reader;
    reader.readFromPath(archive, path);
    return archive;
}

// 直接序列化存档到字节流（逐项写入）
std::vector<u8> ZipIO::writeDirect(const ZipArchive& archive)
{
    std::ostringstream oss(std::ios::binary | std::ios::out);
    DirectZipWriter    writer;
    writer.write(archive, oss);
    const auto& str = oss.str();
    return std::vector<u8>(str.begin(), str.end());
}

// 解压单个条目：仅支持 stored(0) 和 deflate(8)
std::vector<u8> ZipIO::decompressLocalFile(const LocalFileHeader& local)
{
    constexpr u16 kStoredMethod  = 0;
    constexpr u16 kDeflateMethod = 8;

    if (local.getCompressionMethod() == kStoredMethod) {
        return local.getFileData();
    }

    if (local.getCompressionMethod() != kDeflateMethod) {
        throw ZipParseException("unsupported compression method", local.offset());
    }

    MemorySegment segment(local.getFileData());

    DeflateDecompressor decompressor;
    decompressor.setMaxOutputSize(effectiveOutputCap(segment.byteSize()));
    return decompressor.decompress(local, segment);
}

// 对存档中每个元素应用映射器（如 CentralAdoptingMapper）
// 注意：分两阶段执行 —— 先计算映射结果，再移动元素。
// 这是因为 mapper.map() 内部可能自己迭代 archive.getParts()，
// 如果在第一阶段就 move 元素会导致内部迭代遇到 null unique_ptr 而崩溃。
ZipArchive ZipIO::mapArchive(ZipArchive archive, const ZipPartMapper& mapper)
{
    std::vector<std::unique_ptr<ZipPart>> next_parts;
    next_parts.reserve(archive.getParts().size());

    struct MappingResult
    {
        ZipPart*                  mapped = nullptr;
        std::unique_ptr<ZipPart>* src    = nullptr;
    };

    std::vector<MappingResult> results;
    results.reserve(archive.getParts().size());

    for (auto& part : archive.getParts()) {
        results.push_back({mapper.map(archive, part.get()), &part});
    }

    for (auto& r : results) {
        if (r.mapped == nullptr) continue;
        if (r.mapped == r.src->get()) {
            next_parts.push_back(std::move(*r.src));
        }
        else {
            next_parts.push_back(std::unique_ptr<ZipPart>(r.mapped));
        }
    }

    archive.getParts() = std::move(next_parts);
    return archive;
}

}   // namespace jzip
