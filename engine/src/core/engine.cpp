#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include <chrono>
#include <thread>
#include <fstream>
#include <filesystem>

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
    return *s_instance;
}

bool Engine::initialize(const EngineConfig& config) {
    if (m_initialized) {
        ROWL_LOG_WARN("Engine is already initialized.");
        return true;
    }

    m_config = config;
    Logger::init();

    ROWL_LOG_INFO("==================================================");
    ROWL_LOG_INFO("Initializing Rowl Engine v1.0.0 (C++20 Runtime Core)");
    ROWL_LOG_INFO("App Name: " + m_config.appName);
    ROWL_LOG_INFO("Target Virtual Canvas: " + std::to_string(m_config.virtualWidth) + "x" + std::to_string(m_config.virtualHeight));
    ROWL_LOG_INFO("IPC Mode: " + std::string(m_config.isIpcMode ? "ENABLED" : "DISABLED"));
    ROWL_LOG_INFO("==================================================");

    // Initialize VFS Manager
    Rowl::VFS::VFSManager::instance().initialize();

    // Initialize SDL3 Window Subsystem
    m_window = std::make_unique<Rowl::Render::Window>();
    if (!m_window->initialize(m_config.appName, m_config.virtualWidth, m_config.virtualHeight, m_config.vsync)) {
        ROWL_LOG_ERROR("Failed to initialize SDL3 Window!");
        return false;
    }

    // Initialize IPC Server
    m_ipcServer = std::make_unique<Rowl::IPC::IpcServer>();
    m_ipcServer->setPacketCallback([this](const Rowl::IPC::IpcPacket& packet) {
        std::string payloadStr(packet.payload.begin(), packet.payload.end());
        ROWL_LOG_INFO("[Hot-Reload IPC] Received Payload: " + payloadStr);
        
        auto extractValue = [](const std::string& json, const std::string& key) -> std::string {
            size_t pos = json.find("\"" + key + "\"");
            if (pos == std::string::npos) return "";
            size_t colon = json.find(":", pos);
            if (colon == std::string::npos) return "";
            size_t firstQuote = json.find("\"", colon);
            if (firstQuote == std::string::npos) return "";
            size_t secondQuote = json.find("\"", firstQuote + 1);
            if (secondQuote == std::string::npos) return "";
            return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
        };

        std::string speaker = extractValue(payloadStr, "speaker");
        std::string dialogue = extractValue(payloadStr, "dialogue");
        std::string background = extractValue(payloadStr, "background");
        std::string character = extractValue(payloadStr, "character");

        updateActiveScene(speaker, dialogue, background, character);
    });
    m_ipcServer->start(m_config.pipeId.empty() ? "rowl_engine_ipc" : m_config.pipeId);

    // Try loading story graph file saved by Editor
    loadStoryGraphFile();

    m_initialized = true;
    m_isRunning = true;
    return true;
}

void Engine::advanceToNextNode() {
    auto it = m_storyNodes.find(m_currentNodeId);
    if (it != m_storyNodes.end()) {
        uint64_t nextId = it->second.nextNodeId;
        if (nextId != 0 && m_storyNodes.find(nextId) != m_storyNodes.end()) {
            m_currentNodeId = nextId;
            const auto& node = m_storyNodes[m_currentNodeId];
            updateActiveScene(node.speaker, node.dialogue, node.background, node.character, node.characterX, node.characterY, node.dialogueBoxY);
            ROWL_LOG_INFO("▶ Advanced Frame to Node #" + std::to_string(m_currentNodeId) + " (" + node.speaker + "): " + node.dialogue);
        } else {
            ROWL_LOG_INFO("Reached end of connected story nodes (Current Node #" + std::to_string(m_currentNodeId) + ")");
        }
    }
}

