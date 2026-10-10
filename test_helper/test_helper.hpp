#pragma once

// test_helper：libjzip 独立仓测试门面。
//
// 薄封装 libca.test（header-only）的多项目测试布局机制：资源定位 / 输出根。
// API 形态保持 namespace test:: 与 camelCase 命名，
// 只保留本库测试用到的面；测试 target 须 set_rundir("$(projectdir)")，
// main 里先 test::setup("libjzip")。

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "libca/test/test.hpp"

namespace test {

inline void setup(const std::string& subproject)
{
    ca::test::setup(subproject);
}

inline std::filesystem::path getTopProjectPath()
{
    return ca::test::top_project_path();
}

inline std::filesystem::path getCurrentProjectPath()
{
    return ca::test::current_project_path();
}

// ---- 当前项目资源（<项目根>/test_resource/<name>）：委托 ca::test ----

inline std::filesystem::path getResourcePath(const std::string& filename)
{
    return ca::test::resource_path(filename);
}

inline bool hasResource(const std::string& filename)
{
    return ca::test::has_resource(filename);
}

inline std::vector<uint8_t> getResource(const std::string& filename)
{
    return ca::test::resource(filename);
}

}   // namespace test
