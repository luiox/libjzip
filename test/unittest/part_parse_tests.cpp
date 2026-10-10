#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "test_utils.hpp"
#include "test_zip_fixtures.hpp"
#include "jzip/zip_reader.hpp"

using namespace jzip;
using namespace jzip::test;



class AdoptingForwardScanZipReader : public ForwardScanZipReader
{
public:
    using ForwardScanZipReader::ForwardScanZipReader;

    void postProcessLocalFileHeader(LocalFileHeader& header) override
    {
        header.adoptLinkedCentralDirectoryValues();
    }
};

class PartParseStandardCodeZipTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseStandardCodeZipTests, TestStandardCodeZip)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zip = ZipIO::readStandard(MemorySegment(bytes));
        EXPECT_TRUE(has_file(zip, "ClassFile.java"));
        EXPECT_TRUE(has_file(zip, "ClassMember.java"));
        EXPECT_TRUE(has_file(zip, "ConstPool.java"));
        EXPECT_TRUE(has_file(zip, "Descriptor.java"));
        EXPECT_TRUE(has_file(zip, "Field.java"));
        EXPECT_TRUE(has_file(zip, "Method.java"));
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}
INSTANTIATE_TEST_SUITE_P(PartParseTests, PartParseStandardCodeZipTests,
                         ::testing::Values("sample-code-7z.zip", "sample-code-windows.zip"));

class PartParseHelloTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseHelloTests, TestHello)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zipStd = ZipIO::readStandard(MemorySegment(bytes));
        auto zipJvm = ZipIO::readJvm(MemorySegment(bytes));
        EXPECT_TRUE(has_file(zipStd, "META-INF/MANIFEST.MF"));
        EXPECT_TRUE(has_file(zipStd, "Hello.class"));
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}
INSTANTIATE_TEST_SUITE_P(PartParseTests, PartParseHelloTests,
                         ::testing::Values("hello.jar", "hello-secret.jar",
                                           "hello-secret-0-length-locals.jar",
                                           "hello-secret-junkheader.jar"));

class PartParseConcatAndMergedTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseConcatAndMergedTests, TestConcatAndMerged)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zipStd = ZipIO::readStandard(MemorySegment(bytes));
        auto zipJvm = ZipIO::readJvm(MemorySegment(bytes));

        EXPECT_NE(zipJvm.getEnd(), zipStd.getEnd());

        EXPECT_TRUE(has_file(zipJvm, "META-INF/MANIFEST.MF"));
        EXPECT_TRUE(has_file(zipJvm, "Hello.class"));

        const auto* stdHello = get_local_file_by_name(zipStd, "Hello.class");
        const auto* jvmHello = get_local_file_by_name(zipJvm, "Hello.class");
        ASSERT_NE(nullptr, stdHello);
        ASSERT_NE(nullptr, jvmHello);
        EXPECT_NE(stdHello, jvmHello);

        auto stdData     = ZipIO::decompressLocalFile(*stdHello);
        auto jvmData     = ZipIO::decompressLocalFile(*jvmHello);
        auto stdHelloRaw = bytes_to_string(stdData);
        auto jvmHelloRaw = bytes_to_string(jvmData);
        EXPECT_FALSE(stdHelloRaw.empty());
        EXPECT_FALSE(jvmHelloRaw.empty());
        EXPECT_TRUE(stdHelloRaw.find("Hello world") != std::string::npos);
        EXPECT_TRUE(jvmHelloRaw.find("The secret code is: ROSE") != std::string::npos);
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}
INSTANTIATE_TEST_SUITE_P(PartParseTests, PartParseConcatAndMergedTests,
                         ::testing::Values("hello-concat.jar", "hello-concat-junkheader.jar",
                                           "hello-merged.jar", "hello-merged-junkheader.jar"));

class PartParseJvmCanRecoverDataTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseJvmCanRecoverDataTests, TestJvmCanRecoverData)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zip        = ZipIO::readJvm(MemorySegment(bytes));
        auto localFiles = get_filtered_local_files(
            zip, [](const std::string& n) { return n.find(".class") != std::string::npos; });
        ASSERT_EQ(localFiles.size(), 1u) << "More than 1 class";

        auto decompressed    = ZipIO::decompressLocalFile(*localFiles[0]);
        auto decompressedStr = bytes_to_string(decompressed);

        ASSERT_NO_THROW({
            if (!is_valid_class(decompressed)) {
                throw std::runtime_error("Failed to read class, must have failed to decompress");
            }
        }) << "Failed to read class, must have failed to decompress";

        EXPECT_TRUE(decompressedStr.find("Hello world") != std::string::npos ||
                    decompressedStr.find("The secret code is: ROSE") != std::string::npos);
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}
INSTANTIATE_TEST_SUITE_P(
    PartParseTests, PartParseJvmCanRecoverDataTests,
    ::testing::Values("hello-end-declares-0-entries.jar",
                      "hello-end-declares-0-entries-0-offset.jar", "hello-junk-dir-length.jar",
                      "hello-junk-eocd.jar", "hello-junk-local-length.jar", "hello-total-junk.jar",
                      "hello-total-junk-large.jar", "hello-wrong-local-compression.jar",
                      "hello-zeroed-locals.jar", "hello-concat.jar", "hello-concat-junkheader.jar",
                      "hello-merged.jar", "hello-merged-fake-empty.jar",
                      "hello-merged-junkheader.jar", "hello-secret-0-length-locals.jar",
                      "hello-secret-junkheader.jar", "hello-secret-trailing-slash.jar",
                      "hello-secret-trailing-slash-0-length-locals.jar", "hello-txt-stored.jar",
                      "hello-txt-type-0.jar"));

