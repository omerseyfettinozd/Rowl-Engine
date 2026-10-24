#pragma once

#include "rowl/render/window.hpp"
#include "rowl/scene/character_fx.hpp"
#include "rowl/i18n/localization_manager.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/player_ui_strings.hpp"
#include "rowl/platform/game_identity.hpp"
#include "rowl/platform/platform_host.hpp"
#include "rowl/state/game_data_layout.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/state/player_profile.hpp"
#include "rowl/state/session_persistence.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/core/story_graph.hpp"
#include "rowl/core/story_runtime.hpp"
#include <array>
#include <bitset>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace Rowl::Scene {
class Scene;
}

namespace Rowl::Audio {
class AudioEngine;
}

namespace Rowl::Scripting {
class LuaSandbox;
}

namespace Rowl::Core {

using Rowl::Render::CharacterRenderData;
using Rowl::Render::DialogueRenderData;
using Rowl::Render::ChoiceButtonRenderData;

/// D04-1: which lines the player-driven Skip may pass. Names match the profile
/// "skip_mode" values ("off" | "read_only" | "all").
enum class SkipPolicy { Off, ReadOnly, All };

struct EngineConfig {
    std::string appName     = "Rowl Engine Game";
    uint32_t virtualWidth   = 1920;
    uint32_t virtualHeight  = 1080;
    bool isIpcMode          = false; // Legacy field — kept for config compat, ignored
    std::string pipeId      = "";    // Legacy field — ignored in embedded mode
    bool vsync              = true;
    bool standaloneWindow   = false; // When true, creates a visible top-level SDL3 window
};

/// A bounded, editor-facing snapshot of one active Lua component. It contains
/// no executable source and is safe to expose through the C API.
struct ScriptRuntimeStatus {
    std::string moduleId;
    std::string sourcePath;
    std::string state;
    std::string lastError;
};

class Engine {
public:
    explicit Engine(std::shared_ptr<RuntimeContext> context = nullptr);
    ~Engine();

    // Disable copy/move
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    RuntimeContext* getContext() const { return m_context.get(); }
    std::shared_ptr<RuntimeContext> getContextShared() const { return m_context; }
    Rowl::VFS::VFSManager* getVfs() const;
    std::shared_ptr<Rowl::Platform::PlatformHost> getPlatformHost() const;

    // ── Lifecycle ──────────────────────────────────────────────────────────

    bool initialize(const EngineConfig& config = EngineConfig{});
    void run();          // Standalone blocking loop (runtime-only mode)
    void step(float deltaTime);
    void shutdown();

    // ── Native window embedding (Single-Window / Embedded mode) ───────────

    /**
     * Provide a native OS window handle so the engine renders inside the
     * host UI control (e.g. Avalonia NativeControlHost) instead of
     * creating its own SDL3 top-level window.
     *
     * Must be called BEFORE initialize().
     *
     * nativeHandle:
     *   Windows  → HWND (cast to void*)
     *   Linux X11→ Window / unsigned long (cast to void*)
     *   macOS    → NSView* (cast to void*)
     *   Android  → ANativeWindow*
     *   iOS      → UIView*
     */
    void setExternalWindowHandle(void* nativeHandle, uint32_t w, uint32_t h);

    // ── Queries ────────────────────────────────────────────────────────────

    bool isRunning() const { return m_isRunning; }
    // D1 (B1b #108-#132): C-API pre-init guard'ları için fail-closed
    // bayrağı. Non-virtual, üye eklemez — ABI'yi etkilemez.
    bool isInitialized() const { return m_initialized; }
    const EngineConfig& getConfig() const { return m_config; }
    Rowl::Render::Window* getWindow() const { return m_window.get(); }
    Rowl::Render::Camera2D* getCamera() const { return m_window ? m_window->getCamera() : nullptr; }
    Rowl::Render::TransitionManager* getTransitionManager() const { return m_window ? m_window->getTransitionManager() : nullptr; }
    void startTransition(const std::string& kind, float durationSeconds, const std::string& colorHex = "");
    bool isTransitionActive() const { return m_window && m_window->isTransitionActive(); }
    // E2a (yalniz-eklemeli): sprite-sheet saati + gozlemlenebilirlik.
    // advanceSpriteSheets step'ten cagrilir; state JSON C API'ye tasinir.
    void advanceSpriteSheets(float deltaTime);
    std::string spriteSheetStateJson() const;
    bool anySpriteSheetAnimating() const;

    // Camera Shake Presets & Profiles
    void triggerCameraShakePreset(const std::string& preset, float intensityMultiplier = 1.0f, float durationOverride = 0.0f);
    void triggerCameraShakeProfile(float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY);
    float getCameraShakeOffsetX() const;
    float getCameraShakeOffsetY() const;

    // Screen Visual FX Pipeline (Flash, Tint, Vignette)
    void triggerScreenFlash(uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity = 1.0f);
    void triggerScreenFlashHex(const std::string& colorHex, float durationSeconds, float intensity = 1.0f);
    bool isScreenFlashActive() const;

    void setScreenTint(uint8_t r, uint8_t g, uint8_t b, float opacity);
    void setScreenTintHex(const std::string& colorHex, float opacity);
    void clearScreenTint();
    float getScreenTintOpacity() const;

    void setVignette(float intensity, float radius = 0.75f, const std::string& colorHex = "#000000");
    float getVignetteIntensity() const;

    // ── Scene / story API ─────────────────────────────────────────────────

