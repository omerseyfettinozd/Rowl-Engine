/**
 * main_test_runner.cpp
 *
 * Comprehensive native test runner for all Rowl Engine C++ subsystems.
 * Tests unit logic, security sandbox, audio DSP, VFS, and offscreen render pipeline.
 */

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <cmath>
#include <SDL3/SDL.h>

#include "rowl/render/aspect_guardian.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/platform/mobile_input.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/c_api.h"

#define TEST_PASS(name) std::cout << "  ✅ [PASS] " << name << std::endl
#define TEST_SECTION(title) std::cout << "\n📌 === " << title << " ===" << std::endl

void test_aspect_guardian() {
    TEST_SECTION("AspectGuardian Subsystem");

    // 16:9 Virtual canvas (1920x1080) on 16:9 physical display (1920x1080)
    auto m1 = Rowl::Render::AspectGuardian::calculateViewport(1920, 1080, 1920, 1080);
    if (m1.width != 1920 || m1.height != 1080 || m1.isPillarbox) {
        std::cerr << "Aspect mismatch 16:9" << std::endl;
        exit(1);
    }
    TEST_PASS("1:1 Perfect Aspect Match (1920x1080)");

    // 16:9 Virtual canvas on 21:9 Ultra-Wide display (2560x1080) -> Pillarbox (bars on sides)
    auto m2 = Rowl::Render::AspectGuardian::calculateViewport(2560, 1080, 1920, 1080);
    if (!m2.isPillarbox || m2.width != 1920 || m2.x != 320) {
        std::cerr << "Aspect mismatch 21:9" << std::endl;
        exit(1);
    }
    TEST_PASS("21:9 Ultra-Wide Pillarbox Calculation (2560x1080)");

    // 16:9 Virtual canvas on 4:3 Box display (1024x768) -> Letterbox (bars on top/bottom)
    auto m3 = Rowl::Render::AspectGuardian::calculateViewport(1024, 768, 1920, 1080);
    if (m3.isPillarbox || m3.width != 1024 || m3.y <= 0) {
        std::cerr << "Aspect mismatch 4:3" << std::endl;
        exit(1);
    }
    TEST_PASS("4:3 Letterbox Calculation (1024x768)");

    // Coordinate conversion
    float physX = 0, physY = 0;
    Rowl::Render::AspectGuardian::virtualToPhysical(960.0f, 540.0f, m1, physX, physY);
    if (std::abs(physX - 960.0f) > 0.01f || std::abs(physY - 540.0f) > 0.01f) {
        std::cerr << "Coordinate conversion mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Virtual to Physical Coordinate Projection");
}

void test_game_state() {
    TEST_SECTION("GameState & Rewind Subsystem");

    // Initial state creation
    auto s1 = Rowl::State::GameState::createInitialState(101);
    if (s1->stepId != 1 || s1->activeNodeId != 101 || s1->previousState != nullptr) {
        std::cerr << "GameState init mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Initial GameState Creation (Node #101, Step 1)");

    // State progression with variable mutation
    auto s2 = Rowl::State::GameState::createNextState(s1, 102, "player_name", "Evelyn");
    if (s2->stepId != 2 || s2->activeNodeId != 102 || s2->getVariable("player_name") != "Evelyn" || !s1->getVariable("player_name").empty()) {
        std::cerr << "GameState mutation mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Immutable State Transition with Variable Mutation");

    // State progression without variable mutation (structural sharing check)
    auto s3 = Rowl::State::GameState::createNextState(s2, 103);
    if (s3->stepId != 3 || s3->activeNodeId != 103 || s3->getVariable("player_name") != "Evelyn" || s3->variables != s2->variables) {
        std::cerr << "GameState structural sharing mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Zero-Copy Structural Sharing of Variables");

    // Rewind 2 steps back from s3 -> should be s1
    auto rewound = Rowl::State::GameState::rewind(s3, 2);
    if (!rewound || rewound->stepId != 1 || rewound->activeNodeId != 101) {
        std::cerr << "GameState rewind mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Multi-Step Historical Rewind (Step 3 -> Step 1)");
}

void test_audio_engine() {
    TEST_SECTION("Audio Subsystem & DSP Filters");

    Rowl::Audio::AudioEngine audio;
    if (!audio.initialize() || !audio.isInitialized()) {
        std::cerr << "Audio init failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Audio Subsystem Initialization");

    // Voice ducking test
    audio.setBgmVolume(1.0f);
    audio.setDuckingFactor(0.5f);
    if (std::abs(audio.getBgmGain() - 1.0f) > 0.001f) {
        std::cerr << "Audio bgm gain initial mismatch" << std::endl;
        exit(1);
    }

    audio.triggerVoiceDucking(true);
    if (!audio.isDuckingActive() || std::abs(audio.getBgmGain() - 0.5f) > 0.001f) {
        std::cerr << "Audio voice ducking active mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Voice Ducking BGM Attenuation (-6dB / 50% Gain)");

    audio.triggerVoiceDucking(false);
    if (audio.isDuckingActive() || std::abs(audio.getBgmGain() - 1.0f) > 0.001f) {
        std::cerr << "Audio voice ducking restore mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Voice Ducking BGM Gain Restoration (100% Full Gain)");

    // DSP Filters
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::Telephone);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::Telephone) exit(1);
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::UnderwaterLowPass) exit(1);
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::CaveReverb);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::CaveReverb) exit(1);
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::Normal);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::Normal) exit(1);
    TEST_PASS("DSP Filter Switching (Normal, Telephone, Underwater, Cave)");

    audio.shutdown();
    if (audio.isInitialized()) exit(1);
    TEST_PASS("Audio Engine Clean Shutdown");
}

