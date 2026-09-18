#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <exception>
#include <optional>
#include <system_error>

namespace Rowl::VFS {

namespace fs = std::filesystem;

namespace {

constexpr uintmax_t kMaxLooseAssetBytes = 128ULL * 1024 * 1024;

fs::path pathFromUtf8(const std::string& utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::optional<fs::path> resolveInsideRoot(const fs::path& canonicalRoot,
                                          const std::string& relativePath) {
    if (relativePath.empty() || relativePath.find('\0') != std::string::npos) return std::nullopt;

    std::string normalizedRel = relativePath;
    std::replace(normalizedRel.begin(), normalizedRel.end(), '\\', '/');

    // All graph, package and C-API paths are UTF-8. The narrow path
    // constructor uses the active Windows code page and loses valid names.
    fs::path requested = pathFromUtf8(normalizedRel);
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

// A2a: quiet single-lookup loose-file read. nullopt = miss/unreadable (no
// diagnostics — multi-source probing must stay log-clean); an engaged,
// possibly empty vector is a REAL asset. Loud callers (read()) add their
// own single diagnostic on top.
std::optional<std::vector<uint8_t>> readLooseFileQuiet(const fs::path& fullPath) {
    std::error_code error;
    if (!fs::is_regular_file(fullPath, error) || error) return std::nullopt;
    const auto fileSize = fs::file_size(fullPath, error);
    if (error || fileSize > kMaxLooseAssetBytes) return std::nullopt;

    std::ifstream file(fullPath, std::ios::binary);
    if (!file.is_open()) return std::nullopt;
    std::vector<uint8_t> buffer(static_cast<std::size_t>(fileSize));
    if (fileSize > 0 &&
        !file.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(fileSize))) {
        return std::nullopt;
    }
    return buffer;
}

LooseDirectorySource::LooseDirectorySource(std::string physicalPath)
    : m_physicalPath(std::move(physicalPath)) {
    std::error_code error;
    // A2a: canonicalize through UTF-8 — the narrow string ctor would
    // reinterpret a non-ASCII mount root in the ANSI codepage on Windows.
    m_canonicalRoot = fs::weakly_canonical(pathFromUtf8(m_physicalPath), error);
    if (error) {
        m_canonicalRoot.clear();
        ROWL_LOG_WARN("VFS could not canonicalize mount root: " + m_physicalPath);
    }
}

bool LooseDirectorySource::exists(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) return false;
    std::error_code error;
    return fs::exists(*fullPath, error) && !error &&
           fs::is_regular_file(*fullPath, error) && !error;
}

std::vector<uint8_t> LooseDirectorySource::read(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) {
        ROWL_LOG_WARN("VFS rejected path outside mount root: " + path);
        return {};
    }
    auto data = readLooseFileQuiet(*fullPath);
    if (!data) {
        ROWL_LOG_WARN("VFS could not read loose asset: " + fullPath->string());
        return {};
    }
    return std::move(*data);
}

std::optional<std::vector<uint8_t>> LooseDirectorySource::tryRead(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) return std::nullopt;
    return readLooseFileQuiet(*fullPath);
}

std::unique_ptr<std::istream> LooseDirectorySource::openStream(const std::string& path) {
    const auto fullPath = resolveInsideRoot(m_canonicalRoot, path);
    if (!fullPath) return nullptr;
    std::error_code error;
    if (!fs::is_regular_file(*fullPath, error) || error) return nullptr;
    auto stream = std::make_unique<std::ifstream>(*fullPath, std::ios::binary);
    if (!stream->is_open()) return nullptr;
    return stream;
}

