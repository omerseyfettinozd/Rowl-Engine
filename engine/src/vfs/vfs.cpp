#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include <fstream>
#include <filesystem>

namespace Rowl::VFS {

namespace fs = std::filesystem;

LooseDirectorySource::LooseDirectorySource(std::string physicalPath)
    : m_physicalPath(std::move(physicalPath)) {}

bool LooseDirectorySource::exists(const std::string& path) {
    fs::path fullPath = fs::path(m_physicalPath) / path;
    return fs::exists(fullPath) && fs::is_regular_file(fullPath);
}

std::vector<uint8_t> LooseDirectorySource::read(const std::string& path) {
    fs::path fullPath = fs::path(m_physicalPath) / path;
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ROWL_LOG_WARN("Failed to open file: " + fullPath.string());
        return {};
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        ROWL_LOG_WARN("Failed to determine file size: " + fullPath.string());
        return {};
    }
    
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        ROWL_LOG_WARN("Failed to read file: " + fullPath.string());
        return {};
    }
    return buffer;
}

VFSManager& VFSManager::instance() {
    static VFSManager s_instance;
    return s_instance;
}

void VFSManager::initialize() {
    if (m_initialized) return;

    ROWL_LOG_INFO("Initializing Hybrid Virtual File System (VFS)...");

    // Default mounts: Priority 1 = mods/, Priority 2 = data/
    // Check if directories exist before mounting
    if (fs::exists("mods") && fs::is_directory("mods")) {
        mountDirectory("mods", "mods");
    } else {
        ROWL_LOG_WARN("Mods directory not found, skipping mount.");
    }
    
    if (fs::exists("data") && fs::is_directory("data")) {
        mountDirectory("data", "data");
    } else {
        ROWL_LOG_WARN("Data directory not found, skipping mount.");
    }

    m_initialized = true;
    ROWL_LOG_INFO("VFS Initialization Complete.");
}

void VFSManager::mountDirectory(const std::string& virtualPrefix, const std::string& physicalPath) {
    auto source = std::make_shared<LooseDirectorySource>(physicalPath);
    m_mountPoints.emplace_back(virtualPrefix, source);
    ROWL_LOG_INFO("VFS Mounted directory: '" + physicalPath + "' under virtual prefix '" + virtualPrefix + "'");
}

void VFSManager::mountPackage(const std::string& virtualPrefix, const std::string& pkgPath) {
    auto source = std::make_shared<RowlPkgDataSource>(pkgPath);
    if (source->isValid()) {
        m_mountPoints.emplace_back(virtualPrefix, source);
        ROWL_LOG_INFO("VFS Mounted package: '" + pkgPath + "' under virtual prefix '" + virtualPrefix + "'");
    } else {
        ROWL_LOG_WARN("VFS Failed to mount package: '" + pkgPath + "'");
    }
}

bool VFSManager::exists(const std::string& vfsPath) {
    if (vfsPath.empty()) return false;
    
    for (const auto& [prefix, source] : m_mountPoints) {
        // Try direct path first, then try with prefix stripped
        if (source->exists(vfsPath)) {
            return true;
        }
        // If vfsPath starts with prefix, try without it
        if (!prefix.empty() && vfsPath.size() > prefix.size() &&
            vfsPath.compare(0, prefix.size(), prefix) == 0 &&
            vfsPath[prefix.size()] == '/') {
            std::string stripped = vfsPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) {
                return true;
            }
        }
    }
    return false;
}

std::vector<uint8_t> VFSManager::readBytes(const std::string& vfsPath) {
    if (vfsPath.empty()) {
        ROWL_LOG_WARN("VFS attempted to read empty path");
        return {};
    }
    
    for (const auto& [prefix, source] : m_mountPoints) {
        if (source->exists(vfsPath)) {
            ROWL_LOG_TRACE("VFS Resolved '" + vfsPath + "' via " + source->getSourceName());
            return source->read(vfsPath);
        }
        // If vfsPath starts with prefix, try without it
        if (!prefix.empty() && vfsPath.size() > prefix.size() &&
            vfsPath.compare(0, prefix.size(), prefix) == 0 &&
            vfsPath[prefix.size()] == '/') {
            std::string stripped = vfsPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) {
                ROWL_LOG_TRACE("VFS Resolved '" + vfsPath + "' via " + source->getSourceName() + " (prefix-stripped)");
                return source->read(stripped);  // BUG FIX: was reading vfsPath instead of stripped
            }
        }
    }
    ROWL_LOG_WARN("VFS File not found: '" + vfsPath + "'");
    return {};
}

std::string VFSManager::readString(const std::string& vfsPath) {
    auto bytes = readBytes(vfsPath);
    if (bytes.empty()) return "";
    return std::string(bytes.begin(), bytes.end());
}

} // namespace Rowl::VFS