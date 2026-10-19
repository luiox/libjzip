-- libjzip：对齐 JVM 错误容忍行为的 ZIP/JAR 读写库（独立构建根）。
--
-- 依赖：
--   - libca（luiox-repo git 直连包）：core/zip 为运行依赖，test 为测试门面
--     依赖；版本锁固定 0.0.16，升级须显式换版。
--   - zlib（xrepo）。
--   - gtest（xrepo，仅 --with_tests=y）。
--
-- 仓库注册：本文件不声明 add_repositories——包沙箱会继承宿主的仓库注册，
-- 在工程内重复注册同名仓库会导致嵌套包构建时 remote 路径错乱。独立开发/CI
-- 先执行：
--   xmake repo --add luiox-repo https://github.com/luiox/luiox-repo.git

set_languages("c99", "cxx17")
add_rules("mode.debug", "mode.release")

add_requires("libca 0.0.16", {configs = {modules = "core,zip,test"}})
add_requires("zlib")

option("with_tests")
    set_default(false)
    set_showmenu(true)
    set_description("Enable unit tests (pulls gtest)")
option_end()

if has_config("with_tests") then
    add_requires("gtest")
end

target("libjzip")
    set_kind("static")
    add_packages("libca")
    add_packages("zlib")
    add_files("src/jzip/*.cpp")
    add_headerfiles("src/(jzip/*.hpp)")
    add_includedirs("src", {public = true})
    if is_plat("windows") then
        add_cxflags("/utf-8", {tools = "cl"})
    end

-- 单元测试（--with_tests=y 时定义；gmock 随 gtest 包提供）。
if has_config("with_tests") then

target("libjzip_unittest")
    set_kind("binary")
    set_default(false)
    add_deps("libjzip")
    add_packages("libca")
    add_packages("zlib")
    add_packages("gtest")
    add_files("test/unittest/main.cpp")
    add_files("test/unittest/*_test.cpp")
    add_files("test/unittest/*_tests.cpp")
    add_files("test/unittest/test_zip_fixtures.cpp")
    add_includedirs("src")
    add_includedirs("test_helper")
    set_rundir("$(projectdir)")
    if is_plat("linux") then
        add_syslinks("pthread")
    end
    if is_plat("windows") then
        add_cxflags("/utf-8", {tools = "cl"})
    end

-- 原始 Java 版本（LL-Java-Zip）对齐测试。
target("libjzip_origintest")
    set_kind("binary")
    set_default(false)
    add_deps("libjzip")
    add_packages("libca")
    add_packages("zlib")
    add_packages("gtest")
    add_files("test/origin/main.cpp")
    add_files("test/origin/*_test.cpp")
    add_includedirs("src")
    add_includedirs("test_helper")
    set_rundir("$(projectdir)")
    if is_plat("linux") then
        add_syslinks("pthread")
    end
    if is_plat("windows") then
        add_cxflags("/utf-8", {tools = "cl"})
    end

end

-- 对拍工具：C++ dump 两式 + match 比较（开发态，不依赖 gtest）。

target("libjzip_crossval_dump")
    set_kind("binary")
    set_default(false)
    add_deps("libjzip")
    add_packages("libca")
    add_packages("zlib")
    add_files("test/crossval/cpp_dump/zip_dump.cpp")
    add_includedirs("src")
    if is_plat("windows") then
        add_cxflags("/utf-8", {tools = "cl"})
    end

target("libjzip_crossval_zip_dump")
    set_kind("binary")
    set_default(false)
    add_deps("libjzip")
    add_packages("libca")
    add_packages("zlib")
    add_files("test/crossval/cpp_zip_dump/zip_dump.cpp")
    add_includedirs("src")
    if is_plat("windows") then
        add_cxflags("/utf-8", {tools = "cl"})
    end

target("libjzip_crossval_match")
    set_kind("binary")
    set_default(false)
    add_files("test/crossval/match_test/match_test.cpp")
    if is_plat("windows") then
        add_cxflags("/utf-8", {tools = "cl"})
    end
