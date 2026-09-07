#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/scene/scene.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include <chrono>
#include <thread>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Rowl::Core {

Engine* Engine::s_instance = nullptr;
constexpr uint32_t kMaxVirtualCanvasDimension = 16'384;
constexpr uintmax_t kMaxStoryJsonBytes = 16 * 1024 * 1024;
constexpr std::size_t kMaxComponentsPerScene = 2'048;
constexpr std::size_t kMaxCharactersPerScene = 128;
constexpr std::size_t kMaxDialoguesPerScene = 128;
constexpr std::size_t kMaxChoiceButtonsPerScene = 256;
constexpr std::size_t kMaxScriptsPerScene = 32;
constexpr std::size_t kMaxAudioComponentsPerScene = 64;
constexpr std::size_t kMaxStoryNodes = 10'000;
constexpr std::size_t kMaxEdgesPerStoryNode = 4'096;

Engine::Engine() {
    s_instance = this;
}

Engine::~Engine() {
    if (m_initialized) {
        shutdown();
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

Engine& Engine::instance() {
    if (!s_instance) {
        throw std::runtime_error("Engine not initialized");
    }
    return *s_instance;
}

void Engine::setExternalWindowHandle(void* nativeHandle, uint32_t w, uint32_t h) {
    m_externalWindowHandle = nativeHandle;
    m_externalWindowWidth  = w;
    m_externalWindowHeight = h;
    ROWL_LOG_INFO("External window handle set (" + std::to_string(w) + "x" + std::to_string(h) + ")");
}

bool Engine::initialize(const EngineConfig& config) {
    if (m_initialized) {
        ROWL_LOG_WARN("Engine is already initialized.");
        return true;
    }

    if (config.virtualWidth == 0 || config.virtualHeight == 0 ||
        config.virtualWidth > kMaxVirtualCanvasDimension ||
        config.virtualHeight > kMaxVirtualCanvasDimension) {
        ROWL_LOG_ERROR("Invalid virtual canvas dimensions: " +
                       std::to_string(config.virtualWidth) + "x" + std::to_string(config.virtualHeight));
        return false;
    }

    m_config = config;
    Logger::init();

    ROWL_LOG_INFO("==================================================");
    ROWL_LOG_INFO("Initializing Rowl Engine v1.0.0 (Embedded Library Mode)");
    ROWL_LOG_INFO("App Name: " + m_config.appName);
    ROWL_LOG_INFO("Target Virtual Canvas: " + std::to_string(m_config.virtualWidth) + "x" + std::to_string(m_config.virtualHeight));
    ROWL_LOG_INFO("Mode: " + std::string(m_externalWindowHandle ? "EMBEDDED (Single-Window)" : "STANDALONE"));
    ROWL_LOG_INFO("==================================================");

    // Initialize VFS Manager
    Rowl::VFS::VFSManager::instance().initialize();

    // Initialize Render Window
    m_window = std::make_unique<Rowl::Render::Window>();

    bool windowOk = false;
    if (m_externalWindowHandle) {
        // ── Legacy embedded mode: render into host native surface ──
        windowOk = m_window->initializeEmbedded(
            m_externalWindowHandle,
            m_externalWindowWidth  > 0 ? m_externalWindowWidth  : m_config.virtualWidth,
            m_externalWindowHeight > 0 ? m_externalWindowHeight : m_config.virtualHeight,
            m_config.vsync
        );
    } else if (m_config.standaloneWindow) {
        // ── Standalone window mode: top-level SDL3 desktop window ──
        windowOk = m_window->initialize(
            m_config.appName,
            m_config.virtualWidth,
            m_config.virtualHeight,
            m_config.vsync
        );
    } else {
        // ── Offscreen Framebuffer mode (Texture Sharing / Zero-Copy) ──
        windowOk = m_window->initializeOffscreen(
            m_config.virtualWidth,
            m_config.virtualHeight
        );
    }

    if (!windowOk) {
        ROWL_LOG_ERROR("Failed to initialize render window!");
        return false;
    }

    // Initialize Entity-Component Scene Manager
    m_scene = std::make_unique<Rowl::Scene::Scene>();

    // Initialize Audio Engine Subsystem
    m_audio = std::make_unique<Rowl::Audio::AudioEngine>();
    m_audio->initialize();

    // Initialize Sandboxed Lua Scripting Environment
    m_luaSandbox = std::make_unique<Rowl::Scripting::LuaSandbox>();
    m_luaSandbox->initialize();

    // Initialize GameState Subsystem (Root Step #1)
    m_gameState = Rowl::State::GameState::createInitialState(m_startNodeId);

    // Load story graph from disk
    loadStoryGraphFile();

    m_initialized = true;
    m_isRunning   = true;
    return true;
}

void Engine::setPlayState(bool isPlaying) {
    m_isPlaying = isPlaying;
    m_activeDialogueData.isPlaying = isPlaying;
    if (isPlaying) {
        m_activeDialogueData.elapsedTypewriterTime = 0.0f;
    } else {
        m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
    }
    ROWL_LOG_INFO("Engine Play State set to: " + std::string(isPlaying ? "PLAYING" : "STOPPED"));
}

void Engine::resetToStartNode() {
    if (m_storyNodes.empty()) return;

    if (m_storyNodes.find(m_startNodeId) == m_storyNodes.end()) {
        uint64_t minId = UINT64_MAX;
        for (const auto& [id, _] : m_storyNodes) {
            if (id < minId) minId = id;
        }
        m_startNodeId = minId;
    }

    m_currentNodeId = m_startNodeId;
    m_gameState = Rowl::State::GameState::createInitialState(m_startNodeId);
    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        const auto& startNode = it->second;
        if (!startNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : startNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson.dump());
        } else {
            updateActiveScene(
                startNode.speaker, startNode.dialogue,
                startNode.background,
                startNode.backgroundX,  startNode.backgroundY,
                startNode.backgroundWidth, startNode.backgroundHeight,
                startNode.character,
                startNode.characterX,   startNode.characterY,
                startNode.characterWidth, startNode.characterHeight,
                startNode.dialogueBoxX, startNode.dialogueBoxY,
                startNode.dialogueBoxWidth, startNode.dialogueBoxHeight
            );
        }
        if (m_isPlaying) {
            for (auto& dlg : m_activeDialogues) dlg.elapsedTypewriterTime = 0.0f;
            m_activeDialogueData.elapsedTypewriterTime = 0.0f;
        } else {
            for (auto& dlg : m_activeDialogues) dlg.elapsedTypewriterTime = 9999.0f;
            m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
        }
        ROWL_LOG_INFO("Engine Reset to Start Node #" + std::to_string(m_currentNodeId));
    }
}