void Engine::updateActiveScene(const std::string& speaker, const std::string& dialogue, const std::string& background, const std::string& character, float charX, float charY, float dlgBoxY) {
    if (!speaker.empty()) m_activeSpeaker = speaker;
    if (!dialogue.empty()) m_activeDialogue = dialogue;
    if (!background.empty()) m_activeBackground = background;
    if (!character.empty()) m_activeCharacter = character;
    m_activeCharacterX = charX;
    m_activeCharacterY = charY;
    m_activeDialogueBoxY = dlgBoxY;

    ROWL_LOG_INFO("Engine Scene State Updated -> Speaker: '" + m_activeSpeaker + "', Dialogue: '" + m_activeDialogue + "', BG: '" + m_activeBackground + "', Char: '" + m_activeCharacter + "' (CharPos: " + std::to_string(charX) + "," + std::to_string(charY) + ", DlgY: " + std::to_string(dlgBoxY) + ")");
}

void Engine::loadStoryGraphFile() {
    std::vector<std::string> searchPaths = {"data/full_story_graph.json", "../data/full_story_graph.json"};
    std::string graphPath;
    for (const auto& p : searchPaths) {
        if (std::filesystem::exists(p)) {
            graphPath = p;
            break;
        }
    }

    if (graphPath.empty()) {
        loadActiveStoryFile();
        return;
    }

    std::ifstream f(graphPath);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    m_storyNodes.clear();

    size_t startPos = content.find("\"start_node_id\"");
    if (startPos != std::string::npos) {
        size_t colon = content.find(":", startPos);
        if (colon != std::string::npos) {
            try {
                m_startNodeId = std::stoull(content.substr(colon + 1));
            } catch (...) {}
        }
    }

    size_t pos = 0;
    while ((pos = content.find("\"id\":", pos)) != std::string::npos) {
        size_t startNode = content.rfind("{", pos);
        size_t endNode = content.find("}", pos);
        if (startNode == std::string::npos || endNode == std::string::npos || endNode < startNode) {
            pos += 5;
            continue;
        }

        std::string nodeJson = content.substr(startNode, endNode - startNode + 1);

        auto extractStr = [](const std::string& j, const std::string& key) -> std::string {
            size_t kpos = j.find("\"" + key + "\"");
            if (kpos == std::string::npos) return "";
            size_t col = j.find(":", kpos);
            if (col == std::string::npos) return "";
            size_t q1 = j.find("\"", col);
            if (q1 == std::string::npos) return "";
            size_t q2 = j.find("\"", q1 + 1);
            if (q2 == std::string::npos) return "";
            return j.substr(q1 + 1, q2 - q1 - 1);
        };

        auto extractNum = [](const std::string& j, const std::string& key) -> uint64_t {
            size_t kpos = j.find("\"" + key + "\"");
            if (kpos == std::string::npos) return 0;
            size_t col = j.find(":", kpos);
            if (col == std::string::npos) return 0;
            try {
                return std::stoull(j.substr(col + 1));
            } catch (...) { return 0; }
        };

        auto extractFloat = [](const std::string& j, const std::string& key, float def) -> float {
            size_t kpos = j.find("\"" + key + "\"");
            if (kpos == std::string::npos) return def;
            size_t col = j.find(":", kpos);
            if (col == std::string::npos) return def;
            try {
                return std::stof(j.substr(col + 1));
            } catch (...) { return def; }
        };

        StoryNode n;
        n.id = extractNum(nodeJson, "id");
        n.speaker = extractStr(nodeJson, "speaker");
        n.dialogue = extractStr(nodeJson, "dialogue");
        n.background = extractStr(nodeJson, "background");
        n.character = extractStr(nodeJson, "character");
        n.characterX = extractFloat(nodeJson, "character_x", 1440.0f);
        n.characterY = extractFloat(nodeJson, "character_y", 340.0f);
        n.dialogueBoxY = extractFloat(nodeJson, "dialogue_box_y", 860.0f);
        n.nextNodeId = extractNum(nodeJson, "next_id");

        if (n.id != 0) {
            m_storyNodes[n.id] = n;
        }
        pos = endNode + 1;
    }

    if (!m_storyNodes.empty()) {
        if (m_storyNodes.find(m_startNodeId) == m_storyNodes.end()) {
            m_startNodeId = m_storyNodes.begin()->first;
        }
        m_currentNodeId = m_startNodeId;
        const auto& startNode = m_storyNodes[m_currentNodeId];
        updateActiveScene(startNode.speaker, startNode.dialogue, startNode.background, startNode.character, startNode.characterX, startNode.characterY, startNode.dialogueBoxY);
        ROWL_LOG_INFO("Loaded Story Graph (" + std::to_string(m_storyNodes.size()) + " nodes). Starting at Root Node #" + std::to_string(m_startNodeId));
    }
}

