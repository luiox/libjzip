/*
 * 1:1 mapping to LL-Java-Zip test: PatchingTests.java
 * - testTrickJarPatched(String name) x4
 * - testReadIgnoringFileLengths()
 * - testTrailingSlashTransform()
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"
#include "jzip/zip_mapper.hpp"
#include "jzip/zip_reader.hpp"
#include "jzip/zip_writer.hpp"
#include "jar_in_jar_utils.hpp"

using namespace jzip;
using namespace jzip::test_utils;

// 辅助：按文件名查找
static const LocalFileHeader* findLocalByName(const ZipArchive& archive, const std::string& name)
{
    for (const auto* local : archive.getLocalFiles()) {
        if (local->getFileName() == name) return local;
    }
    return nullptr;
}

// ── testTrickJarPatched ─────────────────────────────────────

class PatchingTrickJarTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(PatchingTrickJarTests, TestTrickJarPatched)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);
    auto        data  = MemorySegment(bytes);

    // Validate the original jar file yields the wrong 'Hello' class
    auto        originalStd = ZipIO::readStandard(data);
    const auto* stdHello    = findLocalByName(originalStd, "Hello.class");
    ASSERT_NE(stdHello, nullptr) << name;
    auto stdDecompressed = ZipIO::decompressLocalFile(*stdHello);
    assertDefinesString(stdDecompressed, "Hello world!");

    // Parse with JVM reader, then write using RecompressingZipWriter
    auto                   originalJvm = ZipIO::readJvm(data);
    RecompressingZipWriter writer(false, 0);
    std::ostringstream     oss(std::ios::binary);
    writer.write(originalJvm, oss);
    const auto&         str = oss.str();
    std::vector<ca::u8> fixed(str.begin(), str.end());

    // Validate the new jar bytes can be read and show the true file contents
    auto        patched      = ZipIO::readStandard(MemorySegment(fixed));
    const auto* patchedHello = findLocalByName(patched, "Hello.class");
    ASSERT_NE(patchedHello, nullptr) << name;
    auto patchedDecompressed = ZipIO::decompressLocalFile(*patchedHello);
    assertDefinesString(patchedDecompressed, "The secret code is: ROSE");
}
INSTANTIATE_TEST_SUITE_P(PatchingTests, PatchingTrickJarTests,
                         ::testing::Values("hello-concat.jar", "hello-concat-junkheader.jar",
                                           "hello-merged.jar", "hello-merged-junkheader.jar"));

// ── LengthIgnoringAllocator ──────────────────────────────────
// 1:1 mapping to Java PatchingTests.lengthIgnoringAllocator()
// Custom LocalFileHeader that ignores declared compressed sizes
// and scans for the next local header to determine data length.

class LengthIgnoringLocalFileHeader : public LocalFileHeader
{
public:
    void readFileData(const MemorySegment& data, ca::usize headerOffset) override
    {
        constexpr ca::u32 kLocalHeaderSig = 0x04034b50u;
        ca::usize         localDataOffset = LocalFileHeader::kMinFixedSize +
                                    static_cast<ca::usize>(getFileNameLength()) +
                                    static_cast<ca::usize>(getExtraFieldLength());
        ca::usize dataStart = headerOffset + localDataOffset;
        ca::usize nextStart = MemorySegmentUtil::indexOfQuad(data, dataStart, kLocalHeaderSig);
        ca::usize fileDataLength;
        if (nextStart > headerOffset) {
            fileDataLength = nextStart - dataStart;
        }
        else {
            fileDataLength = data.byteSize() - dataStart;
        }
        setFileData(data.asSlice(dataStart, fileDataLength).toByteArray());
    }
};

class LengthIgnoringAllocator : public ZipPartAllocator
{
    SimpleZipPartAllocator fallback_;

public:
    std::unique_ptr<LocalFileHeader> newLocalFileHeader() override
    {
        return std::make_unique<LengthIgnoringLocalFileHeader>();
    }
    std::unique_ptr<CentralDirectoryFileHeader> newCentralDirectoryFileHeader() override
    {
        return fallback_.newCentralDirectoryFileHeader();
    }
    std::unique_ptr<EndOfCentralDirectory> newEndOfCentralDirectory() override
    {
        return fallback_.newEndOfCentralDirectory();
    }
};

// ── testReadIgnoringFileLengths ─────────────────────────────

TEST(PatchingTests, TestReadIgnoringFileLengths)
{
    auto bytes = ::test::getResource("resource-pack-trick-data-ioobe.zip");
    auto data  = MemorySegment(bytes);

    // Assert default zip part allocation fails for naive/standard
    EXPECT_THROW(ZipIO::readNaive(data), ZipParseException);
    EXPECT_THROW(ZipIO::readStandard(data), ZipParseException);

    // Naive strategy + length-ignoring allocator: finds the file with trailing '/'
    {
        NaiveLocalFileZipReader naiveReader(std::make_unique<LengthIgnoringAllocator>());
        ZipArchive              naiveZip;
        ASSERT_NO_THROW(naiveReader.read(naiveZip, data));
        EXPECT_NE(findLocalByName(naiveZip, "assets/luxbl/lang/en_us.json/"), nullptr)
            << "Missing 'en_us' file";
    }

    // Standard strategy + length-ignoring allocator: finds the file without trailing '/'
    {
        ForwardScanZipReader forwardReader(std::make_unique<LengthIgnoringAllocator>());
        ZipArchive           forwardZip;
        ASSERT_NO_THROW(forwardReader.read(forwardZip, data));
        EXPECT_NE(findLocalByName(forwardZip, "assets/luxbl/lang/en_us.json"), nullptr)
            << "Missing 'en_us' file";
    }
}

// ── testTrailingSlashTransform ──────────────────────────────

TEST(PatchingTests, TestTrailingSlashTransform)
{
    auto bytes = ::test::getResource("hello-secret-trailing-slash.jar");

    auto archive = ZipIO::readStandard(MemorySegment(bytes));
    auto mapped  = archive.withMapping(JvmClassDirectoryMapper(IdentityZipPartMapper{}));
    EXPECT_NE(findLocalByName(mapped, "Hello.class"), nullptr) << "Trailing slash was not patched";
}
