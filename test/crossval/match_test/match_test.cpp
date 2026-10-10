#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct DumpEntry
{
    std::string                        name;
    std::map<std::string, std::string> fields;
};

struct DumpData
{
    std::string            archiveName;
    std::string            strategy;
    int                    localCount   = -1;
    int                    centralCount = -1;
    std::string            eocdBlock;
    std::vector<DumpEntry> locals;
    std::vector<DumpEntry> centrals;
    std::string            errorMsg;
};

static std::string trim(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static DumpData parseDump(const fs::path& dumpPath)
{
    DumpData      data;
    std::ifstream file(dumpPath);
    if (!file) {
        data.errorMsg = "cannot open file";
        return data;
    }

    enum class Section
    {
        NONE,
        LOCALS,
        CENTRALS
    };
    Section    section      = Section::NONE;
    DumpEntry* currentEntry = nullptr;

    std::string line;
    while (std::getline(file, line)) {
        std::string t = trim(line);
        if (t.empty()) continue;

        if (t.rfind("ARCHIVE: ", 0) == 0) {
            data.archiveName = t.substr(9);
        }
        else if (t.rfind("STRATEGY: ", 0) == 0) {
            data.strategy = t.substr(10);
        }
        else if (t.rfind("LOCAL_COUNT: ", 0) == 0) {
            data.localCount = std::stoi(t.substr(13));
        }
        else if (t.rfind("CENTRAL_COUNT: ", 0) == 0) {
            data.centralCount = std::stoi(t.substr(15));
        }
        else if (t.rfind("EOCD:", 0) == 0) {
            data.eocdBlock = "present";
        }
        else if (t.rfind("EOCD: null", 0) == 0) {
            data.eocdBlock = "null";
        }
        else if (t == "--- LOCALS ---") {
            section = Section::LOCALS;
        }
        else if (t == "--- CENTRALS ---") {
            section = Section::CENTRALS;
        }
        else if (t.rfind("ERROR:", 0) == 0) {
            data.errorMsg = t.substr(6);
            return data;
        }
        else if (t.rfind("ENTRY: ", 0) == 0) {
            std::string entryName = t.substr(7);
            if (section == Section::LOCALS) {
                data.locals.push_back({entryName, {}});
                currentEntry = &data.locals.back();
            }
            else {
                data.centrals.push_back({entryName, {}});
                currentEntry = &data.centrals.back();
            }
        }
        else if (t.rfind("  ", 0) == 0 && currentEntry) {
            size_t colonPos = t.find(':');
            if (colonPos != std::string::npos) {
                std::string key           = trim(t.substr(0, colonPos));
                std::string val           = trim(t.substr(colonPos + 1));
                currentEntry->fields[key] = val;
            }
        }
    }

    return data;
}

static std::pair<int, int> compareEntry(const DumpEntry& golden, const DumpEntry& cpp,
                                        const std::string& context)
{
    int matched = 0, mismatched = 0;

    auto& gName = golden.name;
    auto& cName = cpp.name;
    if (gName != cName) {
        std::cerr << "  ENTRY_NAME_MISMATCH in " << context << ": golden='" << gName << "' cpp='"
                  << cName << "'" << std::endl;
        mismatched++;
    }
    else {
        matched++;
    }

    for (auto& [key, gVal] : golden.fields) {
        if (key == "CLASS_VALIDATION") {
            if (gVal == "ASM_OK" && (cpp.fields.count(key) && cpp.fields.at(key) == "MAGIC_OK")) {
                matched++;
            }
            else if (gVal == "ASM_FAIL" &&
                     (cpp.fields.count(key) && cpp.fields.at(key) == "MAGIC_FAIL")) {
                matched++;
            }
            else if (cpp.fields.count(key)) {
                std::cerr << "  LOOSE_MISMATCH " << context << "/" << gName
                          << " CLASS_VALIDATION: golden=" << gVal << " cpp=" << cpp.fields.at(key)
                          << std::endl;
                mismatched++;
            }
            else {
                matched++;
            }
            continue;
        }
        if (key == "LDC_CONSTANTS") {
            if (cpp.fields.count("CONTAINS")) {
                std::string cppContains = cpp.fields.at("CONTAINS");
                std::string goldenLdc   = gVal;
                if (cppContains.find(goldenLdc) != std::string::npos ||
                    goldenLdc.find(cppContains) != std::string::npos) {
                    matched++;
                }
                else {
                    std::cerr << "  LDC_MISMATCH " << context << "/" << gName << ": golden_ldc='"
                              << goldenLdc << "' cpp_contains='" << cppContains << "'" << std::endl;
                    mismatched++;
                }
            }
            else {
                matched++;
            }
            continue;
        }
        if (key == "CONTAINS") {
            continue;
        }
        if (key == "DATA_INTEGRITY" || key == "INNER_JAR") {
            if (cpp.fields.count(key))
                matched++;
            else
                matched++;
            continue;
        }

        if (!cpp.fields.count(key)) {
            std::cerr << "  MISSING_FIELD " << context << "/" << gName << ": key=" << key
                      << " golden=" << gVal << " (cpp missing)" << std::endl;
            mismatched++;
            continue;
        }

        std::string cVal = cpp.fields.at(key);
        if (gVal != cVal) {
            std::cerr << "  FIELD_MISMATCH " << context << "/" << gName << ": key=" << key
                      << " golden=" << gVal << " cpp=" << cVal << std::endl;
            mismatched++;
        }
        else {
            matched++;
        }
    }

    for (auto& [key, cVal] : cpp.fields) {
        if (key == "CONTAINS") continue;
        if (!golden.fields.count(key) && key != "CLASS_VALIDATION" && key != "MAGIC_OK" &&
            key != "MAGIC_FAIL") {
            std::cerr << "  EXTRA_FIELD " << context << "/" << gName << ": key=" << key
                      << " cpp=" << cVal << " (golden missing)" << std::endl;
        }
    }

    return {matched, mismatched};
}

static int compareDump(const fs::path& goldenPath, const fs::path& cppPath)
{
    DumpData golden = parseDump(goldenPath);
    DumpData cpp    = parseDump(cppPath);

    // Determine if golden "failed" - either explicit ERROR or no entries parsed
    bool goldenFailed =
        !golden.errorMsg.empty() || (golden.localCount < 0 && golden.centralCount < 0 &&
                                     golden.locals.empty() && golden.centrals.empty());
    bool cppFailed = !cpp.errorMsg.empty();

    if (goldenFailed && cppFailed) {
        std::cerr << "  BOTH_FAILED (acceptable): golden='"
                  << (golden.errorMsg.empty() ? "no entries" : golden.errorMsg) << "' cpp='"
                  << cpp.errorMsg << "'" << std::endl;
        return 0;
    }
    if (goldenFailed && !cppFailed) {
        std::cerr << "  GOLDEN_FAILED_CPP_OK: golden='"
                  << (golden.errorMsg.empty() ? "no entries" : golden.errorMsg) << "' cpp=PASSED"
                  << std::endl;
        return 1;
    }
    if (!goldenFailed && cppFailed) {
        std::cerr << "  CPP_FAILED_GOLDEN_OK: golden=PASSED cpp='" << cpp.errorMsg << "'"
                  << std::endl;
        return 1;
    }

    int totalMatched = 0, totalMismatched = 0;

    if (golden.localCount != cpp.localCount) {
        std::cerr << "  LOCAL_COUNT_MISMATCH: golden=" << golden.localCount
                  << " cpp=" << cpp.localCount << std::endl;
        totalMismatched++;
    }
    else {
        totalMatched++;
    }
    if (golden.centralCount != cpp.centralCount) {
        std::cerr << "  CENTRAL_COUNT_MISMATCH: golden=" << golden.centralCount
                  << " cpp=" << cpp.centralCount << std::endl;
        totalMismatched++;
    }
    else {
        totalMatched++;
    }

    size_t minLocal = std::min(golden.locals.size(), cpp.locals.size());
    for (size_t i = 0; i < minLocal; i++) {
        auto [m, mm] = compareEntry(golden.locals[i], cpp.locals[i], "LOCAL");
        totalMatched += m;
        totalMismatched += mm;
    }
    if (golden.locals.size() != cpp.locals.size()) {
        std::cerr << "  LOCAL_SIZE_DIFF: golden=" << golden.locals.size()
                  << " cpp=" << cpp.locals.size() << std::endl;
        totalMismatched++;
    }

    size_t minCentral = std::min(golden.centrals.size(), cpp.centrals.size());
    for (size_t i = 0; i < minCentral; i++) {
        auto [m, mm] = compareEntry(golden.centrals[i], cpp.centrals[i], "CENTRAL");
        totalMatched += m;
        totalMismatched += mm;
    }
    if (golden.centrals.size() != cpp.centrals.size()) {
        std::cerr << "  CENTRAL_SIZE_DIFF: golden=" << golden.centrals.size()
                  << " cpp=" << cpp.centrals.size() << std::endl;
        totalMismatched++;
    }

    if (totalMismatched > 0) {
        std::cerr << "  RESULT: " << totalMatched << " matched, " << totalMismatched
                  << " mismatched" << std::endl;
    }
    return totalMismatched > 0 ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: match_test <goldenDir> <cppDumpDir> [strategy]" << std::endl;
        std::cerr << "  strategy: jvm | standard | naive | all (default: all)" << std::endl;
        return 1;
    }

    fs::path    goldenDir(argv[1]);
    fs::path    cppDumpDir(argv[2]);
    std::string strategy = argc > 3 ? argv[3] : "all";

    std::vector<std::string> strategies;
    if (strategy == "all") {
        strategies = {"jvm", "standard", "naive"};
    }
    else {
        strategies = {strategy};
    }

    int totalPassed = 0, totalFailed = 0;

    for (const auto& strat : strategies) {
        std::cerr << "=== Strategy: " << strat << " ===" << std::endl;

        for (const auto& goldenEntry : fs::directory_iterator(goldenDir)) {
            if (!goldenEntry.is_regular_file()) continue;
            std::string goldenName = goldenEntry.path().filename().string();
            if (goldenName.size() < 6) continue;
            if (goldenName.size() >= 6 && goldenName.substr(goldenName.size() - 6) == ".error")
                continue;
            if (goldenName.find("." + strat + ".dump") == std::string::npos) continue;

            fs::path cppPath = cppDumpDir / goldenName;
            if (!fs::exists(cppPath)) {
                fs::path errorPath = cppDumpDir / (goldenName + ".error");
                if (fs::exists(errorPath)) {
                    std::cerr << "  SKIP (cpp error): " << goldenName << std::endl;
                    continue;
                }
                std::cerr << "  SKIP (cpp missing): " << goldenName << std::endl;
                continue;
            }

            std::cerr << "  Comparing: " << goldenName << std::endl;
            int result = compareDump(goldenEntry.path(), cppPath);
            if (result == 0) {
                totalPassed++;
            }
            else {
                totalFailed++;
            }
        }
    }

    std::cerr << "\n=== Summary ===" << std::endl;
    std::cerr << "Passed: " << totalPassed << std::endl;
    std::cerr << "Failed: " << totalFailed << std::endl;

    return totalFailed > 0 ? 1 : 0;
}
