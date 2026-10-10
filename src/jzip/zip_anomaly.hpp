#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "libca/core/datatype.hpp"

namespace jzip {

/// @brief ZIP 结构异常类型。
///
/// reader 的容错语义（跳过 / 收养 / 回退）保持不变；这些事件只做记录，
/// 供上层工具生成诊断报告，不改变解析行为。
enum class ZipAnomalyKind
{
    PrefixJunkDetected,        ///< 条目前存在非 ZIP 前缀数据
    EocdOffsetMismatch,        ///< EOCD 声明的 CEN 偏移与推导值不一致
    BaseOffsetFallback,        ///< baseOffset 映射失败后启用回退扫描并改写
    LocalHeaderSignatureMiss,  ///< CEN 相对偏移处无 LOC 签名
    LocalHeaderReadFailed,     ///< LOC 解析异常（由 CEN 收养修复）
    SizesAdoptedFromCen,       ///< LOC 与 CEN 尺寸不一致，按 CEN 收养
    DuplicateLocalOffset,      ///< 多个 CEN 指向同一 LOC 偏移
    RejectedEocdCandidate,     ///< 反向扫描中拒绝的虚假 EOCD 候选
    TrailingGarbageAfterEocd,  ///< EOCD 之后仍有残余数据
    SuspiciousEntryName        ///< 可疑条目名（NUL/HTML 片段/路径穿越）
};

/// @brief 单条结构异常记录。
struct ZipAnomaly
{
    ZipAnomalyKind kind      = ZipAnomalyKind::PrefixJunkDetected; ///< 异常类型
    ca::u64        offset    = 0;                                 ///< 相关偏移
    std::string    entry;                                         ///< 关联条目名（可为空）
    std::string    detail;                                        ///< 附加说明
};

/// @brief 一次读取聚合的结构异常报告。
class ZipReadReport
{
public:
    /// @brief 记录一条异常。
    void add(ZipAnomaly anomaly);

    /// @brief 获取全部异常（按发生顺序）。
    const std::vector<ZipAnomaly>& anomalies() const;

    /// @brief 按类型计数。
    ca::usize countOf(ZipAnomalyKind kind) const;

    /// @brief 是否无任何异常。
    bool empty() const;

private:
    std::vector<ZipAnomaly> anomalies_;
};

/// @brief 异常事件 sink（推模式）。
///
/// 通过 ZipIO::readXxx 的可选参数或 ZipReader::setAnomalySink 注入；
/// 未注入时仅拉模式的归档内报告生效，零额外开销。
class ZipAnomalySink
{
public:
    /// @brief 析构函数。
    virtual ~ZipAnomalySink() = default;

    /// @brief 收到一条异常事件。
    /// @param anomaly 异常记录
    virtual void onAnomaly(const ZipAnomaly& anomaly) = 0;
};

/// @brief 向归档报告与可选 sink 同时记录异常（reader 内部使用）。
/// @param report 归档持有的报告
/// @param sink 可选 sink（可为空）
/// @param kind 异常类型
/// @param offset 相关偏移
/// @param entry 关联条目名
/// @param detail 附加说明
void emitAnomaly(ZipReadReport& report, ZipAnomalySink* sink, ZipAnomalyKind kind, ca::u64 offset,
                 std::string entry, std::string detail);

/// @brief 判断条目名是否可疑（NUL 开头 / HTML 片段 / 路径穿越 / 控制字符）。
/// @param name 条目名
/// @return true 表示可疑
bool isSuspiciousEntryName(const std::string& name);

}   // namespace jzip
