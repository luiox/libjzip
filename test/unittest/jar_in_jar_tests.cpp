#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "test_utils.hpp"

using namespace jzip;
using namespace jzip::test;

using namespace jzip;
using namespace jzip::test;

class JarInJarTests : public ::testing::TestWithParam<std::string>
{};

TEST_P(JarInJarTests, Test)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    ASSERT_NO_THROW({
        auto archive = ZipIO::readJvm(MemorySegment(bytes));
        handle_jar(archive);
    }) << "Failed to read with readJvm";

    ASSERT_NO_THROW({
        auto archive = ZipIO::readStandard(MemorySegment(bytes));
        auto mapped  = ZipIO::mapArchive(std::move(archive), CentralAdoptingMapper{});
        handle_jar(mapped);
    }) << "Failed to read with readStandard + mapping(CentralAdoptingMapper)";
}

INSTANTIATE_TEST_SUITE_P(JarInJarSamples, JarInJarTests,
                         ::testing::Values("hello-copyjar-at-head.jar", "hello-copyjar-at-tail.jar",
                                           "hello-jar-in-in-jar-in-jar-in-jar-in-jar.jar",
                                           "jar-in-jar-with-data-descriptor.jar"));
