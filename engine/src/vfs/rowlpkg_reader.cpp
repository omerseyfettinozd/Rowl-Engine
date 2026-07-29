#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include <zstd.h>
#include <cstring>

namespace Rowl::VFS {

RowlPkgDataSource::RowlPkgDataSource(std::string pkgFilepath)
    : m_filepath(std::move(pkgFilepath)) {
    m_fileStream.open(m_filepath, std::ios::binary);
    if (!m_fileStream.is_open()) {
        ROWL_LOG_WARN("Failed to open package archive: " + m_filepath);
        return;
    }

    m_isValid = loadIndexTable();
}

RowlPkgDataSource::~RowlPkgDataSource() {
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
}

bool RowlPkgDataSource::loadIndexTable() {
    m_fileStream.seekg(0, std::ios::beg);

    RowlPkgHeader header;
    m_fileStream.read(reinterpret_cast<char*>(&header), sizeof(RowlPkgHeader));

    if (m_fileStream.gcount() < static_cast<std::streamsize>(sizeof(RowlPkgHeader))) {
        ROWL_LOG_ERROR("Package header read failed: " + m_filepath);
        return false;
    }

    if (std::memcmp(header.magic, "ROWL", 4) != 0) {
        ROWL_LOG_ERROR("Invalid package magic cookie in file: " + m_filepath);
        return false;
    }

    // Validate header values
    if (header.fileCount > 1000000) {  // Sanity check
        ROWL_LOG_ERROR("Package file count too large: " + std::to_string(header.fileCount));
        return false;
    }
    
    if (header.indexOffset > 1000000000ULL) {  // Sanity check
        ROWL_LOG_ERROR("Package index offset suspiciously large: " + std::to_string(header.indexOffset));
        return false;
    }

    // Seek to index table offset
    m_fileStream.seekg(header.indexOffset, std::ios::beg);
    if (!m_fileStream.good()) {
        ROWL_LOG_ERROR("Failed to seek to index offset in package: " + m_filepath);
        return false;
    }

    for (uint32_t i = 0; i < header.fileCount; ++i) {
        RowlPkgEntryRaw rawEntry;
        m_fileStream.read(reinterpret_cast<char*>(&rawEntry), sizeof(RowlPkgEntryRaw));

        if (!m_fileStream.good()) {
            ROWL_LOG_ERROR("Failed to read package entry: " + std::to_string(i));
            return false;
        }

        if (rawEntry.pathLength == 0 || rawEntry.pathLength > 4096) {
            ROWL_LOG_ERROR("Invalid path length in package entry: " + std::to_string(rawEntry.pathLength));
            return false;
        }

        std::string relPath(rawEntry.pathLength, '\0');
        m_fileStream.read(&relPath[0], rawEntry.pathLength);

        if (!m_fileStream.good() || m_fileStream.gcount() < static_cast<std::streamsize>(rawEntry.pathLength)) {
            ROWL_LOG_ERROR("Failed to read path data for package entry");
            return false;
        }

        // Validate sizes
        if (rawEntry.compressedSize > 1000000000ULL || rawEntry.uncompressedSize > 1000000000ULL) {
            ROWL_LOG_ERROR("Package entry size too large, possible corruption");
            return false;
        }

        PackageEntry entry;
        entry.relativePath = relPath;
        entry.offset = rawEntry.offset;
        entry.compressedSize = rawEntry.compressedSize;
        entry.uncompressedSize = rawEntry.uncompressedSize;
        entry.flags = rawEntry.flags;

        m_indexTable[relPath] = entry;
    }

    ROWL_LOG_INFO("Successfully loaded package index from '" + m_filepath + "' (" + std::to_string(header.fileCount) + " files)");
    return true;
}

bool RowlPkgDataSource::exists(const std::string& path) {
    if (!m_isValid) return false;
    return m_indexTable.find(path) != m_indexTable.end();
}

std::vector<uint8_t> RowlPkgDataSource::read(const std::string& path) {
    if (!m_isValid) return {};

    auto it = m_indexTable.find(path);
    if (it == m_indexTable.end()) {
        return {};
    }

    const auto& entry = it->second;
    m_fileStream.seekg(entry.offset, std::ios::beg);

    if (!m_fileStream.good()) {
        ROWL_LOG_ERROR("Failed to seek to entry offset in package: " + path);
        return {};
    }

    std::vector<uint8_t> compressedBuffer(entry.compressedSize);
    m_fileStream.read(reinterpret_cast<char*>(compressedBuffer.data()), entry.compressedSize);

    if (!m_fileStream.good()) {
        ROWL_LOG_ERROR("Failed to read compressed data for: " + path);
        return {};
    }

    if (entry.flags == 0) {
        // Raw uncompressed file data
        return compressedBuffer;
    } else if (entry.flags == 1) {
        // Zstd compressed chunk
        std::vector<uint8_t> decompressedBuffer(entry.uncompressedSize);
        size_t result = ZSTD_decompress(
            decompressedBuffer.data(), entry.uncompressedSize,
            compressedBuffer.data(), entry.compressedSize
        );

        if (ZSTD_isError(result)) {
            ROWL_LOG_ERROR("Zstd decompression failed for asset '" + path + "': " + std::string(ZSTD_getErrorName(result)));
            return {};
        }

        return decompressedBuffer;
    }

    ROWL_LOG_WARN("Unsupported compression flag for asset: " + path);
    return {};
}

} // namespace Rowl::VFS