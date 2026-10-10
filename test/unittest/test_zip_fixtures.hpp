#pragma once

#include <string>
#include <vector>

#include "libca/core/datatype.hpp"

namespace jzip::test {

void append_u16_le(std::vector<ca::u8>& out, ca::u16 value);
void append_u32_le(std::vector<ca::u8>& out, ca::u32 value);
void append_u64_le(std::vector<ca::u8>& out, ca::u64 value);

std::vector<ca::u8> build_minimal_single_entry_zip();

// Build a valid ZIP with one stored file entry "a.txt" containing "abc"
std::vector<ca::u8> build_valid_single_entry_zip();

// Build a ZIP where local CRC (0xDEADBEEF) differs from central CRC (0xCAFEBABE)
std::vector<ca::u8> build_mismatch_crc_zip();

// Build a ZIP where local compression (0=stored) differs from central (8=deflated)
std::vector<ca::u8> build_mismatch_compression_zip();

// Build a ZIP with no EOCD record (invalid-no-end equivalent)
std::vector<ca::u8> build_zip_no_eocd();

// Build a ZIP with a deflated entry (method 8, raw deflate "hello")
std::vector<ca::u8> build_deflated_entry_zip();

// Build a ZIP with an NTFS extra field
std::vector<ca::u8> build_extra_field_zip();

// Build a ZIP with data descriptor (bit 3 set)
std::vector<ca::u8> build_data_descriptor_zip();

// Build a ZIP where three CEN entries all point to the same LOC record.
std::vector<ca::u8> build_n_to_one_cen_to_loc_zip();

}   // namespace jzip::test
