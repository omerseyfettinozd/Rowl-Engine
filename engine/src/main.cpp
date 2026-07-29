#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/render/msdf_renderer.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/platform/mobile_input.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <filesystem>

int main(int argc, char* argv[]) {
    using namespace Rowl::Core;
    using namespace Rowl::Render;
    using namespace Rowl::State;
    using namespace Rowl::Scripting;
    using namespace Rowl::Audio;
    using namespace Rowl::Platform;

    EngineConfig config;
    config.appName = "Rowl Engine - Visual Novel Runtime (Phase 5)";
    config.virtualWidth = 1920;
    config.virtualHeight = 1080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--ipc-mode") {
            config.isIpcMode = true;
        } else if (arg == "--pipe-id" && i + 1 < argc) {
            config.pipeId = argv[++i];
        }
    }

    Engine engine;
    if (!engine.initialize(config)) {
        std::cerr << "Failed to initialize Rowl Engine!" << std::endl;
        return 1;
    }

    // Phase 5 Test: Unified Mobile Touch & Input Accessibility
    bool btn56dp = MobileInput::isTouchTargetValid(56.0f, 56.0f);
    bool btn32dp = MobileInput::isTouchTargetValid(32.0f, 32.0f);
    ROWL_LOG_INFO("Mobile Touch Target Accessibility Test -> 56x56dp: " + std::string(btn56dp ? "PASS" : "FAIL") + " | 32x32dp: " + std::string(!btn32dp ? "PASS (Correctly Rejected)" : "FAIL"));

    SDL_Event simulatedTouch;
    simulatedTouch.type = SDL_EVENT_FINGER_DOWN;
    simulatedTouch.tfinger.x = 0.45f;
    simulatedTouch.tfinger.y = 0.75f;
    simulatedTouch.tfinger.fingerID = 1;
    InputEvent processedEvent;
    if (MobileInput::processSdlEvent(simulatedTouch, processedEvent)) {
        ROWL_LOG_INFO("Unified Touch abstraction successfully processed FingerDown event -> Touch ID: " + std::to_string(processedEvent.touchId));
    }

    // Phase 4 Test 1: Sandboxed Lua 5.4 Environment
    LuaSandbox lua;
    if (lua.initialize()) {
        lua.executeString("rowl.var_set(\"player_gold\", \"250\")");
        lua.executeString("rowl.var_set(\"has_key\", \"true\")");
        lua.executeString("print(\"[Lua Script] Code executed inside C++ sandbox successfully!\")");
        ROWL_LOG_INFO("Lua Engine Test -> 'player_gold': " + lua.getVariable("player_gold") + ", 'has_key': " + lua.getVariable("has_key"));
    }

    // Phase 4 Test 2: Persistent Immutable GameState & Structural Sharing Rewind Engine
    auto stateRoot = GameState::createInitialState(101);
    for (int i = 1; i <= 100; ++i) {
        stateRoot = GameState::createNextState(stateRoot, 101 + i, "step_var_" + std::to_string(i), "value_" + std::to_string(i));
    }
    ROWL_LOG_INFO("Created 100 Immutable GameState snapshots. Current Step #" + std::to_string(stateRoot->stepId));

    // Rewind 50 steps backwards
    auto rewoundState = GameState::rewind(stateRoot, 50);

    // Phase 4 Test 3: Dual-Path Audio Engine, Ducking & DSP Filters
    AudioEngine audio;
    if (audio.initialize()) {
        audio.playAudio("bgm_ocean_theme.ogg", AudioChannelType::Bgm);
        audio.playAudio("vo_101_evelyn_hello.ogg", AudioChannelType::Voice, DSPFilterType::Telephone);
        audio.playAudio("sfx_click.wav", AudioChannelType::Sfx);
    }

    // Phase 3 Aspect Guardian Test
    ViewportMetrics metrics = AspectGuardian::calculateViewport(2560, 1080, 1920, 1080);
    ROWL_LOG_INFO("Aspect Guardian Ultrawide Viewport Test -> Offset X: " + std::to_string(metrics.x) + ", Width: " + std::to_string(metrics.width) + ", Scale: " + std::to_string(metrics.scaleFactor));

    // Phase 3 MSDF Test
    MsdfRenderer msdf;
    msdf.loadAtlasMetadata("{}");

    // VFS Lookup - find data directory from executable location
    ROWL_LOG_INFO("Testing VFS Asset lookup...");
    
    // Try multiple paths for package file
    std::string pkgPath;
    if (std::filesystem::exists("data/game_data.rowlpkg")) {
        pkgPath = "data/game_data.rowlpkg";
    } else if (std::filesystem::exists("../data/game_data.rowlpkg")) {
        pkgPath = "../data/game_data.rowlpkg";
    } else {
        ROWL_LOG_WARN("Package file not found at data/game_data.rowlpkg or ../data/game_data.rowlpkg");
    }
    
    if (!pkgPath.empty()) {
        Rowl::VFS::VFSManager::instance().mountPackage("data", pkgPath);
    }

    bool assetExists = Rowl::VFS::VFSManager::instance().exists("test_story.json");
    ROWL_LOG_INFO("VFS 'test_story.json' package asset exists check: " + std::string(assetExists ? "TRUE" : "FALSE"));

    if (assetExists) {
        std::string content = Rowl::VFS::VFSManager::instance().readString("test_story.json");
        ROWL_LOG_INFO("Decompressed Asset Content: " + content);
    }

    // Enter hardware render loop - runs continuously until user closes window
    engine.run();

    return 0;
}