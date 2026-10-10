#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "jzip/zip_io.hpp"
#include "jzip/zip_reader.hpp"

using namespace jzip;

static std::string escapeString(const std::string& s)
{
    std::string result;
    for (char c : s) {
        if (c >= 32 && c < 127) {
            result += c;
        }
        else {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02x", static_cast<unsigned char>(c));
            result += buf;
        }
    }
    return result;
}

static std::string formatHex16(std::uint16_t val)
{
    std::ostringstream oss;
    oss << "0x" << std::setfill('0') << std::setw(4) << std::uppercase << std::hex << val;
    return oss.str();
}

static std::string formatHex32(std::uint32_t val)
{
    std::ostringstream oss;
    oss << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return oss.str();
}

static void dumpLocalFileHeader(std::ofstream& out, const LocalFileHeader& lfh)
{
    const std::string& name = lfh.getFileName();
    out << "ENTRY: " << name << "\n";
    out << "  OFFSET: " << lfh.offset() << "\n";
    out << "  VERSION: " << lfh.getVersionNeededToExtract() << "\n";
    out << "  GP_FLAG: " << formatHex16(lfh.getGeneralPurposeBitFlag()) << "\n";
    out << "  METHOD: " << lfh.getCompressionMethod() << "\n";
    out << "  MOD_TIME: " << formatHex16(lfh.getLastModFileTime()) << "\n";
    out << "  MOD_DATE: " << formatHex16(lfh.getLastModFileDate()) << "\n";
    out << "  CRC32: " << formatHex32(lfh.getCrc32()) << "\n";
    out << "  COMPRESSED_SIZE: " << lfh.getCompressedSize() << "\n";
    out << "  UNCOMPRESSED_SIZE: " << lfh.getUncompressedSize() << "\n";
    out << "  FILE_NAME_LENGTH: " << lfh.getFileNameLength() << "\n";
    out << "  EXTRA_FIELD_LENGTH: " << lfh.getExtraFieldLength() << "\n";

    const auto& fileData = lfh.getFileData();
    out << "  FILE_DATA_LENGTH: " << fileData.size() << "\n";

    if (lfh.hasDifferentValuesThanCentralDirectoryHeader()) {
        out << "  DIFFERS_FROM_CENTRAL: true\n";
    }

    std::string nameStr(name);
    bool isClass = nameStr.size() >= 6 && nameStr.compare(nameStr.size() - 6, 6, ".class") == 0;
    bool isJar   = nameStr.size() >= 4 && nameStr.compare(nameStr.size() - 4, 4, ".jar") == 0;

    if (isClass) {
        try {
            auto decompressed = ZipIO::decompressLocalFile(lfh);
            if (decompressed.size() >= 4 && decompressed[0] == static_cast<ca::u8>(0xCA) &&
                decompressed[1] == static_cast<ca::u8>(0xFE) &&
                decompressed[2] == static_cast<ca::u8>(0xBA) &&
                decompressed[3] == static_cast<ca::u8>(0xBE)) {
                out << "  CLASS_VALIDATION: MAGIC_OK\n";
            }
            else {
                out << "  CLASS_VALIDATION: MAGIC_FAIL\n";
            }
        }
        catch (...) {
            if (fileData.size() >= 4 && fileData[0] == static_cast<ca::u8>(0xCA) &&
                fileData[1] == static_cast<ca::u8>(0xFE) &&
                fileData[2] == static_cast<ca::u8>(0xBA) &&
                fileData[3] == static_cast<ca::u8>(0xBE)) {
                out << "  CLASS_VALIDATION: MAGIC_OK\n";
            }
            else {
                out << "  CLASS_VALIDATION: MAGIC_FAIL\n";
            }
        }
    }
    else if (isJar && !fileData.empty()) {
        out << "  INNER_JAR: parseable\n";
    }
    else if (!fileData.empty()) {
        out << "  DATA_INTEGRITY: OK\n";
    }
}

