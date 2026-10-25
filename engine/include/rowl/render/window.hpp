#pragma once

#include <string>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <functional>
#include <optional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "rowl/platform/platform_host.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/msdf_renderer.hpp"
#include "rowl/render/camera2d.hpp"
#include "rowl/render/transition_manager.hpp"
#include "rowl/state/ui_theme.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Cursor;
struct SDL_Surface;
struct SDL_GPUShader;
struct SDL_GPURenderState;

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Core {
struct PauseMenuView;
}

namespace Rowl::Render {

struct CharacterRenderData {
    std::string sprite;
    float x = 1440.0f;
    float y = 340.0f;
    float width = 360.0f;
    float height = 540.0f;
    float rotation = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    // Faz 5 Dilim 6: katman opakligi (toSpriteDraws -> engine -> render hatti).
    // Yoklugu (legacy) 1.0 sayilir; fail-closed eski davranis korunur.
    float opacity = 1.0f;

    // Character Default Voice Blip Settings (Milestone 25)
    std::string voiceBlipSound = "";
    float voiceBlipPitch = 1.0f;
    float voiceBlipPitchVariance = 0.08f;
    int voiceBlipCadence = 1;

    // E2a sprite-sheet (yalniz-eklemeli; varsayilan kapali = legacy tek-kare).
    // sheetCols/sheetRows: izgara boyutu (or. 4x2 serit). fps: kare/saniye.
    // loop: sonda sarmala (false = son karede dur). sheetElapsed: saniye
    // cinsinden animasyon saati (Engine::step ilerletir).
    int sheetCols = 0;
    int sheetRows = 0;
    float sheetFps = 0.0f;
    bool sheetLoop = true;
    float sheetElapsed = 0.0f;

    bool isSheetEnabled() const {
        if (sheetCols < 1 || sheetRows < 1) return false;
        if (sheetCols <= 1 && sheetRows <= 1) return false;
        if (!std::isfinite(sheetFps) || sheetFps <= 0.0f) return false;
        return true;
    }
    int sheetFrameCount() const {
        if (!isSheetEnabled()) return 1;
        return sheetCols * sheetRows;
    }
    int sheetFrameIndex() const {
        if (!isSheetEnabled()) return 0;
        const int total = sheetCols * sheetRows;
        const float span = static_cast<float>(total) / sheetFps;
        float t = std::isfinite(sheetElapsed) && sheetElapsed > 0.0f ? sheetElapsed : 0.0f;
        if (sheetLoop && span > 0.0f) t = std::fmod(t, span);
        long idx = static_cast<long>(t * sheetFps);
        if (idx < 0) idx = 0;
        if (sheetLoop) return static_cast<int>(idx % total);
        return idx >= total ? total - 1 : static_cast<int>(idx);
    }
    bool isSheetAnimating() const {
        if (!isSheetEnabled()) return false;
        if (sheetLoop) return true;
        return sheetFrameIndex() < sheetFrameCount() - 1;
    }
};

struct DialogueRenderData {
    bool hasDialogueBox = true;
    std::string speaker;
    std::string dialogue;
    /// Persistent Faz 2 content identity (UUID form); empty for legacy
    /// payloads that predate migration. Never interpreted by the renderer.
    std::string contentId;
    /// Node-original strings, kept so a locale switch can re-resolve the
    /// displayed speaker/dialogue without a node change. Empty for legacy
    /// payloads (then speaker/dialogue are already the originals).
    std::string originalSpeaker;
    std::string originalDialogue;
    /// Active BCP 47 tag at assembly time; fed to the shaper (HarfBuzz
    /// language + FriBidi base direction) and part of the shape-cache key.
    std::string language;
    float x = 80.0f;
    float y = 860.0f;
    float width = 1760.0f;
    float height = 180.0f;
    float scale = 1.0f;

    // Typewriter & Timing
    bool typewriterEnabled = false;
    bool isPlaying = false;
    int textSpeed = 30; // ms per char
    float elapsedTypewriterTime = 0.0f; // seconds
    bool autoAdvance = false;
    float autoAdvanceDelay = 2.0f;