    void updateActiveScene(
        const std::string& speaker,
        const std::string& dialogue,
        const std::string& background,
        float bgX = 0.0f, float bgY = 0.0f,
        float bgW = 1920.0f, float bgH = 1080.0f,
        const std::string& character = "",
        float charX = 1440.0f, float charY = 340.0f,
        float charW = 360.0f,  float charH = 540.0f,
        float dlgX = 80.0f,  float dlgY = 860.0f,
        float dlgW = 1760.0f, float dlgH = 180.0f,
        float bgRot = 0.0f,
        float charRot = 0.0f
    );

    /// Updates the scene from a JSON string containing component data.
    /// Used by the editor's component-based architecture.
    /// #86: transactional — a throw mid-update restores the previous scene
    /// visuals AND the live audio mixer AND the camera (snapshot/restore).
    /// #20/#21: camera/FX/script applications are deferred to a
    /// post-validation atomic phase (throw leaves them unapplied instead of
    /// half-applied); the script set + parallax/opacity + SFX marker join
    /// the snapshot, and failure/success is signalled on the context result
    /// (readable via RowlEngine_GetLastResultCode).
    void updateSceneFromComponents(const std::string& componentsJson,
                                   bool replayEntryEffects = true);
    /// A2a-tur2: JSON overload — callers holding a parsed document skip the
    /// dump/parse roundtrip. Owns the snapshot/restore contract; the string
    /// version only parses and delegates.
    /// #86: see the string overload note — the catch re-applies the audio
    /// mixer snapshot and the camera snapshot, not just scene visuals.
    void updateSceneFromComponents(const nlohmann::json& components,
                                   bool replayEntryEffects = true);

    /// #122: true when an overlay was applied; a missing overlay is not a
    /// failure (bare sessions keep the default cursor).
    bool loadActiveStoryFile();
    bool loadStoryGraphFile();

    /**
     * Loads a story graph from a specific file path.
     * Used by the C-API / embedded mode (no CWD search needed).
     */
    /** Returns false and records a diagnostic when the graph is unavailable or invalid. */
    bool loadStoryGraphFromPath(const std::string& jsonPath);

    /** Loads a story graph through the active virtual file system. */
    bool loadStoryGraphFromVfs(const std::string& vfsPath);

    const std::string& getLastStoryGraphLoadError() const {
        return m_storyRuntime.lastLoadError();
    }
    /// #122: records a SPECIFIC story-boot diagnosis (dead remount root,
    /// parse/IO rejection). Always overwrites — each new operation's verdict
    /// replaces the previous one (same convention as loadStoryGraphFromPath's
    /// clearLoadError). A later successful commit() clears the channel.
    void recordStoryGraphCause(const std::string& detail);
    /// #122: records a GENERIC total-miss note. Fills only an empty channel
    /// so it never overwrites a more specific diagnosis recorded above.
    /// Used by loadStoryGraphFile's total miss and by SetProjectDirectory's
    /// remount failure (void C API surface cannot return the diagnosis
    /// itself).
    void recordStoryGraphMiss(const std::string& detail);

    // choiceIndex: which branch to follow (0 = first). Default 0 for backward compat.
    // While a choice menu is presented this only completes a typing line; the
    // menu is answered through advanceToChoice.
    void advanceToNextNode(uint32_t choiceIndex = 0);
    /// Advances by the stable option ID stored in graph v4. Returns false for
    /// missing/disabled options or when dialogue typewriter input consumed it.
    bool advanceToChoice(const std::string& optionId);
    bool handlePointerDown(float physicalX, float physicalY);

    // ── Playback & Offscreen buffer API ───────────────────────────────────
    void setPlayState(bool isPlaying);
    /// Player-local accessibility preferences. They deliberately do not alter
    /// the serialized story graph or save state.
    void setTextSpeedMultiplier(float multiplier);
    void setAutoAdvanceDelayOffset(float seconds);

