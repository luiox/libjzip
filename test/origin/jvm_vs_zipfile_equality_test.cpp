/*
 * 1:1 mapping to LL-Java-Zip test: JvmVsZipFileEqualityEdgeCaseTests.java
 * - test() @Disabled + @ValueSource(1 sample)
 *
 * C++ notes:
 * - Uses JvmZipReader(false, false) to match Java's non-default params.
 * - readAdaptingIO(path) reads from file path (works with getResourcePath).
 * - Entry count and name set difference comparison.
 *
 * DISABLED: see Java @Disabled comment - JDK changes affect ZipFile behavior.
 */

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"
#include "jzip/zip_reader.hpp"

using namespace jzip;

TEST(JvmVsZipFileEqualityEdgeCaseTests, DISABLED_Test)
{
    const std::string name  = "resource-pack-trick-header-N-to-1-cen-to-loc-mapping.zip";
    auto              bytes = ::test::getResource(name);
    auto              path  = ::test::getResourcePath(name).string();

    ASSERT_NO_THROW({
        // JvmZipReader(false, false) - matching Java's non-default params
        JvmZipReader jvmReader(false, false);
        ZipArchive   zipJvm;
        jvmReader.read(zipJvm, MemorySegment(bytes));

        ZipArchive zipAdapting = ZipIO::readAdaptingIO(path);

        auto sizeDel = zipAdapting.getLocalFiles().size();
        auto sizeJvm = zipJvm.getLocalFiles().size();

        // Name set from adaptingIO
        std::set<std::string> namesAdapting;
        for (const auto* l : zipAdapting.getLocalFiles()) namesAdapting.insert(l->getFileName());

        // Name set from JVM reader
        std::set<std::string> namesJvmSet;
        for (const auto* l : zipJvm.getLocalFiles()) namesJvmSet.insert(l->getFileName());

        // namesDifference = namesAdapting \ namesJvmSet
        std::set<std::string> namesDifference;
        for (const auto& n : namesAdapting) {
            if (namesJvmSet.find(n) == namesJvmSet.end()) namesDifference.insert(n);
        }

        // Both files should have the same number of local file headers
        EXPECT_EQ(namesDifference.size(), 0u);
        EXPECT_EQ(sizeDel, sizeJvm);
    });
}