    // Typewriter Voice Blips & Audio Effects (Milestone 25)
    std::string typewriterSound = "";
    float voiceBlipPitch = 1.0f;
    float voiceBlipPitchVariance = 0.08f;
    int voiceBlipCadence = 1;
    bool voiceBlipSkipPunctuation = true;
    int voiceBlipChannel = 1; // 0 = Bgm, 1 = Voice, 2 = Sfx
    float voiceBlipVolume = 0.85f;
    size_t lastBlipCodepointIndex = 0; // reveal-unit index (legacy field name)

    // Typography & Colors
    float fontSize = 24.0f;
    float speakerFontSize = 20.0f;
    std::string textColor = "#F1F5F9";
    std::string speakerColor = "#38BDF8";
    std::string textAlignment = "Left";

    // Box Visuals & Opacity
    float boxOpacity = 0.88f;
    std::string boxColor = "#0F0F1A";
    std::string borderColor = "#00F0FF";
    float borderThickness = 2.0f;
    float cornerRadius = 8.0f;
    std::string customBoxTexture;

    /// T1: bit per field the node's component wrote itself (engine_ui_theme.cpp). The other fields hold the project's UI theme
    /// default and follow a theme reload. Never part of the frame hash.
    uint32_t authoredMask = 0;
};

struct ComposedFrame;

struct ChoiceButtonRenderData {
    std::string optionId;
    std::string text;
    std::string backgroundImage;
    float x = 680.0f;
    float y = 520.0f;
    float width = 560.0f;
    float height = 64.0f;
    float fontSize = 22.0f;
    float opacity = 1.0f;
    float borderThickness = 2.0f;
    float cornerRadius = 8.0f;
    std::string textColor = "#FFFFFF";
    std::string backgroundColor = "#1E293B";
    std::string hoverColor = "#0EA5E9";
    std::string borderColor = "#38BDF8";
    std::string textAlignment = "Center";
    std::string fontFamily = "Default";
    bool enabled = true;
    /// T2: per-state images the option wrote itself (normal_image feeds backgroundImage,
    /// the focused / disabled ones are read from hover_image / disabled_image). A theme
    /// skin supplies the state images the option leaves empty. Empty = not mixed into the frame hash.
    std::string hoverImage;
    std::string pressedImage;
    std::string disabledImage;
    /// Author's own "enabled" flag, before the Lua condition was applied.
    bool authorEnabled = true;
    /// Lua gate ("" / "true" / "1" = always open). Re-checked at selection.
    std::string condition;
    /// D-K3: klavye/fare odagi bu secenekte (cizimde vurgulanir). Yazar
    /// verisi degil, Engine'in anlik durumudur; kare ozetine yalniz true iken girer.
    bool focused = false;
    /// T1: bit per field the option wrote itself (engine_ui_theme.cpp);
    /// the rest follow the project's UI theme. Not part of the frame hash.
    uint32_t authoredMask = 0;
};

class Window {
public:
    explicit Window(Rowl::VFS::VFSManager* vfs = nullptr);
    ~Window();

    // Disable copy/move
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void setVfs(Rowl::VFS::VFSManager* vfs);
    Rowl::VFS::VFSManager* getVfs() const { return m_vfs; }

    /**
     * Offscreen initialization: renders into an internal RGBA32 surface/buffer (e.g. 1920x1080)
     * instead of a native OS window.
     */
    bool initializeOffscreen(uint32_t width, uint32_t height);

    /**
     * Provides direct access to the RGBA32 pixel memory pointer for zero-copy/fast host sharing.
     */
    const uint8_t* getPixelBuffer() const;
    size_t getPixelBufferLength() const;

    /**
     * Returns the offscreen surface row pitch in bytes (bytes per scanline).
     * May exceed width*4 when the backend pads rows; hosts must stride by
     * this value instead of assuming tight packing. Returns 0 when no
     * offscreen surface exists. MS-0 pitch contract.
     */
    uint32_t getPixelPitch() const;

    /**
     * Standard initialization: creates an SDL3 top-level window.
     * Used in standalone / runtime-only mode.
     */
    bool initialize(const std::string& title,
                    uint32_t width,
                    uint32_t height,
                    bool vsync = true);

    /**
     * Embedded initialization: renders into an existing native OS handle.
     */
    bool initializeEmbedded(void* nativeHandle,
                             uint32_t width,
                             uint32_t height,
                             bool vsync = true);

    /**
     * Notify the window of a viewport resize (e.g. host control resized).
     */
    void resizeViewport(uint32_t newWidth, uint32_t newHeight);

