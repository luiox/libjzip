#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "libca/zip/file.hpp"
#include "libca/zip/entry.hpp"

namespace fs = std::filesystem;

static std::string formatHex32(uint32_t val)
{
    std::ostringstream oss;
    oss << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return oss.str();
}

static void dumpZipFile(std::ofstream& out, const fs::path& samplePath)
{
    std::vector<ca::u8> bytes;
    std::ifstream       file(samplePath, std::ios::binary);
    if (!file) {
        out << "ERROR: cannot read file\n";
        return;
    }
    file.seekg(0, std::ios::end);
    bytes.resize(static_cast<size_t>(file.tellg()));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));

    ca::zip::ZipFile zf;
    // libca 0.0.9 [不兼容]：open 异常改 ca::Result<void, ZipErrorInfo>，
    // 原 try/catch 成死代码且损坏输入静默产出 ENTRY_COUNT: 0（R16 评审 F3
    // 探针实证）；恢复头行失败标记语义——open 失败直接返回。
    if (zf.open(bytes).is_err()) {
        return;
    }

    out << "ENTRY_COUNT: " << zf.size() << "\n";
    out << "\n";

    for (size_t i = 0; i < zf.size(); ++i) {
        const auto& entry = zf.get_entry_at(i);
        out << "ENTRY: " << entry.name() << "\n";
        out << "  METHOD: " << entry.compression_method() << "\n";
        out << "  SIZE: " << entry.uncompressed_size() << "\n";
        out << "  COMPRESSED_SIZE: " << entry.compressed_size() << "\n";
        out << "  CRC32: " << formatHex32(entry.crc32()) << "\n";
        out << "  IS_DIRECTORY: "
            << (!entry.name().empty() && entry.name().back() == '/' ? "true" : "false")
            << "\n";
    }
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: zip_dump <testResourceDir> <outputDir>" << std::endl;
        return 1;
    }

    fs::path testResourceDir(argv[1]);
    fs::path outputDir(argv[2]);

    fs::create_directories(outputDir);

    std::vector<fs::path> samples;
    for (const auto& entry : fs::directory_iterator(testResourceDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext == ".jar" || ext == ".zip" || ext == ".png") {
            samples.push_back(entry.path());
        }
    }
    fs::path helloDir = testResourceDir / "hello";
    if (fs::exists(helloDir)) {
        for (const auto& entry : fs::directory_iterator(helloDir)) {
            if (entry.is_regular_file()) {
                samples.push_back(entry.path());
            }
        }
    }
    std::sort(samples.begin(), samples.end());

    std::cerr << "Found " << samples.size() << " sample(s)" << std::endl;

    for (const auto& samplePath : samples) {
        std::string name = samplePath.filename().string();
        std::cerr << "  Processing: " << name << std::endl;

        std::string dumpFileName = name + ".zipfile.dump";
        fs::path    dumpPath     = outputDir / dumpFileName;

        std::ofstream out(dumpPath, std::ios::out | std::ios::trunc);
        if (!out) {
            std::cerr << "    FAILED: cannot create output file" << std::endl;
            std::ofstream errOut(dumpPath.string() + ".error", std::ios::out | std::ios::trunc);
            errOut << "cannot create output file\n";
            continue;
        }

        out << "ARCHIVE: " << name << "\n";
        out << "STRATEGY: zipfile\n";

        dumpZipFile(out, samplePath);
    }

    std::cerr << "Done. Output files written to: " << outputDir << std::endl;
    return 0;
}
