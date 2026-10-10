#include "test_zip_fixtures.hpp"

#include <string>
#include <vector>

using namespace jzip;

namespace jzip::test {
namespace {

constexpr ca::u32   kLfhSig       = 0x04034b50u;
constexpr ca::u32   kCenSig       = 0x02014b50u;
constexpr ca::u32   kEocdSig      = 0x06054b50u;
constexpr ca::u32   kDdSig        = 0x08074b50u;
constexpr ca::usize kLfhFixedSize = 30;
constexpr ca::usize kCenFixedSize = 46;
constexpr ca::usize kEocdSize     = 22;

void build_lfh(std::vector<ca::u8>& out, ca::u16 flags, ca::u16 comp, ca::u32 crc, ca::u32 csize,
               ca::u32 usize, const std::string& name, const std::vector<ca::u8>& extra)
{
    append_u32_le(out, kLfhSig);
    append_u16_le(out, 20);
    append_u16_le(out, flags);
    append_u16_le(out, comp);
    append_u16_le(out, 0);
    append_u16_le(out, 0);
    append_u32_le(out, crc);
    append_u32_le(out, csize);
    append_u32_le(out, usize);
    append_u16_le(out, static_cast<ca::u16>(name.size()));
    append_u16_le(out, static_cast<ca::u16>(extra.size()));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), extra.begin(), extra.end());
}

void build_cen(std::vector<ca::u8>& out, ca::u16 flags, ca::u16 comp, ca::u32 crc, ca::u32 csize,
               ca::u32 usize, const std::string& name, const std::vector<ca::u8>& extra,
               ca::u32 local_offset)
{
    append_u32_le(out, kCenSig);
    append_u16_le(out, 20);
    append_u16_le(out, 20);
    append_u16_le(out, flags);
    append_u16_le(out, comp);
    append_u16_le(out, 0);
    append_u16_le(out, 0);
    append_u32_le(out, crc);
    append_u32_le(out, csize);
    append_u32_le(out, usize);
    append_u16_le(out, static_cast<ca::u16>(name.size()));
    append_u16_le(out, static_cast<ca::u16>(extra.size()));
    append_u16_le(out, 0);
    append_u16_le(out, 0);
    append_u16_le(out, 0);
    append_u32_le(out, 0);
    append_u32_le(out, local_offset);
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), extra.begin(), extra.end());
}

void build_eocd(std::vector<ca::u8>& out, ca::u16 num_entries, ca::u32 cd_size, ca::u32 cd_offset)
{
    append_u32_le(out, kEocdSig);
    append_u16_le(out, 0);
    append_u16_le(out, 0);
    append_u16_le(out, num_entries);
    append_u16_le(out, num_entries);
    append_u32_le(out, cd_size);
    append_u32_le(out, cd_offset);
    append_u16_le(out, 0);
}

std::vector<ca::u8> build_zip_with(ca::u16 lfh_flags, ca::u16 lfh_comp, ca::u32 lfh_crc,
                                   ca::u32 lfh_csize, ca::u32 lfh_usize, const std::string& name,
                                   const std::vector<ca::u8>& extra,
                                   const std::vector<ca::u8>& data, ca::u16 cen_flags,
                                   ca::u16 cen_comp, ca::u32 cen_crc, ca::u32 cen_csize,
                                   ca::u32 cen_usize)
{
    std::vector<ca::u8> out;
    build_lfh(out, lfh_flags, lfh_comp, lfh_crc, lfh_csize, lfh_usize, name, extra);
    const ca::u32 lfh_size = static_cast<ca::u32>(out.size());
    out.insert(out.end(), data.begin(), data.end());
    const ca::u32 local_offset = 0;
    const ca::u32 cd_offset    = static_cast<ca::u32>(out.size());
    build_cen(out, cen_flags, cen_comp, cen_crc, cen_csize, cen_usize, name, extra, local_offset);
    const ca::u32 cd_size = static_cast<ca::u32>(out.size()) - cd_offset;
    build_eocd(out, 1, cd_size, cd_offset);
    return out;
}

}   // namespace

