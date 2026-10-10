#pragma once
#include <optional>
#include "zip_model.hpp"

namespace jzip {

struct Zip64EndInfo
{
    ca::usize recordOffset              = 0;
    ca::u64   locatorStartDisk          = 0;
    ca::u64   totalDisks                = 0;
    ca::u64   diskNumber                = 0;
    ca::u64   centralDirectoryStartDisk = 0;
    ca::u64   entriesOnThisDisk         = 0;
    ca::u64   entryCount                = 0;
    ca::u64   centralDirectorySize      = 0;
    ca::u64   centralDirectoryOffset    = 0;
};

struct ResolvedEnd
{
    ca::usize                   centralDirectoryStart = 0;
    std::ptrdiff_t              baseOffset            = 0;
    ca::usize                   centralDirectoryEnd   = 0;
    std::optional<Zip64EndInfo> zip64Info;
};

class Zip64Support
{
public:
    static ResolvedEnd resolveEndOfCentralDirectory(const MemorySegment&   data,
                                                    EndOfCentralDirectory& end);

    static std::optional<Zip64EndInfo> tryReadZip64EndInfo(const MemorySegment& data,
                                                           ca::usize            endOffset);

    /// Resolve the local header offset from a CEN entry, handling the
    /// Zip64 extra field (tag 0x0001) when getRelativeOffsetOfLocalHeader()
    /// is 0xFFFFFFFF.
    static ca::u64 resolveLocalHeaderOffset(const CentralDirectoryFileHeader& cen);

    /// Resolve compressed size from a CEN entry, handling the Zip64
    /// extra field when getCompressedSize() is 0xFFFFFFFF.
    static ca::u64 resolveCompressedSize(const CentralDirectoryFileHeader& cen);

    /// Resolve uncompressed size from a CEN entry, handling the Zip64
    /// extra field when getUncompressedSize() is 0xFFFFFFFF.
    static ca::u64 resolveUncompressedSize(const CentralDirectoryFileHeader& cen);

    /// Resolve compressed/uncompressed sizes in a LocalFileHeader that
    /// has Zip64 magic values (0xFFFFFFFF), reading the real values from
    /// the local Zip64 extra field (tag 0x0001).
    static void resolveLocalFileHeaderSizes(LocalFileHeader& local);
};

}   // namespace jzip