    /**
     * Maps a physical-surface tap into story-canvas coordinates.
     *
     * Returns false only when the viewport itself is degenerate. Otherwise
     * outBezelTap reports whether the point landed in the letterbox/pillarbox
     * margins (mapped outputs are zeroed then); a canvas hit fills
     * outVirtualX/outVirtualY and clears outBezelTap.
     */
    bool mapPhysicalToVirtual(float physicalX, float physicalY,
                              uint32_t virtualWidth, uint32_t virtualHeight,
                              float& outVirtualX, float& outVirtualY,
                              bool& outBezelTap) const;

    /// D-K3: secenek dugmelerini cizimle AYNI olcumden (yazi boyutu + pencere olcegi
    /// dahil) sigdirir: metni sigmayan dugme alt kenari sabit yukari buyur, ustteki
    /// kardesler yukari itilir (ui_text_fit.hpp). Cizim, tiklama ve gozlem bu
    /// yerlesimi kullanir. Yazi/pencere yoksa ya da sigmayan yoksa girdi aynen kalir.
    void fitChoiceButtons(std::vector<ChoiceButtonRenderData>& choices) const;

    void setInputHandler(std::function<void(const Rowl::Platform::RuntimeInputEvent&)> handler);

    /// #60: IME metin-girdisi anahtarı (kenar-tetiklemeli). Engine her
    /// step'te host'un wantsTextInput() değerini buraya yansıtır; SDL
    /// TEXT_INPUT/TEXT_EDITING olayı enable edilmeden hiç üretilmez.
    /// Penceresiz (offscreen/null) durumda yalnızca durum kaydedilir.
    void setTextInputEnabled(bool enabled);
    bool isTextInputEnabled() const noexcept { return m_textInputEnabled; }

    /// Dispatcher id this window consumes via pollEvents (0 when the window
    /// owns no dispatch registration: offscreen or uninitialized). Public so
    /// headless-capable tests can target synthetic SDL events at a real
    /// visible window without touching dispatcher internals.
    uint32_t eventWindowId() const noexcept { return m_eventWindowId; }
    /// MS-6: pure SDL-key → player-action translation behind pollEvents.
    /// Returns false for unmapped keys. Public and static so headless tests
    /// can pin the Esc/P/digit/arrow contract without a visible window.
    static bool mapKeyToRuntimeInput(uint32_t sdlKey, Rowl::Platform::RuntimeInputEvent& outEvent);

    /// A3 (K2 kilidi): identical-frame skip kararının paketlenmiş-içerik
    /// çekirdeği — yalnız parametre + FNV, üye durumu okumaz. Public ve
    /// static — MS-6 emsali — ki headless golden test bölünme/`render_data.hpp`
    /// çıkarımı sonrası skip-davranışını sabitleyebilsin. İmza/algoritma
    /// değişimi K2 kilit testini güncellemeyi gerektirir (kör değişim YOK).
    /// Dinamik durum (kamera/effekt/boyut) üye `hashFrameContent`'te karışır.
    static uint64_t hashPackedFrameContent(bool hasBackground, const std::string& background,
                                           float bgX, float bgY, float bgW, float bgH,
                                           const std::vector<CharacterRenderData>& characters,
                                           const std::vector<DialogueRenderData>& dialogues,
                                           const std::vector<ChoiceButtonRenderData>& choices,
                                           float bgRotation, float bgParallaxX, float bgParallaxY,
                                           float bgOpacity);

    void pollEvents(bool& outShouldQuit);
    void beginFrame();

    void renderVisualNovelFrame(
        bool hasBackground,
        const std::string& background,
        float bgX,   float bgY,   float bgW,   float bgH,
        const std::vector<CharacterRenderData>& characters,
        const std::vector<DialogueRenderData>& dialogues,
        const std::vector<ChoiceButtonRenderData>& choices = {},
        float bgRotation = 0.0f,
        float bgParallaxX = 1.0f,
        float bgParallaxY = 1.0f,
        float bgOpacity = 1.0f
    );

    void renderVisualNovelFrame(
        bool hasBackground,
        const std::string& background,
        float bgX,   float bgY,   float bgW,   float bgH,
        const std::vector<CharacterRenderData>& characters,
        const DialogueRenderData& dialogueData
    );

