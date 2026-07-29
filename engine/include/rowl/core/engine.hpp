#pragma once

#include "rowl/render/window.hpp"
#include "rowl/ipc/ipc_server.hpp"
#include <cstdint>
#include <string>
#include <memory>

#include <unordered_map>
#include <vector>

namespace Rowl::Core {

struct EngineConfig {
    std::string appName = "Rowl Engine Game";
    uint32_t virtualWidth = 1920;
    uint32_t virtualHeight = 1080;
    bool isIpcMode = false;
    std::string pipeId = "rowl_engine_ipc";
    bool vsync = true;
};

struct StoryNode {
    uint64_t id = 0;
    std::string speaker;
    std::string dialogue;
    std::string background;
    std::string character;
    float characterX = 1440.0f;
    float characterY = 340.0f;
    float dialogueBoxY = 860.0f;
    uint64_t nextNodeId = 0;
};

class Engine {
public:
    Engine();
    ~Engine();

    // Disable copy/move
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    static Engine& instance();

    bool initialize(const EngineConfig& config = EngineConfig{});
    void run();
    void step(float deltaTime);
    void shutdown();

    bool isRunning() const { return m_isRunning; }
    const EngineConfig& getConfig() const { return m_config; }
    Rowl::Render::Window* getWindow() const { return m_window.get(); }
    Rowl::IPC::IpcServer* getIpcServer() const { return m_ipcServer.get(); }

    void updateActiveScene(const std::string& speaker, const std::string& dialogue, const std::string& background, const std::string& character, float charX = 1440.0f, float charY = 340.0f, float dlgBoxY = 860.0f);
    void loadActiveStoryFile();
    void loadStoryGraphFile();
    void advanceToNextNode();

    std::string getActiveSpeaker() const { return m_activeSpeaker; }
    std::string getActiveDialogue() const { return m_activeDialogue; }
    std::string getActiveBackground() const { return m_activeBackground; }
    std::string getActiveCharacter() const { return m_activeCharacter; }
    float getActiveCharacterX() const { return m_activeCharacterX; }
    float getActiveCharacterY() const { return m_activeCharacterY; }
    float getActiveDialogueBoxY() const { return m_activeDialogueBoxY; }
    uint64_t getCurrentNodeId() const { return m_currentNodeId; }

private:
    static Engine* s_instance;
    EngineConfig m_config;
    std::unique_ptr<Rowl::Render::Window> m_window;
    std::unique_ptr<Rowl::IPC::IpcServer> m_ipcServer;
    
    std::unordered_map<uint64_t, StoryNode> m_storyNodes;
    uint64_t m_startNodeId = 101;
    uint64_t m_currentNodeId = 101;

    std::string m_activeSpeaker = "Evelyn";
    std::string m_activeDialogue = "Welcome to Rowl Engine!";
    std::string m_activeBackground = "bg_beach_sunset.png";
    std::string m_activeCharacter = "spr_evelyn.png";
    float m_activeCharacterX = 1440.0f;
    float m_activeCharacterY = 340.0f;
    float m_activeDialogueBoxY = 860.0f;

    bool m_isRunning = false;
    bool m_initialized = false;
};

} // namespace Rowl::Core