    // ── D04-1: player-driven Auto / Skip ──────────────────────────────────
    // Separate from the author's per-line auto_advance flag (which is
    // untouched). Auto waits (delay + chars*0.04/textSpeed, clamped to
    // [0.8, 30] s; also for the node's own voice clip) once the line is fully
    // shown, then advances. Skip advances one node per >=0.05 s step while the
    // lines obey the policy; it stops at a choice menu, at an unread line
    // under read_only, at the chain end, and on pause / title / backlog /
    // preferences / load / rewind / manual advance. Session-only state.
    void setAutoMode(bool enabled);
    bool isAutoMode() const { return m_autoMode; }
    /// Auto toggle as the player does it (A key): flips the mode and writes
    /// the profile's auto_enabled (a read-only/unmounted profile refuses
    /// silently).
    void toggleAutoMode();
    /// Refused (false, state unchanged) while the policy is Off or the menu /
    /// title / backlog is up. Turning it off always succeeds.
    bool setSkipActive(bool active);
    bool isSkipActive() const { return m_skipActive; }
    /// True while skip is requested by S or by holding Ctrl.
    bool isSkipEffective() const { return (m_skipActive || m_skipHold) && m_skipPolicy != SkipPolicy::Off; }
    void setSkipPolicy(SkipPolicy policy);
    SkipPolicy getSkipPolicy() const { return m_skipPolicy; }
    /// NaN keeps the last value; finite values clamp to [0, 10].
    void setAutoDelay(float seconds);
    float getAutoDelay() const { return m_autoDelay; }
    /// Were all lines of the current node already read when they were first
    /// presented (before the presentation marked them)? Fail-closed: a line
    /// without a content id counts as unread; a node without lines is true.
    bool activeLinesReadBeforePresentation() const { return m_activeLinesReadBeforePresentation; }
    /// Seconds of the current auto wait still to go (0 when auto is idle or
    /// only the voice clip is awaited).
    float getAutoWaitRemaining() const { return m_autoWaitRemaining; }
    /// {auto, skip_active, skip_policy, auto_delay, line_read_before,
    ///  auto_wait_remaining}
    std::string getPlayerDriverStateJson() const;
    bool isPlaying() const { return m_isPlaying; }
    void resetToStartNode();
    /// Ends the live session without touching the presented scene: play state
    /// off, lua session cleared, cursor back to start, step/history reset.
    /// Unlike resetToStartNode() there is no scene re-hydration, so no replay
    /// side effect can re-arm the live markers. Mount boundary helper.
    void endSession();
    const uint8_t* getPixelBuffer(uint32_t* outW, uint32_t* outH) const;
    /// Pitch-aware overload (MS-0 contract). outPitch receives the surface
    /// row stride in bytes; may exceed (*outW)*4. Any out-param may be null.
    const uint8_t* getPixelBuffer(uint32_t* outW, uint32_t* outH, uint32_t* outPitch) const;
    // W8-d(3): texture-cache / last-frame gozlemleri icin Engine facade'u.
    // Pencere bosken sifir doner (c_api_render'daki checked->getWindow()
    // zincirinin birebir karsiligi); C ABI imzasi degismez (additive).
    uint32_t getTextureCacheTextureCount() const;
    uint64_t getTextureCacheBytes() const;
    uint64_t getTextureCacheBudgetBytes() const;
    uint64_t getTextureCacheEvictionCount() const;
    double getLastFrameTextureLoadMilliseconds() const;
    double getLastFrameNonTextureRenderMilliseconds() const;
    double getLastFrameTextRasterizationMilliseconds() const;
    double getLastFrameRendererFlushMilliseconds() const;
    void setTextureCacheBudgetBytes(uint64_t bytes);

    // ── Active scene getters ───────────────────────────────────────────────
    std::string getActiveSpeaker()      const { return m_activeSpeaker; }
    std::string getActiveDialogue()     const { return m_activeDialogue; }
    std::string getActiveBackground()   const { return m_activeBackground; }
    std::string getActiveCharacter()    const { return m_activeCharacter; }
    float getActiveCharacterX()         const { return m_activeCharacterX; }
    float getActiveCharacterY()         const { return m_activeCharacterY; }
    float getActiveCharacterWidth()     const { return m_activeCharacterWidth; }
    float getActiveCharacterHeight()    const { return m_activeCharacterHeight; }
    float getActiveCharacterRotation()  const { return m_activeCharacterRotation; }
    float getActiveBackgroundRotation() const { return m_activeBackgroundRotation; }
    float getActiveBackgroundParallaxX() const { return m_activeBackgroundParallaxX; }
    float getActiveBackgroundParallaxY() const { return m_activeBackgroundParallaxY; }
    float getActiveBackgroundOpacity()   const { return m_activeBackgroundOpacity; }
    /// B6 (#8): rejects non-finite inputs (keeps last-valid) and clamps
    /// finite values to [-8,8]; same contract as JSON hydration.
    void setBackgroundParallax(float px, float py) {
        if (!std::isfinite(px) || !std::isfinite(py)) return;
        m_activeBackgroundParallaxX = std::clamp(px, -8.0f, 8.0f);
        m_activeBackgroundParallaxY = std::clamp(py, -8.0f, 8.0f);
    }
    float getActiveDialogueBoxX()       const { return m_activeDialogueData.x; }
    float getActiveDialogueBoxY()       const { return m_activeDialogueData.y; }
    float getActiveDialogueBoxWidth()   const { return m_activeDialogueData.width; }
    float getActiveDialogueBoxHeight()  const { return m_activeDialogueData.height; }
    const Rowl::Render::DialogueRenderData& getActiveDialogueData() const { return m_activeDialogueData; }
    const std::vector<Rowl::Render::DialogueRenderData>& getActiveDialogues() const { return m_activeDialogues; }
    const std::vector<Rowl::Render::CharacterRenderData>& getActiveCharacters() const { return m_activeCharacters; }
    const std::vector<Rowl::Render::ChoiceButtonRenderData>& getActiveChoiceButtons() const {
        return m_activeChoiceButtons;
    }
    uint64_t getCurrentNodeId()         const { return m_storyRuntime.currentNodeId(); }
    std::string getCurrentChapterId() const { return m_storyRuntime.currentChapterId(); }
    const StoryGraphDocument& getStoryGraphDocument() const { return m_storyRuntime.document(); }
    Rowl::Scene::Scene* getScene()       const { return m_scene.get(); }
    Rowl::Audio::AudioEngine* getAudio() const { return m_audio.get(); }

