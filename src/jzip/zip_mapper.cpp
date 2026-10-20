#include "zip_mapper.hpp"

#include <string>

using namespace ca;

namespace jzip {

// 恒等映射：原样返回
ZipPart* IdentityZipPartMapper::map(ZipArchive& archive, ZipPart* part) const
{
    (void)archive;
    return part;
}

// 委派式映射：转发给内部映射器
ZipPart* DelegatingZipPartMapper::map(ZipArchive& archive, ZipPart* part) const
{
    if (delegate_) {
        return delegate_->map(archive, part);
    }
    return part;
}

// 用中央目录的值校正 LFH（按偏移或文件名匹配）
// 先执行委派映射，再对 LocalFileHeader 做 CEN 值采纳
ZipPart* CentralAdoptingMapper::map(ZipArchive& archive, ZipPart* part) const
{
    // Step 1: 先委派给内部映射器
    if (delegate_) {
        part = delegate_->map(archive, part);
        if (part == nullptr) return nullptr;
    }

    // Step 2: 只对 LocalFileHeader 做 CEN 采纳
    if (part->type() != PartType::LOCAL_FILE_HEADER) return part;
    auto* local = static_cast<LocalFileHeader*>(part);

    CentralDirectoryFileHeader* matched = nullptr;
    for (auto& item : archive.getParts()) {
        if (!item) continue;
        if (item->type() != PartType::CENTRAL_DIRECTORY_FILE_HEADER) continue;
        auto* central = static_cast<CentralDirectoryFileHeader*>(item.get());
        if (central->getRelativeOffsetOfLocalHeader() == static_cast<i64>(local->offset())) {
            matched = central;
            break;
        }
        if (central->getFileName() == local->getFileName()) {
            matched = central;
        }
    }

    if (matched == nullptr) {
        return part;
    }

    local->link(matched);
    matched->link(local);
    local->adoptLinkedCentralDirectoryValues();
    return part;
}

// 修复 JAR 中的 .class/ 结尾目录名（去掉末尾斜杠前的字符）
// 先执行委派映射
ZipPart* JvmClassDirectoryMapper::map(ZipArchive& archive, ZipPart* part) const
{
    // 先委派给内部映射器
    if (delegate_) {
        part = delegate_->map(archive, part);
        if (part == nullptr) return nullptr;
    }

    auto normalize = [](std::string& name) {
        static const std::string kSuffix = ".class/";
        if (name.size() >= kSuffix.size() &&
            name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0) {
            name.erase(name.size() - 1);
        }
    };

    if (part->type() == PartType::LOCAL_FILE_HEADER) {
        auto*       local = static_cast<LocalFileHeader*>(part);
        std::string name  = local->getFileName();
        normalize(name);
        local->setFileName(name);
        return part;
    }

    if (part->type() == PartType::CENTRAL_DIRECTORY_FILE_HEADER) {
        auto*       central = static_cast<CentralDirectoryFileHeader*>(part);
        std::string name    = central->getFileName();
        normalize(name);
        central->setFileName(name);
        return part;
    }

    return part;
}

// 复制映射：返回独立深拷贝；原部件由 mapArchive 在收尾时释放
ZipPart* CopyingZipPartMapper::map(ZipArchive& archive, ZipPart* part) const
{
    (void)archive;
    switch (part->type()) {
    case PartType::LOCAL_FILE_HEADER:
        return static_cast<LocalFileHeader*>(part)->copy().release();
    case PartType::CENTRAL_DIRECTORY_FILE_HEADER:
        return static_cast<CentralDirectoryFileHeader*>(part)->copy().release();
    case PartType::END_OF_CENTRAL_DIRECTORY:
        return static_cast<EndOfCentralDirectory*>(part)->copy().release();
    }
    return nullptr;
}

}   // namespace jzip