const uint8_t* Engine::getPixelBuffer(uint32_t* outW, uint32_t* outH) const {
    if (outW) *outW = m_window ? m_window->getWidth() : 0;
    if (outH) *outH = m_window ? m_window->getHeight() : 0;
    return m_window ? m_window->getPixelBuffer() : nullptr;
}

void Engine::advanceToNextNode(uint32_t choiceIndex) {
    if (m_storyNodes.empty()) return;

    // If typewriter is still typing out any line, clicking reveals the full text immediately
    bool anyTyping = false;
    for (const auto& dlg : m_activeDialogues) {
        if (m_isPlaying && dlg.typewriterEnabled && dlg.textSpeed > 0) {
            size_t totalCodepoints = Rowl::Render::FontRenderer::countCodepoints(dlg.dialogue);
            float msPerChar = static_cast<float>(dlg.textSpeed);
            float elapsedMs = dlg.elapsedTypewriterTime * 1000.0f;
            size_t visibleCodepoints = static_cast<size_t>(elapsedMs / msPerChar);
            if (visibleCodepoints < totalCodepoints) {
                anyTyping = true;
                break;
            }
        }
    }
    if (!anyTyping && m_isPlaying && m_activeDialogueData.typewriterEnabled && m_activeDialogueData.textSpeed > 0) {
        size_t totalCodepoints = Rowl::Render::FontRenderer::countCodepoints(m_activeDialogueData.dialogue);
        float msPerChar = static_cast<float>(m_activeDialogueData.textSpeed);
        float elapsedMs = m_activeDialogueData.elapsedTypewriterTime * 1000.0f;
        size_t visibleCodepoints = static_cast<size_t>(elapsedMs / msPerChar);
        if (visibleCodepoints < totalCodepoints) {
            anyTyping = true;
        }
    }

    if (anyTyping) {
        for (auto& dlg : m_activeDialogues) dlg.elapsedTypewriterTime = 9999.0f;
        m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
        return;
    }

    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        const auto& node = it->second;
        if (!node.nextNodes.empty() && choiceIndex < node.nextNodes.size()) {
            uint64_t nextId = node.nextNodes[choiceIndex].nodeId;
            if (nextId != 0 && m_storyNodes.find(nextId) != m_storyNodes.end()) {
                m_currentNodeId = nextId;
                if (m_gameState) {
                    m_gameState = Rowl::State::GameState::createNextState(m_gameState, m_currentNodeId);
                }
            } else {
                return; // End of story chain
            }
        } else {
            // End of story chain: stay on last frame (do not loop back)
            ROWL_LOG_INFO("End of story chain reached on Node #" + std::to_string(m_currentNodeId));
            return;
        }

        auto nextIt = m_storyNodes.find(m_currentNodeId);
        if (nextIt != m_storyNodes.end()) {
            const auto& nextNode = nextIt->second;
            if (!nextNode.components.empty()) {
                nlohmann::json compsJson = nlohmann::json::array();
                for (const auto& c : nextNode.components) {
                    compsJson.push_back({
                        {"type", c.type},
                        {"id", c.id},
                        {"enabled", c.enabled},
                        {"data", c.data}
                    });
                }
                updateSceneFromComponents(compsJson.dump());
            } else {
                updateActiveScene(
                    nextNode.speaker, nextNode.dialogue,
                    nextNode.background,
                    nextNode.backgroundX, nextNode.backgroundY,
                    nextNode.backgroundWidth, nextNode.backgroundHeight,
                    nextNode.character,
                    nextNode.characterX, nextNode.characterY,
                    nextNode.characterWidth, nextNode.characterHeight,
                    nextNode.dialogueBoxX, nextNode.dialogueBoxY,
                    nextNode.dialogueBoxWidth, nextNode.dialogueBoxHeight
                );
            }
            if (m_isPlaying) {
                for (auto& dlg : m_activeDialogues) dlg.elapsedTypewriterTime = 0.0f;
                m_activeDialogueData.elapsedTypewriterTime = 0.0f;
            } else {
                for (auto& dlg : m_activeDialogues) dlg.elapsedTypewriterTime = 9999.0f;
                m_activeDialogueData.elapsedTypewriterTime = 9999.0f;
            }
            ROWL_LOG_INFO("▶ Active Node #" + std::to_string(m_currentNodeId) +
                          " (" + nextNode.speaker + "): " + nextNode.dialogue);
        }
    } else {
        resetToStartNode();
    }
}

bool Engine::advanceToChoice(const std::string& optionId) {
    if (optionId.empty()) return false;
    const auto nodeIt = m_storyNodes.find(m_currentNodeId);
    if (nodeIt == m_storyNodes.end()) return false;

    const auto& options = nodeIt->second.nextNodes;
    const auto optionIt = std::find_if(options.begin(), options.end(),
        [&optionId](const StoryNode::NextNode& option) { return option.optionId == optionId; });
    if (optionIt == options.end()) {
        ROWL_LOG_WARN("Choice option not found on Node #" + std::to_string(m_currentNodeId) + ": " + optionId);
        return false;
    }

    const auto index = static_cast<uint32_t>(std::distance(options.begin(), optionIt));
    advanceToNextNode(index);
    return m_currentNodeId == optionIt->nodeId;
}

bool Engine::handlePointerDown(float physicalX, float physicalY) {
    if (!m_window || m_activeChoiceButtons.empty()) return false;
    const auto metrics = Rowl::Render::AspectGuardian::calculateViewport(
        m_window->getWidth(), m_window->getHeight(), 1920, 1080);
    if (metrics.scaleFactor <= 0.0f) return false;
    const float x = (physicalX - static_cast<float>(metrics.x)) / metrics.scaleFactor;
    const float y = (physicalY - static_cast<float>(metrics.y)) / metrics.scaleFactor;
    for (auto it = m_activeChoiceButtons.rbegin(); it != m_activeChoiceButtons.rend(); ++it) {
        const auto& button = *it;
        if (!button.enabled) continue;
        if (x >= button.x && x <= button.x + button.width &&
            y >= button.y && y <= button.y + button.height) {
            advanceToChoice(button.optionId);
            return true;
        }
    }
    return false;
}