    // ── Localization (Faz 3 Dilim 1 + locale kümesi) ───────────────────
    // The manager owns every locale rule (tags, catalogs, fallback
    // chain); the Engine only resolves assembled dialogue lines against
    // it at updateScene time and on SetLocale. Catalogs load on project
    // mount (see Rowl::I18n::applyProjectLocalesToEngine).
    Rowl::I18n::LocalizationManager& getLocalization() { return m_localization; }
    const Rowl::I18n::LocalizationManager& getLocalization() const {
        return m_localization;
    }
    /// D05-1: language of the player's own menus, derived from the current
    /// locale on every call (never cached: SetLocale flips the next frame).
    UiLang getUiLanguage() const {
        return uiLangForLocale(m_localization.getLocale(), m_localization.isLocaleDeclared());
    }
    /// Re-resolves every live dialogue line (speaker + text) from its
    /// stored original against the current locale, re-syncs the legacy
    /// single-dialogue state, and invalidates the shape cache. Called by
    /// updateScene assembly and after a successful SetLocale so the
    /// visible language flips without a node change.
    void refreshActiveDialogueLocalization();

    // ── D05-2: accessibility state shared by the C API and the Preferences
    // page (engine_pause_prefs.cpp). The state lives in the font renderers
    // (scale / contrast) and the camera (reduced motion); with no window the
    // setters are no-ops and the getters report the defaults.
    void setTextScale(float scale);
    float textScale() const;
    void setHighContrast(bool enabled);
    bool highContrast() const;
    void setReducedMotion(bool enabled);
    bool reducedMotion() const;

    // ── Voice Blips & Audio Effects (Milestone 25) ────────────────────────
    void setDialogueVoiceBlip(const std::string& soundPath, float basePitch, float pitchVariance, int cadence, bool skipPunctuation, int channelType);
    const std::string& getDialogueVoiceBlipSound() const { return m_activeDialogueData.typewriterSound; }
    float getDialogueVoiceBlipPitch() const { return m_activeDialogueData.voiceBlipPitch; }
    float getDialogueVoiceBlipVariance() const { return m_activeDialogueData.voiceBlipPitchVariance; }
    int getDialogueVoiceBlipCadence() const { return m_activeDialogueData.voiceBlipCadence; }
    bool getDialogueVoiceBlipSkipPunctuation() const { return m_activeDialogueData.voiceBlipSkipPunctuation; }
    int getDialogueVoiceBlipChannel() const { return m_activeDialogueData.voiceBlipChannel; }
    float getDialogueVoiceBlipVolume() const { return m_activeDialogueData.voiceBlipVolume; }
    void setDialogueVoiceBlipVolume(float volume) {
        m_activeDialogueData.voiceBlipVolume = std::clamp(volume, 0.0f, 1.0f);
        for (auto& dlg : m_activeDialogues) {
            dlg.voiceBlipVolume = m_activeDialogueData.voiceBlipVolume;
        }
    }
    void playVoiceBlip(const std::string& soundPath, float pitch = 1.0f, float volume = 0.85f, int channelType = 1);
    uint32_t getVoiceBlipCount() const;
    void resetVoiceBlipCount();

    // ── E2b: karakter tween + konusan-vurgusu + dudak-senkronu + expression-harmani ──
    // Eklemeli sunum-katmani: m_activeCharacters CANLI listesi degismez; step'teki
    // kare-birlestirme bu durumu okuyup sunulan kopyayi donusturur (window.cpp'ye
    // dokunulmaz, hash/reuse kilidi etkilenmez). Tum basarisiz dogrulama false +
    // outError doldurur (C API INVALID_ARGUMENT'a esler); durum degismez.
    // Init-bagimsizdir: saf Engine uyesidir, pencere/acilis gerekmez (D1 enjeksiyon
    // riski yoktur — sprite-bekcisi + sayim-kapisi bayat girdiyi sunumda eler).
    bool startCharacterTween(int index, float x, float y, float w, float h,
                             float opacity, float durationSeconds, int easing,
                             std::string& outError);
    bool cancelCharacterTween(int index, std::string& outError);
    bool isCharacterTweenActive() const;
    bool setSpeakerFocus(int focusedIndex, float dimOpacity, std::string& outError);
    bool getSpeakerFocus(int& outIndex, float& outDim) const;
    bool setLipSyncEnabled(bool enabled);
    bool isLipSyncEnabled() const;
    bool beginExpressionBlend(float durationSeconds, std::string& outError);
    bool isExpressionBlendActive() const;
    // C2 yardimcisi: tek-roundtrip toplu-cevap (tween/focus/lipsync/harman).
    std::string characterFxSnapshotJson() const;
    // Son birlestirilen (sunulan) karakter listesi — test gozlemlenebilirligi.
    const std::vector<CharacterRenderData>& getFxComposedCharacters() const {
        return m_lastFxCharacters;
    }
    void updateCharacterFx(float dt);
    std::vector<CharacterRenderData> composeCharacterFx();

