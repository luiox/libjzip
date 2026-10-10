#include "jzip/zip_anomaly.hpp"

namespace jzip {

void ZipReadReport::add(ZipAnomaly anomaly)
{
    anomalies_.push_back(std::move(anomaly));
}

const std::vector<ZipAnomaly>& ZipReadReport::anomalies() const
{
    return anomalies_;
}

ca::usize ZipReadReport::countOf(ZipAnomalyKind kind) const
{
    ca::usize n = 0;
    for (const auto& anomaly : anomalies_) {
        if (anomaly.kind == kind) ++n;
    }
    return n;
}

bool ZipReadReport::empty() const
{
    return anomalies_.empty();
}

void emitAnomaly(ZipReadReport& report, ZipAnomalySink* sink, ZipAnomalyKind kind, ca::u64 offset,
                 std::string entry, std::string detail)
{
    report.add(ZipAnomaly{kind, offset, entry, detail});
    if (sink != nullptr) {
        // 报告已持有副本；sink 拿到独立拷贝，避免别名。
        sink->onAnomaly(report.anomalies().back());
    }
}

bool isSuspiciousEntryName(const std::string& name)
{
    if (name.empty()) return false;
    if (name[0] == '\0') return true;   // NUL 开头（日志/GUI 注入常见前缀）
    if (name.find("../") != std::string::npos || name.find("..\\") != std::string::npos) {
        return true;   // 路径穿越
    }
    if (name.find("<img") != std::string::npos || name.find("<script") != std::string::npos ||
        name.find("<iframe") != std::string::npos) {
        return true;   // HTML 片段注入
    }
    for (unsigned char ch : name) {
        if (ch < 0x20 && ch != '\t') return true;   // 控制字符
    }
    return false;
}

}   // namespace jzip
