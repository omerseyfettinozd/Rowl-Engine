#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>

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

    std::vector<fs::path> candidateRoots = {
        fs::current_path() / "Assets",
        fs::current_path(),
        fs::current_path() / ".." / "Assets",
        fs::current_path() / ".."
    };

    for (const auto& root : candidateRoots) {
        if (fs::exists(root) && fs::is_directory(root)) {
            mountDirectory("", root.string());
            mountDirectory("Assets", root.string());

            fs::path imgPath = root / "images";
            if (fs::exists(imgPath) && fs::is_directory(imgPath)) {
                mountDirectory("", imgPath.string());
                mountDirectory("images", imgPath.string());
            }

            fs::path pkgPath = root / "packages";
            if (fs::exists(pkgPath) && fs::is_directory(pkgPath)) {
                for (const auto& entry : fs::directory_iterator(pkgPath)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".rowlpkg") {
                        mountPackage("", entry.path().string());
                    }
                }
            }
        }
    }

    if (fs::exists("mods") && fs::is_directory("mods")) {
        mountDirectory("mods", "mods");
    }

    m_initialized = true;
    ROWL_LOG_INFO("VFS Initialization Complete (" + std::to_string(m_mountPoints.size()) + " mount points).");
}

void VFSManager::clearMountPoints() {
    m_mountPoints.clear();
    ROWL_LOG_INFO("VFS Mount Points Cleared.");
}

void VFSManager::remountProject(const std::string& projectRoot) {
    clearMountPoints();
    m_initialized = true;

    if (projectRoot.empty()) return;

    fs::path root(projectRoot);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        ROWL_LOG_WARN("VFS cannot remount non-existent project root: " + projectRoot);
        return;
    }

    ROWL_LOG_INFO("Remounting VFS for isolated project root: " + root.string());

    // 1. Mount project root
    mountDirectory("", root.string());

    // 2. Mount project Assets folder
    fs::path assetsPath = root / "Assets";
    if (fs::exists(assetsPath) && fs::is_directory(assetsPath)) {
        mountDirectory("", assetsPath.string());
        mountDirectory("Assets", assetsPath.string());

        // 3. Mount images
        fs::path imgPath = assetsPath / "images";
        if (fs::exists(imgPath) && fs::is_directory(imgPath)) {
            mountDirectory("", imgPath.string());
            mountDirectory("images", imgPath.string());
        }

        // 4. Mount packages
        fs::path pkgPath = assetsPath / "packages";
        if (fs::exists(pkgPath) && fs::is_directory(pkgPath)) {
            for (const auto& entry : fs::directory_iterator(pkgPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".rowlpkg") {
                    mountPackage("", entry.path().string());
                }
            }
        }
    }

    // 5. Mount project mods folder if exists
    fs::path modsPath = root / "mods";
    if (fs::exists(modsPath) && fs::is_directory(modsPath)) {
        mountDirectory("mods", modsPath.string());
    }

    ROWL_LOG_INFO("VFS Remount Complete for project '" + projectRoot + "' (" +
                  std::to_string(m_mountPoints.size()) + " mount points).");
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

    // Try with prefix stripping first (correct priority: mods > data > packages)
    for (const auto& [prefix, source] : m_mountPoints) {
        // If vfsPath starts with prefix, try stripped version first
        if (!prefix.empty() && vfsPath.size() > prefix.size() &&
            vfsPath.compare(0, prefix.size(), prefix) == 0 &&
            vfsPath[prefix.size()] == '/') {
            std::string stripped = vfsPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) {
                return true;
            }
        }
    }

    // Fallback: try direct path (for paths without prefix)
    for (const auto& [prefix, source] : m_mountPoints) {
        if (source->exists(vfsPath)) {
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> VFSManager::readBytes(const std::string& vfsPath) {
    if (vfsPath.empty()) {
        ROWL_LOG_WARN("VFS attempted to read empty path");
        return {};
    }

    // Try with prefix stripping first (correct priority order)
    for (const auto& [prefix, source] : m_mountPoints) {
        if (!prefix.empty() && vfsPath.size() > prefix.size() &&
            vfsPath.compare(0, prefix.size(), prefix) == 0 &&
            vfsPath[prefix.size()] == '/') {
            std::string stripped = vfsPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) {
                ROWL_LOG_TRACE("VFS Resolved '" + vfsPath + "' via " + source->getSourceName() + " (prefix-stripped)");
                return source->read(stripped);
            }
        }
    }

    // Fallback: try direct path
    for (const auto& [prefix, source] : m_mountPoints) {
        if (source->exists(vfsPath)) {
            ROWL_LOG_TRACE("VFS Resolved '" + vfsPath + "' via " + source->getSourceName());
            return source->read(vfsPath);
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