    // ── Save / Load Slots & State Persistence ──────────────────────────────
    bool saveGameSlot(int32_t slotIndex);
    /// #86: transactional restore — on success the scene, the Lua sandbox,
    /// the playtime clock AND the live audio mixer follow the loaded state;
    /// on failure the session (including mixer, camera and clock) rolls back
    /// and the call fails loud.
    bool loadGameSlot(int32_t slotIndex);
    /// MS-6: F5/F9 operate on this slot (kPauseMenuQuickSlotMin..Max).
    /// Out-of-range requests are rejected; the active slot is unchanged.
    bool setQuickSaveSlot(int32_t slotIndex);
    int32_t getQuickSaveSlot() const { return m_activeQuickSlot; }
    /// MS-6: quick save/load through the active slot (player F5/F9 path).
    bool quickSave();
    bool quickLoad();
    // ── MS-6 Pause Menu ──────────────────────────────────────────────────
    /// Opens/closes the pause menu. Opening resets navigation to the main
    /// page; closing resumes the simulation. Never quits the game — quitting
    /// requires the menu's two-step exit confirmation.
    void setPaused(bool paused);
    void togglePause() { setPaused(!m_paused); }
    bool isPaused() const { return m_paused; }
    /// D02: opens the native title screen (New Game / Continue / Load /
    /// Preferences / Exit) on top of the frozen story. Needs an initialized
    /// engine. The title never starts the story on Esc/Back; setPaused(false)
    /// from the host closes it.
    bool openTitleScreen();
    bool isTitleScreenActive() const { return m_titleActive; }
    /// D03-2: opens the dialogue backlog (Gecmis) over the frozen story.
    /// Fails (false, StateError) before init and on the title screen. Opened
    /// from the story, Back returns straight to it; opened while the pause
    /// menu is up, Back returns to the pause main page. Read-only: touches no
    /// variable, step id, history row or save slot.
    bool openBacklog();
    bool isBacklogOpen() const { return m_paused && m_pauseMode == PauseMenuMode::Backlog; }
    /// Keyboard-equivalent menu navigation (window arrows / C API / tests).
    /// Confirm activates the selected row; Back leaves sub-pages, disarms the
    /// exit confirmation, or resumes from the main page.
    void pauseMenuCommand(PauseMenuCommand command);
    /// Pointer-equivalent menu input in virtual 1920x1080 coordinates.
    /// Selecting and activating follow the same helpers as keyboard input.
    void pauseMenuClick(float virtualX, float virtualY);
    /// Editor-facing snapshot for the overlay renderer and the C API.
    PauseMenuView getPauseMenuView() const;
    std::string getPauseMenuJson() const;
    bool hasSaveSlot(int32_t slotIndex) const;
    bool deleteSaveSlot(int32_t slotIndex);
    /// #86: same rollback contract as loadGameSlot — success rewinds the
    /// scene, Lua sandbox, playtime clock and live audio mixer to the target
    /// step; failure restores the pre-rewind session and fails loud.
    bool rewind(uint64_t steps = 1);
    uint64_t getCurrentStepId() const;
    /// stepId right after the engine presented the start node (graph load,
    /// reset, endSession). The start node's own entry effects are not
    /// player progress; SetProjectDirectory's mid-session gate compares
    /// against this instead of the bare initial step (1).
    uint64_t getStartPresentationStepId() const { return m_startPresentationStepId; }
    void setSaveDirectory(const std::string& saveDir);
    std::string getSaveDirectory() const;
    std::filesystem::path getSaveDirectoryPath() const;
    std::string getProfileDirectory() const;
    std::filesystem::path getProfileDirectoryPath() const;
    void setBgmTransitionDefaults(std::string kind, float durationSeconds);
    /// D01a: identity of the mounted game (see platform/game_identity.hpp).
    void setGameIdentity(Rowl::Platform::GameIdentity identity) { m_gameIdentity = std::move(identity); }
    const Rowl::Platform::GameIdentity& getGameIdentity() const { return m_gameIdentity; }
    /// D01b: per-game data layout chosen at project mount (save dir, profile dir, migration).
    void setGameDataLayout(Rowl::State::GameDataLayout layout) { m_gameDataLayout = std::move(layout); }
    const Rowl::State::GameDataLayout& getGameDataLayout() const { return m_gameDataLayout; }
    /// D01c-1: player profile (preferences + read set) of the mounted game.
    Rowl::State::PlayerProfileStore& playerProfile() { return m_playerProfile; }
    const Rowl::State::PlayerProfileStore& playerProfile() const { return m_playerProfile; }
    /// (Re)loads the profile for the current identity + data layout; flushes the
    /// previous one first. Called by SetProjectDirectory after the layout is set.
    void openPlayerProfile();
    std::shared_ptr<const Rowl::State::GameState> getGameState() const { return m_gameState; }
    /// #86: stamps the live mixer gains (master/bgm/sfx/voice) into
    /// m_gameState WITHOUT advancing stepId (see withMixerVolumes), so later
    /// save/load/rewind restore what the player hears. Called by every
    /// persisted volume setter (pause menu + C API). No-op without audio or
    /// without a live game state.
    void commitMixerVolumesToGameState();

    // ── Scripting & Variable Evaluation ───────────────────────────────────
    Rowl::Scripting::LuaSandbox* getLuaSandbox() const { return m_luaSandbox.get(); }
    void setScriptVariable(const std::string& key, const std::string& value);
    std::string getScriptVariable(const std::string& key) const;
    bool evaluateCondition(const std::string& conditionExpr);
    bool executeScript(const std::string& scriptCode);
    const std::vector<ScriptRuntimeStatus>& getScriptRuntimeStatuses() const {
        return m_scriptRuntimeStatuses;
    }
    const std::vector<Rowl::State::DialogueHistoryEntry>& getDialogueHistory() const;
    /// D03-1: plays ONLY the voice clip of backlog row `index` (0 = oldest) on
    /// the Voice channel, cutting any previous Voice clip. Touches no
    /// GameState, story cursor, variable, read mark, history or entry-effect
    /// marker (no sfx/script/variable effect is re-run). Returns false for an
    /// out-of-range index, a voiceless row or a clip the audio side did not
    /// start (no audio engine, undecodable asset).
    bool replayBacklogVoice(size_t index);

private:
    std::shared_ptr<RuntimeContext> m_context;
    EngineConfig m_config;
    std::unique_ptr<Rowl::Render::Window> m_window;
    std::unique_ptr<Rowl::Scene::Scene>   m_scene;
    std::unique_ptr<Rowl::Audio::AudioEngine> m_audio;
    std::shared_ptr<const Rowl::State::GameState> m_gameState;
    mutable Rowl::State::SessionPersistence m_sessionPersistence;
    std::unique_ptr<Rowl::Scripting::LuaSandbox>  m_luaSandbox;
    // Faz 3 Dilim 1: manifest locales + content_id catalogs + fallback chain.
    Rowl::I18n::LocalizationManager m_localization;
    std::filesystem::path m_saveDirectoryOverride;
    Rowl::Platform::GameIdentity m_gameIdentity;
    Rowl::State::GameDataLayout m_gameDataLayout;
    Rowl::State::PlayerProfileStore m_playerProfile;
    std::string m_defaultBgmTransition = "instant";
    float m_defaultBgmTransitionDurationSeconds = 1.0f;

