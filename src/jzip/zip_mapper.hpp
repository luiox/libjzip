#pragma once

#include "zip_model.hpp"

namespace jzip {

/// @brief ZIP 部件映射器。
class ZipPartMapper
{
public:
    /// @brief 析构函数。
    virtual ~ZipPartMapper() = default;

    /// @brief 映射部件。
    /// @param archive 归档模型
    /// @param part 当前部件
    /// @return 返回 nullptr 表示删除部件
    virtual ZipPart* map(ZipArchive& archive, ZipPart* part) const = 0;
};

/// @brief 不做修改的映射器。
class IdentityZipPartMapper final : public ZipPartMapper
{
public:
    /// @brief 映射部件。
    ZipPart* map(ZipArchive& archive, ZipPart* part) const override;
};

/// @brief 委派式映射器基类：先调用委派映射器，再执行自身逻辑。
class DelegatingZipPartMapper : public ZipPartMapper
{
public:
    /// @brief 构造委派映射器。
    /// @param delegate 被委派的映射器
    explicit DelegatingZipPartMapper(const ZipPartMapper& delegate)
        : delegate_(&delegate)
    {}

    /// @brief 无委派构造（等价于 Identity）。
    DelegatingZipPartMapper()
        : delegate_(nullptr)
    {}

    /// @brief 映射部件：先调用委派，再返回。
    ZipPart* map(ZipArchive& archive, ZipPart* part) const override;

protected:
    const ZipPartMapper* delegate_;
};

/// @brief 采用 Central Directory 元数据的映射器。
/// 先委派给内部映射器，再对 LocalFileHeader 做 CEN 值采纳。
class CentralAdoptingMapper final : public DelegatingZipPartMapper
{
public:
    using DelegatingZipPartMapper::DelegatingZipPartMapper;

    /// @brief 映射部件：先委派，再采纳 CEN 值。
    ZipPart* map(ZipArchive& archive, ZipPart* part) const override;
};

/// @brief 规范 JVM .class/ 目录形式的映射器。
class JvmClassDirectoryMapper final : public DelegatingZipPartMapper
{
public:
    using DelegatingZipPartMapper::DelegatingZipPartMapper;

    /// @brief 映射部件：先委派，再规范化文件名。
    ZipPart* map(ZipArchive& archive, ZipPart* part) const override;
};

/// @brief 复制式映射器：返回每个部件的独立深拷贝，原部件由 mapArchive 释放。
/// @note 深拷贝不保留 LocalFileHeader/CentralDirectoryFileHeader 的 link 关系；
///       JvmLocalFileHeader 会退化为基类副本（见 LocalFileHeader::copy()）。
///       本映射器应位于映射链末端：若其后的 mapper 再建立 LOC/CEN 互链
///       （如 CentralAdoptingMapper），链接会指向随后释放的原部件。
class CopyingZipPartMapper final : public ZipPartMapper
{
public:
    /// @brief 映射部件（返回新分配的深拷贝，调用方接管所有权）。
    ZipPart* map(ZipArchive& archive, ZipPart* part) const override;
};

}   // namespace jzip
