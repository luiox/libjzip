#include <gtest/gtest.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "test_utils.hpp"

// 手动演示用（manual showcase）：三种 reader 策略对同一样本的打印对比，无任何断言。
// 对齐 Java 侧 ZipComparisonShowcaseTest 的类级
// @Disabled("only used for demonstration purposes, and does not make any assertions")，
// 保持 DISABLED，勿在 CI 启用；人工查看用 --gtest_also_run_disabled_tests。

using namespace jzip;
using namespace jzip::test;



class ZipComparisonShowcaseTests : public ::testing::TestWithParam<std::string>
{};

TEST_P(ZipComparisonShowcaseTests, DISABLED_Test)
{
    const auto& name  = GetParam();
    const auto  path  = test_resource_path(name);
    const auto  bytes = read_file_bytes(path);
    // 演示测试也不例外：缺资源按失败处理，不做静默通过。
    ASSERT_FALSE(bytes.empty()) << "missing test resource: " << name;

    {
        std::cout << "==== libjzip (jvm-strategy) ====" << std::endl;
        try {
            auto zipJvm = ZipIO::readJvm(MemorySegment(bytes));
            for (const auto* lfh : zipJvm.getLocalFiles()) {
                const auto& entryName = lfh->getFileName();
                try {
                    auto entryData = ZipIO::decompressLocalFile(*lfh);
                    auto entryStr  = bytes_to_string(entryData);
                    if (entryName.find("Hello") != std::string::npos) {
                        std::cout << entryName << " --> Has secret message: "
                                  << (entryStr.find("The secret code is: ROSE") !=
                                      std::string::npos)
                                  << std::endl;
                    }
                    else {
                        std::cout << entryName << std::endl;
                    }
                }
                catch (const std::exception&) {
                    std::cout << entryName << " --> decompression error" << std::endl;
                }
            }
        }
        catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
    }

    {
        std::cout << "==== libjzip (std-strategy) ====" << std::endl;
        try {
            auto zipStd = ZipIO::readStandard(MemorySegment(bytes));
            for (const auto* lfh : zipStd.getLocalFiles()) {
                const auto& entryName = lfh->getFileName();
                try {
                    auto entryData = ZipIO::decompressLocalFile(*lfh);
                    auto entryStr  = bytes_to_string(entryData);
                    if (entryName.find("Hello") != std::string::npos) {
                        std::cout << entryName << " --> Has secret message: "
                                  << (entryStr.find("The secret code is: ROSE") !=
                                      std::string::npos)
                                  << std::endl;
                    }
                    else {
                        std::cout << entryName << std::endl;
                    }
                }
                catch (const std::exception&) {
                    std::cout << entryName << " --> decompression error" << std::endl;
                }
            }
        }
        catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
    }

    {
        std::cout << "==== libjzip (naive-strategy) ====" << std::endl;
        try {
            auto zipNaive = ZipIO::readNaive(MemorySegment(bytes));
            for (const auto* lfh : zipNaive.getLocalFiles()) {
                const auto& entryName = lfh->getFileName();
                try {
                    auto entryData = ZipIO::decompressLocalFile(*lfh);
                    auto entryStr  = bytes_to_string(entryData);
                    if (entryName.find("Hello") != std::string::npos) {
                        std::cout << entryName << " --> Has secret message: "
                                  << (entryStr.find("The secret code is: ROSE") !=
                                      std::string::npos)
                                  << std::endl;
                    }
                    else {
                        std::cout << entryName << std::endl;
                    }
                }
                catch (const std::exception&) {
                    std::cout << entryName << " --> decompression error" << std::endl;
                }
            }
        }
        catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    ZipComparisonShowcase, ZipComparisonShowcaseTests,
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