void append_u16_le(std::vector<ca::u8>& out, ca::u16 value)
{
    out.push_back(static_cast<ca::u8>(value & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 8) & 0xFFu));
}

void append_u32_le(std::vector<ca::u8>& out, ca::u32 value)
{
    out.push_back(static_cast<ca::u8>(value & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 24) & 0xFFu));
}

void append_u64_le(std::vector<ca::u8>& out, ca::u64 value)
{
    out.push_back(static_cast<ca::u8>(value & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 24) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 32) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 40) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 48) & 0xFFu));
    out.push_back(static_cast<ca::u8>((value >> 56) & 0xFFu));
}

std::vector<ca::u8> build_minimal_single_entry_zip()
{
    std::vector<ca::u8> bytes;

    const std::string name = "a.txt";

    append_u32_le(bytes, 0x02014b50u);
    append_u16_le(bytes, 20);
    append_u16_le(bytes, 20);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u32_le(bytes, 0x01020304u);
    append_u32_le(bytes, 3);
    append_u32_le(bytes, 3);
    append_u16_le(bytes, static_cast<ca::u16>(name.size()));
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u32_le(bytes, 0);
    append_u32_le(bytes, 0);
    bytes.insert(bytes.end(), name.begin(), name.end());

    const ca::u32 central_size   = static_cast<ca::u32>(bytes.size());
    const ca::u32 central_offset = 0;

    append_u32_le(bytes, 0x06054b50u);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 0);
    append_u16_le(bytes, 1);
    append_u16_le(bytes, 1);
    append_u32_le(bytes, central_size);
    append_u32_le(bytes, central_offset);
    append_u16_le(bytes, 0);

    return bytes;
}

std::vector<ca::u8> build_valid_single_entry_zip()
{
    const std::vector<ca::u8> data = {'a', 'b', 'c'};
    return build_zip_with(0,
                          0,
                          0,
                          static_cast<ca::u32>(data.size()),
                          static_cast<ca::u32>(data.size()),
                          "a.txt",
                          {},
                          data,
                          0,
                          0,
                          0,
                          static_cast<ca::u32>(data.size()),
                          static_cast<ca::u32>(data.size()));
}

std::vector<ca::u8> build_mismatch_crc_zip()
{
    const std::vector<ca::u8> data  = {'a', 'b', 'c'};
    const ca::u32             usize = static_cast<ca::u32>(data.size());
    return build_zip_with(
        0, 0, 0xDEADBEEFu, usize, usize, "a.txt", {}, data, 0, 0, 0xCAFEBABEu, usize, usize);
}

std::vector<ca::u8> build_mismatch_compression_zip()
{
    const std::vector<ca::u8> data  = {'a', 'b', 'c'};
    const ca::u32             usize = static_cast<ca::u32>(data.size());
    return build_zip_with(0, 0, 0, usize, usize, "a.txt", {}, data, 0, 8, 0, usize, usize);
}

