#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"
#include "jar_in_jar_utils.hpp"

using namespace jzip;
using namespace jzip::test_utils;

class JarInJarTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(JarInJarTests, Test)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);

    // The JVM strategy calculates file data ranges by mapping from the start
    // of the current expected data offset of a local file header, to the start of
    // the next local file header.
    ASSERT_NO_THROW({
        auto archive = ZipIO::readJvm(MemorySegment(bytes));
        handleJar(archive);
    }) << "Failed to read JVM: "
       << name;

    // The standard strategy calculates file data ranges by using the compressed size.
    // The value is assumed to be correct. We'll want to use the authoritative values from
    // the central directory file header though. The local sizes can be bogus.
    ASSERT_NO_THROW({
        auto archive = ZipIO::readStandard(MemorySegment(bytes));
        auto mapped  = archive.withMapping(CentralAdoptingMapper(IdentityZipPartMapper{}));
        handleJar(mapped);
    }) << "Failed to read with read(standard) + mapping(central-adoption): "
       << name;
}

INSTANTIATE_TEST_SUITE_P(JarInJarSamples, JarInJarTests,
                         ::testing::Values("hello-copyjar-at-head.jar", "hello-copyjar-at-tail.jar",
                                           "hello-jar-in-in-jar-in-jar-in-jar-in-jar.jar",
                                           "jar-in-jar-with-data-descriptor.jar"));
