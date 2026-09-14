#include "rowl/platform/platform_host.hpp"

#include "rowl/platform/user_data_directories.hpp"
#include "rowl/vfs/vfs.hpp"

#include <utility>

namespace Rowl::Platform {

DefaultPlatformHost::DefaultPlatformHost(std::shared_ptr<Rowl::VFS::VFSManager> vfs)
    : m_vfs(std::move(vfs)) {
    const auto directories = resolveUserDataDirectories();
    m_savePath = directories.saves;
    m_profilePath = directories.profiles;
}

std::unique_ptr<std::istream> DefaultPlatformHost::openAssetStream(const std::string& path) {
    return m_vfs ? m_vfs->openReadStream(path) : nullptr;
}

std::filesystem::path DefaultPlatformHost::writableSavePath() const {
    return m_savePath;
}

std::filesystem::path DefaultPlatformHost::writableProfilePath() const {
    return m_profilePath;
}

LifecycleState DefaultPlatformHost::lifecycleState() const {
    return LifecycleState::Active;
}

std::vector<RuntimeInputEvent> DefaultPlatformHost::takeInputEvents() {
    return {};
}

RenderSurface DefaultPlatformHost::renderSurface() const {
    return {};
}

AudioFocus DefaultPlatformHost::audioFocus() const {
    return AudioFocus::Granted;
}

void DefaultPlatformHost::setVfs(std::shared_ptr<Rowl::VFS::VFSManager> vfs) {
    m_vfs = std::move(vfs);
}

} // namespace Rowl::Platform