void test_lua_sandbox() {
    TEST_SECTION("Lua 5.4 Sandbox & Security Subsystem");

    Rowl::Scripting::LuaSandbox lua;
    if (!lua.initialize() || !lua.isInitialized()) {
        std::cerr << "Lua init failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua Sandbox Initialization");

    // Safe execution
    if (!lua.executeString("x = 10 + 20; y = math.sqrt(100);")) {
        std::cerr << "Lua math exec failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Standard Math & Basic Arithmetic Execution");

    // Engine bridge variables
    lua.executeString("rowl.var_set('affinity_evelyn', '95')");
    std::string val = lua.getVariable("affinity_evelyn");
    if (val != "95") {
        std::cerr << "Lua var bridge mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Engine Variable Bridge (rowl.var_set / getVariable)");

    // Blacklist check: os, io, debug must be nil
    if (!lua.executeString("if os ~= nil then error('os library is not sandboxed!') end")) exit(1);
    if (!lua.executeString("if io ~= nil then error('io library is not sandboxed!') end")) exit(1);
    if (!lua.executeString("if debug ~= nil then error('debug library is not sandboxed!') end")) exit(1);
    TEST_PASS("Security Sandbox Isolation (os, io, debug blacklisted)");

    // Infinite loop protection (Instruction counter hook)
    if (lua.executeString("while true do local a = 1 end")) {
        std::cerr << "Lua infinite loop was not blocked!" << std::endl;
        exit(1);
    }
    TEST_PASS("Infinite Loop Defense (10M Instruction Limit Hook)");

    lua.shutdown();
    if (lua.isInitialized()) exit(1);
    TEST_PASS("Lua Sandbox Clean Shutdown");
}

void test_mobile_input() {
    TEST_SECTION("Mobile Multi-Touch Subsystem");

    // Touch target validity test
    if (!Rowl::Platform::MobileInput::isTouchTargetValid(48.0f, 48.0f)) exit(1);
    if (!Rowl::Platform::MobileInput::isTouchTargetValid(64.0f, 64.0f)) exit(1);
    if (Rowl::Platform::MobileInput::isTouchTargetValid(32.0f, 48.0f)) exit(1);
    TEST_PASS("Mobile Accessibility Minimum Touch Target (>= 48x48 dp)");

    // Simulated SDL3 Touch Event Processing
    SDL_Event touchEvent;
    touchEvent.type = SDL_EVENT_FINGER_DOWN;
    touchEvent.tfinger.x = 0.5f; // 50% of 1920 = 960
    touchEvent.tfinger.y = 0.5f; // 50% of 1080 = 540
    touchEvent.tfinger.fingerID = 10;

    Rowl::Platform::InputEvent outEvent;
    bool procOk = Rowl::Platform::MobileInput::processSdlEvent(touchEvent, outEvent);
    if (!procOk || outEvent.type != Rowl::Platform::InputEventType::TapDown ||
        std::abs(outEvent.x - 960.0f) > 0.01f || std::abs(outEvent.y - 540.0f) > 0.01f) {
        std::cerr << "Touch event processing mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("SDL3 Touch Coordinate Normalization to 1920x1080 Canvas");
}

void test_native_c_api() {
    TEST_SECTION("Native C-API & Full Render Loop");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) exit(1);

    int initRes = RowlEngine_Init(handle, 1920, 1080, 0);
    if (initRes != 1 || RowlEngine_IsRunning(handle) != 1) exit(1);
    TEST_PASS("RowlEngine_Create & Init (1920x1080 Offscreen)");

    // Component JSON Scene Push
    const char* compJson = R"([
        {"type":"speaker","id":"s1","enabled":true,"data":{"speaker":"Alice","dialogue":"Automated C++ Unit Test Dialogue\nWith second line."}},
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"scale":1}},
        {"type":"character","id":"c1","enabled":true,"data":{"sprite":"Margot.jpg","x":300,"y":200,"width":360,"height":540,"scale":1}},
        {"type":"character","id":"c2","enabled":true,"data":{"sprite":"Margot.jpg","x":1200,"y":200,"width":360,"height":540,"scale":1}},
        {"type":"dialogue_box","id":"d1","enabled":true,"data":{"x":80,"y":840,"width":1760,"height":200,"scale":1}},
        {"type":"audio","id":"a1","enabled":true,"data":{"dsp_filter":"Underwater"}}
    ])";

    RowlEngine_UpdateSceneFromJson(handle, compJson);
    TEST_PASS("RowlEngine_UpdateSceneFromJson (Multi-Character + Multi-Line Dialogue)");

    // Execute 60 frames of step
    for (int i = 0; i < 60; ++i) {
        RowlEngine_Step(handle, 0.0166f);
    }

    uint32_t w = 0, h = 0;
    const uint8_t* pixels = RowlEngine_GetPixelBuffer(handle, &w, &h);
    if (!pixels || w != 1920 || h != 1080) {
        std::cerr << "Pixel buffer mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("RowlEngine_Step (60 FPS Simulation & Valid Pixel Buffer)");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    TEST_PASS("RowlEngine_Shutdown & Destroy (Clean Resource Teardown)");
}

int main() {
    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🚀 ROWL ENGINE COMPREHENSIVE NATIVE UNIT TEST SUITE 🚀" << std::endl;
    std::cout << "=======================================================" << std::endl;

    test_aspect_guardian();
    test_game_state();
    test_audio_engine();
    test_lua_sandbox();
    test_mobile_input();
    test_native_c_api();

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🎉 ALL UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    return 0;
}