    // External window handle (embedded / single-window mode)
    void*    m_externalWindowHandle = nullptr;
    uint32_t m_externalWindowWidth  = 0;
    uint32_t m_externalWindowHeight = 0;

    StoryRuntime m_storyRuntime;

    bool m_hasBackground   = true;
    bool m_hasDialogueBox  = true;
    std::string m_activeSpeaker    = "Evelyn";
    std::string m_activeDialogue   = "Welcome to Rowl Engine!";
    std::string m_activeBackground = "bg_beach_sunset.png";
    float m_activeBackgroundX      = 0.0f;
    float m_activeBackgroundY      = 0.0f;
    float m_activeBackgroundWidth  = 1920.0f;
    float m_activeBackgroundHeight = 1080.0f;
    float m_activeBackgroundRotation = 0.0f;
    float m_activeBackgroundParallaxX = 1.0f;
    float m_activeBackgroundParallaxY = 1.0f;
    float m_activeBackgroundOpacity   = 1.0f;
    std::string m_activeCharacter  = "spr_evelyn.png";
    float m_activeCharacterX       = 1440.0f;
    float m_activeCharacterY       = 340.0f;
    float m_activeCharacterWidth   = 360.0f;
    float m_activeCharacterHeight  = 540.0f;
    float m_activeCharacterRotation = 0.0f;
    std::vector<CharacterRenderData> m_activeCharacters;
    float m_activeDialogueBoxX     = 80.0f;
    float m_activeDialogueBoxY     = 860.0f;
    float m_activeDialogueBoxWidth = 1760.0f;
    float m_activeDialogueBoxHeight = 180.0f;
    Rowl::Render::DialogueRenderData m_activeDialogueData;
    std::vector<Rowl::Render::DialogueRenderData> m_activeDialogues;
    std::vector<Rowl::Render::ChoiceButtonRenderData> m_activeChoiceButtons;
    bool m_hasActiveScript = false;
    std::vector<std::string> m_activeScriptModuleIds;
    // #20/#21: the loaded source per module id (parallel to
    // m_activeScriptModuleIds). The transactional scene restore reloads these
    // when a failed update tore the live set down, so a throw can never leave
    // a silent dead script behind the restored scene.
    std::vector<std::string> m_activeScriptSources;
    std::vector<ScriptRuntimeStatus> m_scriptRuntimeStatuses;
    uint64_t m_lastRecordedDialogueNodeId = 0;
    uint64_t m_startPresentationStepId = 1;
    // Audio components are applied while a scene is refreshed as well as when
    // a story node is entered. Keep the entry marker separate so inspector
    // preview refreshes cannot retrigger a one-shot SFX.
    uint64_t m_lastSfxPlaybackNodeId = 0;
    // D03-1: the active node's voice_track (first non-empty audio component
    // value; refreshed by every component scene update, replay or not, so a
    // load/rewind still knows the line has a full voice and keeps the
    // typewriter blips quiet) and the node whose clip was started on node
    // entry (the clip is cut when a different node is entered).
    std::string m_activeNodeVoiceTrack;
    uint64_t m_voiceClipNodeId = 0;

    // E2b sunum-durumu (canli listeye dokunmaz; yalnizca kare-birlestirmede
    // okunur — bkz. ustteki metot blogu).
    Rowl::Scene::CharacterTweenTrack m_charTweenTrack;
    Rowl::Scene::SpeakerFocusState m_speakerFocus;
    Rowl::Scene::LipSyncState m_lipSync;
    Rowl::Scene::ExpressionBlendState m_exprBlend;
    double m_fxClockSec = 0.0;
    std::vector<CharacterRenderData> m_lastFxCharacters;

