#pragma once

#include <cstdint>
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
};

class LooseDirectorySource : public IDataSource {
public:
    explicit LooseDirectorySource(std::string physicalPath);
    ~LooseDirectorySource() override = default;

    bool exists(const std::string& path) override;
    std::vector<uint8_t> read(const std::string& path) override;
    std::unique_ptr<std::istream> openStream(const std::string& path) override;
    std::string getSourceName() const override { return "LooseDirectorySource [" + m_physicalPath + "]"; }
    const std::string& getPhysicalPath() const { return m_physicalPath; }

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

    static VFSManager& instance();

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
    mutable std::recursive_mutex m_mutex;
    std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>> m_mountPoints;
    bool m_initialized = false;
};

} // namespace Rowl::VFS
