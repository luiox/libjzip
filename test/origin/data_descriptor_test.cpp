#include <gtest/gtest.h>

#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"
#include "jar_in_jar_utils.hpp"

using namespace jzip;
using namespace jzip::test_utils;

TEST(DataDescriptorTests, Validity)
{
    // We're going to read a file that utilizes data-descriptors.
    // This is an optional element outlined in section 4.3.9 of the ZIP spec
    // which is present AFTER the file data.
    auto bytes = ::test::getResource("jar-in-jar-with-data-descriptor.jar");

    // The JVM strategy calculates file data ranges by mapping from the start
    // of the current expected data offset of a local file header, to the start of
    // the next local file header. If there is a data-descriptor, the length needs to
    // accommodate for that and cut off the 16 bytes used by the data-descriptor.
    ASSERT_NO_THROW({
        auto archive = ZipIO::readJvm(MemorySegment(bytes));
        handleJar(archive);
    }) << "Failed to read with readJvm";

    // The standard strategy calculates file data ranges by using the compressed size.
    // The value is assumed to be correct. We'll want to use the authoritative values from
    // the central directory file header though. The local sizes can be bogus.
    //
    // Because the sizes should themselves accommodate for the data-descriptor being present
    // we do not need any special handling like we do in the JVM local file parser logic.
    ASSERT_NO_THROW({
        auto archive = ZipIO::readStandard(MemorySegment(bytes));
        auto mapped  = archive.withMapping(CentralAdoptingMapper(IdentityZipPartMapper{}));
        handleJar(mapped);
    }) << "Failed to read with read(standard) + mapping(central-adoption)";
}
