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
        SwipeBack,
        // MS-6 player shell: pause menu + quick-slot selection. Keyboard and
        // pointer hosts emit these; Engine routes by pause state, so unpaused
        // games ignore menu nav and paused games ignore story advance.
        PauseToggle,
        MenuUp,
        MenuDown,
        MenuLeft,
        MenuRight,
        MenuBack,
        SelectSlot,
        // #16: release/ambient vocabulary. The dispatcher already queued
        // KEY_UP/BUTTON_UP/FINGER_MOTION but Window::pollEvents had no
        // branch for them, and MOUSE_MOTION/MOUSE_WHEEL/TEXT_INPUT never
        // routed at all — all six were silently unconsumed. These carry
        // the now-conscious handler-visible consumption; Engine routes
        // them to no story action (see handleRuntimeInput).
        PointerUp,
        KeyUp,
        PointerMotion,
        Scroll,
        TextInput,
        // #60: IME composition kelime dağarcığı. TEXT_EDITING /
        // TEXT_EDITING_CANDIDATES dispatcher'da düşüyordu (composition
        // bacağı); committed-text (TextInput) gibi bilinçli tüketilir,
        // hikâye eylemi taşımaz (bkz. handleRuntimeInput).
        TextEditing
    };

    Type type;
    float x = 0.0f;
    float y = 0.0f;
    /// Meaningful only for SelectSlot: the requested quick-save slot
    /// (kPauseMenuQuickSlotMin..kPauseMenuQuickSlotMax).
    int32_t slot = 0;
    /// Meaningful only for KeyUp: the released SDL keycode.
    uint32_t key = 0;
    /// Meaningful only for TextInput: the committed UTF-8 text.
    /// Meaningful only for TextEditing: the in-progress composition text.
    std::string text;
    /// Meaningful only for TextEditing: SDL composition selection
    /// (start/length, -1 when unset).
    int32_t compositionStart = -1;
    int32_t compositionLength = -1;
};

/// Minimum host boundary shared by desktop and future mobile shells. Keep this
/// contract restricted to resources and signals that genuinely differ by host.
///
/// Faz 4.5 Dilim 4: every member is a defaulted virtual — there are NO pure
/// virtuals, so a future mobile shell adopts this base incrementally and an
/// unknown host degrades into safe desktop defaults instead of failing to
/// link. Real mobile end-to-end injection (native host + touch-path proof) is
/// gated on Faz 6/7; see docs/PLATFORM_SUPPORT.md "Mobile host gate (Faz 6/7)".
class PlatformHost {
public:
    virtual ~PlatformHost() = default;

    virtual std::unique_ptr<std::istream> openAssetStream(const std::string& path);
    virtual std::filesystem::path writableSavePath() const;
    /// Player-profile storage belongs to the same per-user data capability as
    /// saves. The default keeps existing injected hosts source-compatible.
    virtual std::filesystem::path writableProfilePath() const {
        const auto savePath = writableSavePath();
        return savePath.empty() ? std::filesystem::path("profiles")
                                : savePath.parent_path() / "profiles";
    }
    virtual LifecycleState lifecycleState() const;
    virtual std::vector<RuntimeInputEvent> takeInputEvents();
    virtual RenderSurface renderSurface() const;
    virtual AudioFocus audioFocus() const;
    /// #60: IME composition isteği. Varsayılan kapalıdır (masaüstü
    /// hostsuz akış etkilenmez); metin-alanı olan bir host true'ya
    /// çevirir, Engine her step'te Window metin-girdisini buna göre
    /// açıp kapatır. Faz 4.5 Dilim 4 konvansiyonu: default'lu sanal,
    /// mevcut hostlar kaynak-uyumlu kalır.
    virtual bool wantsTextInput() const { return false; }
};

/// Behavior-preserving desktop/default adapter. SDL window events continue to
/// arrive through Window; this adapter supplies VFS assets and default host
/// policy until a native shell injects its own PlatformHost.
class DefaultPlatformHost final : public PlatformHost {
public:
    explicit DefaultPlatformHost(std::shared_ptr<Rowl::VFS::VFSManager> vfs);

    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override;
    std::filesystem::path writableSavePath() const override;
    std::filesystem::path writableProfilePath() const override;
    LifecycleState lifecycleState() const override;
    std::vector<RuntimeInputEvent> takeInputEvents() override;
    RenderSurface renderSurface() const override;
    AudioFocus audioFocus() const override;

    void setVfs(std::shared_ptr<Rowl::VFS::VFSManager> vfs);

private:
    std::shared_ptr<Rowl::VFS::VFSManager> m_vfs;
    std::filesystem::path m_savePath;
    std::filesystem::path m_profilePath;
};

} // namespace Rowl::Platform
