#pragma once

#include <memory>
#include <vector>

#include "libca/core/datatype.hpp"

namespace jzip {

/// @brief 只读内存段视图，可共享外部缓冲区。
class MemorySegment
{
public:
    /// @brief 构造空内存段。
    MemorySegment() = default;

    /// @brief 以外部数据指针构造只读视图。
    /// @param data 数据指针
    /// @param size 字节长度
    MemorySegment(const ca::u8* data, ca::usize size);

    /// @brief 以字节数组拷贝构造内存段。
    /// @param vec 源数据
    explicit MemorySegment(const std::vector<ca::u8>& vec);

    /// @brief 拷贝构造。
    MemorySegment(const MemorySegment& other) = default;

    /// @brief 拷贝赋值。
    MemorySegment& operator=(const MemorySegment& other) = default;

    /// @brief 获取字节长度。
    ca::usize byteSize() const;

    /// @brief 截取子段视图。
    /// @param offset 起始偏移
    /// @param length 长度
    MemorySegment asSlice(ca::usize offset, ca::usize length) const;

    /// @brief 读取 8 位小端数据。
    /// @param tag 类型占位参数
    /// @param index 起始偏移
    ca::u8 get(ca::u8 tag, ca::usize index) const;

    /// @brief 读取 16 位小端数据。
    /// @param tag 类型占位参数
    /// @param index 起始偏移
    ca::u16 get(ca::u16 tag, ca::usize index) const;

    /// @brief 读取 32 位小端数据。
    /// @param tag 类型占位参数
    /// @param index 起始偏移
    ca::u32 get(ca::u32 tag, ca::usize index) const;

    /// @brief 读取 64 位小端数据。
    /// @param tag 类型占位参数
    /// @param index 起始偏移
    ca::u64 get(ca::u64 tag, ca::usize index) const;

    /// @brief 拷贝为字节数组。
    std::vector<ca::u8> toByteArray() const;

private:
    std::shared_ptr<std::vector<ca::u8>> owned_;
    const ca::u8*                        data_ = nullptr;
    ca::usize                            size_ = 0;
};

namespace MemorySegmentUtil {

/// @brief 读取 16 位小端整数。
/// @param data 数据段
/// @param offset 偏移
ca::u16 readWord(const MemorySegment& data, ca::usize offset);

/// @brief 读取 32 位小端整数。
/// @param data 数据段
/// @param offset 偏移
ca::u32 readQuad(const MemorySegment& data, ca::usize offset);

/// @brief 从起始位置向后查找 32 位模式。
/// @param data 数据段
/// @param start 起始偏移
/// @param pattern 模式值
ca::usize indexOfQuad(const MemorySegment& data, ca::usize start, ca::u32 pattern);

/// @brief 读取 64 位小端整数。
/// @param data 数据段
/// @param offset 偏移
ca::u64 readLong(const MemorySegment& data, ca::usize offset);

/// @brief 读取 4 字节小端整数并零扩展到 64 位（ZIP64 字段用）。
/// @param data 数据段
/// @param offset 偏移
ca::u64 readMaskedLongQuad(const MemorySegment& data, ca::usize offset);

/// @brief 从起始位置向前查找 32 位模式。
/// @param data 数据段
/// @param start 起始偏移
/// @param pattern 模式值
ca::usize lastIndexOfQuad(const MemorySegment& data, ca::usize start, ca::u32 pattern);

}   // namespace MemorySegmentUtil

}   // namespace jzip
