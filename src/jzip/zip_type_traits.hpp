#pragma once

#include "libca/core/tag_cast.hpp"
#include "jzip/zip_model.hpp"

/// TypeOf 特化：ZipPart 子类 → type() PartType 枚举值
/// 这些类通过 type() 枚举区分类型，无需 RTTI dynamic_cast。

namespace ca::core::tag_cast {

template<>
struct TypeOf<jzip::LocalFileHeader>
{
    static constexpr auto value = jzip::PartType::LOCAL_FILE_HEADER;
};
template<>
struct TypeOf<jzip::CentralDirectoryFileHeader>
{
    static constexpr auto value = jzip::PartType::CENTRAL_DIRECTORY_FILE_HEADER;
};
template<>
struct TypeOf<jzip::EndOfCentralDirectory>
{
    static constexpr auto value = jzip::PartType::END_OF_CENTRAL_DIRECTORY;
};

}   // namespace ca::core::tag_cast