    bool m_isRunning    = false;
    bool m_initialized  = false;
    // #123 test-only seam counter (see testArmStepThrow). Idle zero.
    int m_testStepThrowCountdown = 0;
    bool m_isPlaying    = false;
    // MS-6 player shell: pause menu + active quick-save slot.
    bool m_paused = false;
    bool m_pauseConfirmQuit = false;
    PauseMenuMode m_pauseMode = PauseMenuMode::Main;
    int m_pauseSelected = 0;
    // D03-2: backlog page. m_pauseSelected is the selected history index;
    // m_backlogTop the first visible one (kept consistent by backlogEnsureVisible,
    // mutable because the const view builder re-fits it after a font/scale change).
    mutable int m_backlogTop = 0;
    bool m_backlogFromPause = false;
    // D05-2: the Preferences page was opened from the pause main page (Back and
    // Esc return there); false = opened from the title screen.
    bool m_prefsFromPause = false;
    int32_t m_activeQuickSlot = 0;
    // A2b: pause slot-page occupancy cache. getPauseMenuView runs per UI
    // refresh (up to every frame from the editor preview); 10 slots x 2
    // stats per call is pure syscall overhead. Rebuilt on menu open and
    // after any slot mutation (save/delete); a stale "dolu" from an
    // out-of-band delete only costs one fail-closed load. Mutable: the view
    // builder is const, the cache is an internal memo.
    mutable bool m_pauseSlotCacheValid = false;
    // D02: the memo now carries metadata per quick slot (0..9) plus the
    // Continue candidate scanned over every valid slot (0..99).
    mutable std::array<PauseSlotInfo, 10> m_pauseSlotInfo{};
    mutable int m_continueSlot = -1;
    mutable std::string m_continueSavedAt;
    // D02: slots whose load failed this session (graph mismatch etc.) are
    // shown as corrupt and skipped by Continue until they are saved again.
    mutable std::bitset<100> m_slotLoadFailed;
    bool m_titleActive = false;
    UiText m_titleHint = UiText::None;  // D05-1: a key, resolved per view (locale-safe)
    bool m_windowAudioSuspended = false;
    // #160: stall-signal edge latch. Set on the first zero-dt step observed
    // while a camera tween is in flight, cleared when the tween progresses
    // or ends — the context error fires once per stall episode, not per
    // idle step.
    bool m_cameraStallSignalled = false;
    float m_autoAdvanceElapsed = 0.0f;
    // D04-1 (see setAutoMode): player-driven Auto / Skip state. Session-only
    // (the persistent bits live in the profile). m_skipHold = Ctrl is down.
    bool m_autoMode = false;
    bool m_skipActive = false;
    bool m_skipHold = false;
    SkipPolicy m_skipPolicy = SkipPolicy::ReadOnly;
    float m_autoDelay = 2.0f;
    float m_autoWaitElapsed = 0.0f;
    float m_autoWaitRemaining = 0.0f;
    float m_skipStepCredit = 0.0f;
    bool m_activeLinesReadBeforePresentation = true;
    /// Recomputes m_activeLinesReadBeforePresentation from the profile's read
    /// set. Must run BEFORE the presentation marks the lines read.
    void refreshReadSnapshot();
    /// Zeroes the Auto wait credit (manual advance, node change, load, rewind,
    /// language change, title, new game).
    void resetAutoWait();
    void stepPlayerDrivers(float deltaTime, bool advancedThisStep);
    float autoWaitSeconds() const;
    float m_textSpeedMultiplier = 1.0f;
    float m_autoAdvanceDelayOffset = 0.0f;
    // Faz 2 Dilim 4 total playtime: accumulated in step while playing and
    // unpaused, stamped into save metadata, restored on load.
    double m_playtimeSeconds = 0.0;
    Rowl::State::SaveMetadata buildSaveMetadata() const;
    // D2 (#112/#127/#136/#137): shutdown-süpürme — oturum profilini
    // (play/pause, hız/ofset, bgm-varsayılanı, sahne görünür durumu,
    // playtime, pause-menü) fabrika değerlerine döndürür. Non-virtual
    // private helper: sınıf yerleşimini değiştirmez (ABI-güvenli).
    void resetSessionProfile();
    bool parseStoryGraphJson(const std::string& jsonContent);
    Rowl::State::SessionPersistence& sessionPersistence() const;
    bool loadStoryGraphFromAssetStream(const std::string& assetPath,
                                       std::unique_ptr<std::istream> stream);
    void handleRuntimeInput(const Rowl::Platform::RuntimeInputEvent& event);
    /// MS-6: completes every typing line and reports whether any was typing.
    /// Pure elapsed-time predicate — no play-state gate — so keyboard,
    /// pointer, swipe, and choice inputs share one click-to-complete path.
    bool completeTypewriterIfTyping();
    /// Shared advance path. viaChoice = true only from advanceToChoice; a
    /// plain advance never answers a presented choice menu.
    void advanceStory(uint32_t choiceIndex, bool viaChoice);
    /// Lua gate of a choice option ("" / "true" / "1" = open; fail-closed
    /// without a sandbox).
    bool choiceConditionHolds(const std::string& condition) const;
    /// Author-enabled and its condition holds right now.
    bool choiceButtonSelectable(const Rowl::Render::ChoiceButtonRenderData& button) const;
    // MS-6 pause-menu internals: keyboard and pointer funnels share these.
    int pauseMenuRowCount() const;
    void pauseMenuMoveSelection(int direction);
    void pauseMenuAdjustSelected(int direction);
    void menuActivateSelected();
    void menuChooseSlot(int32_t slotIndex);
    void menuBack();
    // D03-2 (engine_backlog.cpp)
    struct BacklogCell {
        std::vector<std::string> lines;
        float height = 0.0f;
    };
    BacklogCell backlogCell(size_t index) const;
    void backlogEnsureVisible() const;
    void backlogMoveSelection(int direction);
    void backlogScroll(float deltaY);
    void backlogActivateSelected();
    void backlogClose();
    bool backlogClick(float virtualX, float virtualY);
    PauseBacklogView buildBacklogView() const;
    // D02 (engine_title_menu.cpp)
    void ensureSlotCache() const;
    void titleActivateSelected();
    void titleBack();
    /// D05-2: leaves the Preferences page for where it was opened from.
    void leavePreferencesPage();
    bool titleLoadSlot(int32_t slotIndex);
    void titleStartNewGame();
    bool titleContinue();
    void leaveTitleScreen();
    void markPresentedDialoguesRead();
    int pauseMenuValueRow() const;
    void persistPauseMenuPreference(int row);
    /// D05-2: Language row — cycles the game's supported locales (same path as
    /// RowlEngine_SetLocale incl. refreshActiveDialogueLocalization); no-op on a
    /// game with fewer than two switchable locales.
    void cyclePreferenceLanguage(int direction);
    std::string preferenceLanguageText() const;
    PauseMenuView getTitleMenuView() const;
    float pauseMenuVolume(int row) const;
    /// #86: setting a volume row also commits the mixer to the game state
    /// (see commitMixerVolumesToGameState) — pause-menu mixing survives
    /// save/load/rewind instead of staying session-local.
    void setPauseMenuVolume(int row, float volume);
    void applyAudioSuspension(const std::shared_ptr<Rowl::Platform::PlatformHost>& host);
    /// #86: restores the FULL persisted mixer (master/bgm/sfx/voice gains +
    /// DSP filter + BGM intent) from m_gameState. Called after every
    /// scene/load/rewind restore.
    void restoreAudioStateFromGameState();
    void deactivateScripts(bool callOnExit = true);
    // #20/#21: JSON shapes are staged (throwing field reads + VFS preload)
    // BEFORE the live script set is torn down; the staged form then activates
    // without touching JSON/VFS, so a malformed script component fails while
    // the previous scripts are still loaded.
    struct StagedScript {
        std::string source;
        std::string path;
        std::size_t index = 0;
    };
    void stageScripts(const std::vector<nlohmann::json>& scripts,
                      std::vector<StagedScript>& outStaged);
    void activateScripts(const std::vector<StagedScript>& scripts,
                         bool callOnEnter = true);
    void markScriptStatus(const std::string& moduleId, const std::string& sourcePath,
                          const std::string& state, const std::string& error = {});
    void recordActiveDialogueHistory();
    bool areActiveDialoguesComplete() const;
    // ── #86: transactional scene/load/rewind restore snapshots ─────────────
    // Live mixer + camera state captured BEFORE a mutating restore chain and
    // re-applied when the chain throws. Plain structs (no engine/audio
    // headers needed here); apply* never throws out (best-effort + WARN).
    struct AudioSnapshot {
        bool hasAudio = false;
        float masterVolume = 1.0f;
        float bgmVolume = 1.0f;
        float sfxVolume = 1.0f;
        float voiceVolume = 1.0f;
        std::string bgmPath;
        bool bgmPlaying = false;
        int dspFilter = 0; // 0=Normal,1=CaveReverb,2=Telephone,3=UnderwaterLowPass
    };
    struct CameraSnapshot {
        bool hasCamera = false;
        float x = 960.0f;
        float y = 540.0f;
        float zoom = 1.0f;
        float rotation = 0.0f;
    };
    // #20/#21: the live script set the scene update may tear down. Sources
    // ride along because sandbox unloadModule() erases the registry entry —
    // ids alone could never resurrect the previous set.
    struct ScriptSnapshot {
        bool hasActiveScript = false;
        std::vector<std::string> moduleIds;
        std::vector<std::string> sources;
        std::vector<ScriptRuntimeStatus> statuses;
    };
    AudioSnapshot captureAudioSnapshot() const;
    void applyAudioSnapshot(const AudioSnapshot& snapshot);
    CameraSnapshot captureCameraSnapshot() const;
    void applyCameraSnapshot(const CameraSnapshot& snapshot);
    ScriptSnapshot captureScriptSnapshot() const;
    // Best-effort + WARN, never throws out (catch-body safe). Reloads the
    // snapshot sources only when the live module set drifted; otherwise the
    // still-loaded modules are left running and only the members are repinned.
    void applyScriptSnapshot(const ScriptSnapshot& snapshot, bool callOnEnter);
    // #20/#21: deferred camera/FX application. The component loop only
    // stages these payloads; the post-validation phase applies them with
    // throwing field reads strictly before any device setter, so a malformed
    // field fails atomically (same precedent as the deferred audio block).
    void applyCameraComponent(const nlohmann::json& data, bool replayEntryEffects);
    void applyScreenFxComponent(const nlohmann::json& data);
public:
    /// MS-4 dirty-frame query: true when the next rendered frame cannot differ
    /// from the currently presented one (no transition, flash, camera motion,
    /// incomplete typewriter, active scripts, or scene entities). Hosts use it
    /// to skip the ~8.3 MB pixel copy; conservative by design — any doubt
    /// reports not-static so the host copies.
    bool isPreviewFrameStatic() const;

    // #123 test-only fault-injection seam (run-abort RED probe). Arms step()
    // to throw std::runtime_error instead of framing once the countdown hits
    // zero (N<=0 disarms). Idle (0) costs one integer compare per step();
    // production never arms it, so behavior is unchanged when idle.
    void testArmStepThrow(int stepsUntilThrow) { m_testStepThrowCountdown = stepsUntilThrow; }
    // #123 test-only observer for the aborted-frame playtime assertion.
    double testPlaytimeSeconds() const { return m_playtimeSeconds; }
};

} // namespace Rowl::Core