    void renderVisualNovelFrame(
        bool hasBackground,
        const std::string& background,
        float bgX,   float bgY,   float bgW,   float bgH,
        const std::vector<CharacterRenderData>& characters,
        bool hasDialogueBox,
        const std::string& speaker,
        const std::string& dialogue,
        float dlgX,  float dlgY,  float dlgW,  float dlgH
    );
    /// Renders a pre-packed frame. Forwards member-wise to the canonical
    /// overload above; the render boundary owns how a frame becomes pixels.
    void renderComposedFrame(const ComposedFrame& frame);
    /// MS-6: pause-menu overlay (dim + panel + rows + hint) in virtual
    /// 1920x1080 layout. No-op unless view.open; safe on offscreen surfaces.
    void renderPauseMenuOverlay(const Rowl::Core::PauseMenuView& view);
    /// D04-2: AUTO / SKIP capsules, drawn on a freshly rendered story frame
    /// (renderComposedFrame decides when).
    void renderPlayerBadges(bool autoBadge, bool skipBadge);
    /// D07-2: achievement toast capsule, top-centre of the 1920x1080 canvas
    /// ("Basarim: X"); drawn on a freshly rendered story frame like the badges.
    void renderUnlockToast(const std::string& text);
    /// D02: frees the cached slot-thumbnail texture (renderer teardown).
    void releasePauseThumbnail();
    /// D07-2: frees the gallery thumbnail cache (renderer teardown).
    void releaseGalleryThumbnails();
    /// Test seam: gallery thumbnail textures currently cached.
    size_t getGalleryThumbnailCount() const { return m_galleryThumbs.size(); }
    void endFrame();

    /// Last-frame profile accessors also report a cache hit: when an identical
    /// frame is skipped all four read back as 0.0 for that frame.
    uint64_t lastFrameContentHash() const { return m_lastFrameContentHash; }
    bool lastFrameReusedCache() const { return m_lastFrameReusedCache; }
    void shutdown();

    bool isOpen()          const { return m_isOpen; }
    uint32_t getWidth()    const { return m_width; }
    uint32_t getHeight()   const { return m_height; }
    bool isEmbedded()      const { return m_isEmbedded; }
    bool isOffscreen()     const { return m_isOffscreen; }

