#include "rowl/platform/platform_host.hpp"

#include "rowl/vfs/vfs.hpp"

#include <utility>

namespace Rowl::Platform {

DefaultPlatformHost::DefaultPlatformHost(std::shared_ptr<Rowl::VFS::VFSManager> vfs)
    : m_vfs(std::move(vfs)) {}

std::unique_ptr<std::istream> DefaultPlatformHost::openAssetStream(const std::string& path) {
    return m_vfs ? m_vfs->openReadStream(path) : nullptr;
}

std::filesystem::path DefaultPlatformHost::writableSavePath() const {
    return "saves";
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
