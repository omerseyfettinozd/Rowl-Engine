#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace Rowl::Core {

Engine* Engine::s_instance = nullptr;

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

    // Load story graph from disk
    loadStoryGraphFile();

    m_initialized = true;
    m_isRunning   = true;
    return true;
}

void Engine::setPlayState(bool isPlaying) {
    m_isPlaying = isPlaying;
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
    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        const auto& startNode = it->second;
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

    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        const auto& node = it->second;
        if (!node.nextNodes.empty() && choiceIndex < node.nextNodes.size()) {
            uint64_t nextId = node.nextNodes[choiceIndex].nodeId;
            if (nextId != 0 && m_storyNodes.find(nextId) != m_storyNodes.end()) {
                m_currentNodeId = nextId;
            }
        }
        // If end of story (no next nodes), m_currentNodeId remains on the last frame.

        auto nextIt = m_storyNodes.find(m_currentNodeId);
        if (nextIt != m_storyNodes.end()) {
            const auto& nextNode = nextIt->second;
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
            ROWL_LOG_INFO("▶ Active Node #" + std::to_string(m_currentNodeId) +
                          " (" + nextNode.speaker + "): " + nextNode.dialogue);
        }
    } else {
        resetToStartNode();
    }
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
    if (!speaker.empty())    m_activeSpeaker    = speaker;
    if (!dialogue.empty())   m_activeDialogue   = dialogue;
    if (!background.empty()) m_activeBackground = background;
    m_activeBackgroundX      = bgX;
    m_activeBackgroundY      = bgY;
    m_activeBackgroundWidth  = bgW;
    m_activeBackgroundHeight = bgH;
    if (!character.empty())  m_activeCharacter  = character;
    m_activeCharacterX       = charX;
    m_activeCharacterY       = charY;
    m_activeCharacterWidth   = charW;
    m_activeCharacterHeight  = charH;
    m_activeDialogueBoxX     = dlgX;
    m_activeDialogueBoxY     = dlgY;
    m_activeDialogueBoxWidth = dlgW;
    m_activeDialogueBoxHeight = dlgH;

    ROWL_LOG_INFO("Scene Updated → Speaker: '" + m_activeSpeaker + "', Dialogue: '" +
                  m_activeDialogue + "', BG: '" + m_activeBackground + "'");
}

void Engine::parseStoryGraphJson(const std::string& jsonContent) {
    if (jsonContent.empty()) return;
    try {
        auto data = nlohmann::json::parse(jsonContent);
        std::unordered_map<uint64_t, StoryNode> parsedNodes;

        uint64_t parsedStartId = data.value("start_node_id", static_cast<uint64_t>(101));

        if (data.contains("nodes") && data["nodes"].is_array()) {
            for (const auto& nodeJson : data["nodes"]) {
                StoryNode n;
                n.id              = nodeJson.value("id",               static_cast<uint64_t>(0));
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

                if (nodeJson.contains("next_nodes") && nodeJson["next_nodes"].is_array()) {
                    for (const auto& nextJson : nodeJson["next_nodes"]) {
                        StoryNode::NextNode next;
                        next.nodeId = nextJson.value("id",    static_cast<uint64_t>(0));
                        next.label  = nextJson.value("label", std::string{});
                        if (next.nodeId != 0) n.nextNodes.push_back(next);
                    }
                } else if (nodeJson.contains("next_id")) {
                    uint64_t nextId = nodeJson.value("next_id", static_cast<uint64_t>(0));
                    if (nextId != 0) n.nextNodes.push_back({nextId, ""});
                }

                if (n.id != 0) parsedNodes[n.id] = n;
            }
        }

        if (!parsedNodes.empty()) {
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

            if (m_storyNodes.count(m_currentNodeId)) {
                const auto& startNode = m_storyNodes[m_currentNodeId];
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
                ROWL_LOG_INFO("Story graph loaded: " + std::to_string(m_storyNodes.size()) +
                              " nodes. Start node #" + std::to_string(m_currentNodeId));
            }
        }

    } catch (const nlohmann::json::parse_error& e) {
        ROWL_LOG_ERROR("Story graph JSON parse error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        ROWL_LOG_ERROR("Story graph load error: " + std::string(e.what()));
    }
}

void Engine::loadStoryGraphFromPath(const std::string& jsonPath) {
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
        if (std::filesystem::exists(path)) {
            std::ifstream f(path);
            if (f.is_open()) {
                try {
                    nlohmann::json data = nlohmann::json::parse(f);
                    uint64_t nodeId = data.value("node_id", static_cast<uint64_t>(0));
                    if (nodeId != 0) m_currentNodeId = nodeId;

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

    bool shouldQuit = false;
    m_window->pollEvents(shouldQuit);
    if (shouldQuit) {
        m_isRunning = false;
        return;
    }

    m_window->renderVisualNovelFrame(
        m_activeSpeaker,    m_activeDialogue,
        m_activeBackground,
        m_activeBackgroundX,  m_activeBackgroundY,
        m_activeBackgroundWidth, m_activeBackgroundHeight,
        m_activeCharacter,
        m_activeCharacterX,  m_activeCharacterY,
        m_activeCharacterWidth, m_activeCharacterHeight,
        m_activeDialogueBoxX, m_activeDialogueBoxY,
        m_activeDialogueBoxWidth, m_activeDialogueBoxHeight
    );
    m_window->endFrame();
}

void Engine::run() {
    if (!m_initialized) {
        ROWL_LOG_ERROR("Engine run() called without initialization!");
        return;
    }

    ROWL_LOG_INFO("Entering standalone render loop...");

    auto lastTime = std::chrono::high_resolution_clock::now();
    while (m_isRunning) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        if (dt > 0.25f) dt = 0.25f;
        if (dt < 0.0f)  dt = 0.0f;
        step(dt);
    }

    ROWL_LOG_INFO("Engine render loop finished.");
    shutdown();
}

void Engine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Rowl Engine...");

    if (m_window) {
        m_window->shutdown();
        m_window.reset();
    }

    m_isRunning   = false;
    m_initialized = false;
    ROWL_LOG_INFO("Engine shutdown complete.");
}

} // namespace Rowl::Core