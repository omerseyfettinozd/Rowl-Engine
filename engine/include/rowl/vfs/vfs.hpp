#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace Rowl::VFS {

class IDataSource {
public:
    virtual ~IDataSource() = default;
    virtual bool exists(const std::string& path) = 0;
    virtual std::vector<uint8_t> read(const std::string& path) = 0;
    virtual std::string getSourceName() const = 0;
};

class LooseDirectorySource : public IDataSource {
public:
    explicit LooseDirectorySource(std::string physicalPath);
    ~LooseDirectorySource() override = default;

    bool exists(const std::string& path) override;
    std::vector<uint8_t> read(const std::string& path) override;
    std::string getSourceName() const override { return "LooseDirectorySource [" + m_physicalPath + "]"; }

private:
    std::string m_physicalPath;
};

class VFSManager {
public:
    static VFSManager& instance();

    void initialize();
    void remountProject(const std::string& projectRoot);
    void clearMountPoints();
    void mountDirectory(const std::string& virtualPrefix, const std::string& physicalPath);
    void mountPackage(const std::string& virtualPrefix, const std::string& pkgPath);

    bool exists(const std::string& vfsPath);
    std::vector<uint8_t> readBytes(const std::string& vfsPath);
    std::string readString(const std::string& vfsPath);

private:
    VFSManager() = default;
    ~VFSManager() = default;

    std::vector<std::pair<std::string, std::shared_ptr<IDataSource>>> m_mountPoints;
    bool m_initialized = false;
};

} // namespace Rowl::VFS
