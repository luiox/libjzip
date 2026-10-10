#pragma once

#include <mutex>
#include <vector>

#include "libca/core/datatype.hpp"
#include "memory_segment.hpp"

namespace jzip {

class LocalFileHeader;

/// @brief 解压缩器接口。
class Decompressor
{
public:
    /// @brief 析构函数。
    virtual ~Decompressor() = default;

    /// @brief 解压缩本地文件内容。
    /// @param header 本地文件头
    /// @param compressedData 压缩数据
    virtual std::vector<ca::u8> decompress(const LocalFileHeader& header,
                                           const MemorySegment&   compressedData) = 0;
};

/// @brief Deflate 解压缩器。
class DeflateDecompressor : public Decompressor
{
public:
    /// @brief 设置解压输出上限（opt-in，默认 0 = 不限制）。
    ///
    /// 批量扫描来源不明的 JAR 时启用：头部声称的 uncompressedSize 不可信
    /// （虚高声明直接大额预分配、负声明经 usize 环绕变成天文数字），启用后
    /// 预分配按 min(声明值, 上限) 钳制，实际产出需要超出上限时抛
    /// ZipParseException。
    void setMaxOutputSize(size_t bytes) noexcept { maxOutput_ = bytes; }

    /// @brief 解压缩 Deflate 数据。
    std::vector<ca::u8> decompress(const LocalFileHeader& header,
                                   const MemorySegment&   compressedData) override;

private:
    size_t maxOutput_ = 0;
};

/// @brief Deflate 压缩器。
class DeflateCompressor
{
public:
    std::vector<ca::u8> compress(const std::vector<ca::u8>& input);
};

std::vector<ca::u8> inflateRaw(const ca::u8* data, size_t size, size_t uncompressedSize);

/// @brief 不安全的 Deflate 复用条目。
struct DeflateEntry
{
    void*               zs{};
    std::vector<ca::u8> inBuf;
    std::vector<ca::u8> outBuf;
    int                 state  = 0;
    int                 offset = 0;

    /// @brief 重置条目状态。
    void reset();
};

/// @brief 复用条目的 Deflate 解压缩器。
///
/// 管理 z_stream 对象池，避免反复 inflateInit/inflateEnd。
/// 对应 Java 的 UnsafeDeflateDecompressor（64 个 Inflater 线程池）。
class UnsafeDeflateDecompressor : public Decompressor
{
public:
    /// @brief 构建解压缩器，预分配指定数量的条目。
    /// @param poolSize 池大小（默认 8）
    explicit UnsafeDeflateDecompressor(size_t poolSize = 64);

    /// @brief 析构函数，释放池中所有 z_stream。
    ~UnsafeDeflateDecompressor() override;

    /// @brief 设置解压输出上限（opt-in，语义同 DeflateDecompressor）。
    void setMaxOutputSize(size_t bytes) noexcept { maxOutput_ = bytes; }

    /// @brief 解压缩 Deflate 数据（从池中取一个条目）。
    std::vector<ca::u8> decompress(const LocalFileHeader& header,
                                   const MemorySegment&   compressedData) override;

private:
    std::mutex                 mutex_;
    std::vector<DeflateEntry*> pool_;
    size_t                     maxOutput_ = 0;
};

}   // namespace jzip
