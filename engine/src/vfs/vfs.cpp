#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <optional>

namespace Rowl::VFS {

namespace fs = std::filesystem;

namespace {

constexpr uintmax_t kMaxLooseAssetBytes = 128ULL * 1024 * 1024;

std::optional<fs::path> resolveInsideRoot(const fs::path& canonicalRoot,
                                          const std::string& relativePath) {
    if (relativePath.empty() || relativePath.find('\0') != std::string::npos) return std::nullopt;

    std::string normalizedRel = relativePath;
    std::replace(normalizedRel.begin(), normalizedRel.end(), '\\', '/');

    fs::path requested(normalizedRel);
    if (requested.is_absolute() || requested.has_root_name() || requested.has_root_directory()) {
        return std::nullopt;
    }

    std::error_code error;
    if (canonicalRoot.empty()) return std::nullopt;
    fs::path candidate = fs::weakly_canonical(canonicalRoot / requested, error);
    if (error) return std::nullopt;

    fs::path relative = candidate.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute()) return std::nullopt;
    const auto first = relative.begin();
    if (first != relative.end() && *first == "..") return std::nullopt;
    return candidate;
}

} // namespace

LooseDirectorySource::LooseDirectorySource(std::string physicalPath)
    : m_physicalPath(std::move(physicalPath)) {
    std::error_code error;
    m_canonicalRoot = fs::weakly_canonical(m_physicalPath, error);
    if (error) {
        m_canonicalRoot.clear();
        ROWL_LOG_WARN("VFS could not canonicalize mount root: " + m_physicalPath);
    }
}

bool LooseDirectorySource::exists(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    return fullPath && fs::exists(*fullPath) && fs::is_regular_file(*fullPath);
}

std::vector<uint8_t> LooseDirectorySource::read(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) {
        ROWL_LOG_WARN("VFS rejected path outside mount root: " + path);
        return {};
    }
    std::ifstream file(*fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        ROWL_LOG_WARN("Failed to open file: " + fullPath->string());
        return {};
    }

    std::streamsize size = file.tellg();
    if (size < 0) {
        ROWL_LOG_WARN("Failed to determine file size: " + fullPath->string());
        return {};
    }
    if (static_cast<uintmax_t>(size) > kMaxLooseAssetBytes) {
        ROWL_LOG_WARN("VFS rejected oversized loose asset: " + fullPath->string());
        return {};
    }

    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        ROWL_LOG_WARN("Failed to read file: " + fullPath->string());
        return {};
    }
    return buffer;
}

std::unique_ptr<std::istream> LooseDirectorySource::openStream(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath || !fs::is_regular_file(*fullPath)) return nullptr;
    auto stream = std::make_unique<std::ifstream>(*fullPath, std::ios::binary);
    if (!stream->is_open()) return nullptr;
    return stream;
}

VFSManager& VFSManager::instance() {
    static VFSManager s_instance;
    return s_instance;
}

void VFSManager::initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return;

    ROWL_LOG_INFO("Initializing Hybrid Virtual File System (VFS)...");

    // A mod using an ordinary asset path overrides the packaged entry at the
    // same path.  Preserve the explicit mods/ namespace for tooling too.
    if (fs::exists("mods") && fs::is_directory("mods")) {
        mountDirectory("", "mods");
        mountDirectory("mods", "mods");
    }

    // A default runtime may only expose its asset root. Mounting the current
    // directory here used to make unrelated project files readable through an
    // empty VFS prefix whenever the engine was launched from a project root.
    const std::vector<fs::path> candidateRoots = {
        fs::current_path() / "Assets"
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

    m_initialized = true;
    ROWL_LOG_INFO("VFS Initialization Complete (" + std::to_string(m_mountPoints.size()) + " mount points).");
}

void VFSManager::clearMountPoints() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_mountPoints.clear();
    ROWL_LOG_INFO("VFS Mount Points Cleared.");
}

