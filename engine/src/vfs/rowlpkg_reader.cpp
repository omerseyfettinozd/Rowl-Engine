#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include <zstd.h>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <algorithm>
#include <optional>

namespace Rowl::VFS {

namespace {

constexpr uint16_t kSupportedPackageVersion = 1;
constexpr uint64_t kMaxPackageEntryBytes = 128ULL * 1024 * 1024;
constexpr uint32_t kMaxPackageFileCount = 100'000;
constexpr uint64_t kMaxCompressionExpansionRatio = 1'024;

std::optional<std::string> normalizePackagePath(std::string path) {
    if (path.empty() || path.find('\0') != std::string::npos) return std::nullopt;

    std::replace(path.begin(), path.end(), '\\', '/');
    const std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
    if (normalized.empty() || normalized.is_absolute() || normalized.has_root_name() ||
        normalized.has_root_directory()) {
        return std::nullopt;
    }

    const auto first = normalized.begin();
    if (first == normalized.end() || *first == "..") return std::nullopt;
    return normalized.generic_string();
}

} // namespace

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
    m_fileStream.seekg(0, std::ios::end);
    const auto archiveEnd = m_fileStream.tellg();
    if (archiveEnd < static_cast<std::streamoff>(sizeof(RowlPkgHeader))) {
        ROWL_LOG_ERROR("Package is smaller than its header: " + m_filepath);
        return false;
    }
    const auto archiveSize = static_cast<uint64_t>(archiveEnd);
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

    if (header.specVersion != kSupportedPackageVersion) {
        ROWL_LOG_ERROR("Unsupported package version: " + std::to_string(header.specVersion));
        return false;
    }

    // Validate header values
    if (header.fileCount > kMaxPackageFileCount) {
        ROWL_LOG_ERROR("Package file count too large: " + std::to_string(header.fileCount));
        return false;
    }

    if (header.indexOffset < sizeof(RowlPkgHeader) || header.indexOffset > archiveSize) {
        ROWL_LOG_ERROR("Package index offset suspiciously large: " + std::to_string(header.indexOffset));
        return false;
    }
    // Every entry needs its fixed record plus at least one path byte. Check
    // this before reserving vectors/maps from untrusted fileCount metadata.
    constexpr uint64_t kMinIndexEntryBytes = sizeof(RowlPkgEntryRaw) + 1;
    const uint64_t availableIndexBytes = archiveSize - header.indexOffset;
    if (header.fileCount > availableIndexBytes / kMinIndexEntryBytes) {
        ROWL_LOG_ERROR("Package file count cannot fit in the declared index: " + m_filepath);
        return false;
    }

    // Seek to index table offset
    m_fileStream.seekg(header.indexOffset, std::ios::beg);
    if (!m_fileStream.good()) {
        ROWL_LOG_ERROR("Failed to seek to index offset in package: " + m_filepath);
        return false;
    }

    std::vector<std::pair<uint64_t, uint64_t>> payloadRanges;
    payloadRanges.reserve(header.fileCount);
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

        const auto normalizedPath = normalizePackagePath(relPath);
        if (!normalizedPath) {
            ROWL_LOG_ERROR("Unsafe path in package entry: " + relPath);
            return false;
        }
        // pathHash is deliberately not used as an authority boundary. Older
        // package writers stored a 32-bit legacy hash here, while current
        // writers use FNV-1a 64. The canonical path is the lookup key, and
        // duplicate canonical paths are rejected below.

        // Validate sizes
        if (rawEntry.compressedSize > kMaxPackageEntryBytes ||
            rawEntry.uncompressedSize > kMaxPackageEntryBytes) {
            ROWL_LOG_ERROR("Package entry size too large, possible corruption");
            return false;
        }
        if (rawEntry.offset < sizeof(RowlPkgHeader) || rawEntry.offset > header.indexOffset ||
            rawEntry.compressedSize > header.indexOffset - rawEntry.offset) {
            ROWL_LOG_ERROR("Package entry points outside archive: " + relPath);
            return false;
        }
        if (rawEntry.flags > 1 ||
            (rawEntry.flags == 0 && rawEntry.compressedSize != rawEntry.uncompressedSize) ||
            (rawEntry.flags == 1 && (rawEntry.compressedSize == 0 || rawEntry.uncompressedSize == 0))) {
            ROWL_LOG_ERROR("Invalid compression metadata for package entry: " + relPath);
            return false;
        }
        if (rawEntry.flags == 1 &&
            rawEntry.uncompressedSize / rawEntry.compressedSize > kMaxCompressionExpansionRatio) {
            ROWL_LOG_ERROR("Package entry compression ratio is too large: " + relPath);
            return false;
        }

        PackageEntry entry;
        entry.relativePath = *normalizedPath;
        entry.offset = rawEntry.offset;
        entry.compressedSize = rawEntry.compressedSize;
        entry.uncompressedSize = rawEntry.uncompressedSize;
        entry.flags = rawEntry.flags;

        if (!m_indexTable.emplace(entry.relativePath, entry).second) {
            ROWL_LOG_ERROR("Duplicate package path: " + entry.relativePath);
            return false;
        }
        payloadRanges.emplace_back(entry.offset, entry.offset + entry.compressedSize);
    }

    std::sort(payloadRanges.begin(), payloadRanges.end());
    for (size_t i = 1; i < payloadRanges.size(); ++i) {
        if (payloadRanges[i].first < payloadRanges[i - 1].second) {
            ROWL_LOG_ERROR("Overlapping payload ranges in package: " + m_filepath);
            return false;
        }
    }

    ROWL_LOG_INFO("Successfully loaded package index from '" + m_filepath + "' (" + std::to_string(header.fileCount) + " files)");
    return true;
}

bool RowlPkgDataSource::exists(const std::string& path) {
    if (!m_isValid) return false;
    const auto normalizedPath = normalizePackagePath(path);
    return normalizedPath && m_indexTable.find(*normalizedPath) != m_indexTable.end();
}

std::vector<uint8_t> RowlPkgDataSource::read(const std::string& path) {
    if (!m_isValid) return {};

    const auto normalizedPath = normalizePackagePath(path);
    if (!normalizedPath) return {};
    auto it = m_indexTable.find(*normalizedPath);
    if (it == m_indexTable.end()) {
        return {};
    }

    const auto& entry = it->second;

    // Thread-safe file access for multi-threaded VFS
    std::lock_guard<std::mutex> lock(m_fileMutex);

    m_fileStream.seekg(entry.offset, std::ios::beg);

    if (!m_fileStream.good()) {
        ROWL_LOG_ERROR("Failed to seek to entry offset in package: " + path);
        return {};
    }

    std::vector<uint8_t> compressedBuffer(entry.compressedSize);
    m_fileStream.read(reinterpret_cast<char*>(compressedBuffer.data()), entry.compressedSize);

    if (m_fileStream.gcount() != static_cast<std::streamsize>(entry.compressedSize)) {
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
        if (result != entry.uncompressedSize) {
            ROWL_LOG_ERROR("Zstd output size mismatch for asset '" + path + "'");
            return {};
        }

        return decompressedBuffer;
    }

    ROWL_LOG_WARN("Unsupported compression flag for asset: " + path);
    return {};
}

} // namespace Rowl::VFS