static void dumpCentralDirectoryFileHeader(std::ofstream&                    out,
                                           const CentralDirectoryFileHeader& cen)
{
    const std::string& name = cen.getFileName();
    out << "ENTRY: " << name << "\n";
    out << "  OFFSET: " << cen.offset() << "\n";
    out << "  VERSION_MADE_BY: " << cen.getVersionMadeBy() << "\n";
    out << "  VERSION_NEEDED: " << cen.getVersionNeededToExtract() << "\n";
    out << "  GP_FLAG: " << formatHex16(cen.getGeneralPurposeBitFlag()) << "\n";
    out << "  METHOD: " << cen.getCompressionMethod() << "\n";
    out << "  MOD_TIME: " << formatHex16(cen.getLastModFileTime()) << "\n";
    out << "  MOD_DATE: " << formatHex16(cen.getLastModFileDate()) << "\n";
    out << "  CRC32: " << formatHex32(cen.getCrc32()) << "\n";
    out << "  COMPRESSED_SIZE: " << cen.getCompressedSize() << "\n";
    out << "  UNCOMPRESSED_SIZE: " << cen.getUncompressedSize() << "\n";
    out << "  FILE_NAME_LENGTH: " << cen.getFileNameLength() << "\n";
    out << "  EXTRA_FIELD_LENGTH: " << cen.getExtraFieldLength() << "\n";
    out << "  DISK_NUMBER_START: " << cen.getDiskNumberStart() << "\n";
    out << "  INTERNAL_ATTRS: " << cen.getInternalFileAttributes() << "\n";
    out << "  EXTERNAL_ATTRS: " << formatHex32(cen.getExternalFileAttributes()) << "\n";
    out << "  LOCAL_HEADER_OFFSET: " << cen.getRelativeOffsetOfLocalHeader() << "\n";
}

static void dumpLljzip(std::ofstream& out, const std::filesystem::path& samplePath,
                       const std::string& strategy)
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

    MemorySegment data(bytes);

    ZipArchive archive;
    try {
        if (strategy == "jvm") {
            archive = ZipIO::readJvm(data);
        }
        else if (strategy == "standard") {
            archive = ZipIO::readStandard(data);
        }
        else if (strategy == "naive") {
            archive = ZipIO::readNaive(data);
        }
        else {
            out << "ERROR: unknown strategy: " << strategy << "\n";
            return;
        }
    }
    catch (const ZipParseException& ex) {
        out << "ERROR: " << ex.what() << " offset=" << ex.getOffset() << "\n";
        return;
    }
    catch (const std::exception& ex) {
        out << "ERROR: " << ex.what() << "\n";
        return;
    }

    auto                         locals   = archive.getLocalFiles();
    auto                         centrals = archive.getCentralDirectories();
    const EndOfCentralDirectory* end      = archive.getEnd();

    out << "LOCAL_COUNT: " << locals.size() << "\n";
    out << "CENTRAL_COUNT: " << centrals.size() << "\n";

    if (end) {
        out << "EOCD:\n";
        out << "  OFFSET: " << end->offset() << "\n";
        out << "  DISK_NUMBER: " << end->getDiskNumber() << "\n";
        out << "  CEN_START_DISK: " << end->getCentralDirectoryStartDisk() << "\n";
        out << "  CEN_START_OFFSET: " << end->getCentralDirectoryStartOffset() << "\n";
        out << "  NUM_ENTRIES: " << end->getNumEntries() << "\n";
        out << "  CEN_SIZE: " << end->getCentralDirectorySize() << "\n";
        out << "  CEN_OFFSET: " << end->getCentralDirectoryOffset() << "\n";
    }
    else {
        out << "EOCD: null\n";
    }

    out << "\n--- LOCALS ---\n";
    for (const auto* lfh : locals) {
        dumpLocalFileHeader(out, *lfh);
    }

    out << "\n--- CENTRALS ---\n";
    for (const auto* cen : centrals) {
        dumpCentralDirectoryFileHeader(out, *cen);
    }
}

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: zip_dump <testResourceDir> <outputDir> <strategy>" << std::endl;
        std::cerr << "  strategy: jvm | standard | naive" << std::endl;
        return 1;
    }

    std::filesystem::path testResourceDir(argv[1]);
    std::filesystem::path outputDir(argv[2]);
    std::string           strategy(argv[3]);

    std::filesystem::create_directories(outputDir);

    std::vector<std::filesystem::path> samples;
    for (const auto& entry : std::filesystem::directory_iterator(testResourceDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext == ".jar" || ext == ".zip" || ext == ".png") {
            samples.push_back(entry.path());
        }
    }
    // Also check subdirectory "hello"
    std::filesystem::path helloDir = testResourceDir / "hello";
    if (std::filesystem::exists(helloDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(helloDir)) {
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

        std::string           dumpFileName = name + "." + strategy + ".dump";
        std::filesystem::path dumpPath     = outputDir / dumpFileName;

        std::ofstream out(dumpPath, std::ios::out | std::ios::trunc);
        if (!out) {
            std::cerr << "    FAILED: cannot create output file" << std::endl;
            std::ofstream errOut(dumpPath.string() + ".error", std::ios::out | std::ios::trunc);
            errOut << "cannot create output file\n";
            continue;
        }

        out << "ARCHIVE: " << name << "\n";
        out << "STRATEGY: " << strategy << "\n";

        dumpLljzip(out, samplePath, strategy);
    }

    std::cerr << "Done. Output files written to: " << outputDir << std::endl;
    return 0;
}