void Engine::loadActiveStoryFile() {
    std::vector<std::string> searchPaths = {"data/active_story.json", "../data/active_story.json"};
    for (const auto& path : searchPaths) {
        if (std::filesystem::exists(path)) {
            std::ifstream f(path);
            if (f.is_open()) {
                std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                auto extractValue = [](const std::string& json, const std::string& key) -> std::string {
                    size_t pos = json.find("\"" + key + "\"");
                    if (pos == std::string::npos) return "";
                    size_t colon = json.find(":", pos);
                    if (colon == std::string::npos) return "";
                    size_t firstQuote = json.find("\"", colon);
                    if (firstQuote == std::string::npos) return "";
                    size_t secondQuote = json.find("\"", firstQuote + 1);
                    if (secondQuote == std::string::npos) return "";
                    return json.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                };

                auto extractFloatVal = [](const std::string& json, const std::string& key, float def) -> float {
                    size_t pos = json.find("\"" + key + "\"");
                    if (pos == std::string::npos) return def;
                    size_t colon = json.find(":", pos);
                    if (colon == std::string::npos) return def;
                    try {
                        return std::stof(json.substr(colon + 1));
                    } catch (...) { return def; }
                };

                std::string speaker = extractValue(content, "speaker");
                std::string dialogue = extractValue(content, "dialogue");
                std::string background = extractValue(content, "background");
                std::string character = extractValue(content, "character");
                float charX = extractFloatVal(content, "character_x", 1440.0f);
                float charY = extractFloatVal(content, "character_y", 340.0f);
                float dlgBoxY = extractFloatVal(content, "dialogue_box_y", 860.0f);

                updateActiveScene(speaker, dialogue, background, character, charX, charY, dlgBoxY);
                ROWL_LOG_INFO("Successfully loaded active story file from: " + path);
                break;
            }
        }
    }
}

void Engine::step(float deltaTime) {
    // Poll input events and render active visual novel frame
    bool shouldQuit = false;
    if (m_window) {
        m_window->pollEvents(shouldQuit);
        if (shouldQuit) {
            m_isRunning = false;
            return;
        }

        m_window->renderVisualNovelFrame(m_activeSpeaker, m_activeDialogue, m_activeBackground, m_activeCharacter, m_activeCharacterX, m_activeCharacterY, m_activeDialogueBoxY);
        m_window->endFrame();
    }
}

void Engine::run() {
    if (!m_initialized) {
        ROWL_LOG_ERROR("Engine run requested without initialization!");
        return;
    }

    ROWL_LOG_INFO("Entering Hardware Render Loop...");

    auto lastTime = std::chrono::high_resolution_clock::now();

    while (m_isRunning) {
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // Clamp deltaTime to prevent spiral of death on first frame or after pause
        if (deltaTime > 0.25f) deltaTime = 0.25f;
        if (deltaTime < 0.0f) deltaTime = 0.0f;

        step(deltaTime);
    }

    ROWL_LOG_INFO("Engine Render Loop Finished.");
    shutdown();
}

void Engine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Rowl Engine...");

    if (m_ipcServer) {
        m_ipcServer->stop();
        m_ipcServer.reset();
    }

    if (m_window) {
        m_window->shutdown();
        m_window.reset();
    }

    m_isRunning = false;
    m_initialized = false;
    ROWL_LOG_INFO("Engine Shutdown Complete.");
}

} // namespace Rowl::Core
