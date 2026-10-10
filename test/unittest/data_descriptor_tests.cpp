#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "test_utils.hpp"

using namespace jzip;
using namespace jzip::test;

using namespace jzip;
using namespace jzip::test;

TEST(DataDescriptorTests, Validity)
{
    const auto path  = test_resource_path("jar-in-jar-with-data-descriptor.jar");
    const auto bytes = read_file_bytes(path);
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