void Engine::updateActiveScene(
    const std::string& speaker,
    const std::string& dialogue,
    const std::string& background,
    float bgX, float bgY, float bgW, float bgH,
    const std::string& character,
    float charX, float charY, float charW, float charH,
    float dlgX, float dlgY, float dlgW, float dlgH
) {
    m_activeChoiceButtons.clear();
    m_hasBackground = !background.empty();
    m_hasDialogueBox = !dialogue.empty() || !speaker.empty();

    // Empty values are meaningful: switching to a silent/blank frame must not
    // leak text or assets from the previously active node.
    m_activeSpeaker    = speaker;
    m_activeDialogue   = dialogue;
    m_activeBackground = background;
    m_activeBackgroundX      = bgX;
    m_activeBackgroundY      = bgY;
    m_activeBackgroundWidth  = bgW;
    m_activeBackgroundHeight = bgH;

    m_activeCharacters.clear();
    if (!character.empty()) {
        m_activeCharacter       = character;
        m_activeCharacterX       = charX;
        m_activeCharacterY       = charY;
        m_activeCharacterWidth   = charW;
        m_activeCharacterHeight  = charH;
        m_activeCharacters.push_back({character, charX, charY, charW, charH});
    } else {
        m_activeCharacter = "";
    }

    m_activeDialogueBoxX     = dlgX;
    m_activeDialogueBoxY     = dlgY;
    m_activeDialogueBoxWidth = dlgW;
    m_activeDialogueBoxHeight = dlgH;

    m_activeDialogueData.hasDialogueBox = m_hasDialogueBox;
    m_activeDialogueData.speaker = m_activeSpeaker;
    m_activeDialogueData.dialogue = m_activeDialogue;
    m_activeDialogueData.x = dlgX;
    m_activeDialogueData.y = dlgY;
    m_activeDialogueData.width = dlgW;
    m_activeDialogueData.height = dlgH;
    m_activeDialogueData.typewriterEnabled = false;

    m_activeDialogues.clear();
    if (m_hasDialogueBox) {
        m_activeDialogues.push_back(m_activeDialogueData);
    }

    ROWL_LOG_INFO("Scene Updated (Legacy) → Speaker: '" + m_activeSpeaker + "', Dialogue: '" +
                  m_activeDialogue + "', BG: '" + m_activeBackground + "'");
}