    SDL_Texture* loadTexture(const std::string& filename);
    void clearTextureCache();
    // #164: render-cihaz reset kurtarma. TARGETS_RESET/DEVICE_RESET sonrasi
    // VRAM'daki her doku suphelidir: doku onbellegi + MSDF atlas/shader durumu
    // bastan kurulur (init re-entrant'tir: giriste shutdown). Sayac kurtarma
    // gozlemlenebilirligini kilit-testine verir.
    void rebuildRenderResources();
    uint64_t getRenderDeviceRebuildCount() const { return m_renderDeviceRebuildCount; }
    // A3-tur6 (hygiene): remount, negatif-hükümleri (missing + budget-dışı +
    // font-miss) geçersiz kılar — mount-öncesi miss, mount-sonrası zehirli
    // kalmasın diye. Pozitif önbelleklere DOKUNMAZ (dar invalidasyon).
    void invalidateMissingCaches();
    size_t getNegativeTextureCacheSize() const { return m_missingTextureCache.size(); }
    size_t getMissingFontCacheSize() const { return m_missingFontCache.size(); }
    size_t getTextureCacheTextureCount() const { return m_textureMemoryBytes.size(); }
    uint64_t getTextureCacheBytes() const;
    uint64_t getTextureCacheBudgetBytes() const { return m_textureCacheBudgetBytes; }
    uint64_t getTextureCacheEvictionCount() const { return m_textureCacheEvictionCount; }
    double getLastFrameTextureLoadMilliseconds() const { return m_lastFrameTextureLoadMilliseconds; }
    double getLastFrameNonTextureRenderMilliseconds() const { return m_lastFrameNonTextureRenderMilliseconds; }
    double getLastFrameTextRasterizationMilliseconds() const { return m_lastFrameTextRasterizationMilliseconds; }
    double getLastFrameRendererFlushMilliseconds() const { return m_lastFrameRendererFlushMilliseconds; }
    void setTextureCacheBudgetBytes(uint64_t bytes);
    FontRenderer* getFontRenderer() const { return m_fontRenderer.get(); }
    /// T1: the renderer the menus / badges / toast are drawn AND measured with
    /// (the theme's `fonts.menu` when it loaded, else the project font), and
    /// likewise for the dialogue text and the speaker tag.
    FontRenderer* getMenuFontRenderer() const {
        return m_themeMenuFont && m_themeMenuFont->isLoaded() ? m_themeMenuFont.get() : m_fontRenderer.get();
    }
    FontRenderer* getDialogueFontRenderer() const {
        return m_themeDialogueFont && m_themeDialogueFont->isLoaded() ? m_themeDialogueFont.get()
                                                                       : m_fontRenderer.get();
    }
    FontRenderer* getSpeakerFontRenderer() const {
        return m_themeSpeakerFont && m_themeSpeakerFont->isLoaded() ? m_themeSpeakerFont.get()
                                                                      : m_fontRenderer.get();
    }
    /// T2: problems found with the theme's images (missing / unreadable / too large),
    /// one line each, collected when the theme is installed. Merged into GetUiThemeJson.
    const std::vector<std::string>& skinDiagnostics() const { return m_skinDiagnostics; }
    /// T2: a theme cursor image is installed on the window.
    bool hasThemeCursor() const { return m_themeCursor != nullptr; }
    /// T2: seconds the UI clock has run (drives the continue-indicator animation).
    double uiClock() const { return m_uiClock; }
    /// T1: installs the player-UI theme (and its optional high-contrast
    /// palette), (re)loads the theme fonts from the VFS and invalidates every
    /// cached frame, including the baked badge / toast state.
    void setUiTheme(const Rowl::State::UiTheme& theme,
                    const std::optional<Rowl::State::UiTheme>& highContrast = std::nullopt);
    /// The theme in force right now (the high-contrast palette while the
    /// player's high-contrast setting is on and the theme has one).
    const Rowl::State::UiTheme& uiTheme() const {
        return (m_themeHc && m_fontRenderer && m_fontRenderer->highContrast()) ? *m_themeHc : m_theme;
    }
    /// Test seam: the theme fonts that loaded (menu, dialogue, speaker).
    bool hasThemeFont(int which) const {
        const auto& font = which == 0 ? m_themeMenuFont : (which == 1 ? m_themeDialogueFont : m_themeSpeakerFont);
        return font && font->isLoaded();
    }
    /// Faz 3 Dilim 5 — visits the main renderer and every cached button
    /// renderer so accessibility display settings apply uniformly.
    template <typename Fn> void forEachFontRenderer(Fn&& fn) {
        if (m_fontRenderer) fn(*m_fontRenderer);
        if (m_themeMenuFont) fn(*m_themeMenuFont);
        if (m_themeDialogueFont) fn(*m_themeDialogueFont);
        if (m_themeSpeakerFont) fn(*m_themeSpeakerFont);
        for (auto& entry : m_buttonFontCache)
            if (entry.second) fn(*entry.second);
    }
    /// D05-2: display settings (text scale, contrast) change glyph sizes without
    /// touching scene content; the static-frame cache must re-render then.
    void invalidateDisplayCache() { m_frameCacheValid = false; }
    void reloadFonts();

    /**
     * Renders a 2D sprite/texture at virtual canvas coordinates (default 1920x1080),
     * automatically projected to physical window coordinates via AspectGuardian.
     */
    void drawSprite(const std::string& filename,
                    float virtualX,
                    float virtualY,
                    float virtualWidth,
                    float virtualHeight,
                    float opacity = 1.0f);

    SDL_Renderer* getRenderer() const { return m_sdlRenderer; }
    bool isGpuMsdfAvailable() const { return m_msdfRenderState != nullptr; }

    Camera2D* getCamera() const { return m_camera.get(); }
    TransitionManager* getTransitionManager() const { return m_transitionManager.get(); }

    // #162: yakalama-başarısızlığında false — kapı-reddi/coalesce sessiz
    // no-op BAŞARIDIR (true, D6-#147 semantiği korunur).
    bool startTransition(const std::string& kind, float durationSeconds, const std::string& colorHex = "");
    bool isTransitionActive() const;

    // Screen Visual FX Pipeline (Tint, Flash, Vignette Post-Process)
    void triggerScreenFlash(uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity = 1.0f);
    void triggerScreenFlashHex(const std::string& colorHex, float durationSeconds, float intensity = 1.0f);
    bool isScreenFlashActive() const;
    float getScreenFlashProgress() const;

