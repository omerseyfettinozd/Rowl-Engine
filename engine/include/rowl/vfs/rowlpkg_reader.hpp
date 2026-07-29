#pragma once

#include "rowl/vfs/vfs.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <memory>

namespace Rowl::VFS {

#pragma pack(push, 1)
struct RowlPkgHeader {
    char magic[4];          // "ROWL"
    uint16_t specVersion;   // Version e.g. 1
    uint32_t fileCount;     // Number of entries in archive
    uint64_t indexOffset;    // File offset to start of index table
};

struct RowlPkgEntryRaw {
    uint64_t pathHash;
    uint32_t pathLength;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    uint32_t flags;         // 0 = Raw, 1 = Zstd
};
#pragma pack(pop)

struct PackageEntry {
    std::string relativePath;
    uint64_t offset;
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    uint32_t flags;
};

class RowlPkgDataSource : public IDataSource {
public:
    explicit RowlPkgDataSource(std::string pkgFilepath);
    ~RowlPkgDataSource() override;

    bool exists(const std::string& path) override;
    std::vector<uint8_t> read(const std::string& path) override;
    std::string getSourceName() const override { return "RowlPkgDataSource [" + m_filepath + "]"; }

    bool isValid() const { return m_isValid; }

private:
    bool loadIndexTable();

    std::string m_filepath;
    mutable std::ifstream m_fileStream;
    std::unordered_map<std::string, PackageEntry> m_indexTable;
    bool m_isValid = false;
};

} // namespace Rowl::VFS
