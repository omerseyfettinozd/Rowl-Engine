#pragma once

#include "rowl/render/window.hpp"
#include "rowl/i18n/localization_manager.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/platform/platform_host.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/state/session_persistence.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/core/story_graph.hpp"
#include "rowl/core/story_runtime.hpp"
#include <array>
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
    /// Re-resolves every live dialogue line (speaker + text) from its
    /// stored original against the current locale, re-syncs the legacy
    /// single-dialogue state, and invalidates the shape cache. Called by
    /// updateScene assembly and after a successful SetLocale so the
    /// visible language flips without a node change.
    void refreshActiveDialogueLocalization();

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
    void setSaveDirectory(const std::string& saveDir);
    std::string getSaveDirectory() const;
    std::filesystem::path getSaveDirectoryPath() const;
    std::string getProfileDirectory() const;
    std::filesystem::path getProfileDirectoryPath() const;
    void setBgmTransitionDefaults(std::string kind, float durationSeconds);
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
    // Audio components are applied while a scene is refreshed as well as when
    // a story node is entered. Keep the entry marker separate so inspector
    // preview refreshes cannot retrigger a one-shot SFX.
    uint64_t m_lastSfxPlaybackNodeId = 0;

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
    int32_t m_activeQuickSlot = 0;
    // A2b: pause slot-page occupancy cache. getPauseMenuView runs per UI
    // refresh (up to every frame from the editor preview); 10 slots x 2
    // stats per call is pure syscall overhead. Rebuilt on menu open and
    // after any slot mutation (save/delete); a stale "dolu" from an
    // out-of-band delete only costs one fail-closed load. Mutable: the view
    // builder is const, the cache is an internal memo.
    mutable bool m_pauseSlotCacheValid = false;
    mutable std::array<bool, 10> m_pauseSlotPresent{};
    bool m_windowAudioSuspended = false;
    float m_autoAdvanceElapsed = 0.0f;
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
    // MS-6 pause-menu internals: keyboard and pointer funnels share these.
    int pauseMenuRowCount() const;
    void pauseMenuMoveSelection(int direction);
    void pauseMenuAdjustSelected(int direction);
    void menuActivateSelected();
    void menuChooseSlot(int32_t slotIndex);
    void menuBack();
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