void VFSManager::initialize() {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_initialized) return;

    ROWL_LOG_INFO("Initializing Hybrid Virtual File System (VFS)...");

    // A mod using an ordinary asset path overrides the packaged entry at the
    // same path.  Preserve the explicit mods/ namespace for tooling too.
    // A2a: error_code overloads throughout — a vanished CWD or an unreadable
    // directory must degrade to "no mounts", never throw across the VFS.
    std::error_code fsError;
    if (!fsError && fs::exists("mods", fsError) && !fsError &&
        fs::is_directory("mods", fsError) && !fsError) {
        mountDirectory("", "mods");
        mountDirectory("mods", "mods");
    }

    // A default runtime may only expose its asset root. Mounting the current
    // directory here used to make unrelated project files readable through an
    // empty VFS prefix whenever the engine was launched from a project root.
    const fs::path candidatesRoot = fs::current_path(fsError);
    std::vector<fs::path> candidateRoots;
    if (!fsError) candidateRoots.push_back(candidatesRoot / "Assets");

    for (const auto& root : candidateRoots) {
        fsError.clear();
        if (!fs::exists(root, fsError) || fsError) continue;
        if (!fs::is_directory(root, fsError) || fsError) continue;
        // A2a: pathToUtf8, never .string() — .string() is ANSI-encoded on
        // Windows and would lose a non-ASCII project root.
        const std::string rootUtf8 = Rowl::Platform::pathToUtf8(root);
        mountDirectory("", rootUtf8);
        mountDirectory("Assets", rootUtf8);

        fs::path imgPath = root / "images";
        if (fs::exists(imgPath, fsError) && !fsError &&
            fs::is_directory(imgPath, fsError) && !fsError) {
            const std::string imgUtf8 = Rowl::Platform::pathToUtf8(imgPath);
            mountDirectory("", imgUtf8);
            mountDirectory("images", imgUtf8);
        }

        fs::path pkgPath = root / "packages";
        mountPackagesUnder(pkgPath);
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

    // A2a: UTF-8 contract — the narrow path ctor would lose a non-ASCII root
    // on Windows before any lookup even runs.
    fs::path root = pathFromUtf8(projectRoot);
    std::error_code fsError;
    if (!fs::exists(root, fsError) || fsError ||
        !fs::is_directory(root, fsError) || fsError) {
        ROWL_LOG_WARN("VFS cannot remount non-existent project root: " + projectRoot);
        return;
    }

    ROWL_LOG_INFO("Remounting VFS for isolated project root: " + root.string());

    // Mount mods first so a release can override package content without a
    // second loose Assets tree.  The package remains the only base source.
    // A2a-fix1: clear before every probe chain — a reused error_code must
    // never carry a previous step's state into the next decision.
    fs::path modsPath = root / "mods";
    fsError.clear();
    if (fs::exists(modsPath, fsError) && !fsError &&
        fs::is_directory(modsPath, fsError) && !fsError) {
        const std::string modsUtf8 = Rowl::Platform::pathToUtf8(modsPath);
        mountDirectory("", modsUtf8);
        mountDirectory("mods", modsUtf8);
    }

    // Only expose declared runtime content. Mounting the project root at the
    // empty prefix would make project metadata and arbitrary source files
    // readable through an asset lookup after a project switch.
    // 1. Mount project Assets folder.
    fs::path assetsPath = root / "Assets";
    fsError.clear();
    if (fs::exists(assetsPath, fsError) && !fsError &&
        fs::is_directory(assetsPath, fsError) && !fsError) {
        const std::string assetsUtf8 = Rowl::Platform::pathToUtf8(assetsPath);
        mountDirectory("", assetsUtf8);
        mountDirectory("Assets", assetsUtf8);

        // 2. Mount images
        fs::path imgPath = assetsPath / "images";
        fsError.clear();
        if (fs::exists(imgPath, fsError) && !fsError &&
            fs::is_directory(imgPath, fsError) && !fsError) {
            const std::string imgUtf8 = Rowl::Platform::pathToUtf8(imgPath);
            mountDirectory("", imgUtf8);
            mountDirectory("images", imgUtf8);
        }

        // 3. Mount packages
        mountPackagesUnder(assetsPath / "packages");
    }

    ROWL_LOG_INFO("VFS Remount Complete for project '" + projectRoot + "' (" +
                  std::to_string(m_mountPoints.size()) + " mount points).");
}

