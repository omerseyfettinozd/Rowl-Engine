#pragma once

#include "rowl/render/window.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/core/runtime_context.hpp"
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

/// Component data container for the component-based node architecture.
struct ComponentData {
    std::string type;       // "speaker", "background", "character", "dialogue_box", "audio"
    std::string id;         // Unique component instance ID
    bool enabled = true;
    nlohmann::json data;    // Component-specific JSON payload
};

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

struct StoryNode {
    uint64_t id = 0;
    std::string speaker;
    std::string dialogue;
    std::string background;
    float backgroundX      = 0.0f;
    float backgroundY      = 0.0f;
    float backgroundWidth  = 1920.0f;
    float backgroundHeight = 1080.0f;
    std::string character;
    float characterX       = 1440.0f;
    float characterY       = 340.0f;
    float characterWidth   = 360.0f;
    float characterHeight  = 540.0f;
    float characterScale   = 1.0f;
    float dialogueBoxX     = 80.0f;
    float dialogueBoxY     = 860.0f;
    float dialogueBoxWidth = 1760.0f;
    float dialogueBoxHeight = 180.0f;

    // Branching: multiple next nodes with optional choice labels
    struct NextNode {
        uint64_t nodeId = 0;
        std::string label;    // e.g. "Option A", "Accept", "Refuse"
        std::string optionId; // Stable ID; never use display order as identity.
    };
    std::vector<NextNode> nextNodes;

    // Component-based data (v2 format)
    std::vector<ComponentData> components;
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

    /// Legacy test-observability helper. Runtime code must use explicit
    /// ownership and never depend on this process-wide convenience pointer.
    static Engine& instance();