void Engine::updateSceneFromComponents(const std::string& componentsJson) {
    if (componentsJson.size() > kMaxStoryJsonBytes) {
        ROWL_LOG_ERROR("Component JSON exceeds the maximum accepted size");
        return;
    }
    const auto previousCharacters = m_activeCharacters;
    const auto previousDialogues = m_activeDialogues;
    const auto previousChoices = m_activeChoiceButtons;
    const auto previousSpeaker = m_activeSpeaker;
    const auto previousDialogue = m_activeDialogue;
    const auto previousBackground = m_activeBackground;
    const auto previousCharacter = m_activeCharacter;
    const auto previousBackgroundX = m_activeBackgroundX;
    const auto previousBackgroundY = m_activeBackgroundY;
    const auto previousBackgroundWidth = m_activeBackgroundWidth;
    const auto previousBackgroundHeight = m_activeBackgroundHeight;
    const auto previousCharacterX = m_activeCharacterX;
    const auto previousCharacterY = m_activeCharacterY;
    const auto previousCharacterWidth = m_activeCharacterWidth;
    const auto previousCharacterHeight = m_activeCharacterHeight;
    const auto previousDialogueBoxX = m_activeDialogueBoxX;
    const auto previousDialogueBoxY = m_activeDialogueBoxY;
    const auto previousDialogueBoxWidth = m_activeDialogueBoxWidth;
    const auto previousDialogueBoxHeight = m_activeDialogueBoxHeight;
    const auto previousDialogueData = m_activeDialogueData;
    const auto previousHasBackground = m_hasBackground;
    const auto previousHasDialogueBox = m_hasDialogueBox;
    const auto previousGameState = m_gameState;
    const auto previousLuaVariables = m_luaSandbox ? m_luaSandbox->getAllVariables() : std::unordered_map<std::string, std::string>{};
    const auto restorePreviousState = [&] {
        m_activeCharacters = previousCharacters;
        m_activeDialogues = previousDialogues;
        m_activeChoiceButtons = previousChoices;
        m_activeSpeaker = previousSpeaker;
        m_activeDialogue = previousDialogue;
        m_activeBackground = previousBackground;
        m_activeCharacter = previousCharacter;
        m_activeBackgroundX = previousBackgroundX;
        m_activeBackgroundY = previousBackgroundY;
        m_activeBackgroundWidth = previousBackgroundWidth;
        m_activeBackgroundHeight = previousBackgroundHeight;
        m_activeCharacterX = previousCharacterX;
        m_activeCharacterY = previousCharacterY;
        m_activeCharacterWidth = previousCharacterWidth;
        m_activeCharacterHeight = previousCharacterHeight;
        m_activeDialogueBoxX = previousDialogueBoxX;
        m_activeDialogueBoxY = previousDialogueBoxY;
        m_activeDialogueBoxWidth = previousDialogueBoxWidth;
        m_activeDialogueBoxHeight = previousDialogueBoxHeight;
        m_activeDialogueData = previousDialogueData;
        m_hasBackground = previousHasBackground;
        m_hasDialogueBox = previousHasDialogueBox;
        m_gameState = previousGameState;
        if (m_luaSandbox) {
            m_luaSandbox->clearVariables();
            for (const auto& [key, value] : previousLuaVariables) m_luaSandbox->setVariable(key, value);
        }
    };
    try {
        auto comps = nlohmann::json::parse(componentsJson);
        if (!comps.is_array()) return;
        if (comps.size() > kMaxComponentsPerScene) {
            ROWL_LOG_ERROR("Component JSON exceeds the maximum component count");
            return;
        }

        // Validate the complete external payload before clearing the active
        // scene. A schema error must not leave the renderer half-updated.
        std::size_t dialogueCount = 0;
        std::size_t characterCount = 0;
        std::size_t choiceButtonCount = 0;
        std::size_t scriptCount = 0;
        std::size_t audioComponentCount = 0;
        for (const auto& comp : comps) {
            if (!comp.is_object() || !comp.contains("type") || !comp.contains("data") ||
                !comp["type"].is_string() || !comp["data"].is_object() ||
                (comp.contains("enabled") && !comp["enabled"].is_boolean())) {
                ROWL_LOG_ERROR("Component JSON contains an invalid component schema");
                return;
            }
            if (!comp.value("enabled", true)) continue;

            const auto type = comp["type"].get<std::string>();
            if (type == "dialogue" && ++dialogueCount > kMaxDialoguesPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum dialogue count");
                return;
            }
            if (type == "character" && ++characterCount > kMaxCharactersPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum character count");
                return;
            }
            if (type == "script" && ++scriptCount > kMaxScriptsPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum script count");
                return;
            }
            if (type == "audio" && ++audioComponentCount > kMaxAudioComponentsPerScene) {
                ROWL_LOG_ERROR("Component JSON exceeds the maximum audio component count");
                return;
            }
            if (type == "choice") {
                const auto& options = comp["data"].value("options", nlohmann::json::array());
                if (options.is_array()) {
                    choiceButtonCount += options.size();
                    if (choiceButtonCount > kMaxChoiceButtonsPerScene) {
                        ROWL_LOG_ERROR("Component JSON exceeds the maximum choice button count");
                        return;
                    }
                }
            }
        }

        m_activeCharacters.clear();
        m_activeDialogues.clear();
        m_activeChoiceButtons.clear();
        m_activeSpeaker.clear();
        m_activeDialogue.clear();
        m_activeBackground.clear();
        m_activeCharacter.clear();
        m_activeDialogueData = {};
        m_hasBackground = false;
        m_hasDialogueBox = false;
        std::vector<nlohmann::json> pendingAudioComponents;

        for (const auto& comp : comps) {
            if (!comp.contains("type") || !comp.contains("data")) continue;
            bool enabled = comp.value("enabled", true);
            if (!enabled) continue;

            std::string type = comp["type"].get<std::string>();
            const auto& data = comp["data"];

            if (type == "dialogue") {
                DialogueRenderData dlgData;
                dlgData.hasDialogueBox = true;
                dlgData.speaker = data.value("speaker", "Evelyn");
                dlgData.dialogue = data.value("dialogue", "");
                dlgData.x = data.value("x", 80.0f);
                dlgData.y = data.value("y", 860.0f);
                dlgData.width = data.value("width", 1760.0f);
                dlgData.height = data.value("height", 180.0f);
                dlgData.scale = data.value("scale", 1.0f);
                dlgData.typewriterEnabled = data.value("typewriter_enabled", true);
                dlgData.textSpeed = data.value("text_speed", 30);
                dlgData.fontSize = data.value("font_size", 24.0f);
                dlgData.speakerFontSize = data.value("speaker_font_size", 20.0f);
                dlgData.textColor = data.value("text_color", "#F1F5F9");
                dlgData.speakerColor = data.value("speaker_color", "#38BDF8");
                dlgData.textAlignment = data.value("text_alignment", "Left");
                dlgData.boxOpacity = data.value("box_opacity", 0.88f);
                dlgData.boxColor = data.value("box_color", "#0F0F1A");
                dlgData.borderColor = data.value("border_color", "#00F0FF");
                dlgData.borderThickness = data.value("border_thickness", 2.0f);
                dlgData.cornerRadius = data.value("corner_radius", 8.0f);
                dlgData.customBoxTexture = data.value("custom_box_texture", "");
                dlgData.isPlaying = m_isPlaying;
                if (m_isPlaying) {
                    dlgData.elapsedTypewriterTime = 0.0f;
                } else {
                    dlgData.elapsedTypewriterTime = 9999.0f;
                }

                m_activeDialogues.push_back(dlgData);
                m_hasDialogueBox = true;

                // Sync first dialogue component with legacy single-dialogue state
                if (m_activeDialogues.size() == 1) {
                    m_activeSpeaker = dlgData.speaker;
                    m_activeDialogue = dlgData.dialogue;
                    m_activeDialogueBoxX = dlgData.x;
                    m_activeDialogueBoxY = dlgData.y;
                    m_activeDialogueBoxWidth = dlgData.width;
                    m_activeDialogueBoxHeight = dlgData.height;
                    m_activeDialogueData = dlgData;
                }
            } else if (type == "speaker") {
                m_activeSpeaker = data.value("speaker", m_activeSpeaker);
                m_activeDialogue = data.value("dialogue", m_activeDialogue);
                m_activeDialogueData.speaker = m_activeSpeaker;
                m_activeDialogueData.dialogue = m_activeDialogue;
                if (!m_activeDialogues.empty()) {
                    m_activeDialogues[0].speaker = m_activeSpeaker;
                    m_activeDialogues[0].dialogue = m_activeDialogue;
                }
            } else if (type == "background") {
                m_activeBackground = data.value("texture", "");
                m_activeBackgroundX = data.value("x", 0.0f);
                m_activeBackgroundY = data.value("y", 0.0f);
                m_activeBackgroundWidth = data.value("width", 1920.0f);
                m_activeBackgroundHeight = data.value("height", 1080.0f);
                m_hasBackground = !m_activeBackground.empty();
            } else if (type == "character") {
                CharacterRenderData cd;
                cd.sprite = data.value("sprite", "");
                if (!cd.sprite.empty()) {
                    cd.x = data.value("x", 1440.0f);
                    cd.y = data.value("y", 340.0f);
                    cd.width = data.value("width", 360.0f);
                    cd.height = data.value("height", 540.0f);
                    m_activeCharacters.push_back(cd);

                    // Set legacy single-character fallback to first character
                    if (m_activeCharacters.size() == 1) {
                        m_activeCharacter = cd.sprite;
                        m_activeCharacterX = cd.x;
                        m_activeCharacterY = cd.y;
                        m_activeCharacterWidth = cd.width;
                        m_activeCharacterHeight = cd.height;
                    }
                }
            } else if (type == "dialogue_box") {
                m_activeDialogueBoxX = data.value("x", m_activeDialogueBoxX);
                m_activeDialogueBoxY = data.value("y", m_activeDialogueBoxY);
                m_activeDialogueBoxWidth = data.value("width", m_activeDialogueBoxWidth);
                m_activeDialogueBoxHeight = data.value("height", m_activeDialogueBoxHeight);
                m_hasDialogueBox = true;
                m_activeDialogueData.x = m_activeDialogueBoxX;
                m_activeDialogueData.y = m_activeDialogueBoxY;
                m_activeDialogueData.width = m_activeDialogueBoxWidth;
                m_activeDialogueData.height = m_activeDialogueBoxHeight;
                m_activeDialogueData.hasDialogueBox = true;
                if (!m_activeDialogues.empty()) {
                    m_activeDialogues[0].x = m_activeDialogueBoxX;
                    m_activeDialogues[0].y = m_activeDialogueBoxY;
                    m_activeDialogues[0].width = m_activeDialogueBoxWidth;
                    m_activeDialogues[0].height = m_activeDialogueBoxHeight;
                    m_activeDialogues[0].hasDialogueBox = true;
                }
            } else if (type == "audio") {
                // Defer device side effects until every component has been
                // converted successfully; a later malformed field must not
                // leave audio changed while the scene is rolled back.
                pendingAudioComponents.push_back(data);
            } else if (type == "choice" && data.contains("options") && data["options"].is_array()) {
                size_t index = 0;
                for (const auto& option : data["options"]) {
                    ChoiceButtonRenderData button;
                    button.optionId = option.value("option_id", "");
                    button.text = option.value("text", "Choice");
                    button.enabled = option.value("enabled", true);

                    // Lua condition check
                    std::string condition = option.value("condition", "");
                    if (!condition.empty() && condition != "true" && condition != "1") {
                        if (m_luaSandbox && !m_luaSandbox->evaluateCondition(condition)) {
                            button.enabled = false;
                        }
                    }

                    button.x = option.value("x", 680.0f);
                    button.y = option.value("y", 520.0f + static_cast<float>(index) * 80.0f);
                    button.width = option.value("width", 560.0f);
                    button.height = option.value("height", 64.0f);
                    button.fontSize = option.value("font_size", 22.0f);
                    button.opacity = option.value("opacity", 1.0f);
                    button.borderThickness = option.value("border_thickness", 2.0f);
                    button.cornerRadius = option.value("corner_radius", 8.0f);
                    button.textColor = option.value("text_color", "#FFFFFF");
                    button.backgroundColor = option.value("background_color", "#1E293B");
                    button.hoverColor = option.value("hover_color", "#0EA5E9");
                    button.borderColor = option.value("border_color", "#38BDF8");
                    button.textAlignment = option.value("text_alignment", "Center");
                    button.fontFamily = option.value("font_family", "Default");
                    button.backgroundImage = option.value("normal_image", option.value("background_image", ""));
                    if (!button.optionId.empty()) m_activeChoiceButtons.push_back(std::move(button));
                    ++index;
                }
            } else if (type == "variable") {
                std::string varKey = data.value("key", "");
                std::string varVal = data.value("value", "");
                std::string op = data.value("operation", "set");
                if (!varKey.empty()) {
                    if (op == "add" && m_luaSandbox) {
                        double cur = m_luaSandbox->getGlobalNumber(varKey, 0.0);
                        const double delta = std::stod(varVal);
                        double res = cur + delta;
                        if (!std::isfinite(cur) || !std::isfinite(delta) || !std::isfinite(res)) {
                            throw std::invalid_argument("Variable add requires finite numeric values");
                        }
                        m_luaSandbox->setGlobalNumber(varKey, res);
                        if (m_gameState) {
                            m_gameState = Rowl::State::GameState::createNextState(m_gameState, m_currentNodeId, varKey, std::to_string(res));
                        }
                    } else {
                        setScriptVariable(varKey, varVal);
                    }
                }
            } else if (type == "script") {
                std::string scriptCode = data.value("code", "");
                if (!scriptCode.empty()) {
                    executeScript(scriptCode);
                }
            }
        }

        if (m_activeCharacters.empty()) {
            m_activeCharacter = "";
        }

        if (m_activeDialogues.empty() && m_hasDialogueBox) {
            m_activeDialogues.push_back(m_activeDialogueData);
        }

        for (const auto& data : pendingAudioComponents) {
            std::string dsp = data.value("dsp_filter", "Normal");
            std::string bgm = data.value("bgm_track", "");
            std::string sfx = data.value("sfx_track", "");
            float vol = data.value("volume", 1.0f);
            ROWL_LOG_INFO("[Audio] Applied DSP Filter from Component: " + dsp);
            if (!m_audio) continue;
            if (dsp == "Cave" || dsp == "CaveReverb") {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::CaveReverb);
            } else if (dsp == "Telephone") {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::Telephone);
            } else if (dsp == "Underwater" || dsp == "UnderwaterLowPass") {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::UnderwaterLowPass);
            } else {
                m_audio->applyDspFilter(Rowl::Audio::DSPFilterType::Normal);
            }
            m_audio->setBgmVolume(vol);
            if (!bgm.empty() && bgm != m_audio->getCurrentBgmPath()) {
                m_audio->playAudio(bgm, Rowl::Audio::AudioChannelType::Bgm);
            }
            if (!sfx.empty()) m_audio->playAudio(sfx, Rowl::Audio::AudioChannelType::Sfx);
        }

        ROWL_LOG_INFO("Scene Updated (Components) → " + std::to_string(comps.size()) +
                      " comps, " + std::to_string(m_activeCharacters.size()) + " chars, " +
                      std::to_string(m_activeDialogues.size()) + " dlgs, HasBg: " +
                      (m_hasBackground ? "true" : "false") + ", HasDlg: " +
                      (m_hasDialogueBox ? "true" : "false"));
    } catch (const std::exception& e) {
        restorePreviousState();
        ROWL_LOG_ERROR("Failed to parse components JSON: " + std::string(e.what()));
    }
}