void VFSManager::mountPackagesUnder(const fs::path& pkgPath) {
    // A2a-fix3: Windows CI'da probe "present" demesine rağmen tarama sessiz
    // ölüyordu — ne mount, ne "no archives", ne WARN, üstelik "Remount
    // Complete" bile basılmadan (invokeNoexcept çağrıcıyı da yutuyordu).
    // Kod incelemesi tek sessiz çıkışı gösteriyor: exists "present" + iterator
    // no_such_file (8.3-kısa-yollu RUNNER~1 formunda exists/iterator ayrışması)
    // ya da yutulan bir istisna. İkisine de çare: önce canonicalize (aynı
    // kökte loose mount'ların KANITLANMIŞ çalışan primitive'i — exists+iterator
    // artık özdeş formda koşar), tarama try/catch kalkanında, skip dalı INFO.
    std::error_code canonError;
    const fs::path canonPath = fs::weakly_canonical(pkgPath, canonError);
    if (canonError || canonPath.empty()) {
        ROWL_LOG_WARN("VFS package scan skipped: cannot resolve '" +
                      Rowl::Platform::pathToUtf8(pkgPath) + "': " + canonError.message());
        return;
    }
    const std::string pkgUtf8 = Rowl::Platform::pathToUtf8(canonPath);
    // A2a-fix2: exists() Windows CI'da MEVCUT bir dizin için FALSE döndü;
    // exists artık sadece tanı bilgisidir — kararı bağımsız syscall olan
    // iterator verir. Yokluk normaldir (loose-only projeler).
    std::error_code probeError;
    const bool present = fs::exists(canonPath, probeError) && !probeError;
    const std::string probeDetail =
        present ? "present"
                : "negative (" + (probeError ? probeError.message() : "absent") +
                      ") — scan attempted anyway";
    ROWL_LOG_INFO("VFS packages probe for '" + pkgUtf8 + "': " + probeDetail);

    std::error_code iterError;
    bool mountedAny = false;
    try {
        // A2a-fix4: canonical tarama, girdi-seviyesinde izole edilir. Windows
        // CI'da STL iterasyonu ERROR_NO_UNICODE_TRANSLATION fırlattı
        // (wide→ANSI-codepage çevirisi); tek patlayan girdi BÜTÜN taramayı
        // öldürmemeli — game.rowlpkg yine mount'lanmalı. increment ec'li
        // (throw etmez); deref+işleme iç try'da (patlarsa WARN+continue).
        fs::directory_iterator it(canonPath, iterError);
        const fs::directory_iterator end;
        while (!iterError && it != end) {
            try {
                const fs::directory_entry entry = *it;
                std::error_code entryError;
                const bool regular = entry.is_regular_file(entryError);
                if (entryError) {
                    // A2a-fix1: an iterator/entry failure used to end the scan
                    // silently (Windows CI mounted a verified game.rowlpkg
                    // never, with no log line at all). Loud now — a skipped
                    // scan must explain itself.
                    ROWL_LOG_WARN("VFS package scan skipped unreadable entry in '" + pkgUtf8 +
                                  "': " + entryError.message());
                } else if (regular && entry.path().extension().wstring() == L".rowlpkg") {
                    // Wide-karşılaştırma: hot-loop'ta narrow-literal/path
                    // dönüşümü sıfır. pathToUtf8 temiz u8-roundtrip'tir
                    // (kanıtlı) ve o da iç try'ın kalkanındadır.
                    mountPackage("", Rowl::Platform::pathToUtf8(entry.path()));
                    mountedAny = true;
                }
            } catch (const std::exception& ex) {
                ROWL_LOG_WARN("VFS package scan skipped throwing entry in '" + pkgUtf8 +
                              "': " + ex.what());
            } catch (...) {
                ROWL_LOG_WARN("VFS package scan skipped throwing entry in '" + pkgUtf8 + "'");
            }
            it.increment(iterError);
        }
        if (iterError) {
            ROWL_LOG_WARN("VFS package scan failed in '" + pkgUtf8 + "': " + iterError.message());
            return;
        }
    } catch (const std::exception& ex) {
        // A2a-fix3: çağrıcı invokeNoexcept altında — yutulan istisna hem
        // mount'u hem teşhisi öldürüyordu. Artık her çıkış konuşur.
        ROWL_LOG_WARN("VFS package scan threw in '" + pkgUtf8 + "': " + ex.what());
        return;
    } catch (...) {
        ROWL_LOG_WARN("VFS package scan threw in '" + pkgUtf8 + "'");
        return;
    }
    if (iterError) {
        // A2a-fix3: eskiden no_such_file burada SESSİZ dönüyordu — Windows
        // CI'daki kör nokta buydu. Skip normaldir, ama sessiz değildir.
        ROWL_LOG_INFO("VFS package scan skipped '" + pkgUtf8 + "': " + iterError.message());
        return;
    }
    if (!mountedAny) {
        ROWL_LOG_INFO("VFS package scan found no archives under '" + pkgUtf8 + "'");
    }
}

