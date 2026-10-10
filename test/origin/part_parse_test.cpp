#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"
#include "jzip/zip_reader.hpp"
#include "libca/zip/file.hpp"
#include "jar_in_jar_utils.hpp"

using namespace jzip;
using namespace jzip::test_utils;

// ── 辅助：按文件名查找 LocalFileHeader ─────────────────────
static const LocalFileHeader* findLocalByName(const ZipArchive& archive, const std::string& name)
{
    for (const auto* local : archive.getLocalFiles()) {
        if (local->getFileName() == name) return local;
    }
    return nullptr;
}

static bool hasFile(const ZipArchive& archive, const std::string& name)
{
    return findLocalByName(archive, name) != nullptr;
}

// ── testStandardCodeZip ─────────────────────────────────────

class PartParseStandardCodeZipTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseStandardCodeZipTests, TestStandardCodeZip)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);

    auto archive = ZipIO::readStandard(MemorySegment(bytes));

    EXPECT_TRUE(hasFile(archive, "ClassFile.java")) << name;
    EXPECT_TRUE(hasFile(archive, "ClassMember.java")) << name;
    EXPECT_TRUE(hasFile(archive, "ConstPool.java")) << name;
    EXPECT_TRUE(hasFile(archive, "Descriptor.java")) << name;
    EXPECT_TRUE(hasFile(archive, "Field.java")) << name;
    EXPECT_TRUE(hasFile(archive, "Method.java")) << name;
}
INSTANTIATE_TEST_SUITE_P(PartParseTests, PartParseStandardCodeZipTests,
                         ::testing::Values("sample-code-7z.zip", "sample-code-windows.zip"));

// ── testHello ───────────────────────────────────────────────

class PartParseHelloTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseHelloTests, TestHello)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);

    auto archiveStd = ZipIO::readStandard(MemorySegment(bytes));
    auto archiveJvm = ZipIO::readJvm(MemorySegment(bytes));

    EXPECT_TRUE(hasFile(archiveStd, "META-INF/MANIFEST.MF")) << name;
    EXPECT_TRUE(hasFile(archiveStd, "Hello.class")) << name;
}
INSTANTIATE_TEST_SUITE_P(PartParseTests, PartParseHelloTests,
                         ::testing::Values("hello.jar", "hello-secret.jar",
                                           "hello-secret-0-length-locals.jar",
                                           "hello-secret-junkheader.jar"));

// ── testConcatAndMerged ─────────────────────────────────────

class PartParseConcatAndMergedTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseConcatAndMergedTests, TestConcatAndMerged)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);

    auto zipStd = ZipIO::readStandard(MemorySegment(bytes));
    auto zipJvm = ZipIO::readJvm(MemorySegment(bytes));

    EXPECT_NE(zipJvm.getEnd(), zipStd.getEnd());

    EXPECT_TRUE(hasFile(zipJvm, "META-INF/MANIFEST.MF")) << name;
    EXPECT_TRUE(hasFile(zipJvm, "Hello.class")) << name;

    const auto* stdHello = findLocalByName(zipStd, "Hello.class");
    const auto* jvmHello = findLocalByName(zipJvm, "Hello.class");
    ASSERT_NE(stdHello, nullptr) << name;
    ASSERT_NE(jvmHello, nullptr) << name;
    EXPECT_NE(stdHello, jvmHello) << name;

    auto        stdData = ZipIO::decompressLocalFile(*stdHello);
    auto        jvmData = ZipIO::decompressLocalFile(*jvmHello);
    std::string stdContent(reinterpret_cast<const char*>(stdData.data()), stdData.size());
    std::string jvmContent(reinterpret_cast<const char*>(jvmData.data()), jvmData.size());
    EXPECT_FALSE(stdContent.empty()) << name;
    EXPECT_FALSE(jvmContent.empty()) << name;
    EXPECT_TRUE(stdContent.find("Hello world") != std::string::npos) << name;
    EXPECT_TRUE(jvmContent.find("The secret code is: ROSE") != std::string::npos) << name;
}
INSTANTIATE_TEST_SUITE_P(PartParseTests, PartParseConcatAndMergedTests,
                         ::testing::Values("hello-concat.jar", "hello-concat-junkheader.jar",
                                           "hello-merged.jar", "hello-merged-junkheader.jar"));

// ── testJvmCanRecoverData ───────────────────────────────────

class PartParseJvmCanRecoverDataTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PartParseJvmCanRecoverDataTests, TestJvmCanRecoverData)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);

    auto zip        = ZipIO::readJvm(MemorySegment(bytes));
    auto classFiles = zip.getLocalFiles();
    // 过滤 .class 文件
    std::vector<const LocalFileHeader*> filtered;
    for (const auto* local : classFiles) {
        if (local->getFileName().find(".class") != std::string::npos) {
            filtered.push_back(local);
        }
    }
    ASSERT_EQ(filtered.size(), 1u) << "More than 1 class: " << name;

    auto decompressed = ZipIO::decompressLocalFile(*filtered[0]);
    ASSERT_GE(decompressed.size(), 4u) << name;
    EXPECT_TRUE(decompressed[0] == 0xCA && decompressed[1] == 0xFE && decompressed[2] == 0xBA &&
                decompressed[3] == 0xBE)
        << "Failed to read class, must have failed to decompress: " << name;

    std::string decompressedStr(reinterpret_cast<const char*>(decompressed.data()),
                                decompressed.size());
    EXPECT_TRUE(decompressedStr.find("Hello world") != std::string::npos ||
                decompressedStr.find("The secret code is: ROSE") != std::string::npos)
        << name;
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

// ── testEocdMisleading ──────────────────────────────────────

TEST(PartParseTests, TestEocdMisleading)
{
    auto bytes = ::test::getResource("sample-eocd-oob.jar");

    auto archive = ZipIO::readJvm(MemorySegment(bytes));
    EXPECT_TRUE(hasFile(archive, "META-INF/MANIFEST.MF"));
    EXPECT_TRUE(hasFile(archive, "Hello.class/"));
}

TEST(PartParseTests, TestDuplicateManifest)
{
    auto bytes = ::test::getResource("sample-duplicate-manifest-oob.jar");

    auto archive = ZipIO::readJvm(MemorySegment(bytes));
    EXPECT_TRUE(hasFile(archive, "META-INF/MANIFEST.MF"));
    EXPECT_TRUE(hasFile(archive, "Hello.class/"));

    bool visitedCode = false;
    for (const auto* local : archive.getLocalFiles()) {
        if (local->getFileName().find(".class") == std::string::npos) {
            continue;
        }
        try {
            auto entryData = ZipIO::decompressLocalFile(*local);
            if (entryData.size() >= 4 && entryData[0] == 0xCA && entryData[1] == 0xFE &&
                entryData[2] == 0xBA && entryData[3] == 0xBE) {
                std::string classContent(reinterpret_cast<const char*>(entryData.data()),
                                         entryData.size());
                if (classContent.find("java/io/PrintStream") != std::string::npos ||
                    classContent.find("println") != std::string::npos) {
                    visitedCode = true;
                    break;
                }
            }
        }
        catch (const std::exception&) {
            // Ignore trap classes in this crafted sample.
        }
    }
    EXPECT_TRUE(visitedCode);

    const auto* manifest = findLocalByName(archive, "META-INF/MANIFEST.MF");
    ASSERT_NE(manifest, nullptr);
    auto        manifestBytes = ZipIO::decompressLocalFile(*manifest);
    std::string manifestText(reinterpret_cast<const char*>(manifestBytes.data()),
                             manifestBytes.size());
    EXPECT_NE(manifestText.find("Main-Class: Hello"), std::string::npos);
    EXPECT_EQ(manifestText.find("com/sun/internal/Cleanup"), std::string::npos);
}

// ── testZip64ByEntryCount ───────────────────────────────────
// Requires there-is-a-zip64-in-here.zip and C++ Zip64Support.