void Engine::parseStoryGraphJson(const std::string& jsonContent) {
    if (jsonContent.empty()) return;
    if (jsonContent.size() > kMaxStoryJsonBytes) {
        ROWL_LOG_ERROR("Story graph JSON exceeds the maximum accepted size");
        return;
    }
    try {
        auto data = nlohmann::json::parse(jsonContent);
        if (!data.is_object() || !data.contains("nodes") || !data["nodes"].is_array()) {
            ROWL_LOG_ERROR("Story graph must be an object containing a nodes array");
            return;
        }
        if (data["nodes"].size() > kMaxStoryNodes) {
            ROWL_LOG_ERROR("Story graph exceeds the maximum node count");
            return;
        }
        std::unordered_map<uint64_t, StoryNode> parsedNodes;

        uint64_t parsedStartId = data.value("start_node_id", static_cast<uint64_t>(101));

        for (const auto& nodeJson : data["nodes"]) {
            if (!nodeJson.is_object()) {
                ROWL_LOG_ERROR("Story graph contains a non-object node");
                return;
            }
                StoryNode n;
                n.id              = nodeJson.value("id",               static_cast<uint64_t>(0));
                if (n.id == 0 || parsedNodes.contains(n.id)) {
                    ROWL_LOG_ERROR("Story graph contains a missing or duplicate node ID");
                    return;
                }
                n.speaker         = nodeJson.value("speaker",          std::string{});
                n.dialogue        = nodeJson.value("dialogue",         std::string{});
                n.background      = nodeJson.value("background",       std::string{});
                n.backgroundX     = nodeJson.value("background_x",     0.0f);
                n.backgroundY     = nodeJson.value("background_y",     0.0f);
                n.backgroundWidth = nodeJson.value("background_width",  1920.0f);
                n.backgroundHeight= nodeJson.value("background_height", 1080.0f);
                n.character       = nodeJson.value("character",         std::string{});
                n.characterX      = nodeJson.value("character_x",       1440.0f);
                n.characterY      = nodeJson.value("character_y",       340.0f);
                n.characterWidth  = nodeJson.value("character_width",   360.0f);
                n.characterHeight = nodeJson.value("character_height",  540.0f);
                n.characterScale  = nodeJson.value("character_scale",   1.0f);
                n.dialogueBoxX    = nodeJson.value("dialogue_box_x",    80.0f);
                n.dialogueBoxY    = nodeJson.value("dialogue_box_y",    860.0f);
                n.dialogueBoxWidth= nodeJson.value("dialogue_box_width",1760.0f);
                n.dialogueBoxHeight=nodeJson.value("dialogue_box_height",180.0f);

                if (nodeJson.contains("components")) {
                    if (!nodeJson["components"].is_array()) {
                        ROWL_LOG_ERROR("Story graph node components must be an array");
                        return;
                    }
                    for (const auto& compJson : nodeJson["components"]) {
                        if (n.components.size() >= kMaxComponentsPerScene) {
                            ROWL_LOG_ERROR("Story graph node exceeds the maximum component count");
                            return;
                        }
                        if (!compJson.is_object()) {
                            ROWL_LOG_ERROR("Story graph contains a non-object component");
                            return;
                        }
                        ComponentData cd;
                        cd.type = compJson.value("type", "");
                        cd.id = compJson.value("id", "");
                        cd.enabled = compJson.value("enabled", true);
                        if (compJson.contains("data"))
                            cd.data = compJson["data"];
                        n.components.push_back(cd);
                    }
                }
                // Graph v3/v4 stores components under Unity-style objects.
                if (nodeJson.contains("objects") && !nodeJson["objects"].is_array()) {
                    ROWL_LOG_ERROR("Story graph node objects must be an array");
                    return;
                }
                if (nodeJson.contains("objects") && nodeJson["objects"].is_array()) {
                    for (const auto& objectJson : nodeJson["objects"]) {
                        if (!objectJson.is_object()) {
                            ROWL_LOG_ERROR("Story graph contains a non-object scene object");
                            return;
                        }
                        if (!objectJson.value("is_active", true) || !objectJson.contains("components")) continue;
                        if (!objectJson["components"].is_array()) {
                            ROWL_LOG_ERROR("Story graph object components must be an array");
                            return;
                        }
                        for (const auto& compJson : objectJson["components"]) {
                            if (n.components.size() >= kMaxComponentsPerScene) {
                                ROWL_LOG_ERROR("Story graph node exceeds the maximum component count");
                                return;
                            }
                            if (!compJson.is_object()) {
                                ROWL_LOG_ERROR("Story graph contains a non-object component");
                                return;
                            }
                            ComponentData cd;
                            cd.type = compJson.value("type", "");
                            cd.id = compJson.value("id", "");
                            cd.enabled = compJson.value("enabled", true);
                            if (compJson.contains("data")) cd.data = compJson["data"];
                            if (!cd.type.empty()) n.components.push_back(std::move(cd));
                        }
                    }
                }

                if (nodeJson.contains("next_nodes") && nodeJson["next_nodes"].is_array()) {
                    for (const auto& nextJson : nodeJson["next_nodes"]) {
                        if (!nextJson.is_object()) {
                            ROWL_LOG_ERROR("Story graph contains a non-object edge");
                            return;
                        }
                        StoryNode::NextNode next;
                        next.nodeId = nextJson.value("id",    static_cast<uint64_t>(0));
                        next.label  = nextJson.value("label", std::string{});
                        next.optionId = nextJson.value("option_id", std::string{});
                        if (next.nodeId == 0) {
                            ROWL_LOG_ERROR("Story graph contains an edge with no target node ID");
                            return;
                        }
                        if (n.nextNodes.size() >= kMaxEdgesPerStoryNode) {
                            ROWL_LOG_ERROR("Story graph node exceeds the maximum edge count");
                            return;
                        }
                        n.nextNodes.push_back(next);
                    }
                } else if (nodeJson.contains("next_nodes")) {
                    ROWL_LOG_ERROR("Story graph node next_nodes must be an array");
                    return;
                } else if (nodeJson.contains("next_id")) {
                    uint64_t nextId = nodeJson.value("next_id", static_cast<uint64_t>(0));
                    if (nextId != 0) n.nextNodes.push_back({nextId, "", ""});
                }

                parsedNodes.emplace(n.id, std::move(n));
        }

        if (parsedNodes.empty()) {
            ROWL_LOG_ERROR("Story graph does not contain any valid nodes");
            return;
        }
        for (const auto& [nodeId, node] : parsedNodes) {
            for (const auto& next : node.nextNodes) {
                if (!parsedNodes.contains(next.nodeId)) {
                    ROWL_LOG_ERROR("Story graph node #" + std::to_string(nodeId) +
                                   " references a missing node #" + std::to_string(next.nodeId));
                    return;
                }
            }
        }
        if (data.contains("start_node_id") &&
            (parsedStartId == 0 || !parsedNodes.contains(parsedStartId))) {
            ROWL_LOG_ERROR("Story graph start_node_id does not reference a node");
            return;
        }

            m_storyNodes = std::move(parsedNodes);
            m_startNodeId = parsedStartId;

            // Start at the defined start node
            if (m_startNodeId != 0 && m_storyNodes.count(m_startNodeId)) {
                m_currentNodeId = m_startNodeId;
            } else {
                uint64_t minId = UINT64_MAX;
                for (const auto& [id, _] : m_storyNodes) {
                    if (id < minId) minId = id;
                }
                m_startNodeId = minId;
                m_currentNodeId = minId;
            }

            // Loading a graph is a new story session. Keeping a previous
            // graph's history here could make Save/Load or rewind jump to a
            // node that belongs to a different graph.
            m_gameState = Rowl::State::GameState::createInitialState(m_currentNodeId);
            if (m_luaSandbox) m_luaSandbox->clearVariables();

            if (m_storyNodes.count(m_currentNodeId)) {
                const auto& startNode = m_storyNodes[m_currentNodeId];
                if (!startNode.components.empty()) {
                    nlohmann::json compsJson = nlohmann::json::array();
                    for (const auto& c : startNode.components) {
                        compsJson.push_back({
                            {"type", c.type},
                            {"id", c.id},
                            {"enabled", c.enabled},
                            {"data", c.data}
                        });
                    }
                    updateSceneFromComponents(compsJson.dump());
                } else {
                    updateActiveScene(
                        startNode.speaker, startNode.dialogue,
                        startNode.background,
                        startNode.backgroundX,  startNode.backgroundY,
                        startNode.backgroundWidth, startNode.backgroundHeight,
                        startNode.character,
                        startNode.characterX,   startNode.characterY,
                        startNode.characterWidth, startNode.characterHeight,
                        startNode.dialogueBoxX, startNode.dialogueBoxY,
                        startNode.dialogueBoxWidth, startNode.dialogueBoxHeight
                    );
                }
                ROWL_LOG_INFO("Story graph loaded: " + std::to_string(m_storyNodes.size()) +
                              " nodes. Start node #" + std::to_string(m_currentNodeId));
            }
    } catch (const nlohmann::json::parse_error& e) {
        ROWL_LOG_ERROR("Story graph JSON parse error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Story graph load error: " + std::string(e.what()));
    }
}