    void setScreenTint(uint8_t r, uint8_t g, uint8_t b, float opacity);
    void setScreenTintHex(const std::string& colorHex, float opacity);
    void clearScreenTint();
    float getScreenTintOpacity() const;
    bool hasScreenTint() const;

    void setVignette(float intensity, float radius = 0.75f, const std::string& colorHex = "#000000");
    float getVignetteIntensity() const;
    bool isVignetteActive() const;

    // #163: pencere-FX snapshot çifti. Başarısız sahne güncellemesi ScreenFx
    // skalelerini birebir geri yazar (transition/flash/tint/vignette); geçiş
    // descriptor'u Engine tarafında karşılaştırılıp yetim kalan abort edilir.
    struct ScreenFxState {
        bool flashActive = false;
        uint8_t flashR = 255;
        uint8_t flashG = 255;
        uint8_t flashB = 255;
        float flashDuration = 0.0f;
        float flashElapsed = 0.0f;
        float flashIntensity = 1.0f;

        bool hasTint = false;
        uint8_t tintR = 0;
        uint8_t tintG = 0;
        uint8_t tintB = 0;
        float tintOpacity = 0.0f;

        bool vignetteEnabled = false;
        float vignetteIntensity = 0.0f;
        float vignetteRadius = 0.75f;
        uint8_t vignetteR = 0;
        uint8_t vignetteG = 0;
        uint8_t vignetteB = 0;
    };
    ScreenFxState getScreenFxState() const { return m_screenFx; }
    void restoreScreenFxState(const ScreenFxState& state);

    void update(float dt);

private:
    void initFontRenderer();
    void initGpuMsdfRenderer();
    void shutdownGpuMsdfRenderer();
    bool renderGpuMsdfText(const std::string&, float, float, float, SDL_Color);
    bool evictTexturesToFit(uint64_t incomingBytes);
    void destroyCachedTexture(SDL_Texture* texture);
    void touchTexture(SDL_Texture* texture);
    void rememberMissingTexture(std::string path);

    SDL_Window*   m_sdlWindow         = nullptr;
    SDL_Renderer* m_sdlRenderer       = nullptr;
    SDL_Surface*  m_offscreenSurface  = nullptr;
    std::unordered_map<std::string, SDL_Texture*> m_textureCache;
    // D02: pause/title slot thumbnail, one texture keyed "slot|saved_at".
    SDL_Texture* m_pauseThumbTexture = nullptr;
    std::string m_pauseThumbKey;
    std::string m_pauseThumbFailedKey;
    void drawPauseThumbnail(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics);

    // ── T2: skins (window_skin.cpp) ──
    /// How one skin will be painted: the image (null = colours only) and the
    /// fill / border colours that apply after the surface's own defaults.
    struct SkinPaint {
        SDL_Texture* texture = nullptr;
        const Rowl::State::UiSkin* skin = nullptr;
        bool fillOn = false;
        SDL_Color fill{0, 0, 0, 0};
        bool borderOn = false;
        SDL_Color border{0, 0, 0, 0};
        int rings = 0;       ///< border thickness, physical px
        float radius = 0.0f; ///< corner radius, physical px
    };
    /// Loads the image (or notes it as missing) and merges the skin's colours with the
    /// surface's own: with an image the surface colours are NOT drawn unless the skin names them.
    SkinPaint resolveSkin(const Rowl::State::UiSkin& skin,
                          const std::optional<Rowl::State::UiColor>& baseFill,
                          const std::optional<Rowl::State::UiColor>& baseBorder,
                          float baseBorderPx, float scale);
    /// True when `skin` would put something on screen: its picture loads or it names a colour.
    /// A skin whose picture is unusable (and names no colour) is as good as absent: the surface
    /// then draws exactly as it did before skins.
    bool skinDraws(const Rowl::State::UiSkin& skin);
    void paintSkinFill(const SDL_FRect& rect, const SkinPaint& paint);
    void paintSkinImage(const SDL_FRect& rect, const SkinPaint& paint, float scale);
    void paintSkinBorder(const SDL_FRect& rect, const SkinPaint& paint);
    /// fill + image + border in that order. True when the image was drawn.
    bool paintSkin(const SDL_FRect& rect, const Rowl::State::UiSkin& skin,
                   const std::optional<Rowl::State::UiColor>& baseFill,
                   const std::optional<Rowl::State::UiColor>& baseBorder,
                   float baseBorderPx, float scale);
    /// Image only (no colours), e.g. the title logo or an icon.
    bool paintSkinImageOnly(const SDL_FRect& rect, const Rowl::State::UiSkin& skin, float scale);
    void probeThemeImages();
    void forgetTexture(const std::string& path);
    void applyThemeCursor();
    void noteSkinDiagnostic(const std::string& line);
    std::vector<std::string> m_skinDiagnostics;
    SDL_Cursor* m_themeCursor = nullptr;
    double m_uiClock = 0.0;
    /// Panel / row skin sets that answer for the page being drawn.
    const Rowl::State::UiSkinSet& panelSkinFor(const std::string& page) const;
    const Rowl::State::UiSkinSet& rowSkinFor(const std::string& page) const;
    /// True while a continue-indicator animation needs a fresh frame each tick.
    bool continueIndicatorAnimating(const std::vector<DialogueRenderData>& dialogues,
                                    const std::vector<ChoiceButtonRenderData>& choices) const;
    void drawContinueIndicator(const DialogueRenderData& dlg, const SDL_FRect& box, float scale);
    bool dialogueTextComplete(const DialogueRenderData& dlg, float scaledWidth, float scale);
    /// Title screen background / logo / story-frame hiding.
    bool drawTitleBackdrop(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics);
    void drawTitleLogo(const ViewportMetrics& metrics);