    RuntimeContext* getContext() const { return m_context.get(); }
    std::shared_ptr<RuntimeContext> getContextShared() const { return m_context; }
    Rowl::VFS::VFSManager* getVfs() const;

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
    const EngineConfig& getConfig() const { return m_config; }
    Rowl::Render::Window* getWindow() const { return m_window.get(); }
    Rowl::Render::Camera2D* getCamera() const { return m_window ? m_window->getCamera() : nullptr; }
    Rowl::Render::TransitionManager* getTransitionManager() const { return m_window ? m_window->getTransitionManager() : nullptr; }
    void startTransition(const std::string& kind, float durationSeconds, const std::string& colorHex = "");
    bool isTransitionActive() const { return m_window && m_window->isTransitionActive(); }

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
        float dlgW = 1760.0f, float dlgH = 180.0f
    );

    /// Updates the scene from a JSON string containing component data.
    /// Used by the editor's component-based architecture.
    void updateSceneFromComponents(const std::string& componentsJson);

    void loadActiveStoryFile();
    void loadStoryGraphFile();

    /**
     * Loads a story graph from a specific file path.
     * Used by the C-API / embedded mode (no CWD search needed).
     */
    /** Returns false and records a diagnostic when the graph is unavailable or invalid. */
    bool loadStoryGraphFromPath(const std::string& jsonPath);

    /** Loads a story graph through the active virtual file system. */
    bool loadStoryGraphFromVfs(const std::string& vfsPath);

    const std::string& getLastStoryGraphLoadError() const { return m_lastStoryGraphLoadError; }

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
    const uint8_t* getPixelBuffer(uint32_t* outW, uint32_t* outH) const;

    // ── Active scene getters ───────────────────────────────────────────────
    std::string getActiveSpeaker()      const { return m_activeSpeaker; }
    std::string getActiveDialogue()     const { return m_activeDialogue; }
    std::string getActiveBackground()   const { return m_activeBackground; }
    std::string getActiveCharacter()    const { return m_activeCharacter; }
    float getActiveCharacterX()         const { return m_activeCharacterX; }
    float getActiveCharacterY()         const { return m_activeCharacterY; }
    float getActiveCharacterWidth()     const { return m_activeCharacterWidth; }
    float getActiveCharacterHeight()    const { return m_activeCharacterHeight; }
    float getActiveDialogueBoxX()       const { return m_activeDialogueData.x; }
    float getActiveDialogueBoxY()       const { return m_activeDialogueData.y; }
    float getActiveDialogueBoxWidth()   const { return m_activeDialogueData.width; }
    float getActiveDialogueBoxHeight()  const { return m_activeDialogueData.height; }
    const Rowl::Render::DialogueRenderData& getActiveDialogueData() const { return m_activeDialogueData; }
    const std::vector<Rowl::Render::DialogueRenderData>& getActiveDialogues() const { return m_activeDialogues; }
    uint64_t getCurrentNodeId()         const { return m_currentNodeId; }
    Rowl::Scene::Scene* getScene()       const { return m_scene.get(); }
    Rowl::Audio::AudioEngine* getAudio() const { return m_audio.get(); }

    // ── Save / Load Slots & State Persistence ──────────────────────────────
    bool saveGameSlot(int32_t slotIndex);
    bool loadGameSlot(int32_t slotIndex);
    bool hasSaveSlot(int32_t slotIndex) const;
    bool deleteSaveSlot(int32_t slotIndex);
    bool rewind(uint64_t steps = 1);
    uint64_t getCurrentStepId() const;
    void setSaveDirectory(const std::string& saveDir) { m_saveDirectory = saveDir; }
    std::string getSaveDirectory() const { return m_saveDirectory; }
    void setBgmTransitionDefaults(std::string kind, float durationSeconds);
    std::shared_ptr<const Rowl::State::GameState> getGameState() const { return m_gameState; }

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
    static std::mutex s_legacyInstanceMutex;
    static std::vector<Engine*> s_legacyInstances;

    std::shared_ptr<RuntimeContext> m_context;
    EngineConfig m_config;
    std::unique_ptr<Rowl::Render::Window> m_window;
    std::unique_ptr<Rowl::Scene::Scene>   m_scene;
    std::unique_ptr<Rowl::Audio::AudioEngine> m_audio;
    std::shared_ptr<const Rowl::State::GameState> m_gameState;
    std::unique_ptr<Rowl::Scripting::LuaSandbox>  m_luaSandbox;
    std::string m_saveDirectory = "saves";
    std::string m_defaultBgmTransition = "instant";
    float m_defaultBgmTransitionDurationSeconds = 1.0f;

    // External window handle (embedded / single-window mode)
    void*    m_externalWindowHandle = nullptr;
    uint32_t m_externalWindowWidth  = 0;
    uint32_t m_externalWindowHeight = 0;

    std::unordered_map<uint64_t, StoryNode> m_storyNodes;
    uint64_t m_startNodeId   = 101;
    uint64_t m_currentNodeId = 101;

    bool m_hasBackground   = true;
    bool m_hasDialogueBox  = true;
    std::string m_activeSpeaker    = "Evelyn";
    std::string m_activeDialogue   = "Welcome to Rowl Engine!";
    std::string m_activeBackground = "bg_beach_sunset.png";
    float m_activeBackgroundX      = 0.0f;
    float m_activeBackgroundY      = 0.0f;
    float m_activeBackgroundWidth  = 1920.0f;
    float m_activeBackgroundHeight = 1080.0f;
    std::string m_activeCharacter  = "spr_evelyn.png";
    float m_activeCharacterX       = 1440.0f;
    float m_activeCharacterY       = 340.0f;
    float m_activeCharacterWidth   = 360.0f;
    float m_activeCharacterHeight  = 540.0f;
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
    std::vector<ScriptRuntimeStatus> m_scriptRuntimeStatuses;
    uint64_t m_lastRecordedDialogueNodeId = 0;
    // Audio components are applied while a scene is refreshed as well as when
    // a story node is entered. Keep the entry marker separate so inspector
    // preview refreshes cannot retrigger a one-shot SFX.
    uint64_t m_lastSfxPlaybackNodeId = 0;

    bool m_isRunning    = false;
    bool m_initialized  = false;
    bool m_isPlaying    = false;
    float m_autoAdvanceElapsed = 0.0f;
    float m_textSpeedMultiplier = 1.0f;
    float m_autoAdvanceDelayOffset = 0.0f;
    // Parsing is transactional. Increment only after a fully validated graph
    // replaces active state so callers can distinguish rejection from success.
    uint64_t m_storyGraphRevision = 0;
    std::string m_lastStoryGraphLoadError;

    void parseStoryGraphJson(const std::string& jsonContent);
    void restoreAudioStateFromGameState();
    void deactivateScripts();
    void activateScripts(const std::vector<nlohmann::json>& scripts);
    void markScriptStatus(const std::string& moduleId, const std::string& sourcePath,
                          const std::string& state, const std::string& error = {});
    void recordActiveDialogueHistory();
    bool areActiveDialoguesComplete() const;
};

} // namespace Rowl::Core