void Engine::loadStoryGraphFromPath(const std::string& jsonPath) {
    std::error_code fileError;
    const std::filesystem::path graphPath(jsonPath);
    if (!std::filesystem::is_regular_file(graphPath, fileError) || fileError ||
        std::filesystem::file_size(graphPath, fileError) > kMaxStoryJsonBytes || fileError) {
        ROWL_LOG_ERROR("Story graph is missing, not a regular file, or exceeds the size limit: " + jsonPath);
        return;
    }
    std::ifstream f(jsonPath);
    if (!f.is_open()) {
        ROWL_LOG_ERROR("Cannot open story graph: " + jsonPath);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    parseStoryGraphJson(content);
}

void Engine::loadStoryGraphFile() {
    std::vector<std::string> searchPaths = {
        "Assets/json/full_story_graph.json",
        "Assets/full_story_graph.json",
        "../Assets/json/full_story_graph.json",
        "../Assets/full_story_graph.json"
    };
    for (const auto& p : searchPaths) {
        if (std::filesystem::exists(p)) {
            loadStoryGraphFromPath(p);
            return;
        }
    }
    loadActiveStoryFile();
}

void Engine::loadActiveStoryFile() {
    std::vector<std::string> searchPaths = {
        "Assets/json/active_story.json",
        "Assets/active_story.json",
        "../Assets/json/active_story.json",
        "../Assets/active_story.json"
    };

    for (const auto& path : searchPaths) {
        std::error_code fileError;
        if (std::filesystem::is_regular_file(path, fileError) && !fileError &&
            std::filesystem::file_size(path, fileError) <= kMaxStoryJsonBytes && !fileError) {
            std::ifstream f(path);
            if (f.is_open()) {
                try {
                    nlohmann::json data = nlohmann::json::parse(f);
                    uint64_t nodeId = data.value("node_id", static_cast<uint64_t>(0));
                    if (nodeId != 0) m_currentNodeId = nodeId;

                    if (data.contains("components") && data["components"].is_array()) {
                        updateSceneFromComponents(data["components"].dump());
                    } else {
                        updateActiveScene(
                            data.value("speaker",          std::string{}),
                            data.value("dialogue",         std::string{}),
                            data.value("background",       std::string{}),
                            data.value("background_x",     0.0f),
                            data.value("background_y",     0.0f),
                            data.value("background_width",  1920.0f),
                            data.value("background_height", 1080.0f),
                            data.value("character",        std::string{}),
                            data.value("character_x",      1440.0f),
                            data.value("character_y",      340.0f),
                            data.value("character_width",  360.0f),
                            data.value("character_height", 540.0f),
                            data.value("dialogue_box_x",   80.0f),
                            data.value("dialogue_box_y",   860.0f),
                            data.value("dialogue_box_width",1760.0f),
                            data.value("dialogue_box_height",180.0f)
                        );
                    }
                    ROWL_LOG_INFO("Loaded active story node #" +
                                  std::to_string(m_currentNodeId) + " from: " + path);
                    return;
                } catch (const std::exception& e) {
                    ROWL_LOG_ERROR("Active story load error in " + path + ": " + e.what());
                }
            }
        }
    }
}

void Engine::step(float deltaTime) {
    if (!m_window) return;

    // C API hosts can pass arbitrary frame durations. Keep time-dependent
    // systems deterministic and avoid poisoning typewriter state with NaN.
    if (!std::isfinite(deltaTime) || deltaTime < 0.0f) {
        deltaTime = 0.0f;
    } else if (deltaTime > 0.25f) {
        deltaTime = 0.25f;
    }

    bool shouldQuit = false;
    m_window->pollEvents(shouldQuit);
    if (shouldQuit) {
        m_isRunning = false;
        return;
    }

    for (auto& dlg : m_activeDialogues) {
        dlg.isPlaying = m_isPlaying;
        if (m_isPlaying && dlg.typewriterEnabled) {
            dlg.elapsedTypewriterTime += deltaTime;
        }
    }
    m_activeDialogueData.isPlaying = m_isPlaying;
    if (m_isPlaying && m_activeDialogueData.typewriterEnabled) {
        m_activeDialogueData.elapsedTypewriterTime += deltaTime;
    }

    m_window->renderVisualNovelFrame(
        m_hasBackground,
        m_activeBackground,
        m_activeBackgroundX,  m_activeBackgroundY,
        m_activeBackgroundWidth, m_activeBackgroundHeight,
        m_activeCharacters,
        m_activeDialogues,
        m_activeChoiceButtons
    );

    // Update & Render Entity-Component Scene
    if (m_scene) {
        m_scene->update(deltaTime);
        m_scene->render(m_window.get());
    }

    if (m_audio) {
        m_audio->update();
    }

    m_window->endFrame();
}

void Engine::run() {
    if (!m_initialized) {
        ROWL_LOG_ERROR("Engine run() called without initialization!");
        return;
    }

    ROWL_LOG_INFO("Entering standalone render loop...");
    setPlayState(true);

    auto lastTime = std::chrono::high_resolution_clock::now();
    while (m_isRunning) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (dt > 0.25f) dt = 0.25f;
        if (dt < 0.0f)  dt = 0.0f;

        // step() internally calls pollEvents() and sets m_isRunning = false on quit
        step(dt);
    }

    ROWL_LOG_INFO("Engine render loop finished.");
    shutdown();
}

