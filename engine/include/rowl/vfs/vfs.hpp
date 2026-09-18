#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <utility>
#include <mutex>
#include <filesystem>
#include <istream>

namespace Rowl::VFS {

class IDataSource {
public:
    virtual ~IDataSource() = default;
    virtual bool exists(const std::string& path) = 0;
    virtual std::vector<uint8_t> read(const std::string& path) = 0;
    virtual std::unique_ptr<std::istream> openStream(const std::string& path) = 0;
    virtual std::string getSourceName() const = 0;
    /// A2a: single-lookup read — nullopt means absent/unreadable, with no
    /// exists+read TOCTOU and no double lookup. An engaged (even empty)
    /// vector is a REAL asset, never a miss. Quiet by contract: multi-source
    /// probing must not log per-source failures. The default keeps external
    /// implementers working through the legacy two-step path.
    virtual std::optional<std::vector<uint8_t>> tryRead(const std::string& path) {
        if (!exists(path)) return std::nullopt;
        return read(path);
    }
    /// A2a: single-lookup stream open. Default keeps the legacy path.
    virtual std::unique_ptr<std::istream> tryOpenStream(const std::string& path) {
        return openStream(path);
    }
};

class LooseDirectorySource : public IDataSource {
public:
    explicit LooseDirectorySource(std::string physicalPath);
    ~LooseDirectorySource() override = default;

    bool exists(const std::string& path) override;
    std::vector<uint8_t> read(const std::string& path) override;
    std::optional<std::vector<uint8_t>> tryRead(const std::string& path) override;
    std::unique_ptr<std::istream> openStream(const std::string& path) override;
    std::string getSourceName() const override { return "LooseDirectorySource [" + m_physicalPath + "]"; }
    const std::string& getPhysicalPath() const { return m_physicalPath; }
    /// A2a: false when the mount root could not be canonicalized (dead
    /// mount). mountDirectory() refuses such sources instead of letting
    /// every lookup silently miss.
    bool isValid() const { return !m_canonicalRoot.empty(); }

private:
    std::string m_physicalPath;
    // The mount root cannot change for the lifetime of a data source. Keep
    // its canonical form so every asset lookup only needs to validate the
    // requested child path (including its symlink boundary).
    std::filesystem::path m_canonicalRoot;
};

class VFSManager {
public:
    VFSManager() = default;
    ~VFSManager() = default;

    VFSManager(const VFSManager&) = delete;
    VFSManager& operator=(const VFSManager&) = delete;

    void initialize();
    void remountProject(const std::string& projectRoot);
    void clearMountPoints();
    void mountDirectory(const std::string& virtualPrefix, const std::string& physicalPath);
    void mountPackage(const std::string& virtualPrefix, const std::string& pkgPath);

    bool exists(const std::string& vfsPath);
    std::vector<uint8_t> readBytes(const std::string& vfsPath);
    /// Opens a seekable, read-only asset stream. Callers own the returned stream.
    std::unique_ptr<std::istream> openReadStream(const std::string& vfsPath);
    std::string readString(const std::string& vfsPath);
    std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>> getMountPoints() const {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        return m_mountPoints;
    }

private:
    /// A2a: mount-list snapshot + single-pass probe. The global lock covers
    /// only the snapshot; file IO runs lock-free so one slow source cannot
    /// stall mounts/unmounts. nullopt = missed on every source.
    std::optional<std::vector<uint8_t>> readBytesSinglePass(const std::string& cleanPath);
    std::unique_ptr<std::istream> openStreamSinglePass(const std::string& cleanPath);
    /// A2a-fix1: shared package-directory scan used by initialize() and
    /// remountProject(). Probes with fresh error_codes, warns loudly on an
    /// iterator/entry failure (never a silent skip), and notes when a
    /// present directory yields no archives. Call with m_mutex held.
    void mountPackagesUnder(const std::filesystem::path& pkgPath);

    mutable std::recursive_mutex m_mutex;
    std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>> m_mountPoints;
    bool m_initialized = false;
};

} // namespace Rowl::VFS