    /// D-K3: konusmaci etiketinin tek yerlesimi (genislik + cizilecek metin + sekil).
    /// Etiket genisligi, cizilen sekil ve kisaltma ayni olcumden (yazi boyutu dahil)
    /// gelir; ad asla sarilmaz, kutu sinirini asarsa "…" ile kisalir.
    struct SpeakerTagLayout {
        std::string text;
        float tagWidth = 0.0f;
        std::shared_ptr<const Rowl::Text::ShapedText> shaped;
    };
    SpeakerTagLayout layoutSpeakerTag(const DialogueRenderData& dlg, float scaledDialogueWidth,
                                      float scale) const;
    // D07-2: gallery thumbnails. Each visible cell needs a small texture; going
    // through loadTexture would hold 1920x1080 RGBA (8 MB) per cell and thrash
    // the 64 MB cache, so the grid keeps its own downscaled copies (at most
    // kMaxGalleryThumbs, least-recently-used out). The full-screen viewer uses
    // loadTexture (one image at a time).
    struct GalleryThumb {
        SDL_Texture* texture = nullptr;
        uint64_t lastUsed = 0;
    };
    std::unordered_map<std::string, GalleryThumb> m_galleryThumbs;
    std::unordered_set<std::string> m_galleryThumbFailed;
    uint64_t m_galleryThumbClock = 0;
    int m_galleryThumbDecodesThisFrame = 0;
    SDL_Texture* galleryThumbnail(const std::string& filename);
    void drawExtrasGraphics(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics);
    void drawExtrasText(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics);
    void renderGalleryViewer(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics);
    void drawOverlayText(const std::string& text, float x, float y, float px, SDL_Color color,
                         float w, float h, const char* align);
    std::string fitOverlayText(const std::string& text, float px, float maxWidth);
    static void fillCapsule(SDL_Renderer* renderer, const SDL_FRect& rect, SDL_Color color);
    /// T1: rectangle outline `rings` physical pixels thick (rings 1 == SDL_RenderRect).
    static void strokeRect(SDL_Renderer* renderer, const SDL_FRect& rect, int rings);
    // D07-2: toast text currently baked into the cached frame pixels (see
    // m_bakedBadges); a different wanted text forces a re-render.
    std::string m_bakedToast;
    std::unordered_map<SDL_Texture*, uint64_t> m_textureMemoryBytes;
    std::unordered_map<SDL_Texture*, uint64_t> m_textureLastUsed;
    std::unordered_set<std::string> m_missingTextureCache;
    // These assets exist but do not fit the active device budget. They are
    // retried when the host raises that budget, unlike genuinely missing files.
    std::unordered_set<std::string> m_budgetRejectedTextureCache;
    std::unique_ptr<FontRenderer> m_fontRenderer;
    // T1: player-UI theme + its optional fonts (see setUiTheme).
    Rowl::State::UiTheme m_theme;
    std::optional<Rowl::State::UiTheme> m_themeHc;
    std::unique_ptr<FontRenderer> m_themeMenuFont;
    std::unique_ptr<FontRenderer> m_themeDialogueFont;
    std::unique_ptr<FontRenderer> m_themeSpeakerFont;
    std::unique_ptr<FontRenderer> loadThemeFont(const std::string& path);
    SDL_GPUShader* m_msdfFragmentShader = nullptr;
    SDL_GPURenderState* m_msdfRenderState = nullptr;
    std::unique_ptr<MsdfRenderer> m_msdfRenderer;
    SDL_Texture* m_msdfAtlasTexture = nullptr;
    // #164: render-cihaz reset sonrasi kurtarma sayaci (TARGETS/DEVICE_RESET).
    uint64_t m_renderDeviceRebuildCount = 0;
    // A3-tur2 (log-only): sessiz MSDF kapilarinin tek-sefer INFO bayragi.
    bool m_msdfSkipReasonLogged = false;
    std::unordered_map<std::string, std::unique_ptr<FontRenderer>> m_buttonFontCache;
    // A3-tur6 (hygiene): yüklenemeyen buton-font adları — texture yolundaki
    // m_missingTextureCache emsali; kare-başı VFS+parse tekrarını keser.
    std::unordered_set<std::string> m_missingFontCache;

