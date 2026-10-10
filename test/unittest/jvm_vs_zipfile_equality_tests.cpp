#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "test_utils.hpp"

using namespace jzip;
using namespace jzip::test;



// DISABLED: 对应 Java @Disabled("Updating to JDK changes how ZipFile works, ...")
// readAdaptingIO() 现已委托 src/zip::ZipFile；当前该样本会像 Java ZipFile 一样报错，
// 因此继续保持禁用，仅用于记录历史边界行为。
TEST(JvmVsZipFileEqualityEdgeCaseTests, DISABLED_Test)
{
    const auto name  = std::string("resource-pack-trick-header-N-to-1-cen-to-loc-mapping.zip");
    const auto path  = test_resource_path(name);
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zipJvm      = ZipIO::readJvm(MemorySegment(bytes));
        auto zipAdapting = ZipIO::readAdaptingIO(path.string());
        auto sizeDel     = zipAdapting.getLocalFiles().size();
        auto sizeJvm     = zipJvm.getLocalFiles().size();

        std::set<std::string> namesAdapting;
        for (const auto* lf : zipAdapting.getLocalFiles()) namesAdapting.insert(lf->getFileName());
        std::set<std::string> namesJvm;
        for (const auto* lf : zipJvm.getLocalFiles()) namesJvm.insert(lf->getFileName());

        std::set<std::string> namesDifference(namesAdapting);
        for (const auto& n : namesJvm) namesDifference.erase(n);

        EXPECT_EQ(0, namesDifference.size());
        EXPECT_EQ(sizeDel, sizeJvm);
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}
