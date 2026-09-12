#pragma once

#include <cstdint>
#include <filesystem>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Platform {

enum class LifecycleState {
    Active,
    Suspended,
    Stopping
};

enum class AudioFocus {
    Granted,
    Lost
};

enum class RenderSurfaceKind {
    Automatic,
    Offscreen,
    Native
};

struct RenderSurface {
    RenderSurfaceKind kind = RenderSurfaceKind::Automatic;
    void* nativeHandle = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

/// Platform-neutral player action. Native event translation belongs to the
/// host/window adapter; story/session behavior remains inside Engine.
struct RuntimeInputEvent {
    enum class Type {
        Advance,
        QuickSave,
        QuickLoad,
        Rewind,
        PointerDown,
        SwipeForward,
        SwipeBack
    };

    Type type;
    float x = 0.0f;
    float y = 0.0f;
};

/// Minimum host boundary shared by desktop and future mobile shells. Keep this
/// contract restricted to resources and signals that genuinely differ by host.
class PlatformHost {
public:
    virtual ~PlatformHost() = default;

    virtual std::unique_ptr<std::istream> openAssetStream(const std::string& path) = 0;
    virtual std::filesystem::path writableSavePath() const = 0;
    virtual LifecycleState lifecycleState() const = 0;
    virtual std::vector<RuntimeInputEvent> takeInputEvents() = 0;
    virtual RenderSurface renderSurface() const = 0;
    virtual AudioFocus audioFocus() const = 0;
};

/// Behavior-preserving desktop/default adapter. SDL window events continue to
/// arrive through Window; this adapter supplies VFS assets and default host
/// policy until a native shell injects its own PlatformHost.
class DefaultPlatformHost final : public PlatformHost {
public:
    explicit DefaultPlatformHost(std::shared_ptr<Rowl::VFS::VFSManager> vfs);

    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override;
    std::filesystem::path writableSavePath() const override;
    LifecycleState lifecycleState() const override;
    std::vector<RuntimeInputEvent> takeInputEvents() override;
    RenderSurface renderSurface() const override;
    AudioFocus audioFocus() const override;

    void setVfs(std::shared_ptr<Rowl::VFS::VFSManager> vfs);

private:
    std::shared_ptr<Rowl::VFS::VFSManager> m_vfs;
};

} // namespace Rowl::Platform