    uint32_t m_width       = 1920;
    uint32_t m_height      = 1080;
    uint64_t m_textureCacheBudgetBytes = 64ULL * 1024ULL * 1024ULL;
    uint64_t m_textureUseClock = 0;
    uint64_t m_textureCacheEvictionCount = 0;
    double m_lastFrameTextureLoadMilliseconds = 0.0;
    double m_lastFrameNonTextureRenderMilliseconds = 0.0;
    double m_lastFrameTextRasterizationMilliseconds = 0.0;
    double m_lastFrameRendererFlushMilliseconds = 0.0;
    bool m_collectingFrameProfile = false;
    bool m_isOpen          = false;
    bool m_initialized     = false;
    bool m_isEmbedded      = false; // true → rendering into host control
    bool m_isOffscreen     = false; // true → rendering to RGBA32 surface
    std::function<void(const Rowl::Platform::RuntimeInputEvent&)> m_inputHandler;
    // #60: SDL metin-girdisi enable durumu (kenar-tetiklemeli Start/Stop).
    bool m_textInputEnabled = false;
    std::unordered_map<int64_t, std::pair<float, float>> m_touchStarts;
    Rowl::VFS::VFSManager* m_vfs = nullptr;
    std::shared_ptr<Rowl::VFS::VFSManager> m_ownedVfs;
    std::unique_ptr<Camera2D> m_camera;
    std::unique_ptr<TransitionManager> m_transitionManager;
    // Identical-frame cache: when the packed content and all dynamic render
    // state hash equal to the last presented frame — and no camera move,
    // transition or flash is in flight — the re-render is skipped and the
    // readable pixel surface keeps the previous identical pixels.
    uint64_t m_lastFrameContentHash = 0;
    bool m_frameCacheValid = false;
    bool m_lastFrameReusedCache = false;
    uint64_t hashFrameContent(bool hasBackground, const std::string& background,
                              float bgX, float bgY, float bgW, float bgH,
                              const std::vector<CharacterRenderData>& characters,
                              const std::vector<DialogueRenderData>& dialogues,
                              const std::vector<ChoiceButtonRenderData>& choices,
                              float bgRotation, float bgParallaxX, float bgParallaxY,
                              float bgOpacity) const;
    void invalidateFrameCache() { m_frameCacheValid = false; }
    // D04-2: bit 0 = AUTO, bit 1 = SKIP capsule currently baked into the
    // cached frame pixels. renderComposedFrame forces a re-render whenever it
    // differs from the wanted set, so a stale capsule never survives.
    uint8_t m_bakedBadges = 0;

    ScreenFxState m_screenFx;
    SDL_Texture* m_vignetteTexture = nullptr;
    void ensureVignetteTexture();
    void renderScreenEffects(const ViewportMetrics& metrics);

    bool m_videoLeaseHeld = false;
    uint32_t m_eventWindowId = 0;
    Rowl::VFS::VFSManager& vfs() const;
};

} // namespace Rowl::Render
