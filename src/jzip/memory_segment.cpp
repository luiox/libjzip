#include "memory_segment.hpp"

#include <stdexcept>

using namespace ca;

namespace jzip {

// 构造非拥有的裸指针切片
MemorySegment::MemorySegment(const u8* data, usize size)
    : data_(data)
    , size_(size)
{}

// 构造拥有所有权的共享副本
MemorySegment::MemorySegment(const std::vector<u8>& vec)
    : owned_(std::make_shared<std::vector<u8>>(vec))
    , data_(owned_->data())
    , size_(owned_->size())
{}

usize MemorySegment::byteSize() const
{
    return size_;
}

// 返回子切片，共享同一所有权
MemorySegment MemorySegment::asSlice(usize offset, usize length) const
{
    if (offset > size_ || length > size_ || offset + length > size_) {
        throw std::out_of_range("MemorySegment slice out of range");
    }

    MemorySegment sliced;
    sliced.data_  = data_ + offset;
    sliced.size_  = length;
    sliced.owned_ = owned_;
    return sliced;
}

u8 MemorySegment::get(u8, usize index) const
{
    if (index >= size_) {
        throw std::out_of_range("MemorySegment index out of range");
    }
    return data_[index];
}

u16 MemorySegment::get(u16, usize index) const
{
    if (index + 1 >= size_) {
        throw std::out_of_range("MemorySegment index out of range");
    }
    return static_cast<u16>(data_[index]) | static_cast<u16>(data_[index + 1] << 8);
}

u32 MemorySegment::get(u32, usize index) const
{
    if (index + 3 >= size_) {
        throw std::out_of_range("MemorySegment index out of range");
    }
    return static_cast<u32>(data_[index]) | static_cast<u32>(data_[index + 1] << 8) |
           static_cast<u32>(data_[index + 2] << 16) | static_cast<u32>(data_[index + 3] << 24);
}

u64 MemorySegment::get(u64, usize index) const
{
    if (index + 7 >= size_) {
        throw std::out_of_range("MemorySegment index out of range");
    }
    const u32 low  = get(u32{}, index);
    const u32 high = get(u32{}, index + 4);
    return static_cast<u64>(low) | (static_cast<u64>(high) << 32);
}

std::vector<u8> MemorySegment::toByteArray() const
{
    if (size_ == 0) {
        return {};
    }
    return std::vector<u8>(data_, data_ + size_);
}

namespace MemorySegmentUtil {

// 从 MemorySegment 中按偏移读 u16
u16 readWord(const MemorySegment& data, usize offset)
{
    return data.get(u16{}, offset);
}

// 从 MemorySegment 中按偏移读 u32
u32 readQuad(const MemorySegment& data, usize offset)
{
    return data.get(u32{}, offset);
}

// 前向扫描指定的 4 字节模式
usize indexOfQuad(const MemorySegment& data, usize start, u32 pattern)
{
    if (data.byteSize() < 4 || start >= data.byteSize()) {
        return data.byteSize();
    }

    const usize last = data.byteSize() - 4;
    for (usize i = start; i <= last; ++i) {
        if (readQuad(data, i) == pattern) {
            return i;
        }
    }
    return data.byteSize();
}

// 从 MemorySegment 中按偏移读 u64
u64 readLong(const MemorySegment& data, usize offset)
{
    return data.get(u64{}, offset);
}

// 读取 4 字节小端整数并零扩展到 64 位（ZIP64 字段用）
u64 readMaskedLongQuad(const MemorySegment& data, usize offset)
{
    return static_cast<u64>(data.get(u32{}, offset));
}

// 向后扫描指定的 4 字节模式（从 start 往前）
usize lastIndexOfQuad(const MemorySegment& data, usize start, u32 pattern)
{
    if (data.byteSize() < 4) {
        return data.byteSize();
    }

    usize       cursor = start;
    const usize last   = data.byteSize() - 4;
    if (cursor > last) {
        cursor = last;
    }

    while (true) {
        if (readQuad(data, cursor) == pattern) {
            return cursor;
        }
        if (cursor == 0) {
            break;
        }
        --cursor;
    }

    return data.byteSize();
}

}   // namespace MemorySegmentUtil

}   // namespace jzip