TEST(PartParseTests, TestZip64ByEntryCount)
{
    // The sample is a zipped jar with a Zip64 jar inside of it.
    // First extract the inner jar using ca::zip::ZipFile, then read it.
    auto         outerBytes = ::test::getResource("there-is-a-zip64-in-here.zip");
    ca::zip::ZipFile outer(outerBytes);
    ASSERT_EQ(outer.size(), 1u);
    // libca 0.0.9 [不兼容]：read 异常改 ca::Result（R16 评审 F2）。
    auto readResult = outer.read("lljzip-zip64-4605877303365506557.jar");
    ASSERT_TRUE(readResult.is_ok());
    auto innerJarBytes = std::move(readResult).unwrap();
    ASSERT_FALSE(innerJarBytes.empty());

    // Both standard/jvm readers should handle all entries in the zip64 jar.
    constexpr int kEntryCount = 70000;
    auto          zipStd      = ZipIO::readStandard(MemorySegment(innerJarBytes));
    EXPECT_EQ(zipStd.getCentralDirectories().size(), static_cast<size_t>(kEntryCount));
    EXPECT_EQ(zipStd.getLocalFiles().size(), static_cast<size_t>(kEntryCount));

    auto zipJvm = ZipIO::readJvm(MemorySegment(innerJarBytes));
    EXPECT_EQ(zipJvm.getCentralDirectories().size(), static_cast<size_t>(kEntryCount));
    EXPECT_EQ(zipJvm.getLocalFiles().size(), static_cast<size_t>(kEntryCount));

    // Verify last entry is reachable
    auto lastLocal = findLocalByName(zipStd, "E" + std::to_string(kEntryCount - 1));
    EXPECT_NE(lastLocal, nullptr);
    auto lastLocalJvm = findLocalByName(zipJvm, "E" + std::to_string(kEntryCount - 1));
    EXPECT_NE(lastLocalJvm, nullptr);
}

// ── testLocalHeaderDetectMismatch ───────────────────────────

TEST(PartParseTests, TestLocalHeaderDetectMismatch)
{
    auto bytes = ::test::getResource("hello-secret-0-length-locals.jar");

    auto        zipStd = ZipIO::readStandard(MemorySegment(bytes));
    const auto* hello  = findLocalByName(zipStd, "Hello.class");
    ASSERT_NE(hello, nullptr);
    EXPECT_EQ(hello->getFileData().size(), 0u);
    EXPECT_TRUE(hello->hasDifferentValuesThanCentralDirectoryHeader());

    // The solution to differing values is to adopt values in the reader strategy
    class AdoptingForwardScanReader : public ForwardScanZipReader
    {
    public:
        explicit AdoptingForwardScanReader(std::unique_ptr<ZipPartAllocator> allocator)
            : ForwardScanZipReader(std::move(allocator))
        {}

        void postProcessLocalFileHeader(LocalFileHeader& header) override
        {
            header.adoptLinkedCentralDirectoryValues();
        }
    };

    ZipArchive zipStdAndAdopt;
    {
        AdoptingForwardScanReader adoptingReader(std::make_unique<SimpleZipPartAllocator>());
        adoptingReader.read(zipStdAndAdopt, MemorySegment(bytes));
    }
    const auto* helloAdopted = findLocalByName(zipStdAndAdopt, "Hello.class");
    ASSERT_NE(helloAdopted, nullptr);
    EXPECT_FALSE(helloAdopted->hasDifferentValuesThanCentralDirectoryHeader());
    EXPECT_NE(helloAdopted->getFileData().size(), 0u);

    // The JVM strategy copies most properties, except for size.
    auto        zipJvm   = ZipIO::readJvm(MemorySegment(bytes));
    const auto* jvmHello = findLocalByName(zipJvm, "Hello.class");
    ASSERT_NE(jvmHello, nullptr);
    EXPECT_FALSE(jvmHello->hasDifferentValuesThanCentralDirectoryHeader());
    EXPECT_NE(jvmHello->getFileData().size(), 0u);
}

// ── testEndInLocalDataDesc ──────────────────────────────────

TEST(PartParseTests, TestEndInLocalDataDesc)
{
    auto bytes = ::test::getResource("end-in-local-data-desc.jar");

    auto                                archive    = ZipIO::readJvm(MemorySegment(bytes));
    auto                                classFiles = archive.getLocalFiles();
    std::vector<const LocalFileHeader*> filtered;
    for (const auto* local : classFiles) {
        if (local->getFileName().size() >= 6 &&
            local->getFileName().compare(local->getFileName().size() - 6, 6, ".class") == 0) {
            filtered.push_back(local);
        }
    }
    EXPECT_EQ(filtered.size(), 639u);
}

// ── testMergedFakeEmpty ─────────────────────────────────────

TEST(PartParseTests, TestMergedFakeEmpty)
{
    auto bytes = ::test::getResource("hello-merged-fake-empty.jar");

    auto archive = ZipIO::readJvm(MemorySegment(bytes));
    EXPECT_TRUE(hasFile(archive, "META-INF/MANIFEST.MF"));
    EXPECT_TRUE(hasFile(archive, "Hello.class/"));
}
