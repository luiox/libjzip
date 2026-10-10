#pragma once

/*
 * 1:1 mapping to LL-Java-Zip test: Utils.java
 * - ZIP64 archive generation utilities
 * - Binary LE writing helpers
 * - CRC32 computation (via zlib)
 *
 * Used by Zip64Tests (DISABLED, pending Zip64Support).
 */

#include <cstdint>
#include <string>
#include <vector>

#include <zlib.h>

#include "jzip/memory_segment.hpp"

namespace jzip::test_utils {

// ── ThrowingConsumer (mirrors Java Utils.ThrowingConsumer<T>) ───

template<typename T>
using ThrowingConsumer = void (*)(const T&);

// ── Binary LE writing helpers ────────────────────────────────────

inline void write_u16_le(std::vector<ca::u8>& out, ca::u16 value)
{
    out.push_back(static_cast<ca::u8>(value & 0xFF));
    out.push_back(static_cast<ca::u8>((value >> 8) & 0xFF));
}

inline void write_u32_le(std::vector<ca::u8>& out, ca::u32 value)
{
    out.push_back(static_cast<ca::u8>(value & 0xFF));
    out.push_back(static_cast<ca::u8>((value >> 8) & 0xFF));
    out.push_back(static_cast<ca::u8>((value >> 16) & 0xFF));
    out.push_back(static_cast<ca::u8>((value >> 24) & 0xFF));
}

inline void write_u64_le(std::vector<ca::u8>& out, ca::u64 value)
{
    write_u32_le(out, static_cast<ca::u32>(value & 0xFFFFFFFF));
    write_u32_le(out, static_cast<ca::u32>((value >> 32) & 0xFFFFFFFF));
}

// ── CRC32 helper ─────────────────────────────────────────────────

inline ca::u32 computeCrc32(const std::vector<ca::u8>& data)
{
    return ::crc32(0L, data.data(), static_cast<uInt>(data.size()));
}

// ── Entry spec (mirrors Java Utils.EntrySpec / EntryMeta) ────────

struct EntrySpec
{
    std::string         name;
    std::vector<ca::u8> data;
    bool                useDataDescriptor          = false;
    bool                includeDescriptorSignature = false;
};

struct EntryMeta
{
    EntrySpec           entry;
    std::vector<ca::u8> name;
    ca::u64             localOffset;
    ca::u32             crc32;
};

// ── ZIP64 Extra Field builders ────────────────────────────────────

inline std::vector<ca::u8> localZip64Extra(ca::u64 size)
{
    std::vector<ca::u8> extra;
    write_u16_le(extra, 0x0001);   // tag
    write_u16_le(extra, 16);       // data size (8 + 8)
    write_u64_le(extra, size);     // uncompressed size
    write_u64_le(extra, size);     // compressed size
    return extra;
}

inline std::vector<ca::u8> centralZip64Extra(ca::u64 size, ca::u64 localOffset)
{
    std::vector<ca::u8> extra;
    write_u16_le(extra, 0x0001);   // tag
    write_u16_le(extra, 24);       // data size (8 + 8 + 8)
    write_u64_le(extra, size);     // uncompressed size
    write_u64_le(extra, size);     // compressed size
    write_u64_le(extra, localOffset);
    return extra;
}

// ── Core archive builder ──────────────────────────────────────────

inline std::vector<ca::u8> createZip64Archive(const std::vector<EntrySpec>& entries,
                                              bool                          splitArchive)
{

    constexpr ca::u16 kVersionZip64   = 45;
    constexpr ca::u16 kGpFlagDataDesc = 0x0008;
    constexpr ca::u32 kZip64Magic     = 0xFFFFFFFFu;

    std::vector<ca::u8>    localOut;
    std::vector<EntryMeta> metas;

    // ── Write local file entries ──────────────────────────────────
    for (const auto& entry : entries) {
        ca::u64 localOffset = localOut.size();
        ca::u32 crc         = computeCrc32(entry.data);
        auto    nameBytes   = std::vector<ca::u8>(entry.name.begin(), entry.name.end());
        auto    localExtra  = localZip64Extra(entry.data.size());
        ca::u16 gpFlag      = entry.useDataDescriptor ? kGpFlagDataDesc : 0;

        // Local file header
        write_u32_le(localOut, 0x04034b50u);
        write_u16_le(localOut, kVersionZip64);
        write_u16_le(localOut, gpFlag);
        write_u16_le(localOut, 0);   // compression method: stored
        write_u16_le(localOut, 0);   // last mod time
        write_u16_le(localOut, 0);   // last mod date
        write_u32_le(localOut, entry.useDataDescriptor ? 0u : crc);
        write_u32_le(localOut, kZip64Magic);   // compressed size (in extra field)
        write_u32_le(localOut, kZip64Magic);   // uncompressed size (in extra field)
        write_u16_le(localOut, static_cast<ca::u16>(nameBytes.size()));
        write_u16_le(localOut, static_cast<ca::u16>(localExtra.size()));
        localOut.insert(localOut.end(), nameBytes.begin(), nameBytes.end());
        localOut.insert(localOut.end(), localExtra.begin(), localExtra.end());
        localOut.insert(localOut.end(), entry.data.begin(), entry.data.end());

        // Optional data descriptor
        if (entry.useDataDescriptor) {
            if (entry.includeDescriptorSignature) write_u32_le(localOut, 0x08074b50u);
            write_u32_le(localOut, crc);
            write_u64_le(localOut, entry.data.size());
            write_u64_le(localOut, entry.data.size());
        }

        metas.push_back({entry, nameBytes, localOffset, crc});
    }

    // ── Write central directory entries ───────────────────────────
    ca::u64             centralDirOffset = localOut.size();
    std::vector<ca::u8> centralOut;

    for (const auto& meta : metas) {
        auto    centralExtra = centralZip64Extra(meta.entry.data.size(), meta.localOffset);
        ca::u16 gpFlag       = meta.entry.useDataDescriptor ? kGpFlagDataDesc : 0;

        write_u32_le(centralOut, 0x02014b50u);
        write_u16_le(centralOut, kVersionZip64);   // version made by
        write_u16_le(centralOut, kVersionZip64);   // version needed
        write_u16_le(centralOut, gpFlag);
        write_u16_le(centralOut, 0);   // compression method
        write_u16_le(centralOut, 0);   // last mod time
        write_u16_le(centralOut, 0);   // last mod date
        write_u32_le(centralOut, meta.crc32);
        write_u32_le(centralOut, kZip64Magic);   // compressed size
        write_u32_le(centralOut, kZip64Magic);   // uncompressed size
        write_u16_le(centralOut, static_cast<ca::u16>(meta.name.size()));
        write_u16_le(centralOut, static_cast<ca::u16>(centralExtra.size()));
        write_u16_le(centralOut, 0);             // comment length
        write_u16_le(centralOut, 0);             // disk start
        write_u16_le(centralOut, 0);             // internal attrs
        write_u32_le(centralOut, 0);             // external attrs
        write_u32_le(centralOut, kZip64Magic);   // local header offset
        centralOut.insert(centralOut.end(), meta.name.begin(), meta.name.end());
        centralOut.insert(centralOut.end(), centralExtra.begin(), centralExtra.end());
    }

    // ── Append central directory ──────────────────────────────────
    ca::u64 centralDirSize = centralOut.size();
    localOut.insert(localOut.end(), centralOut.begin(), centralOut.end());
    ca::u64 zip64EndOffset = localOut.size();

    // ── Zip64 End of Central Directory Record ─────────────────────
    write_u32_le(localOut, 0x06064b50u);
    write_u64_le(localOut, 44);              // size of record
    write_u16_le(localOut, kVersionZip64);   // version made by
    write_u16_le(localOut, kVersionZip64);   // version needed
    write_u32_le(localOut, 0);               // disk number
    write_u32_le(localOut, 0);               // central dir disk
    write_u64_le(localOut, metas.size());    // entries on disk
    write_u64_le(localOut, metas.size());    // total entries
    write_u64_le(localOut, centralDirSize);
    write_u64_le(localOut, centralDirOffset);

    // ── Zip64 End of Central Directory Locator ────────────────────
    write_u32_le(localOut, 0x07064b50u);
    write_u32_le(localOut, splitArchive ? 1u : 0u);
    write_u64_le(localOut, zip64EndOffset);
    write_u32_le(localOut, splitArchive ? 2u : 1u);   // total disks

    // ── End of Central Directory ──────────────────────────────────
    write_u32_le(localOut, 0x06054b50u);
    write_u16_le(localOut, static_cast<ca::u16>(splitArchive ? 1 : 0));
    write_u16_le(localOut, static_cast<ca::u16>(splitArchive ? 1 : 0));
    write_u16_le(localOut, 0xFFFF);        // entries on disk
    write_u16_le(localOut, 0xFFFF);        // total entries
    write_u32_le(localOut, kZip64Magic);   // central dir size
    write_u32_le(localOut, kZip64Magic);   // central dir offset
    write_u16_le(localOut, 0);             // comment length

    return localOut;
}

// ── Convenience archive builders ──────────────────────────────────

inline std::vector<ca::u8> zip64OffsetArchive()
{
    std::vector<EntrySpec> entries = {
        {"alpha.txt", {'a', 'l', 'p', 'h', 'a'}, false, false},
        {"beta.txt", {'s', 'e', 'c', 'o', 'n', 'd', '-', 'e', 'n', 't', 'r', 'y'}, false, false}};
    return createZip64Archive(entries, false);
}

inline std::vector<ca::u8> zip64DataDescriptorArchive(bool includeDescriptorSignature)
{
    std::vector<EntrySpec> entries = {
        {"descriptor.txt",
         {'d', 'e', 's', 'c', 'r', 'i', 'p', 't', 'o', 'r', '-', 'd', 'a', 't', 'a'},
         true,
         includeDescriptorSignature}};
    return createZip64Archive(entries, false);
}

inline std::vector<ca::u8> splitZip64Archive()
{
    std::vector<EntrySpec> entries = {
        {"split.txt",
         {'s', 'p', 'l', 'i', 't', '-', 'a', 'r', 'c', 'h', 'i', 'v', 'e'},
         false,
         false}};
    return createZip64Archive(entries, true);
}

}   // namespace jzip::test_utils
