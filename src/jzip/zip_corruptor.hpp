#pragma once

#include <functional>
#include <string>
#include <vector>

#include "libca/core/datatype.hpp"

namespace jzip {

/// @brief ZIP 破坏注入器。
///
/// 从干净归档制造已知损坏形态，用于结构异常侦测（zip_anomaly）的回归验证
/// 与下游容错测试。与真实 crasher 样本对齐的形态包括：前缀垃圾 +
/// 陈旧 cdOffset（OS3.0.0H 形态）、虚假 EOCD、CEN→LOC 映射断裂、
/// LOC/CEN 尺寸不一致、可疑条目名等。
///
/// 两级操作：
/// - 模型级（renameEntry / shrinkLocalSizes）：作用于解析后的归档模型，
///   在序列化前生效；
/// - 字节级（其余）：在序列化结果上按注册顺序依次应用。
class ZipCorruptor
{
public:
    /// @brief 从干净归档字节构造。
    /// @param cleanBytes 干净归档（须可被 readJvm 解析）
    explicit ZipCorruptor(std::vector<ca::u8> cleanBytes);

    // ── 模型级 ────────────────────────────────────────────────

    /// @brief 重命名第 index 个条目（LOC 与关联 CEN 同步改名）。
    /// @param index 条目序号（按文件偏移序）
    /// @param name 新名称
    ZipCorruptor& renameEntry(ca::usize index, const std::string& name);

    /// @brief 把第 index 个条目的 LOC 尺寸字段改小，制造 LOC/CEN 尺寸不一致。
    /// @param index 条目序号
    ZipCorruptor& shrinkLocalSizes(ca::usize index);

    // ── 字节级 ────────────────────────────────────────────────

    /// @brief 前置垃圾数据（默认 CAFEBABE 魔数平铺），并保持 EOCD 的 cdOffset
    /// 声明值为加垃圾前的旧值——即"陈旧 cdOffset"，OS3.0.0H crasher 形态。
    /// @param size 垃圾字节数
    /// @param magic 垃圾头部魔数（平铺填充）
    ZipCorruptor& prependJunk(ca::usize size, const std::string& magic = "\xCA\xFE\xBA\xBE");

    /// @brief 把 EOCD 声明的 cdOffset 改为任意假值。
    /// @param fakeOffset 假偏移
    ZipCorruptor& lieAboutCdOffset(ca::u32 fakeOffset);

    /// @brief 把第 index 个 CEN 条目的相对偏移改指到 CEN 块自身，
    /// 制造 CEN→LOC 签名映射失败。
    /// @param index 条目序号
    ZipCorruptor& breakEntryLocalOffset(ca::usize index);

    /// @brief 让第 fromIndex 个 CEN 复用第 toIndex 个条目的 LOC 偏移，
    /// 制造重复 LOC 偏移。
    /// @param fromIndex 被改写的条目序号
    /// @param toIndex 被复用的条目序号
    ZipCorruptor& aliasEntryLocalOffset(ca::usize fromIndex, ca::usize toIndex);

    /// @brief 在文件尾追加垃圾数据（无虚假 EOCD）。
    /// @param size 垃圾字节数
    ZipCorruptor& appendTrailingGarbage(ca::usize size);

    /// @brief 在文件尾追加垃圾并以一个无法通过验证的虚假 EOCD 收尾，
    /// 触发反向扫描的候选拒绝路径。
    /// @param size 垃圾字节数（不含虚假 EOCD）
    ZipCorruptor& appendGarbageWithFakeEocd(ca::usize size);

    /// @brief 序列化并应用全部字节级操作，产出损坏归档。
    std::vector<ca::u8> build();

private:
    std::vector<ca::u8> cleanBytes_;
    ca::usize           modelOpsPending_ = 0;
    // 字节级操作管道：按注册顺序应用
    std::vector<std::function<std::vector<ca::u8>(std::vector<ca::u8>)>> byteOps_;
};

}   // namespace jzip