std::vector<ca::u8> build_zip_no_eocd()
{
    std::vector<ca::u8> out;
    build_lfh(out, 0, 0, 0, 3, 3, "a.txt", {});
    const std::vector<ca::u8> data = {'a', 'b', 'c'};
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

std::vector<ca::u8> build_deflated_entry_zip()
{
    // Raw deflate stored block for "hello" (5 bytes)
    // Format: BFINAL=1, BTYPE=00 (stored), LEN=5, ~LEN=0xFFFA, then literal bytes
    const std::vector<ca::u8> compressed = {0x01, 0x05, 0x00, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};
    return build_zip_with(0,
                          8,
                          0,
                          static_cast<ca::u32>(compressed.size()),
                          5,
                          "a.txt",
                          {},
                          compressed,
                          0,
                          8,
                          0,
                          static_cast<ca::u32>(compressed.size()),
                          5);
}

static ca::u64 unix_ms_to_filetime(ca::u64 unix_ms)
{
    constexpr ca::u64 kEpochDiff = 11644473600000ull;
    return (unix_ms + kEpochDiff) * 10000ull;
}

std::vector<ca::u8> build_extra_field_zip()
{
    // NTFS extra field (tag 0x000A)
    std::vector<ca::u8> ntfsextra;
    append_u16_le(ntfsextra, 0x000Au);   // tag
    append_u16_le(ntfsextra, 32);        // total data size after tag+size
    // reserved (4 bytes)
    append_u32_le(ntfsextra, 0);
    // time attribute
    append_u16_le(ntfsextra, 0x0001u);                                 // attr tag = time
    append_u16_le(ntfsextra, 24);                                      // attr size = 3 * 8
    append_u64_le(ntfsextra, unix_ms_to_filetime(1000000000000ull));   // creation
    append_u64_le(ntfsextra, unix_ms_to_filetime(1200000000000ull));   // modify
    append_u64_le(ntfsextra, unix_ms_to_filetime(1400000000000ull));   // access

    const std::vector<ca::u8> data = {'a', 'b', 'c'};
    return build_zip_with(0,
                          0,
                          0,
                          static_cast<ca::u32>(data.size()),
                          static_cast<ca::u32>(data.size()),
                          "a.txt",
                          ntfsextra,
                          data,
                          0,
                          0,
                          0,
                          static_cast<ca::u32>(data.size()),
                          static_cast<ca::u32>(data.size()));
}

std::vector<ca::u8> build_data_descriptor_zip()
{
    const std::vector<ca::u8> data     = {'a', 'b', 'c'};
    const ca::u32             usize    = static_cast<ca::u32>(data.size());
    const ca::u16             dd_flags = 0x0008u;   // bit 3 = data descriptor

    std::vector<ca::u8> out;
    // LFH with bit 3 set and zeroed crc/sizes
    build_lfh(out, dd_flags, 0, 0, 0, 0, "a.txt", {});
    const ca::u32 lfh_end = static_cast<ca::u32>(out.size());
    out.insert(out.end(), data.begin(), data.end());
    const ca::u32 data_end = static_cast<ca::u32>(out.size());

    // Data descriptor (16-byte with signature)
    append_u32_le(out, kDdSig);
    append_u32_le(out, 0);   // crc32 (not checked by test)
    append_u32_le(out, usize);
    append_u32_le(out, usize);

    // CEN: bit 3 set, actual crc=0, csize=data size (excluding DD)
    const ca::u32 cd_offset = static_cast<ca::u32>(out.size());
    build_cen(out, dd_flags, 0, 0, usize, usize, "a.txt", {}, 0);
    const ca::u32 cd_size = static_cast<ca::u32>(out.size()) - cd_offset;
    build_eocd(out, 1, cd_size, cd_offset);
    return out;
}

std::vector<ca::u8> build_n_to_one_cen_to_loc_zip()
{
    const std::vector<ca::u8> data = {'a', 'b', 'c'};
    const ca::u32             size = static_cast<ca::u32>(data.size());

    std::vector<ca::u8> out;
    build_lfh(out, 0, 0, 0, size, size, "first.txt", {});
    out.insert(out.end(), data.begin(), data.end());

    const ca::u32 cd_offset = static_cast<ca::u32>(out.size());
    build_cen(out, 0, 0, 0, size, size, "first.txt", {}, 0);
    build_cen(out, 0, 0, 0, size, size, "second.txt", {}, 0);
    build_cen(out, 0, 0, 0, size, size, "third.txt", {}, 0);
    const ca::u32 cd_size = static_cast<ca::u32>(out.size()) - cd_offset;
    build_eocd(out, 3, cd_size, cd_offset);
    return out;
}

}   // namespace jzip::test
