/*
 * 1:1 mapping to LL-Java-Zip test: ZipComparisonShowcaseTest.java
 * - test(String name) @Disabled parameterized with 32 samples
 * - handle(name, data) console output function
 *
 * DISABLED: manual showcase only - no assertions, do not enable in CI.
 * Mirrors Java class-level @Disabled("only used for demonstration purposes,
 * and does not make any assertions").
 * LL-J-ZIP readers (jvm/std/naive) are fully mirrored.
 * ZipInputStream/ZipFile/FileSystem are Java stdlib, no C++ equivalent here.
 */

#include <gtest/gtest.h>

#include <iostream>
#include <string>
#include <vector>

#include "test_helper.hpp"
#include "jzip/zip_io.hpp"

using namespace jzip;

// Mirror of Java handle(): prints each entry, with secret-message detection for Hello
static void handle(const std::string& entryName, const std::vector<ca::u8>& data)
{
    if (entryName.find("Hello") != std::string::npos) {
        std::string dataStr(data.begin(), data.end());
        bool        hasSecret = dataStr.find("The secret code is: ROSE") != std::string::npos;
        std::cout << "  " << entryName
                  << " --> Has secret message: " << (hasSecret ? "true" : "false") << std::endl;
    }
    else {
        std::cout << "  " << entryName << std::endl;
    }
}

class ZipComparisonShowcaseTests : public ::testing::TestWithParam<std::string>
{};
TEST_P(ZipComparisonShowcaseTests, DISABLED_Test)
{
    const auto& name  = GetParam();
    auto        bytes = ::test::getResource(name);
    auto        data  = MemorySegment(bytes);

    // ── LL-J-ZIP (jvm-strategy) ──
    std::cout << "==== LL-J-ZIP (jvm-strategy) ====" << std::endl;
    try {
        auto archive = ZipIO::readJvm(data);
        for (const auto* local : archive.getLocalFiles()) {
            auto decompressed = ZipIO::decompressLocalFile(*local);
            handle(local->getFileName(), decompressed);
        }
    }
    catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
    }

    // ── LL-J-ZIP (std-strategy) ──
    std::cout << "==== LL-J-ZIP (std-strategy) ====" << std::endl;
    try {
        auto archive = ZipIO::readStandard(data);
        for (const auto* local : archive.getLocalFiles()) {
            auto decompressed = ZipIO::decompressLocalFile(*local);
            handle(local->getFileName(), decompressed);
        }
    }
    catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
    }

    // ── LL-J-ZIP (naive-strategy) ──
    std::cout << "==== LL-J-ZIP (naive-strategy) ====" << std::endl;
    try {
        auto archive = ZipIO::readNaive(data);
        for (const auto* local : archive.getLocalFiles()) {
            auto decompressed = ZipIO::decompressLocalFile(*local);
            handle(local->getFileName(), decompressed);
        }
    }
    catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
    }
}

INSTANTIATE_TEST_SUITE_P(
    ZipComparisonShowcaseTests, ZipComparisonShowcaseTests,
    ::testing::Values(
        "hello.jar", "hello.png", "hello-concat.jar", "hello-concat-junkheader.jar",
        "hello-copyjar-at-head.jar", "hello-copyjar-at-tail.jar", "hello-deceptive.jar",
        "hello-end-declares-0-entries.jar", "hello-end-declares-0-entries-0-offset.jar",
        "hello-jar-in-in-jar-in-jar-in-jar-in-jar.jar", "hello-junk-dir-length.jar",
        "hello-junk-eocd.jar", "hello-junk-local-length.jar", "hello-merged.jar",
        "hello-merged-fake-empty.jar", "hello-merged-junkheader.jar", "hello-no-local-length.jar",
        "hello-no-local-names.jar", "hello-secret.jar", "hello-secret-0-length-locals.jar",
        "hello-secret-junkheader.jar", "hello-secret-trailing-slash.jar",
        "hello-secret-trailing-slash-0-length-locals.jar", "hello-total-junk.jar",
        "hello-total-junk-large.jar", "hello-txt-stored.jar", "hello-txt-type-0.jar",
        "hello-wrong-crc-both.jar", "hello-wrong-crc-central.jar", "hello-wrong-crc-local.jar",
        "hello-wrong-local-compression.jar", "hello-zeroed-locals.jar"));