TEST(PartParseTests, TestEocdMisleading)
{
    const auto path  = test_resource_path("sample-eocd-oob.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zip = ZipIO::readJvm(MemorySegment(bytes));
        EXPECT_TRUE(has_file(zip, "META-INF/MANIFEST.MF"));
        EXPECT_TRUE(has_file(zip, "Hello.class/"));
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}

TEST(PartParseTests, TestDuplicateManifest)
{
    const auto path  = test_resource_path("sample-duplicate-manifest-oob.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zip = ZipIO::readJvm(MemorySegment(bytes));
        EXPECT_TRUE(has_file(zip, "META-INF/MANIFEST.MF"));
        EXPECT_TRUE(has_file(zip, "Hello.class/"));

        bool visitedCode = false;
        auto localFiles  = get_filtered_local_files(
            zip, [](const std::string& n) { return n.find(".class") != std::string::npos; });
        for (const auto* localFile : localFiles) {
            try {
                auto byteArray = ZipIO::decompressLocalFile(*localFile);
                if (is_valid_class(byteArray) &&
                    (class_defines_string(byteArray, "java/io/PrintStream") ||
                     class_defines_string(byteArray, "println"))) {
                    visitedCode = true;
                    break;
                }
            }
            catch (const std::exception&) {
                // There are junk classes in this sample. Ignore failed decoys.
            }
        }
        EXPECT_TRUE(visitedCode) << "One of the classes should have had code to visit, but none "
                                    "did. We fell for trap classes";

        const auto* manifestEntry = get_local_file_by_name(zip, "META-INF/MANIFEST.MF");
        ASSERT_NE(nullptr, manifestEntry);
        auto manifestBytes = ZipIO::decompressLocalFile(*manifestEntry);
        auto manifest      = bytes_to_string(manifestBytes);
        EXPECT_NE(manifest.find("Main-Class: Hello"), std::string::npos);
        EXPECT_EQ(manifest.find("com/sun/internal/Cleanup"), std::string::npos);
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}

TEST(PartParseTests, TestLocalHeaderDetectMismatch)
{
    const auto path  = test_resource_path("hello-secret-0-length-locals.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto        zipStd = ZipIO::readStandard(MemorySegment(bytes));
        const auto* hello  = get_local_file_by_name(zipStd, "Hello.class");
        ASSERT_NE(nullptr, hello);
        EXPECT_EQ(0, hello->getFileData().size());
        EXPECT_TRUE(hello->hasDifferentValuesThanCentralDirectoryHeader());

        {
            AdoptingForwardScanZipReader reader;
            ZipArchive                   zipStdAndAdopt;
            ASSERT_NO_THROW(reader.read(zipStdAndAdopt, MemorySegment(bytes)));
            const auto* helloAdopted = get_local_file_by_name(zipStdAndAdopt, "Hello.class");
            ASSERT_NE(nullptr, helloAdopted);
            EXPECT_FALSE(helloAdopted->hasDifferentValuesThanCentralDirectoryHeader());
            EXPECT_NE(0, helloAdopted->getFileData().size());
        }

        {
            auto        zipJvm       = ZipIO::readJvm(MemorySegment(bytes));
            const auto* helloAdopted = get_local_file_by_name(zipJvm, "Hello.class");
            ASSERT_NE(nullptr, helloAdopted);
            EXPECT_NE(0, helloAdopted->getFileData().size());
        }
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}

TEST(PartParseTests, TestEndInLocalDataDesc)
{
    const auto path  = test_resource_path("end-in-local-data-desc.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto archive    = ZipIO::readJvm(MemorySegment(bytes));
        auto classFiles = get_filtered_local_files(archive, [](const std::string& n) {
            return n.size() >= 6 && n.compare(n.size() - 6, 6, ".class") == 0;
        });
        EXPECT_EQ(classFiles.size(), 639u);
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}

TEST(PartParseTests, TestMergedFakeEmpty)
{
    const auto path  = test_resource_path("hello-merged-fake-empty.jar");
    const auto bytes = read_file_bytes(path);
    ASSERT_FALSE(bytes.empty());

    try {
        auto zipJvm = ZipIO::readJvm(MemorySegment(bytes));
        EXPECT_TRUE(has_file(zipJvm, "META-INF/MANIFEST.MF"));
        EXPECT_TRUE(has_file(zipJvm, "Hello.class/"));
    }
    catch (const std::exception& ex) {
        FAIL() << ex.what();
    }
}

TEST(PartParseTests, TestJvmReAdoptsDuplicateCenToLocalLinks)
{
    const auto bytes = build_n_to_one_cen_to_loc_zip();

    auto archive  = ZipIO::readJvm(MemorySegment(bytes));
    auto locals   = archive.getLocalFiles();
    auto centrals = archive.getCentralDirectories();

    ASSERT_EQ(locals.size(), 1u);
    ASSERT_EQ(centrals.size(), 3u);
    EXPECT_EQ(locals[0]->getFileName(), "third.txt");

    for (const auto* central : centrals) {
        EXPECT_EQ(central->getLinkedFileHeader(), locals[0]);
    }
}