void VFSManager::mountDirectory(const std::string& virtualPrefix, const std::string& physicalPath) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto source = std::make_shared<LooseDirectorySource>(physicalPath);
    // A2a: refuse dead mounts loudly. Previously an unreachable root was
    // mounted anyway and every lookup silently missed.
    if (!source->isValid()) {
        ROWL_LOG_ERROR("VFS refused to mount unreachable directory: '" + physicalPath +
                       "' under virtual prefix '" + virtualPrefix + "'");
        return;
    }
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

std::optional<std::vector<uint8_t>> VFSManager::readBytesSinglePass(const std::string& cleanPath) {
    // Snapshot under lock; IO runs lock-free (A2a).
    const auto mounts = getMountPoints();
    // Try with prefix stripping first (correct priority: mods > data > packages)
    for (const auto& [prefix, source] : mounts) {
        // If cleanPath starts with prefix, try stripped version first
        if (!prefix.empty() && cleanPath.size() > prefix.size() &&
            cleanPath.compare(0, prefix.size(), prefix) == 0 &&
            cleanPath[prefix.size()] == '/') {
            std::string stripped = cleanPath.substr(prefix.size() + 1);
            if (auto data = source->tryRead(stripped)) {
                ROWL_LOG_TRACE("VFS Resolved '" + cleanPath + "' via " + source->getSourceName() + " (prefix-stripped)");
                return data;
            }
        }
    }

    // Fallback: try direct path (for paths without prefix)
    for (const auto& [prefix, source] : mounts) {
        (void)prefix;
        if (auto data = source->tryRead(cleanPath)) {
            ROWL_LOG_TRACE("VFS Resolved '" + cleanPath + "' via " + source->getSourceName());
            return data;
        }
    }
    return std::nullopt;
}

std::unique_ptr<std::istream> VFSManager::openStreamSinglePass(const std::string& cleanPath) {
    const auto mounts = getMountPoints();
    for (const auto& [prefix, source] : mounts) {
        if (!prefix.empty() && cleanPath.size() > prefix.size() &&
            cleanPath.compare(0, prefix.size(), prefix) == 0 && cleanPath[prefix.size()] == '/') {
            const std::string stripped = cleanPath.substr(prefix.size() + 1);
            if (auto stream = source->tryOpenStream(stripped)) return stream;
        }
    }
    for (const auto& [prefix, source] : mounts) {
        (void)prefix;
        if (auto stream = source->tryOpenStream(cleanPath)) return stream;
    }
    return nullptr;
}

std::vector<uint8_t> VFSManager::readBytes(const std::string& vfsPath) {
    if (vfsPath.empty()) {
        ROWL_LOG_WARN("VFS attempted to read empty path");
        return {};
    }

    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');

    // A2a: engaged-but-empty is a REAL empty asset (no "not found" warn);
    // nullopt is a true miss on every source.
    if (auto data = readBytesSinglePass(cleanPath)) return std::move(*data);

    ROWL_LOG_WARN("VFS File not found: '" + cleanPath + "'");
    return {};
}

std::unique_ptr<std::istream> VFSManager::openReadStream(const std::string& vfsPath) {
    if (vfsPath.empty()) return nullptr;
    std::string cleanPath = vfsPath;
    std::replace(cleanPath.begin(), cleanPath.end(), '\\', '/');
    return openStreamSinglePass(cleanPath);
}

std::string VFSManager::readString(const std::string& vfsPath) {
    auto bytes = readBytes(vfsPath);
    if (bytes.empty()) return "";
    return std::string(bytes.begin(), bytes.end());
}

} // namespace Rowl::VFS
