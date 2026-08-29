#pragma once

#include "rowl/render/window.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace Rowl::Core {

/// Component data container for the component-based node architecture.
struct ComponentData {
    std::string type;       // "speaker", "background", "character", "dialogue_box", "audio"
    std::string id;         // Unique component instance ID
    bool enabled = true;
    nlohmann::json data;    // Component-specific JSON payload
};

using Rowl::Render::CharacterRenderData;

struct EngineConfig {
    std::string appName     = "Rowl Engine Game";
    uint32_t virtualWidth   = 1920;
    uint32_t virtualHeight  = 1080;
    bool isIpcMode          = false; // Legacy field — kept for config compat, ignored
    std::string pipeId      = "";    // Legacy field — ignored in embedded mode
    bool vsync              = true;
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
        std::string label; // e.g. "Option A", "Accept", "Refuse"
    };
    std::vector<NextNode> nextNodes;

    // Component-based data (v2 format)
    std::vector<ComponentData> components;
};

class Engine {
public:
    Engine();
    ~Engine();

    // Disable copy/move
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static Engine& instance();

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
    void loadStoryGraphFromPath(const std::string& jsonPath);

    // choiceIndex: which branch to follow (0 = first). Default 0 for backward compat.
    void advanceToNextNode(uint32_t choiceIndex = 0);

    // ── Playback & Offscreen buffer API ───────────────────────────────────
    void setPlayState(bool isPlaying);
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
    float getActiveDialogueBoxX()       const { return m_activeDialogueBoxX; }
    float getActiveDialogueBoxY()       const { return m_activeDialogueBoxY; }
    float getActiveDialogueBoxWidth()   const { return m_activeDialogueBoxWidth; }
    float getActiveDialogueBoxHeight()  const { return m_activeDialogueBoxHeight; }
    uint64_t getCurrentNodeId()         const { return m_currentNodeId; }

private:
    static Engine* s_instance;

    EngineConfig m_config;
    std::unique_ptr<Rowl::Render::Window> m_window;

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

    bool m_isRunning    = false;
    bool m_initialized  = false;
    bool m_isPlaying    = false;

    void parseStoryGraphJson(const std::string& jsonContent);
};

} // namespace Rowl::Core
