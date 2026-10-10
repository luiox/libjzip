#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4996)   // tmpnam/fopen: used for temp file in DISABLED test
#endif

#include <gtest/gtest.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_model.hpp"
#include "utils.hpp"

using namespace jzip;
using namespace jzip::test_utils;

// ── Helpers ──────────────────────────────────────────────────

static const LocalFileHeader* findLocalByName(const std::vector<const LocalFileHeader*>& locals,
                                              const std::string&                         name)
{
    for (const auto* l : locals) {
        if (l->getFileName() == name) return l;
    }
    return nullptr;
}

static const CentralDirectoryFileHeader* findCenByName(
    const std::vector<const CentralDirectoryFileHeader*>& cens, const std::string& name)
{
    for (const auto* c : cens) {
        if (c->getFileName() == name) return c;
    }
    return nullptr;
}

// Mirror of Java withArchive(): write bytes to temp file, then invoke consumer.
// Passes both the file path (for readAdaptingIO) and raw bytes (for MemorySegment readers).
static void withArchive(const std::vector<ca::u8>& archiveBytes,
                        void (*consumer)(const std::string& path, const std::vector<ca::u8>& bytes))
{
    char tmpPath[L_tmpnam];
    std::tmpnam(tmpPath);
    std::string path(tmpPath);
    path += ".zip";

    FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr) << "Failed to create temp file";
    std::fwrite(archiveBytes.data(), 1, archiveBytes.size(), f);
    std::fclose(f);

    consumer(path, archiveBytes);

    std::remove(path.c_str());
}

// ── testZip64OffsetsAndParity ───────────────────────────────

TEST(Zip64Tests, TestZip64OffsetsAndParity)
{
    withArchive(
        zip64OffsetArchive(), [](const std::string& path, const std::vector<ca::u8>& bytes) {
            auto data = MemorySegment(bytes);

            // For the test archive, all three strategies that can reasonably take ZIP64 input
            // should be able to read the same local file headers, and the same central directory
            // entries.
            auto zipStd      = ZipIO::readStandard(data);
            auto zipJvm      = ZipIO::readJvm(data);
            auto zipAdapting = ZipIO::readAdaptingIO(path);

            EXPECT_EQ(zipStd.getLocalFiles().size(), 2u);
            EXPECT_EQ(zipJvm.getLocalFiles().size(), 2u);
            EXPECT_EQ(zipAdapting.getLocalFiles().size(), 2u);

            const auto* stdEntry = findLocalByName(zipStd.getLocalFiles(), "beta.txt");
            const auto* jvmEntry = findLocalByName(zipJvm.getLocalFiles(), "beta.txt");
            ASSERT_NE(stdEntry, nullptr);
            ASSERT_NE(jvmEntry, nullptr);
            {
                auto        decomp = ZipIO::decompressLocalFile(*stdEntry);
                std::string content(decomp.begin(), decomp.end());
                EXPECT_EQ(content, "second-entry");
            }
            {
                auto        decomp = ZipIO::decompressLocalFile(*jvmEntry);
                std::string content(decomp.begin(), decomp.end());
                EXPECT_EQ(content, "second-entry");
            }

            // CEN entries: check Zip64-relative offset
            // After Zip64 field alignment, read() directly overwrites the 0xFFFFFFFF magic
            // with the actual 64-bit offset from the Zip64 extra field (对标 Java 行为)
            const auto* stdDir = findCenByName(zipStd.getCentralDirectories(), "beta.txt");
            const auto* jvmDir = findCenByName(zipJvm.getCentralDirectories(), "beta.txt");
            ASSERT_NE(stdDir, nullptr);
            ASSERT_NE(jvmDir, nullptr);
            // The offset should now be the actual offset, not 0xFFFFFFFF
            EXPECT_NE(stdDir->getRelativeOffsetOfLocalHeader(), static_cast<ca::i64>(0xFFFFFFFF));
            EXPECT_NE(jvmDir->getRelativeOffsetOfLocalHeader(), static_cast<ca::i64>(0xFFFFFFFF));
            // Both should agree on the actual offset
            EXPECT_EQ(stdDir->getRelativeOffsetOfLocalHeader(),
                      jvmDir->getRelativeOffsetOfLocalHeader());
            // Zip64 flag should be set
            EXPECT_TRUE(stdDir->hasZip64RelativeOffsetOfLocalHeader());
            EXPECT_TRUE(jvmDir->hasZip64RelativeOffsetOfLocalHeader());

            // Name set equality across all three readers
            auto collectNames = [](const std::vector<const LocalFileHeader*>& locals) {
                std::set<std::string> names;
                for (const auto* l : locals) names.insert(l->getFileName());
                return names;
            };
            auto adaptingNames = collectNames(zipAdapting.getLocalFiles());
            auto stdNames      = collectNames(zipStd.getLocalFiles());
            auto jvmNames      = collectNames(zipJvm.getLocalFiles());
            EXPECT_EQ(adaptingNames, stdNames);
            EXPECT_EQ(adaptingNames, jvmNames);
        });
}

// ── Data descriptor archive helper ──────────────────────────

static void assertZip64DataDescriptorArchive(const std::vector<ca::u8>& archiveBytes)
{
    withArchive(archiveBytes, [](const std::string& path, const std::vector<ca::u8>& bytes) {
        auto data   = MemorySegment(bytes);
        auto zipStd = ZipIO::readStandard(data);
        auto zipJvm = ZipIO::readJvm(data);

        const auto* stdEntry = findLocalByName(zipStd.getLocalFiles(), "descriptor.txt");
        const auto* jvmEntry = findLocalByName(zipJvm.getLocalFiles(), "descriptor.txt");
        ASSERT_NE(stdEntry, nullptr);
        ASSERT_NE(jvmEntry, nullptr);

        std::string expected      = "descriptor-data";
        auto        expectedBytes = std::vector<ca::u8>(expected.begin(), expected.end());
        {
            auto decomp = ZipIO::decompressLocalFile(*stdEntry);
            EXPECT_EQ(decomp, expectedBytes);
            EXPECT_EQ(static_cast<ca::u32>(expected.size()), stdEntry->getCompressedSize());
        }
        {
            auto decomp = ZipIO::decompressLocalFile(*jvmEntry);
            EXPECT_EQ(decomp, expectedBytes);
            EXPECT_EQ(static_cast<ca::u32>(expected.size()), jvmEntry->getCompressedSize());
        }
    });
}

TEST(Zip64Tests, TestZip64DataDescriptorWithSignature)
{
    assertZip64DataDescriptorArchive(zip64DataDescriptorArchive(true));
}

TEST(Zip64Tests, TestZip64DataDescriptorWithoutSignature)
{
    assertZip64DataDescriptorArchive(zip64DataDescriptorArchive(false));
}

#ifdef _MSC_VER
#    pragma warning(pop)
#endif
