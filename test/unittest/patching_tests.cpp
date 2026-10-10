#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "test_utils.hpp"
#include "jzip/zip_reader.hpp"
#include "jzip/zip_writer.hpp"

using namespace jzip;
using namespace jzip::test;



class LengthIgnoringAllocator : public ZipPartAllocator
{
    SimpleZipPartAllocator default_;

public:
    std::unique_ptr<LocalFileHeader> newLocalFileHeader() override
    {
        return std::make_unique<JvmLocalFileHeader>();
    }

    std::unique_ptr<CentralDirectoryFileHeader> newCentralDirectoryFileHeader() override
    {
        return default_.newCentralDirectoryFileHeader();
    }

    std::unique_ptr<EndOfCentralDirectory> newEndOfCentralDirectory() override
    {
        return default_.newEndOfCentralDirectory();
    }
};

class PatchingTrickJarTests : public ::testing::TestWithParam<std::string>
{};

TEST_P(PatchingTrickJarTests, TestTrickJarPatched)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        {
            auto        zipStd   = ZipIO::readStandard(MemorySegment(bytes));
            const auto* stdHello = get_local_file_by_name(zipStd, "Hello.class");
            ASSERT_NE(nullptr, stdHello);
            auto stdData = ZipIO::decompressLocalFile(*stdHello);
            auto stdStr  = bytes_to_string(stdData);
            EXPECT_TRUE(stdStr.find("Hello world") != std::string::npos);
        }

        auto                   zip = ZipIO::readJvm(MemorySegment(bytes));
        RecompressingZipWriter writer(false, 0);
        std::ostringstream     oss(std::ios::binary);
        writer.write(zip, oss);
        const auto&         str = oss.str();
        std::vector<ca::u8> fixed(str.begin(), str.end());

        {
            auto        patched      = ZipIO::readStandard(MemorySegment(fixed));
            const auto* patchedHello = get_local_file_by_name(patched, "Hello.class");
            ASSERT_NE(nullptr, patchedHello);
            auto patchedData = ZipIO::decompressLocalFile(*patchedHello);
            auto patchedStr  = bytes_to_string(patchedData);
            EXPECT_TRUE(patchedStr.find("The secret code is: ROSE") != std::string::npos);
        }
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}

INSTANTIATE_TEST_SUITE_P(PatchingTests, PatchingTrickJarTests,
                         ::testing::Values("hello-concat.jar", "hello-concat-junkheader.jar",
                                           "hello-merged.jar", "hello-merged-junkheader.jar"));

TEST(PatchingTests, TestReadIgnoringFileLengths)
{
    const auto path  = test_resource_path("resource-pack-trick-data-ioobe.zip");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    EXPECT_ANY_THROW(ZipIO::readNaive(MemorySegment(bytes)));
    EXPECT_ANY_THROW(ZipIO::readStandard(MemorySegment(bytes)));

    {
        NaiveLocalFileZipReader naiveStrategy(std::make_unique<LengthIgnoringAllocator>());
        ZipArchive              zip;
        ASSERT_NO_THROW(naiveStrategy.read(zip, MemorySegment(bytes)));
        const auto* naive_en = get_local_file_by_name(zip, "assets/luxbl/lang/en_us.json/");
        ASSERT_NE(nullptr, naive_en) << "Missing 'en_us' file";
    }

    {
        ForwardScanZipReader forwardStrategy(std::make_unique<LengthIgnoringAllocator>());
        ZipArchive           zip;
        ASSERT_NO_THROW(forwardStrategy.read(zip, MemorySegment(bytes)));
        const auto* forward_en = get_local_file_by_name(zip, "assets/luxbl/lang/en_us.json");
        ASSERT_NE(nullptr, forward_en) << "Missing 'en_us' file";
    }
}

TEST(PatchingTests, TestTrailingSlashTransform)
{
    const auto path  = test_resource_path("hello-secret-trailing-slash.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    ZipArchive zip;
    ASSERT_NO_THROW({
        zip = ZipIO::readStandard(MemorySegment(bytes));
        zip = ZipIO::mapArchive(std::move(zip), JvmClassDirectoryMapper{});
    });
    const auto* hello = get_local_file_by_name(zip, "Hello.class");
    EXPECT_NE(nullptr, hello) << "Trailing slash was not patched";
}