void VFSManager::remountProject(const std::string& projectRoot) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    clearMountPoints();
    m_initialized = true;

    if (projectRoot.empty()) return;

    fs::path root(projectRoot);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        ROWL_LOG_WARN("VFS cannot remount non-existent project root: " + projectRoot);
        return;
    }

    ROWL_LOG_INFO("Remounting VFS for isolated project root: " + root.string());

    // Mount mods first so a release can override package content without a
    // second loose Assets tree.  The package remains the only base source.
    fs::path modsPath = root / "mods";
    if (fs::exists(modsPath) && fs::is_directory(modsPath)) {
        mountDirectory("", modsPath.string());
        mountDirectory("mods", modsPath.string());
    }

    // Only expose declared runtime content. Mounting the project root at the
    // empty prefix would make project metadata and arbitrary source files
    // readable through an asset lookup after a project switch.
    // 1. Mount project Assets folder.
    fs::path assetsPath = root / "Assets";
    if (fs::exists(assetsPath) && fs::is_directory(assetsPath)) {
        mountDirectory("", assetsPath.string());
        mountDirectory("Assets", assetsPath.string());

        // 2. Mount images
        fs::path imgPath = assetsPath / "images";
        if (fs::exists(imgPath) && fs::is_directory(imgPath)) {
            mountDirectory("", imgPath.string());
            mountDirectory("images", imgPath.string());
        }

        // 3. Mount packages
        fs::path pkgPath = assetsPath / "packages";
        if (fs::exists(pkgPath) && fs::is_directory(pkgPath)) {
            for (const auto& entry : fs::directory_iterator(pkgPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".rowlpkg") {
                    mountPackage("", entry.path().string());
                }
            }
        }
    }

    ROWL_LOG_INFO("VFS Remount Complete for project '" + projectRoot + "' (" +
                  std::to_string(m_mountPoints.size()) + " mount points).");
}

void VFSManager::mountDirectory(const std::string& virtualPrefix, const std::string& physicalPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto source = std::make_shared<LooseDirectorySource>(physicalPath);
    m_mountPoints.emplace_back(virtualPrefix, source);
    ROWL_LOG_INFO("VFS Mounted directory: '" + physicalPath + "' under virtual prefix '" + virtualPrefix + "'");
}

void VFSManager::mountPackage(const std::string& virtualPrefix, const std::string& pkgPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
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

    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Try with prefix stripping first (correct priority: mods > data > packages)
    for (const auto& [prefix, source] : m_mountPoints) {
        // If cleanPath starts with prefix, try stripped version first
        if (!prefix.empty() && cleanPath.size() > prefix.size() &&
            cleanPath.compare(0, prefix.size(), prefix) == 0 &&
            cleanPath[prefix.size()] == '/') {
            std::string stripped = cleanPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) {
                return true;
            }
        }
    }

    // Fallback: try direct path (for paths without prefix)
    for (const auto& [prefix, source] : m_mountPoints) {
        if (source->exists(cleanPath)) {
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

    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    // Try with prefix stripping first (correct priority order)
    for (const auto& [prefix, source] : m_mountPoints) {
        if (!prefix.empty() && cleanPath.size() > prefix.size() &&
            cleanPath.compare(0, prefix.size(), prefix) == 0 &&
            cleanPath[prefix.size()] == '/') {
            std::string stripped = cleanPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) {
                ROWL_LOG_TRACE("VFS Resolved '" + cleanPath + "' via " + source->getSourceName() + " (prefix-stripped)");
                return source->read(stripped);
            }
        }
    }

    // Fallback: try direct path
    for (const auto& [prefix, source] : m_mountPoints) {
        if (source->exists(cleanPath)) {
            ROWL_LOG_TRACE("VFS Resolved '" + cleanPath + "' via " + source->getSourceName());
            return source->read(cleanPath);
        }
    }

    ROWL_LOG_WARN("VFS File not found: '" + cleanPath + "'");
    return {};
}

std::unique_ptr<std::istream> VFSManager::openReadStream(const std::string& vfsPath) {
    if (vfsPath.empty()) return nullptr;
    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    for (const auto& [prefix, source] : m_mountPoints) {
        if (!prefix.empty() && cleanPath.size() > prefix.size() &&
            cleanPath.compare(0, prefix.size(), prefix) == 0 && cleanPath[prefix.size()] == '/') {
            const std::string stripped = cleanPath.substr(prefix.size() + 1);
            if (source->exists(stripped)) return source->openStream(stripped);
        }
    }
    for (const auto& [prefix, source] : m_mountPoints) {
        (void)prefix;
        if (source->exists(cleanPath)) return source->openStream(cleanPath);
    }
    return nullptr;
}

std::string VFSManager::readString(const std::string& vfsPath) {
    auto bytes = readBytes(vfsPath);
    if (bytes.empty()) return "";
    return std::string(bytes.begin(), bytes.end());
}

} // namespace Rowl::VFS
