#include "decompressor.hpp"

#include <algorithm>
#include <limits>

#include <zlib.h>

#include "zip_model.hpp"

using namespace ca;

namespace jzip {

namespace {

// 共享解压循环：预分配与增长均受 opt-in 上限钳制（maxOutput == 0 表示不限制）。
// 敌意 JAR 批量扫描场景下，头部声称的 uncompressedSize 不可信——负值会经
// usize 环绕变成天文数字预分配，虚高声明会直接 OOM；钳制后预分配最多到
// 上限，实际产出一旦需要超出上限即抛 ZipParseException。
// next_in/avail_in 由调用方设置；z_stream 生命周期亦由调用方管理。
std::vector<u8> decompress_with_stream(z_stream& stream, const LocalFileHeader& header,
                                       usize compressedSize, usize maxOutput)
{
    const i64 declared = header.getUncompressedSize();
    usize initial_capacity = declared > 0 ? static_cast<usize>(declared) : 0;
    if (initial_capacity == 0) {
        initial_capacity = compressedSize == 0 ? 64u : (compressedSize * 2u);
    }
    if (maxOutput > 0 && initial_capacity > maxOutput) {
        initial_capacity = maxOutput;
    }
    if (initial_capacity == 0) {
        initial_capacity = 64u;
    }

    std::vector<u8> out(initial_capacity);
    int             status = Z_OK;

    // 循环解压直到 Z_STREAM_END，缓冲区不足时自动翻倍（受上限钳制）
    while (true) {
        if (stream.total_out >= out.size()) {
            if (maxOutput > 0 && stream.total_out >= maxOutput) {
                throw ZipParseException("decompressed payload exceeds output cap",
                                        header.offset());
            }
            usize grow = out.size() * 2u;
            if (out.size() > (std::numeric_limits<usize>::max() / 2u) || grow < out.size()) {
                throw ZipParseException("decompressed payload would overflow output buffer",
                                        header.offset());
            }
            if (maxOutput > 0 && grow > maxOutput) {
                grow = maxOutput;
            }
            out.resize(grow);
        }

        stream.next_out  = reinterpret_cast<Bytef*>(out.data() + stream.total_out);
        stream.avail_out = static_cast<uInt>(out.size() - stream.total_out);

        status = inflate(&stream, Z_NO_FLUSH);
        if (status == Z_STREAM_END) {
            break;
        }
        if (status != Z_OK) {
            throw ZipParseException("zlib inflate failed", header.offset());
        }
    }

    out.resize(stream.total_out);
    return out;
}

}   // namespace

std::vector<u8> DeflateDecompressor::decompress(const LocalFileHeader& header,
                                                const MemorySegment&   compressedData)
{
    if (compressedData.byteSize() > static_cast<usize>(std::numeric_limits<uInt>::max())) {
        throw ZipParseException("compressed payload is too large for zlib api", header.offset());
    }

    auto input = compressedData.toByteArray();

    z_stream stream{};
    // -MAX_WBITS 禁用 zlib 头部，使用原始 deflate 流
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw ZipParseException("zlib inflateInit2 failed", header.offset());
    }
    try {
        stream.next_in  = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
        stream.avail_in = static_cast<uInt>(input.size());
        auto out        = decompress_with_stream(stream, header, input.size(), maxOutput_);
        if (inflateEnd(&stream) != Z_OK) {
            throw ZipParseException("zlib inflateEnd failed", header.offset());
        }
        return out;
    }
    catch (...) {
        inflateEnd(&stream);
        throw;
    }
}

void DeflateEntry::reset()
{
    state  = 0;
    offset = 0;
    inBuf.clear();
    outBuf.clear();
}

UnsafeDeflateDecompressor::UnsafeDeflateDecompressor(size_t poolSize)
{
    pool_.reserve(poolSize);
    for (size_t i = 0; i < poolSize; ++i) {
        // DeflateEntry::zs 是 void*，inflateInit2 需要真实 z_stream 对象
        // （此前传 nullptr 初始化必然失败，池化路径自落地即不可用）。
        auto* entry = new DeflateEntry();
        entry->zs   = new z_stream{};
        if (inflateInit2(static_cast<z_stream*>(entry->zs), -MAX_WBITS) != Z_OK) {
            delete static_cast<z_stream*>(entry->zs);
            delete entry;
            for (auto* e : pool_) {
                inflateEnd(static_cast<z_stream*>(e->zs));
                delete static_cast<z_stream*>(e->zs);
                delete e;
            }
            throw ZipParseException("zlib inflateInit2 failed for pool entry", 0);
        }
        pool_.push_back(entry);
    }
}

UnsafeDeflateDecompressor::~UnsafeDeflateDecompressor()
{
    for (auto* entry : pool_) {
        if (entry) {
            inflateEnd(static_cast<z_stream*>(entry->zs));
            delete static_cast<z_stream*>(entry->zs);
            delete entry;
        }
    }
}

std::vector<u8> UnsafeDeflateDecompressor::decompress(const LocalFileHeader& header,
                                                      const MemorySegment&   compressedData)
{
    if (compressedData.byteSize() > static_cast<usize>(std::numeric_limits<uInt>::max())) {
        throw ZipParseException("compressed payload is too large for zlib api", header.offset());
    }

    auto input = compressedData.toByteArray();

    DeflateEntry* entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pool_.empty()) {
            entry = pool_.back();
            pool_.pop_back();
        }
    }

    if (entry) {
        // Reuse pooled entry — reset stream state without inflateEnd/inflateInit
        inflateReset(static_cast<z_stream*>(entry->zs));
        try {
            auto& stream = *static_cast<z_stream*>(entry->zs);
            stream.next_in  = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
            stream.avail_in = static_cast<uInt>(input.size());
            auto result     = decompress_with_stream(stream, header, input.size(), maxOutput_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pool_.push_back(entry);
            }
            return result;
        }
        catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pool_.push_back(entry);
            }
            throw;
        }
    }

    // Pool exhausted — create a fresh stream
    DeflateDecompressor fallback;
    fallback.setMaxOutputSize(maxOutput_);
    return fallback.decompress(header, compressedData);
}

std::vector<u8> inflateRaw(const ca::u8* data, size_t size, size_t uncompressedSize)
{
    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        throw ZipParseException("zlib inflateInit2 failed", 0);
    }
    try {
        stream.next_in  = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(data));
        stream.avail_in = static_cast<uInt>(size);

        LocalFileHeader header;
        header.setUncompressedSize(static_cast<ca::i64>(uncompressedSize));
        auto out = decompress_with_stream(stream, header, size, 0);
        inflateEnd(&stream);
        return out;
    }
    catch (...) {
        inflateEnd(&stream);
        throw;
    }
}

std::vector<u8> DeflateCompressor::compress(const std::vector<u8>& input)
{
    if (input.empty()) return {};

    z_stream stream{};
    if (deflateInit2(
            &stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        throw ZipParseException("zlib deflateInit2 failed", 0);
    }

    const auto      bound = ::deflateBound(&stream, static_cast<uLong>(input.size()));
    std::vector<u8> out(bound);

    stream.next_in   = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(input.data()));
    stream.avail_in  = static_cast<uInt>(input.size());
    stream.next_out  = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    int status = ::deflate(&stream, Z_FINISH);
    if (status != Z_STREAM_END) {
        ::deflateEnd(&stream);
        throw ZipParseException("zlib deflate failed", 0);
    }

    ::deflateEnd(&stream);
    out.resize(stream.total_out);
    return out;
}

}   // namespace jzip