void Engine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Rowl Engine...");

    if (m_scene) {
        m_scene->clear();
        m_scene.reset();
    }

    if (m_audio) {
        m_audio->shutdown();
        m_audio.reset();
    }

    if (m_luaSandbox) {
        m_luaSandbox->shutdown();
        m_luaSandbox.reset();
    }

    if (m_window) {
        m_window->shutdown();
        m_window.reset();
    }

    m_isRunning   = false;
    m_initialized = false;
    ROWL_LOG_INFO("Engine shutdown complete.");
}

bool Engine::saveGameSlot(int32_t slotIndex) {
    if (!m_gameState) {
        m_gameState = Rowl::State::GameState::createInitialState(m_currentNodeId);
    }
    if (m_gameState->activeNodeId != m_currentNodeId) {
        m_gameState = Rowl::State::GameState::createNextState(m_gameState, m_currentNodeId);
    }
    return Rowl::State::GameState::saveToSlot(m_gameState, slotIndex, m_saveDirectory);
}

bool Engine::loadGameSlot(int32_t slotIndex) {
    auto loaded = Rowl::State::GameState::loadFromSlot(slotIndex, m_saveDirectory);
    if (!loaded) return false;

    m_gameState = loaded;
    m_currentNodeId = m_gameState->activeNodeId;

    // Sync variables to Lua sandbox
    if (m_luaSandbox && m_gameState->variables) {
        m_luaSandbox->clearVariables();
        for (const auto& [k, v] : m_gameState->variables->data) {
            m_luaSandbox->setVariable(k, v);
        }
    }

    // Synchronize scene to loaded node
    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        const auto& nextNode = it->second;
        if (!nextNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : nextNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson.dump());
        } else {
            updateActiveScene(
                nextNode.speaker, nextNode.dialogue,
                nextNode.background,
                nextNode.backgroundX, nextNode.backgroundY,
                nextNode.backgroundWidth, nextNode.backgroundHeight,
                nextNode.character,
                nextNode.characterX, nextNode.characterY,
                nextNode.characterWidth, nextNode.characterHeight,
                nextNode.dialogueBoxX, nextNode.dialogueBoxY,
                nextNode.dialogueBoxWidth, nextNode.dialogueBoxHeight
            );
        }
    }
    ROWL_LOG_INFO("Loaded Game Slot #" + std::to_string(slotIndex) + " → Node #" + std::to_string(m_currentNodeId));
    return true;
}

bool Engine::hasSaveSlot(int32_t slotIndex) const {
    return Rowl::State::GameState::hasSlot(slotIndex, m_saveDirectory);
}

bool Engine::deleteSaveSlot(int32_t slotIndex) {
    return Rowl::State::GameState::deleteSlot(slotIndex, m_saveDirectory);
}

bool Engine::rewind(uint64_t steps) {
    if (!m_gameState) return false;
    auto rewound = Rowl::State::GameState::rewind(m_gameState, steps);
    if (!rewound || rewound == m_gameState) return false;

    m_gameState = rewound;
    m_currentNodeId = m_gameState->activeNodeId;

    if (m_luaSandbox && m_gameState->variables) {
        m_luaSandbox->clearVariables();
        for (const auto& [k, v] : m_gameState->variables->data) {
            m_luaSandbox->setVariable(k, v);
        }
    }

    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        const auto& nextNode = it->second;
        if (!nextNode.components.empty()) {
            nlohmann::json compsJson = nlohmann::json::array();
            for (const auto& c : nextNode.components) {
                compsJson.push_back({
                    {"type", c.type},
                    {"id", c.id},
                    {"enabled", c.enabled},
                    {"data", c.data}
                });
            }
            updateSceneFromComponents(compsJson.dump());
        } else {
            updateActiveScene(
                nextNode.speaker, nextNode.dialogue,
                nextNode.background,
                nextNode.backgroundX, nextNode.backgroundY,
                nextNode.backgroundWidth, nextNode.backgroundHeight,
                nextNode.character,
                nextNode.characterX, nextNode.characterY,
                nextNode.characterWidth, nextNode.characterHeight,
                nextNode.dialogueBoxX, nextNode.dialogueBoxY,
                nextNode.dialogueBoxWidth, nextNode.dialogueBoxHeight
            );
        }
    }
    return true;
}

uint64_t Engine::getCurrentStepId() const {
    return m_gameState ? m_gameState->stepId : 0;
}

void Engine::setScriptVariable(const std::string& key, const std::string& value) {
    if (m_luaSandbox) {
        m_luaSandbox->setVariable(key, value);
    }
    if (m_gameState) {
        m_gameState = Rowl::State::GameState::createNextState(m_gameState, m_currentNodeId, key, value);
    }
}

std::string Engine::getScriptVariable(const std::string& key) const {
    if (m_luaSandbox) {
        return m_luaSandbox->getVariable(key);
    }
    if (m_gameState) {
        return m_gameState->getVariable(key);
    }
    return "";
}

bool Engine::evaluateCondition(const std::string& conditionExpr) {
    if (m_luaSandbox) {
        return m_luaSandbox->evaluateCondition(conditionExpr);
    }
    return true;
}

bool Engine::executeScript(const std::string& scriptCode) {
    if (m_luaSandbox) {
        if (!m_luaSandbox->executeString(scriptCode)) return false;

        // Scripts persist game data through rowl.var_set. Capture its complete
        // post-script snapshot as one immutable state transition so save/load
        // and rewind cannot lose a multi-variable script update.
        if (m_gameState) {
            m_gameState = Rowl::State::GameState::createNextStateWithVariables(
                m_gameState, m_currentNodeId, m_luaSandbox->getAllVariables());
        }
        return true;
    }
    return false;
}

} // namespace Rowl::Core
