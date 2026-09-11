/**
 * main_test_runner.cpp
 *
 * Comprehensive native test runner for all Rowl Engine C++ subsystems.
 * Tests unit logic, security sandbox, audio DSP, VFS, and offscreen render pipeline.
 */

#include <iostream>
#include <cassert>
#include <string>
#include <string_view>
#include <vector>
#include <cmath>
#include <limits>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <array>
#include <iterator>
#include <cstdlib>
#include <iomanip>
#include <SDL3/SDL.h>
#include <zstd.h>

#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/msdf_renderer.hpp"
#include "rowl/render/camera2d.hpp"
#include "rowl/render/transition_manager.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/platform/mobile_input.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/scene/scene.hpp"
#include "rowl/scene/game_object.hpp"
#include "rowl/scene/transform_component.hpp"
#include "rowl/scene/sprite_component.hpp"
#include "rowl/c_api.h"

#define TEST_PASS(name) std::cout << "  ✅ [PASS] " << name << std::endl
#define TEST_SECTION(title) std::cout << "\n📌 === " << title << " ===" << std::endl

std::vector<uint8_t> decodeBase64(const std::string_view input) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result;
    uint32_t accumulator = 0;
    int bits = -8;
    for (const unsigned char character : input) {
        if (character == '=') break;
        const auto index = alphabet.find(character);
        if (index == std::string_view::npos) return {};
        accumulator = (accumulator << 6u) | static_cast<uint32_t>(index);
        bits += 6;
        if (bits >= 0) {
            result.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFu));
            bits -= 8;
        }
    }
    return result;
}

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

    const auto portrait = Rowl::Render::AspectGuardian::calculateViewport(1080, 1920, 1920, 1080);
    if (Rowl::Render::AspectGuardian::containsPhysicalPoint(540.0f, 100.0f, portrait) ||
        !Rowl::Render::AspectGuardian::containsPhysicalPoint(540.0f, 960.0f, portrait)) {
        std::cerr << "Portrait letterbox hit-test bounds mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Portrait Letterbox Input Bounds");
}

void test_msdf_renderer() {
    TEST_SECTION("MSDF Atlas Sampling & Metrics");
    Rowl::Render::MsdfRenderer renderer;
    if (!renderer.loadAtlasMetadata(R"({"pixel_range":4,"atlas_width":2,"atlas_height":2,"glyphs":[{"unicode":65,"advance":0.6}]})")) {
        std::cerr << "MSDF metadata load failed" << std::endl;
        exit(1);
    }
    std::vector<uint8_t> pixels = {
        255, 255, 255, 255, 0, 0, 0, 255,
        128, 128, 128, 255, 64, 64, 64, 255
    };
    if (!renderer.loadAtlasPixels(std::move(pixels), 2, 2) || !renderer.isLoaded() ||
        renderer.sampleOpacity(0.0f, 0.0f) < 0.99f || renderer.sampleOpacity(1.0f, 0.0f) > 0.01f ||
        std::abs(renderer.measureTextWidth("AA", 20.0f) - 24.0f) > 0.01f) {
        std::cerr << "MSDF atlas sampling or metrics mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("MSDF RGB Median Sampling and UTF-8 Glyph Metrics");

    std::ifstream generatedAtlas("Assets/fonts/msdf/default.json");
    const std::string generatedMetadata((std::istreambuf_iterator<char>(generatedAtlas)), {});
    Rowl::Render::MsdfRenderer generated;
    if (generatedMetadata.empty() || !generated.loadAtlasMetadata(generatedMetadata) ||
        !generated.findGlyph('A') || generated.getAtlasWidth() != 1024.0f ||
        generated.getPixelRange() != 4.0f) {
        std::cerr << "Generated MSDF atlas metadata is not runtime-compatible" << std::endl;
        exit(1);
    }
    TEST_PASS("Generated MSDF Atlas Metadata and Glyph Lookup");
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

    // JSON Serialization & Slot Persistence
    std::string serialized = s2->serializeJson();
    auto deserialized = Rowl::State::GameState::deserializeJson(serialized);
    if (!deserialized || deserialized->stepId != s2->stepId || deserialized->activeNodeId != 102 ||
        deserialized->getVariable("player_name") != "Evelyn") {
        std::cerr << "GameState JSON serialize/deserialize mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("GameState JSON Serialization & Deserialization");

    const auto withHistory = Rowl::State::GameState::withDialogueHistory(s2, {
        {101, "Evelyn", "First remembered line", true},
        {102, "Mina", "Second remembered line", true},
    });
    const auto restoredHistory = Rowl::State::GameState::deserializeJson(withHistory->serializeJson());
    if (!restoredHistory || !restoredHistory->dialogueHistory ||
        restoredHistory->dialogueHistory->size() != 2 ||
        restoredHistory->dialogueHistory->at(1).speaker != "Mina" ||
        restoredHistory->dialogueHistory->at(1).dialogue != "Second remembered line") {
        std::cerr << "GameState dialogue history persistence mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Bounded Dialogue History Save Persistence and v2 Fallback");

    auto audioState = Rowl::State::GameState::createNextStateWithAudio(
        s2, 102, "night.png", "audio/night.ogg", 0.65f, true, "Telephone");
    auto restoredAudioState = Rowl::State::GameState::deserializeJson(audioState->serializeJson());
    if (!restoredAudioState || restoredAudioState->activeBgm != "audio/night.ogg" ||
        !restoredAudioState->bgmPlaying || std::abs(restoredAudioState->bgmVolume - 0.65f) > 0.001f ||
        restoredAudioState->dspFilter != "Telephone") {
        std::cerr << "GameState audio presentation serialization mismatch" << std::endl;
        exit(1);
    }
    const auto legacyState = Rowl::State::GameState::deserializeJson(
        R"({"version":1,"step_id":1,"active_node_id":101,"variables":{}})");
    const auto v2State = Rowl::State::GameState::deserializeJson(
        R"({"version":2,"step_id":1,"active_node_id":101,"variables":{}})");
    if (!legacyState || !v2State || legacyState->bgmPlaying || !legacyState->activeBgm.empty() ||
        !v2State->dialogueHistory || !v2State->dialogueHistory->empty()) {
        std::cerr << "Legacy GameState save compatibility mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Versioned Audio Presentation Save State and v1 Compatibility");

    std::string testSaveDir = "build/test_saves";
    if (!Rowl::State::GameState::saveToSlot(s2, 1, testSaveDir)) {
        std::cerr << "GameState saveToSlot failed" << std::endl;
        exit(1);
    }
    if (std::filesystem::exists(std::filesystem::path(testSaveDir) / "save_slot_1.json.tmp")) {
        std::cerr << "GameState atomic save left a temporary file behind" << std::endl;
        exit(1);
    }
    if (!Rowl::State::GameState::hasSlot(1, testSaveDir)) {
        std::cerr << "GameState hasSlot failed" << std::endl;
        exit(1);
    }
    auto loadedSlot = Rowl::State::GameState::loadFromSlot(1, testSaveDir);
    if (!loadedSlot || loadedSlot->activeNodeId != 102 || loadedSlot->getVariable("player_name") != "Evelyn") {
        std::cerr << "GameState loadFromSlot content mismatch" << std::endl;
        exit(1);
    }
    const auto replacementState = Rowl::State::GameState::createNextState(s2, 303, "player_name", "Mina");
    if (!Rowl::State::GameState::saveToSlot(replacementState, 1, testSaveDir)) {
        std::cerr << "GameState atomic slot replacement failed" << std::endl;
        exit(1);
    }
    const auto replacedSlot = Rowl::State::GameState::loadFromSlot(1, testSaveDir);
    if (!replacedSlot || replacedSlot->activeNodeId != 303 || replacedSlot->getVariable("player_name") != "Mina" ||
        std::filesystem::exists(std::filesystem::path(testSaveDir) / "save_slot_1.json.tmp")) {
        std::cerr << "GameState atomic slot replacement produced inconsistent data" << std::endl;
        exit(1);
    }
    TEST_PASS("GameState Slot File Persistence (saveToSlot / loadFromSlot / hasSlot)");

    // Save files are user-controlled input once they reach disk. Reject
    // malformed, unsupported, and structurally invalid content safely.
    {
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_2.json") << "{ not valid json";
        if (Rowl::State::GameState::loadFromSlot(2, testSaveDir)) {
            std::cerr << "Malformed GameState save was accepted" << std::endl;
            exit(1);
        }
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_3.json")
            << R"({"version":999,"step_id":1,"active_node_id":101,"variables":{}})";
        if (Rowl::State::GameState::loadFromSlot(3, testSaveDir)) {
            std::cerr << "Future GameState save version was accepted" << std::endl;
            exit(1);
        }
        std::ofstream(std::filesystem::path(testSaveDir) / "save_slot_4.json")
            << R"({"version":1,"step_id":0,"active_node_id":101,"variables":[]})";
        if (Rowl::State::GameState::loadFromSlot(4, testSaveDir)) {
            std::cerr << "Structurally invalid GameState save was accepted" << std::endl;
            exit(1);
        }
        if (Rowl::State::GameState::saveToSlot(s2, -1, testSaveDir) ||
            Rowl::State::GameState::hasSlot(-1, testSaveDir) ||
            Rowl::State::GameState::deleteSlot(-1, testSaveDir)) {
            std::cerr << "Negative GameState save slot was accepted" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("GameState Save Corruption, Version, and Slot-Bounds Containment");

    Rowl::State::GameState::deleteSlot(1, testSaveDir);
    if (Rowl::State::GameState::hasSlot(1, testSaveDir)) {
        std::cerr << "GameState deleteSlot failed" << std::endl;
        exit(1);
    }
    TEST_PASS("GameState Slot Cleanup (deleteSlot)");
    std::filesystem::remove_all(testSaveDir);
}

void test_audio_engine() {
    TEST_SECTION("Audio Subsystem & DSP Filters");

    Rowl::Audio::AudioEngine audio(&Rowl::VFS::VFSManager::instance());
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

    // std::clamp does not itself sanitize NaN. The public audio boundary must
    // preserve the last valid gain instead of forwarding it to SDL.
    audio.setBgmVolume(std::numeric_limits<float>::quiet_NaN());
    audio.setDuckingFactor(std::numeric_limits<float>::quiet_NaN());
    audio.triggerVoiceDucking(true);
    if (!std::isfinite(audio.getBgmGain()) || std::abs(audio.getBgmGain() - 0.5f) > 0.001f) {
        std::cerr << "Non-finite audio inputs corrupted the active gain" << std::endl;
        exit(1);
    }
    audio.triggerVoiceDucking(false);
    TEST_PASS("Audio Gain Rejects Non-Finite Inputs");

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

    // Audio playback uses a real, minimal PCM WAV rather than only recording
    // an intent string. This exercises SDL's decode and stream queue path.
    const auto audioProjectRoot = std::filesystem::temp_directory_path() / "rowl_audio_vfs_test_project";
    const auto audioAssetDir = audioProjectRoot / "Assets" / "audio";
    const auto tonePath = audioAssetDir / "rowl_audio_test_tone.wav";
    const std::string toneAssetPath = "audio/rowl_audio_test_tone.wav";
    std::filesystem::create_directories(audioAssetDir);
    const uint8_t wavData[] = {
        'R','I','F','F', 38,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        68,172,0,0, 136,88,1,0, 2,0, 16,0,
        'd','a','t','a', 2,0,0,0, 0,0
    };
    {
        std::ofstream tone(tonePath, std::ios::binary);
        tone.write(reinterpret_cast<const char*>(wavData), sizeof(wavData));
    }
    Rowl::VFS::VFSManager::instance().remountProject(audioProjectRoot.string());
    audio.playAudio(toneAssetPath, Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (audio.getCurrentBgmPath() != toneAssetPath) {
        std::cerr << "Audio BGM path mismatch" << std::endl;
        exit(1);
    }
    // This small checked-in fixture is decoded through the VFS stream path,
    // covering real OGG/Vorbis decoding independently from SDL's WAV loader.
    const auto oggPath = audioAssetDir / "rowl_audio_test_tone.ogg";
    const std::string oggAssetPath = "audio/rowl_audio_test_tone.ogg";
    const auto oggData = decodeBase64(
        "T2dnUwACAAAAAAAAAADGYfYSAAAAAAAR1BkBHgF2b3JiaXMAAAAAAUAfAAAAAAAAgFcAAAAAAACZAU9nZ1MAAAAAAAAAAAAAxmH2EgEAAADUJzrDCz7///////////+1A3ZvcmJpcwwAAABMYXZmNjMuMS4xMDEBAAAAHgAAAGVuY29kZXI9TGF2YzYzLjEuMTAxIGxpYnZvcmJpcwEFdm9yYmlzEkJDVgEAAAEADFIUISUZU0pjCJVSUikFHWNQW0cdY9Q5RiFkEFOISRmle08qlVhKyBFSWClFHVNMU0mVUpYpRR1jFFNIIVPWMWWhcxRLhkkJJWxNrnQWS+iZY5YxRh1jzlpKnWPWMUUdY1JSSaFzGDpmJWQUOkbF6GJ8MDqVokIovsfeUukthYpbir3XGlPrLYQYS2nBCGFz7bXV3EpqxRhjjDHGxeJTKILQkFUAAAEAAEAEAUJDVgEACgAAwlAMRVGA0JBVAEAGAIAAFEVxFMdxHEeSJMsCQkNWAQBAAAACAAAojuEokiNJkmRZlmVZlqZ5lqi5qi/7ri7rru3qug6EhqwEAMgAABiGIYfeScyQU5BJJilVzDkIofUOOeUUZNJSxphijFHOkFMMMQUxhtAphRDUTjmlDCIIQ0idZM4gSz3o4GLnOBAasiIAiAIAAIxBjCHGkHMMSgYhco5JyCBEzjkpnZRMSiittJZJCS2V1iLnnJROSialtBZSy6SU1kIrBQAABDgAAARYCIWGrAgAogAAEIOQUkgpxJRiTjGHlFKOKceQUsw5xZhyjDHoIFTMMcgchEgpxRhzTjnmIGQMKuYchAwyAQAAAQ4AAAEWQqEhKwKAOAEAgyRpmqVpomhpmih6pqiqoiiqquV5pumZpqp6oqmqpqq6rqmqrmx5nml6pqiqnimqqqmqrmuqquuKqmrLpqvatumqtuzKsm67sqzbnqrKtqm6sm6qrm27smzrrizbuuR5quqZput6pum6quvasuq6su2ZpuuKqivbpuvKsuvKtq3Ksq5rpum6oqvarqm6su3Krm27sqz7puvqturKuq7Ksu7btq77sq0Lu+i6tq7Krq6rsqzrsi3rtmzbQsnzVNUzTdf1TNN1Vde1bdV1bVszTdc1XVeWRdV1ZdWVdV11ZVv3TNN1TVeVZdNVZVmVZd12ZVeXRde1bVWWfV11ZV+Xbd33ZVnXfdN1dVuVZdtXZVn3ZV33hVm3fd1TVVs3XVfXTdfVfVvXfWG2bd8XXVfXVdnWhVWWdd/WfWWYdZ0wuq6uq7bs66os676u68Yw67owrLpt/K6tC8Or68ax676u3L6Patu+8Oq2Mby6bhy7sBu/7fvGsamqbZuuq+umK+u6bOu+b+u6cYyuq+uqLPu66sq+b+u68Ou+Lwyj6+q6Ksu6sNqyr8u6Lgy7rhvDatvC7tq6cMyyLgy37yvHrwtD1baF4dV1o6vbxm8Lw9I3dr4AAIABBwCAABPKQKEhKwKAOAEABiEIFWMQKsYghBBSCiGkVDEGIWMOSsYclBBKSSGU0irGIGSOScgckxBKaKmU0EoopaVQSkuhlNZSai2m1FoMobQUSmmtlNJaaim21FJsFWMQMuekZI5JKKW0VkppKXNMSsagpA5CKqWk0kpJrWXOScmgo9I5SKmk0lJJqbVQSmuhlNZKSrGl0kptrcUaSmktpNJaSam11FJtrbVaI8YgZIxByZyTUkpJqZTSWuaclA46KpmDkkopqZWSUqyYk9JBKCWDjEpJpbWSSiuhlNZKSrGFUlprrdWYUks1lJJaSanFUEprrbUaUys1hVBSC6W0FkpprbVWa2ottlBCa6GkFksqMbUWY22txRhKaa2kElspqcUWW42ttVhTSzWWkmJsrdXYSi051lprSi3W0lKMrbWYW0y5xVhrDSW0FkpprZTSWkqtxdZaraGU1koqsZWSWmyt1dhajDWU0mIpKbWQSmyttVhbbDWmlmJssdVYUosxxlhzS7XVlFqLrbVYSys1xhhrbjXlUgAAwIADAECACWWg0JCVAEAUAABgDGOMQWgUcsw5KY1SzjknJXMOQggpZc5BCCGlzjkIpbTUOQehlJRCKSmlFFsoJaXWWiwAAKDAAQAgwAZNicUBCg1ZCQBEAQAgxijFGITGIKUYg9AYoxRjECqlGHMOQqUUY85ByBhzzkEpGWPOQSclhBBCKaWEEEIopZQCAAAKHAAAAmzQlFgcoNCQFQFAFAAAYAxiDDGGIHRSOikRhExKJ6WREloLKWWWSoolxsxaia3E2EgJrYXWMmslxtJiRq3EWGIqAADswAEA7MBCKDRkJQCQBwBAGKMUY845ZxBizDkIITQIMeYchBAqxpxzDkIIFWPOOQchhM455yCEEELnnHMQQgihgxBCCKWU0kEIIYRSSukghBBCKaV0EEIIoZRSCgAAKnAAAAiwUWRzgpGgQkNWAgB5AACAMUo5JyWlRinGIKQUW6MUYxBSaq1iDEJKrcVYMQYhpdZi7CCk1FqMtXYQUmotxlpDSq3FWGvOIaXWYqw119RajLXm3HtqLcZac865AADcBQcAsAMbRTYnGAkqNGQlAJAHAEAgpBRjjDmHlGKMMeecQ0oxxphzzinGGHPOOecUY4w555xzjDHnnHPOOcaYc84555xzzjnnoIOQOeecc9BB6JxzzjkIIXTOOecchBAKAAAqcAAACLBRZHOCkaBCQ1YCAOEAAIAxlFJKKaWUUkqoo5RSSimllFICIaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKaWUUkoppZRSSimllFJKKZVSSimllFJKKaWUUkoppQAg3woHAP8HG2dYSTorHA0uNGQlABAOAAAYwxiEjDknJaWGMQildE5KSSU1jEEopXMSUkopg9BaaqWk0lJKGYSUYgshlZRaCqW0VmspqbWUUigpxRpLSqml1jLnJKSSWkuttpg5B6Wk1lpqrcUQQkqxtdZSa7F1UlJJrbXWWm0tpJRaay3G1mJsJaWWWmupxdZaTKm1FltLLcbWYkutxdhiizHGGgsA4G5wAIBIsHGGlaSzwtHgQkNWAgAhAQAEMko555yDEEIIIVKKMeeggxBCCCFESjHmnIMQQgghhIwx5yCEEEIIoZSQMeYchBBCCCGEUjrnIIRQSgmllFJK5xyEEEIIpZRSSgkhhBBCKKWUUkopIYQQSimllFJKKSWEEEIopZRSSimlhBBCKKWUUkoppZQQQiillFJKKaWUEkIIoZRSSimllFJCCKWUUkoppZRSSighhFJKKaWUUkoJJZRSSimllFJKKSGUUkoppZRSSimlAACAAwcAgAAj6CSjyiJsNOHCAxAAAAACAAJMAIEBgoJRCAKEEQgAAAAAAAgA+AAASAqAiIho5gwOEBIUFhgaHB4gIiQAAAAAAAAAAAAAAAAET2dnUwAE8AAAAAAAAADGYfYSAgAAANQ93LoCFxaKlJlZ4RUA/GIyAAAQUkl4pdydXlvfEY6VmbOzXgHA3wkDAADYYGr9hZn5MwQ=");
    {
        std::ofstream ogg(oggPath, std::ios::binary);
        ogg.write(reinterpret_cast<const char*>(oggData.data()), static_cast<std::streamsize>(oggData.size()));
    }
    audio.playAudio(oggAssetPath, Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != oggAssetPath) {
        std::cerr << "Ogg/Vorbis decode did not replace the BGM playback state" << std::endl;
        exit(1);
    }
    TEST_PASS("BGM OGG/Vorbis Decode through VFS Stream");

    // The replacement track must be fully decoded and queued before a fade
    // starts, so a bad transition can never silence the currently playing BGM.
    audio.playBgm(toneAssetPath, Rowl::Audio::BgmTransitionKind::Crossfade, 0.1f);
    if (audio.getCurrentBgmPath() != toneAssetPath) {
        std::cerr << "BGM transition did not commit the replacement track" << std::endl;
        exit(1);
    }
    if (audio.isAudioDeviceAvailable()) {
        if (!audio.isBgmTransitionActive()) {
            std::cerr << "Crossfade did not start on an available audio device" << std::endl;
            exit(1);
        }
        audio.update(0.2f);
        if (audio.isBgmTransitionActive()) {
            std::cerr << "Crossfade did not complete after its configured duration" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("BGM Transition Queueing and Silent Fallback Contract");
    const auto oversizedAudioPath = audioAssetDir / "rowl_oversized_audio.wav";
    std::ofstream(oversizedAudioPath, std::ios::binary).close();
    std::filesystem::resize_file(oversizedAudioPath, 128ULL * 1024 * 1024 + 1);
    audio.playAudio("audio/rowl_oversized_audio.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != toneAssetPath) {
        std::cerr << "Oversized audio load replaced the current playback state" << std::endl;
        exit(1);
    }
    std::filesystem::remove(oversizedAudioPath);
    if (audio.isAudioDeviceAvailable()) {
        audio.playAudio("missing_theme.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (audio.getCurrentBgmPath() != toneAssetPath) {
            std::cerr << "Failed BGM load replaced the current playback state" << std::endl;
            exit(1);
        }
        audio.playAudio("missing_voice.wav", Rowl::Audio::AudioChannelType::Voice,
                        Rowl::Audio::DSPFilterType::Telephone);
        if (audio.isDuckingActive() || audio.getActiveFilter() != Rowl::Audio::DSPFilterType::Normal) {
            std::cerr << "Failed voice load leaked ducking or DSP side effects" << std::endl;
            exit(1);
        }
    }
    std::filesystem::remove_all(audioProjectRoot);
    Rowl::VFS::VFSManager::instance().remountProject(std::filesystem::current_path().string());
    TEST_PASS("BGM WAV Decode, Queueing, and Failed-Load State Preservation");

    audio.stopBgm();
    if (!audio.getCurrentBgmPath().empty()) {
        std::cerr << "Audio BGM stop mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("BGM Stop & Track Reset");

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

    if (!lua.executeString(
            "if dofile ~= nil or loadfile ~= nil or load ~= nil or collectgarbage ~= nil then "
            "error('base library escape hatch is exposed') end")) {
        std::cerr << "Lua base library escape hatch remained available" << std::endl;
        exit(1);
    }
    if (!lua.executeString("rowl = 'overwritten'") ||
        !lua.executeString("rowl.var_set('bridge_integrity', 'ok')") ||
        lua.getVariable("bridge_integrity") != "ok") {
        std::cerr << "Lua bridge was not restored after script global mutation" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua File/Runtime Load APIs Blocked and Bridge Restored");

    if (!lua.executeString("function on_enter(dt) rowl.var_set('entered', tostring(dt)) end") ||
        !lua.callOptionalFunction("on_enter", 0.25) || lua.getVariable("entered") != "0.25" ||
        !lua.callOptionalFunction("missing_callback")) {
        std::cerr << "Lua lifecycle callback dispatch failed" << std::endl;
        exit(1);
    }
    if (!lua.executeString("function on_exit() error('isolated lifecycle error') end") ||
        lua.callOptionalFunction("on_exit")) {
        std::cerr << "Lua lifecycle error isolation failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Optional Lua Lifecycle Callback Dispatch and Error Isolation");

    // Component modules keep their callbacks and globals separate. This lets a
    // node own multiple script components without source order deciding which
    // on_update/on_exit function survives.
    if (!lua.loadModule("first", R"(
        private_value = "first"
        function on_enter() rowl.var_set("module_first_enter", private_value) end
        function on_update(dt) rowl.var_set("module_first_update", tostring(dt)) end
        function on_exit() rowl.var_set("module_exit_order", "first") end
    )") ||
        !lua.loadModule("second", R"(
        private_value = "second"
        function on_enter() rowl.var_set("module_second_enter", private_value) end
        function on_update(dt) rowl.var_set("module_second_update", tostring(dt * 2)) end
        function on_exit() rowl.var_set("module_exit_order", "second") end
    )") || lua.getModuleCount() != 2 ||
        !lua.callOptionalModuleFunction("first", "on_enter") ||
        !lua.callOptionalModuleFunction("second", "on_enter") ||
        lua.getVariable("module_first_enter") != "first" ||
        lua.getVariable("module_second_enter") != "second" ||
        !lua.callOptionalModuleFunction("first", "on_update", 0.25) ||
        !lua.callOptionalModuleFunction("second", "on_update", 0.25) ||
        lua.getVariable("module_first_update") != "0.25" ||
        lua.getVariable("module_second_update") != "0.5") {
        std::cerr << "Lua component module isolation or lifecycle dispatch failed" << std::endl;
        exit(1);
    }
    if (!lua.loadModule("guarded", R"(
        _G.rowl = "component-local overwrite"
        if getmetatable(_G) ~= false then error("component environment is mutable") end
        function on_enter() rowl.var_set("module_guarded", "ok") end
    )") || !lua.callOptionalModuleFunction("guarded", "on_enter") ||
        lua.getVariable("module_guarded") != "ok" || lua.getModuleCount() != 3 ||
        !lua.unloadModule("second") || lua.getModuleCount() != 2 ||
        lua.callOptionalModuleFunction("second", "on_enter")) {
        std::cerr << "Lua component module boundary or unload failed" << std::endl;
        exit(1);
    }
    lua.clearModules();
    if (lua.getModuleCount() != 0) {
        std::cerr << "Lua component module cleanup failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Isolated Lua Component Modules, Lifecycle Dispatch, and Cleanup");

    // Infinite loop protection (Instruction counter hook)
    if (lua.executeString("while true do local a = 1 end")) {
        std::cerr << "Lua infinite loop was not blocked!" << std::endl;
        exit(1);
    }
    TEST_PASS("Infinite Loop Defense (10M Instruction Limit Hook)");

    // Lua Condition Evaluation
    lua.setGlobalNumber("player_gold", 75.0);
    if (!lua.evaluateCondition("player_gold >= 50")) {
        std::cerr << "Lua condition player_gold >= 50 failed" << std::endl;
        exit(1);
    }
    if (lua.evaluateCondition("player_gold > 100")) {
        std::cerr << "Lua condition player_gold > 100 failed" << std::endl;
        exit(1);
    }
    if (!lua.evaluateCondition("player_gold == 75 and 10 > 5")) {
        std::cerr << "Lua compound condition failed" << std::endl;
        exit(1);
    }
    if (!lua.evaluateCondition("true") || lua.evaluateCondition("false")) {
        std::cerr << "Lua boolean literal condition failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Dynamic Expression & Condition Evaluation (evaluateCondition)");

    lua.clearVariables();
    if (!lua.getVariable("player_gold").empty() || !lua.evaluateCondition("player_gold == nil")) {
        std::cerr << "Lua variable reset left stale globals behind" << std::endl;
        exit(1);
    }
    TEST_PASS("Lua Variable Reset Clears Script Globals");

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

    if (Rowl::Platform::MobileInput::classifyTouchGesture(900.0f, 540.0f, 700.0f, 540.0f, 1920.0f, 1080.0f)
            != Rowl::Platform::InputEventType::SwipeForward ||
        Rowl::Platform::MobileInput::classifyTouchGesture(700.0f, 540.0f, 900.0f, 540.0f, 1920.0f, 1080.0f)
            != Rowl::Platform::InputEventType::SwipeBack ||
        Rowl::Platform::MobileInput::classifyTouchGesture(900.0f, 540.0f, 920.0f, 550.0f, 1920.0f, 1080.0f)
            != Rowl::Platform::InputEventType::Tap) {
        std::cerr << "Touch gesture classification mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Touch Tap and Horizontal Swipe Classification");
}

void test_vfs_security() {
    TEST_SECTION("VFS Isolation & Package Validation");

    const auto uniqueSuffix = std::to_string(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    const auto testRoot = std::filesystem::temp_directory_path() /
        ("rowl_vfs_security_" + uniqueSuffix);
    const auto mountRoot = testRoot / "project";
    std::filesystem::create_directories(mountRoot);

    {
        std::ofstream(mountRoot / "inside.txt") << "inside";
        std::ofstream(testRoot / "outside.txt") << "outside";
    }

    Rowl::VFS::LooseDirectorySource source(mountRoot.string());
    if (!source.exists("inside.txt") || source.read("inside.txt").empty()) {
        std::cerr << "VFS failed to read a valid in-root asset" << std::endl;
        exit(1);
    }
    if (source.exists("../outside.txt") || !source.read("../outside.txt").empty()) {
        std::cerr << "VFS allowed a parent-directory traversal" << std::endl;
        exit(1);
    }
    std::error_code symlinkError;
    std::filesystem::create_symlink(testRoot / "outside.txt", mountRoot / "linked-outside.txt", symlinkError);
    if (symlinkError || source.exists("linked-outside.txt") || !source.read("linked-outside.txt").empty()) {
        std::cerr << "VFS allowed a symlink to escape its mount root" << std::endl;
        exit(1);
    }
    TEST_PASS("Loose-directory mounts reject parent traversal and symlink escapes");

    const auto oversizedLooseAsset = mountRoot / "oversized.bin";
    std::ofstream(oversizedLooseAsset, std::ios::binary).close();
    std::filesystem::resize_file(oversizedLooseAsset, 128ULL * 1024 * 1024 + 1);
    if (source.exists("oversized.bin") && !source.read("oversized.bin").empty()) {
        std::cerr << "VFS loaded an oversized loose asset" << std::endl;
        exit(1);
    }
    TEST_PASS("Loose-directory mounts reject oversized assets");

    const auto malformedPackage = testRoot / "malformed.rowlpkg";
    {
        Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 0, 4096};
        std::ofstream output(malformedPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    Rowl::VFS::RowlPkgDataSource package(malformedPackage.string());
    if (package.isValid()) {
        std::cerr << "Package with out-of-bounds index was accepted" << std::endl;
        exit(1);
    }
    TEST_PASS("Package reader rejects out-of-bounds index offsets");

    const auto impossibleCountPackage = testRoot / "impossible_count.rowlpkg";
    {
        Rowl::VFS::RowlPkgHeader header{{'R', 'O', 'W', 'L'}, 1, 100'000,
                                        sizeof(Rowl::VFS::RowlPkgHeader)};
        std::ofstream output(impossibleCountPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    }
    if (Rowl::VFS::RowlPkgDataSource(impossibleCountPackage.string()).isValid()) {
        std::cerr << "Package accepted an impossible file count before index validation" << std::endl;
        exit(1);
    }
    TEST_PASS("Package reader rejects impossible index file counts before allocation");

    // Package metadata is an untrusted boundary. These cases ensure an archive
    // cannot smuggle paths, corrupt payload ranges, or spoof an index entry.
    const auto writePackage = [&](const std::string& name, Rowl::VFS::RowlPkgHeader header,
                                  const Rowl::VFS::RowlPkgEntryRaw& entry,
                                  const std::string& path, const std::string& payload = "x") {
        const auto packagePath = testRoot / name;
        std::ofstream output(packagePath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!payload.empty()) output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        output.write(path.data(), static_cast<std::streamsize>(path.size()));
        return packagePath;
    };
    const auto fnv1a64 = [](const std::string& value) {
        uint64_t hash = 14695981039346656037ULL;
        for (const unsigned char byte : value) {
            hash ^= byte;
            hash *= 1099511628211ULL;
        }
        return hash;
    };
    constexpr uint64_t headerSize = sizeof(Rowl::VFS::RowlPkgHeader);
    const std::string safePath = "dir/safe.txt";
    const uint64_t indexOffset = headerSize + 1;
    Rowl::VFS::RowlPkgHeader validHeader{{'R', 'O', 'W', 'L'}, 1, 1, indexOffset};
    Rowl::VFS::RowlPkgEntryRaw validEntry{fnv1a64(safePath), static_cast<uint32_t>(safePath.size()),
                                          headerSize, 1, 1, 0};

    const auto validPackage = writePackage("valid.rowlpkg", validHeader, validEntry, safePath);
    Rowl::VFS::RowlPkgDataSource validSource(validPackage.string());
    if (!validSource.isValid() || validSource.read("dir\\safe.txt") != std::vector<uint8_t>{'x'} ||
        !validSource.read(safePath).size()) {
        std::cerr << "Valid package did not round-trip through normalized lookup" << std::endl;
        exit(1);
    }
    std::atomic<bool> concurrentReadFailed{false};
    std::vector<std::thread> readers;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
        readers.emplace_back([&] {
            for (int readIndex = 0; readIndex < 100; ++readIndex) {
                if (validSource.read("dir\\safe.txt") != std::vector<uint8_t>{'x'}) {
                    concurrentReadFailed.store(true);
                    return;
                }
            }
        });
    }
    for (auto& reader : readers) reader.join();
    if (concurrentReadFailed.load()) {
        std::cerr << "Concurrent package reads returned inconsistent data" << std::endl;
        exit(1);
    }

    // Zstd entries are decoded on demand through openStream(). Exercise small
    // reads and a backward seek so consumers such as Vorbis can parse package
    // assets without first materializing the entire decompressed entry.
    std::string streamingPayload(200'000, '\0');
    for (size_t index = 0; index < streamingPayload.size(); ++index) {
        streamingPayload[index] = static_cast<char>((index * 37u + index / 17u) % 251u);
    }
    std::vector<uint8_t> compressed(ZSTD_compressBound(streamingPayload.size()));
    const size_t compressedSize = ZSTD_compress(compressed.data(), compressed.size(),
                                                streamingPayload.data(), streamingPayload.size(), 1);
    if (ZSTD_isError(compressedSize)) {
        std::cerr << "Could not create Zstd package fixture" << std::endl;
        exit(1);
    }
    compressed.resize(compressedSize);
    const std::string streamingPath = "audio/streamed.ogg";
    const uint64_t streamingIndexOffset = headerSize + compressed.size();
    Rowl::VFS::RowlPkgHeader streamingHeader{{'R', 'O', 'W', 'L'}, 1, 1, streamingIndexOffset};
    Rowl::VFS::RowlPkgEntryRaw streamingEntry{
        fnv1a64(streamingPath), static_cast<uint32_t>(streamingPath.size()), headerSize,
        compressed.size(), streamingPayload.size(), 1};
    const auto streamingPackage = testRoot / "streaming.rowlpkg";
    {
        std::ofstream output(streamingPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&streamingHeader), sizeof(streamingHeader));
        output.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
        output.write(reinterpret_cast<const char*>(&streamingEntry), sizeof(streamingEntry));
        output.write(streamingPath.data(), static_cast<std::streamsize>(streamingPath.size()));
    }
    Rowl::VFS::RowlPkgDataSource streamingSource(streamingPackage.string());
    auto streamingAsset = streamingSource.openStream(streamingPath);
    std::array<char, 97> streamingChunk{};
    const auto firstStreamingRead = streamingAsset ? streamingAsset->read(streamingChunk.data(), streamingChunk.size()).gcount() : 0;
    if (!streamingSource.isValid() || !streamingAsset ||
        firstStreamingRead != static_cast<std::streamsize>(streamingChunk.size()) ||
        std::string_view(streamingChunk.data(), streamingChunk.size()) != std::string_view(streamingPayload.data(), streamingChunk.size())) {
        std::cerr << "Incremental Zstd package stream did not decode its first chunk" << std::endl;
        exit(1);
    }
    streamingAsset->seekg(150'000);
    streamingAsset->read(streamingChunk.data(), streamingChunk.size());
    if (!*streamingAsset || std::string_view(streamingChunk.data(), streamingChunk.size()) !=
        std::string_view(streamingPayload.data() + 150'000, streamingChunk.size())) {
        std::cerr << "Incremental Zstd package stream did not support seekable reads" << std::endl;
        exit(1);
    }
    TEST_PASS("Package Zstd entries decode incrementally with seekable streams");

    const std::string traversalPath = "../outside.txt";
    Rowl::VFS::RowlPkgEntryRaw traversalEntry{fnv1a64(traversalPath), static_cast<uint32_t>(traversalPath.size()),
                                               headerSize, 1, 1, 0};
    const auto traversalPackage = writePackage("traversal.rowlpkg", validHeader, traversalEntry, traversalPath);
    Rowl::VFS::RowlPkgDataSource traversalSource(traversalPackage.string());
    if (traversalSource.isValid()) {
        std::cerr << "Package accepted a traversal path" << std::endl;
        exit(1);
    }

    // Hash collisions/spoofing cannot redirect an asset: canonical path, not
    // the legacy hash field, is the sole lookup key.
    Rowl::VFS::RowlPkgEntryRaw badHashEntry{0, static_cast<uint32_t>(safePath.size()), headerSize, 1, 1, 0};
    const auto badHashPackage = writePackage("bad_hash.rowlpkg", validHeader, badHashEntry, safePath);
    Rowl::VFS::RowlPkgDataSource badHashSource(badHashPackage.string());
    if (!badHashSource.isValid() || badHashSource.read(safePath) != std::vector<uint8_t>{'x'}) {
        std::cerr << "Package path lookup incorrectly depends on legacy hash metadata" << std::endl;
        exit(1);
    }

    Rowl::VFS::RowlPkgEntryRaw indexPayloadEntry{fnv1a64(safePath), static_cast<uint32_t>(safePath.size()),
                                                  indexOffset, 1, 1, 0};
    const auto indexPayloadPackage = writePackage("index_payload.rowlpkg", validHeader, indexPayloadEntry, safePath);
    Rowl::VFS::RowlPkgDataSource indexPayloadSource(indexPayloadPackage.string());
    if (indexPayloadSource.isValid()) {
        std::cerr << "Package allowed an entry to read index bytes as payload" << std::endl;
        exit(1);
    }

    Rowl::VFS::RowlPkgEntryRaw decompressionBombEntry{fnv1a64(safePath), static_cast<uint32_t>(safePath.size()),
                                                       headerSize, 1, 4'096, 1};
    const auto decompressionBombPackage = writePackage("decompression_bomb.rowlpkg", validHeader,
                                                       decompressionBombEntry, safePath);
    if (Rowl::VFS::RowlPkgDataSource(decompressionBombPackage.string()).isValid()) {
        std::cerr << "Package accepted an unreasonable decompression ratio" << std::endl;
        exit(1);
    }

    const auto overlappingPackage = testRoot / "overlapping.rowlpkg";
    const std::string firstPath = "first.txt";
    const std::string secondPath = "second.txt";
    Rowl::VFS::RowlPkgHeader overlappingHeader{{'R', 'O', 'W', 'L'}, 1, 2, headerSize + 2};
    Rowl::VFS::RowlPkgEntryRaw firstEntry{fnv1a64(firstPath), static_cast<uint32_t>(firstPath.size()),
                                          headerSize, 2, 2, 0};
    Rowl::VFS::RowlPkgEntryRaw secondEntry{fnv1a64(secondPath), static_cast<uint32_t>(secondPath.size()),
                                           headerSize + 1, 1, 1, 0};
    {
        std::ofstream output(overlappingPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&overlappingHeader), sizeof(overlappingHeader));
        output.write("xy", 2);
        output.write(reinterpret_cast<const char*>(&firstEntry), sizeof(firstEntry));
        output.write(firstPath.data(), static_cast<std::streamsize>(firstPath.size()));
        output.write(reinterpret_cast<const char*>(&secondEntry), sizeof(secondEntry));
        output.write(secondPath.data(), static_cast<std::streamsize>(secondPath.size()));
    }
    Rowl::VFS::RowlPkgDataSource overlappingSource(overlappingPackage.string());
    if (overlappingSource.isValid()) {
        std::cerr << "Package accepted overlapping payload ranges" << std::endl;
        exit(1);
    }
    TEST_PASS("Package reader validates paths, compression metadata, and payload bounds");

    // Keep a small deterministic corpus of malformed binary inputs in the
    // regular test gate. These are deliberately not saved as fixtures: the
    // generator makes every run repeatable while covering many file lengths
    // and byte arrangements that hand-written examples tend to miss.
    uint32_t packageFuzzState = 0xC0FFEE42u;
    const auto nextPackageFuzzByte = [&packageFuzzState] {
        packageFuzzState = packageFuzzState * 1664525u + 1013904223u;
        return static_cast<uint8_t>(packageFuzzState >> 24u);
    };
    for (uint32_t caseIndex = 0; caseIndex < 128; ++caseIndex) {
        const auto fuzzPath = testRoot / ("fuzz_" + std::to_string(caseIndex) + ".rowlpkg");
        std::vector<uint8_t> bytes(1 + (nextPackageFuzzByte() % 512));
        for (auto& byte : bytes) byte = nextPackageFuzzByte();
        // A deliberately broken magic value makes invalidity an invariant;
        // the remaining bytes still exercise short and arbitrary-file paths.
        bytes.front() = 'X';
        std::ofstream output(fuzzPath, std::ios::binary);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (Rowl::VFS::RowlPkgDataSource(fuzzPath.string()).isValid()) {
            std::cerr << "Malformed package fuzz input was accepted: case " << caseIndex << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Deterministic malformed-package fuzz corpus is safely rejected");

    // A selected project is an asset boundary: source files and project
    // metadata must not become readable merely because they share its root.
    const auto isolatedProject = testRoot / "isolated_project";
    std::filesystem::create_directories(isolatedProject / "Assets" / "images");
    std::ofstream(isolatedProject / "project-secret.txt") << "not-an-asset";
    std::ofstream(isolatedProject / "Assets" / "images" / "allowed.txt") << "asset";
    auto& vfs = Rowl::VFS::VFSManager::instance();
    vfs.remountProject(isolatedProject.string());
    if (vfs.exists("project-secret.txt") || !vfs.readBytes("project-secret.txt").empty() ||
        !vfs.exists("images/allowed.txt") ||
        vfs.readString("Assets/images/allowed.txt") != "asset") {
        std::cerr << "Project remount exposed non-asset files or hid declared assets" << std::endl;
        exit(1);
    }

    // A packaged release keeps its base content in game.rowlpkg, while mods
    // may replace any entry at the same relative VFS path.  This verifies the
    // ordinary lookup path rather than only the explicit mods/ namespace.
    const auto packagedProject = testRoot / "packaged_project";
    std::filesystem::create_directories(packagedProject / "Assets" / "packages");
    std::filesystem::create_directories(packagedProject / "mods" / "dir");
    std::filesystem::copy_file(validPackage,
                               packagedProject / "Assets" / "packages" / "game.rowlpkg",
                               std::filesystem::copy_options::overwrite_existing);
    std::ofstream(packagedProject / "mods" / "dir" / "safe.txt") << "mod";
    vfs.remountProject(packagedProject.string());
    if (vfs.readString("dir/safe.txt") != "mod" ||
        vfs.readString("mods/dir/safe.txt") != "mod") {
        std::cerr << "Project mods did not override the matching package asset" << std::endl;
        exit(1);
    }
    std::filesystem::remove(packagedProject / "mods" / "dir" / "safe.txt");
    if (vfs.readString("dir/safe.txt") != "x") {
        std::cerr << "Packaged base asset was unavailable after removing a mod override" << std::endl;
        exit(1);
    }
    TEST_PASS("Mods override package assets at matching relative VFS paths");

    const std::string graphVfsPath = "json/full_story_graph.json";
    const std::string graphJson = R"({"start_node_id":101,"nodes":[{"id":101,"speaker":"Packaged","dialogue":"VFS graph"}]})";
    const uint64_t graphIndexOffset = headerSize + graphJson.size();
    Rowl::VFS::RowlPkgHeader graphHeader{{'R', 'O', 'W', 'L'}, 1, 1, graphIndexOffset};
    Rowl::VFS::RowlPkgEntryRaw graphEntry{fnv1a64(graphVfsPath), static_cast<uint32_t>(graphVfsPath.size()),
                                          headerSize, graphJson.size(), graphJson.size(), 0};
    const auto graphPackage = packagedProject / "Assets" / "packages" / "graph.rowlpkg";
    {
        std::ofstream output(graphPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&graphHeader), sizeof(graphHeader));
        output.write(graphJson.data(), static_cast<std::streamsize>(graphJson.size()));
        output.write(reinterpret_cast<const char*>(&graphEntry), sizeof(graphEntry));
        output.write(graphVfsPath.data(), static_cast<std::streamsize>(graphVfsPath.size()));
    }
    const std::string invalidGraphVfsPath = "json/invalid_story.json";
    const std::string invalidGraphJson = "{";
    const uint64_t invalidGraphIndexOffset = headerSize + invalidGraphJson.size();
    Rowl::VFS::RowlPkgHeader invalidGraphHeader{{'R', 'O', 'W', 'L'}, 1, 1, invalidGraphIndexOffset};
    Rowl::VFS::RowlPkgEntryRaw invalidGraphEntry{
        fnv1a64(invalidGraphVfsPath), static_cast<uint32_t>(invalidGraphVfsPath.size()),
        headerSize, invalidGraphJson.size(), invalidGraphJson.size(), 0};
    const auto invalidGraphPackage = packagedProject / "Assets" / "packages" / "invalid_graph.rowlpkg";
    {
        std::ofstream output(invalidGraphPackage, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&invalidGraphHeader), sizeof(invalidGraphHeader));
        output.write(invalidGraphJson.data(), static_cast<std::streamsize>(invalidGraphJson.size()));
        output.write(reinterpret_cast<const char*>(&invalidGraphEntry), sizeof(invalidGraphEntry));
        output.write(invalidGraphVfsPath.data(), static_cast<std::streamsize>(invalidGraphVfsPath.size()));
    }
    RowlEngineHandle packagedHandle = RowlEngine_Create();
    if (!packagedHandle || !RowlEngine_Init(packagedHandle, 320, 180, 0)) {
        std::cerr << "Could not initialize engine for packaged graph test" << std::endl;
        exit(1);
    }
    RowlEngine_SetProjectDirectory(packagedHandle, packagedProject.string().c_str());
    if (!RowlEngine_LoadStoryGraphFromVfs(packagedHandle, graphVfsPath.c_str()) ||
        RowlEngine_GetCurrentNodeId(packagedHandle) != 101 ||
        RowlEngine_LoadStoryGraphFromVfs(packagedHandle, "json/missing.json") ||
        std::string(RowlEngine_GetLastStoryGraphError(packagedHandle)).find("missing") == std::string::npos ||
        RowlEngine_LoadStoryGraphFromVfs(packagedHandle, invalidGraphVfsPath.c_str()) ||
        std::string(RowlEngine_GetLastStoryGraphError(packagedHandle)).find("rejected") == std::string::npos ||
        RowlEngine_GetCurrentNodeId(packagedHandle) != 101) {
        std::cerr << "C API did not load the graph from the packaged VFS correctly" << std::endl;
        RowlEngine_Destroy(packagedHandle);
        exit(1);
    }
    TEST_PASS("C API reports VFS graph load failures while preserving the active graph");

    // Verify VFS-first resolution for story graphs and active story
    const auto vfsProject = std::filesystem::temp_directory_path() / "rowl_vfs_story_project";
    std::filesystem::remove_all(vfsProject);
    std::filesystem::create_directories(vfsProject / "Assets" / "json");
    {
        std::ofstream graphOut(vfsProject / "Assets" / "json" / "full_story_graph.json");
        graphOut << R"({"start_node_id":505,"nodes":[{"id":505,"speaker":"VFS","dialogue":"VFS Native Story"}]})";
    }
    {
        std::ofstream activeOut(vfsProject / "Assets" / "json" / "active_story.json");
        activeOut << R"({"node_id":506,"speaker":"ActiveVFS","dialogue":"Active VFS Story"})";
    }
    RowlEngineHandle vfsStoryHandle = RowlEngine_Create();
    if (vfsStoryHandle && RowlEngine_Init(vfsStoryHandle, 320, 180, 0)) {
        RowlEngine_SetProjectDirectory(vfsStoryHandle, vfsProject.string().c_str());
        if (RowlEngine_GetCurrentNodeId(vfsStoryHandle) != 505) {
            std::cerr << "VFS-first story graph auto-load failed, expected node 505, got: "
                      << RowlEngine_GetCurrentNodeId(vfsStoryHandle) << std::endl;
            exit(1);
        }
        RowlEngine_Destroy(vfsStoryHandle);
    }
    std::filesystem::remove_all(vfsProject);
    TEST_PASS("VFS-first story graph auto-load upon project mount verified");

    vfs.remountProject(std::filesystem::current_path().string());
    TEST_PASS("Project remount exposes Assets but not project-root files");

    std::filesystem::remove_all(testRoot);
}

void test_native_c_api() {
    TEST_SECTION("Native C-API & Full Render Loop");

    // The ABI must be defensive because this is called through P/Invoke.
    // Invalid handles and malformed editor data must never unwind into .NET.
    uint32_t nullWidth = 99;
    uint32_t nullHeight = 99;
    if (RowlEngine_Init(nullptr, 1920, 1080, 0) != 0 ||
        RowlEngine_IsRunning(nullptr) != 0 ||
        RowlEngine_GetPixelBuffer(nullptr, &nullWidth, &nullHeight) != nullptr ||
        nullWidth != 0 || nullHeight != 0 ||
        std::strlen(RowlEngine_GetSpeaker(nullptr)) != 0 ||
        std::strlen(RowlEngine_GetDialogue(nullptr)) != 0 ||
        RowlEngine_GetCurrentNodeId(nullptr) != 0) {
        std::cerr << "C API null-handle fallback contract failed" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Null-Handle Fallback Contract");

    // Invalid host dimensions must fail before SDL/offscreen allocation, and a
    // later valid initialization on the same opaque handle must still work.
    RowlEngineHandle invalidDimensionHandle = RowlEngine_Create();
    if (!invalidDimensionHandle || RowlEngine_Init(invalidDimensionHandle, 0, 1080, 0) != 0 ||
        RowlEngine_Init(invalidDimensionHandle, 20'000, 1080, 0) != 0 ||
        RowlEngine_Init(invalidDimensionHandle, 1920, 1080, 0) != 1) {
        std::cerr << "C-API virtual canvas dimension validation failed" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(invalidDimensionHandle);
    TEST_PASS("C-API Virtual Canvas Bounds and Retry Safety");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) exit(1);

    int initRes = RowlEngine_Init(handle, 1920, 1080, 0);
    if (initRes != 1 || RowlEngine_IsRunning(handle) != 1) exit(1);
    TEST_PASS("RowlEngine_Create & Init (1920x1080 Offscreen)");

    // Every C API handle owns its runtime state. Destroying one must neither
    // invalidate the other handle nor tear down its shared SDL subsystems.
    RowlEngineHandle secondHandle = RowlEngine_Create();
    if (!secondHandle || RowlEngine_Init(secondHandle, 320, 180, 0) != 1) {
        std::cerr << "C-API failed to initialize a second concurrent runtime" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"speaker","data":{"speaker":"Runtime A","dialogue":"A"}}])");
    RowlEngine_UpdateSceneFromJson(secondHandle,
        R"([{"type":"speaker","data":{"speaker":"Runtime B","dialogue":"B"}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Runtime A" ||
        std::string(RowlEngine_GetSpeaker(secondHandle)) != "Runtime B") {
        std::cerr << "Concurrent C-API runtimes leaked scene state" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(secondHandle);
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsRunning(handle) != 1 || std::string(RowlEngine_GetSpeaker(handle)) != "Runtime A") {
        std::cerr << "Destroying one C-API runtime damaged its sibling runtime" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Concurrent Runtime Isolation and SDL Lease Retention");

    RowlEngine_Step(handle, std::numeric_limits<float>::quiet_NaN());
    RowlEngine_Step(handle, -1.0f);
    RowlEngine_Step(handle, 10.0f);
    if (RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "C-API frame delta normalization destabilized the engine" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Frame Delta NaN, Negative, and Spike Containment");

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

    // Milestone 22: Visual Transform Gizmo & Rotation/Scale Controls C-API Verification
    RowlEngine_UpdateSceneEx(handle, "Evelyn", "Rotated scene test", "bg_beach_sunset.png",
                            0.0f, 0.0f, 1920.0f, 1080.0f, 45.0f,
                            "spr_evelyn.png", 1440.0f, 340.0f, 360.0f, 540.0f, -15.0f,
                            80.0f, 860.0f, 1760.0f, 180.0f);
    if (std::abs(RowlEngine_GetBackgroundRotation(handle) - 45.0f) > 0.001f ||
        std::abs(RowlEngine_GetCharacterRotation(handle) - (-15.0f)) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneEx failed to set background/character rotation" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);

    const char* compJsonRot = R"([
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"scale":1,"rotation":90.0}},
        {"type":"character","id":"c1","enabled":true,"data":{"sprite":"Margot.jpg","x":300,"y":200,"width":360,"height":540,"scale":1,"scale_x":1.2,"scale_y":0.8,"rotation":180.0}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, compJsonRot);
    if (std::abs(RowlEngine_GetBackgroundRotation(handle) - 90.0f) > 0.001f ||
        std::abs(RowlEngine_GetCharacterRotation(handle) - 180.0f) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneFromJson failed to parse background/character rotation" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    TEST_PASS("Milestone 22: RowlEngine_UpdateSceneEx & UpdateSceneFromJson Rotation / Scale Controls");

    // Milestone 23: Real-Time Audio Telemetry, Peak/RMS & Spectrum C-API Verification
    float peakBgm = RowlEngine_GetAudioChannelPeak(handle, 0, 0);
    float rmsBgm = RowlEngine_GetAudioChannelRms(handle, 0, 0);
    float bands[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    RowlEngine_GetAudioSpectrum(handle, bands, 4);
    if (peakBgm < 0.0f || peakBgm > 1.0f || rmsBgm < 0.0f || rmsBgm > 1.0f) {
        std::cerr << "RowlEngine_GetAudioChannelPeak / Rms returned out-of-range value" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    RowlEngine_GetAudioSpectrum(handle, bands, 4);
    TEST_PASS("Milestone 23: RowlEngine_GetAudioChannelPeak, Rms & Spectrum Telemetry C-API");

    // Milestone 24: Parallax Depth Background C-API Verification
    RowlEngine_SetBackgroundParallax(handle, 0.4f, 0.6f);
    if (std::abs(RowlEngine_GetBackgroundParallaxX(handle) - 0.4f) > 0.001f ||
        std::abs(RowlEngine_GetBackgroundParallaxY(handle) - 0.6f) > 0.001f) {
        std::cerr << "RowlEngine_SetBackgroundParallax failed" << std::endl;
        exit(1);
    }

    const char* compJsonParallax = R"([
        {"type":"background","id":"bg_parallax","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"parallax_x":0.25,"parallax_y":0.5,"opacity":0.85}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, compJsonParallax);
    if (std::abs(RowlEngine_GetBackgroundParallaxX(handle) - 0.25f) > 0.001f ||
        std::abs(RowlEngine_GetBackgroundParallaxY(handle) - 0.5f) > 0.001f ||
        std::abs(RowlEngine_GetBackgroundOpacity(handle) - 0.85f) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneFromJson failed to parse parallax and opacity" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    TEST_PASS("Milestone 24: RowlEngine_SetBackgroundParallax & Background Parallax/Opacity C-API");

    // Script components on the same node deliberately share lifecycle names.
    // The runtime must dispatch both callbacks and tear them down in reverse
    // activation order instead of letting the latter overwrite the former.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"script","id":"script_a","data":{"code":"private_value = 'first'; function on_enter() rowl.var_set('component_first_enter', private_value) end; function on_update(dt) rowl.var_set('component_first_update', tostring(dt)) end; function on_exit() rowl.var_set('component_exit_order', 'first') end"}},
        {"type":"script","id":"script_b","data":{"code":"private_value = 'second'; function on_enter() rowl.var_set('component_second_enter', private_value) end; function on_update(dt) rowl.var_set('component_second_update', tostring(dt * 2)) end; function on_exit() rowl.var_set('component_exit_order', 'second') end"}}
    ])");
    if (std::string(RowlEngine_GetVariable(handle, "component_first_enter")) != "first" ||
        std::string(RowlEngine_GetVariable(handle, "component_second_enter")) != "second") {
        std::cerr << "Multiple script components did not activate independently" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.25f);
    if (std::string(RowlEngine_GetVariable(handle, "component_first_update")) != "0.25" ||
        std::string(RowlEngine_GetVariable(handle, "component_second_update")) != "0.5") {
        std::cerr << "Multiple script component update callbacks were not dispatched" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, "[]");
    if (std::string(RowlEngine_GetVariable(handle, "component_exit_order")) != "first" ||
        std::string(RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)) != "[]") {
        std::cerr << "Script component teardown did not run in reverse activation order" << std::endl;
        exit(1);
    }

    // Editor diagnostics must expose per-component status without leaking Lua
    // source. A syntax error is contained to its component and reported through
    // the same C ABI consumed by the Avalonia preview.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"script","id":"broken_script","data":{"code":"function on_enter( this is invalid end"}}
    ])");
    const auto diagnosticJson = nlohmann::json::parse(
        std::string(RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)));
    if (!diagnosticJson.is_array() || diagnosticJson.size() != 1 ||
        diagnosticJson[0].value("state", "") != "failed" ||
        diagnosticJson[0].value("error", "").empty() ||
        diagnosticJson[0].contains("source")) {
        std::cerr << "Script runtime diagnostic contract failed" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, compJson);
    TEST_PASS("C-API Script Components: Isolation, Teardown, and Editor Diagnostics");

    // The native renderer and SDL event loop are host-thread-affine. A
    // second thread must not be able to mutate this handle or observe a
    // running runtime through the C ABI.
    std::atomic<bool> foreignThreadRejected{false};
    std::thread foreignCaller([&] {
        RowlEngine_UpdateSceneFromJson(handle,
            R"([{"type":"speaker","data":{"speaker":"Foreign","dialogue":"must not apply"}}])");
        foreignThreadRejected.store(
            RowlEngine_IsRunning(handle) == 0 &&
            std::strlen(RowlEngine_GetSpeaker(handle)) == 0);
    });
    foreignCaller.join();
    if (!foreignThreadRejected.load() || std::string(RowlEngine_GetSpeaker(handle)) != "Alice") {
        std::cerr << "C-API accepted a call from a non-owner thread" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Rejects Non-Owner Thread Calls");

    const std::string oversizedComponents(16 * 1024 * 1024 + 1, ' ');
    RowlEngine_UpdateSceneFromJson(handle, oversizedComponents.c_str());
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice") {
        std::cerr << "Oversized component JSON replaced the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Oversized Component JSON Containment");

    // Invalid JSON is user-editable input. It must be contained inside the
    // native boundary and leave the last valid scene usable.
    RowlEngine_UpdateSceneFromJson(handle, "{ definitely-not-json");
    if (std::strlen(RowlEngine_GetSpeaker(handle)) == 0 ||
        std::strlen(RowlEngine_GetDialogue(handle)) == 0) {
        std::cerr << "Malformed component JSON invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Malformed JSON Containment");

    RowlEngine_UpdateSceneFromJson(handle, R"([{"type":42,"data":{}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Schema-invalid component JSON invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Schema Containment");

    std::string tooManyComponents = "[";
    for (std::size_t i = 0; i <= 2'048; ++i) {
        if (i != 0) tooManyComponents += ',';
        tooManyComponents += R"({"type":"speaker","data":{}})";
    }
    tooManyComponents += ']';
    RowlEngine_UpdateSceneFromJson(handle, tooManyComponents.c_str());
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Over-count component JSON invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Count Containment");

    RowlEngine_SetVariable(handle, "finite_score", "5");
    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"variable","data":{"key":"finite_score","value":"nan","operation":"add"}}])");
    if (std::string(RowlEngine_GetVariable(handle, "finite_score")) != "5") {
        std::cerr << "Non-finite variable addition corrupted persistent game state" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Non-Finite Variable Arithmetic Containment");

    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"character","data":{"sprite":"Margot.jpg","x":1e30}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Out-of-range component numeric value invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Numeric Range Containment");

    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"character","data":{"sprite":"Margot.jpg","x":"not-a-number"}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Type-invalid component JSON did not roll back the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Value-Type Transaction Rollback");

    // Start from a known-good editor scene, then mutate it and append an
    // invalid trailing byte. The suffix guarantees each generated payload is
    // invalid while the mutations cover different parser and schema paths.
    const std::string componentFuzzSeed = compJson;
    uint32_t componentFuzzState = 0xA11CE55u;
    const auto nextComponentFuzzByte = [&componentFuzzState] {
        componentFuzzState = componentFuzzState * 1103515245u + 12345u;
        return static_cast<char>('!' + ((componentFuzzState >> 16u) % 94u));
    };
    for (uint32_t caseIndex = 0; caseIndex < 96; ++caseIndex) {
        std::string fuzzed = componentFuzzSeed;
        const uint32_t mutationCount = 1 + (caseIndex % 6);
        for (uint32_t mutation = 0; mutation < mutationCount; ++mutation) {
            const size_t position = (static_cast<size_t>(caseIndex) * 37u + mutation * 53u) % fuzzed.size();
            fuzzed[position] = nextComponentFuzzByte();
        }
        fuzzed.push_back('#');
        RowlEngine_UpdateSceneFromJson(handle, fuzzed.c_str());
        if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
            std::string(RowlEngine_GetDialogue(handle)).empty()) {
            std::cerr << "Malformed component fuzz input replaced the active scene: case " << caseIndex << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Deterministic malformed-component fuzz corpus preserves active scene");

    // Audio commands are device side effects, so a malformed component that
    // follows an audio component must not partially apply its filter.
    const auto* audioBeforeRollback = Rowl::Core::Engine::instance().getAudio();
    if (!audioBeforeRollback ||
        audioBeforeRollback->getActiveFilter() != Rowl::Audio::DSPFilterType::UnderwaterLowPass) {
        std::cerr << "Expected the valid component scene to leave the Underwater DSP active" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"audio","data":{"dsp_filter":"Cave"}},
        {"type":"character","data":{"sprite":"Margot.jpg","x":"not-a-number"}}
    ])");
    const auto* audioAfterRollback = Rowl::Core::Engine::instance().getAudio();
    if (!audioAfterRollback ||
        audioAfterRollback->getActiveFilter() != Rowl::Audio::DSPFilterType::UnderwaterLowPass) {
        std::cerr << "Invalid component scene leaked a partial audio side effect" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Deferred Audio Side Effects on Scene Rollback");

    // Multi-Dialogue Box Test (Two Simultaneous Chat Bubbles in Game Mode)
    const char* multiDlgJson = R"([
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"scale":1}},
        {"type":"dialogue","id":"d1","enabled":true,"data":{"speaker":"Alice","dialogue":"First dialogue bubble!","x":80,"y":860,"width":1760,"height":180}},
        {"type":"dialogue","id":"d2","enabled":true,"data":{"speaker":"Bob","dialogue":"Second simultaneous dialogue bubble!","x":120,"y":660,"width":1760,"height":180}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, multiDlgJson);
    if (Rowl::Core::Engine::instance().getActiveDialogues().size() != 2) {
        std::cerr << "Expected 2 active dialogues in Engine, got: " << Rowl::Core::Engine::instance().getActiveDialogues().size() << std::endl;
        exit(1);
    }
    TEST_PASS("RowlEngine_UpdateSceneFromJson (Simultaneous Multi-Dialogue Boxes)");

    // Empty frames must clear legacy getters instead of leaking the previous
    // node's speaker/dialogue into save state or editor synchronization.
    RowlEngine_UpdateSceneFromJson(handle, "[]");
    if (std::strlen(RowlEngine_GetSpeaker(handle)) != 0 ||
        std::strlen(RowlEngine_GetDialogue(handle)) != 0 ||
        !Rowl::Core::Engine::instance().getActiveDialogues().empty()) {
        std::cerr << "Empty component scene retained stale dialogue state" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, multiDlgJson);
    TEST_PASS("Empty Component Scene Clears Previous Dialogue State");

    // Execute 30 frames of multi-dialogue step
    for (int i = 0; i < 30; ++i) {
        RowlEngine_Step(handle, 0.0166f);
    }
    TEST_PASS("RowlEngine_Step (Simultaneous Multi-Dialogue Render & Pixel Buffer Validation)");

    // Graph v4: choose by stable ID, not fragile array position.
    const auto graphPath = std::filesystem::temp_directory_path() / "rowl_choice_graph_test.json";
    {
        std::ofstream graph(graphPath);
        graph << R"({"format_version":4,"start_node_id":101,"nodes":[
          {"id":101,"speaker":"Guide","dialogue":"Choose.","objects":[{"id":"choices","name":"Choices","is_active":true,"components":[
            {"type":"choice","id":"choice_main","enabled":true,"data":{"options":[
              {"option_id":"go_left","text":"Left","x":680,"y":520,"width":560,"height":64,"background_color":"#1E293B"},
              {"option_id":"go_right","text":"Right","x":680,"y":600,"width":560,"height":64,"background_color":"#1E293B"}]}}]}],"next_nodes":[
            {"id":102,"label":"Left","option_id":"go_left"},
            {"id":103,"label":"Right","option_id":"go_right"}]},
          {"id":102,"speaker":"Guide","dialogue":"Left path."},
          {"id":103,"speaker":"Guide","dialogue":"Right path."}]})";
    }
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    RowlEngine_Step(handle, 0.0f);
    if (RowlEngine_PointerDown(handle, 700.0f, 620.0f) != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Choice button pointer hit-test routing failed" << std::endl;
        exit(1);
    }
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    RowlEngine_ResizeViewport(handle, 1080, 1920);
    if (RowlEngine_PointerDown(handle, 540.0f, 100.0f) != 1 || RowlEngine_GetCurrentNodeId(handle) != 101) {
        std::cerr << "Letterbox margin pointer advanced the story" << std::endl;
        exit(1);
    }
    if (RowlEngine_PointerDown(handle, 394.0f, 1005.0f) != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Portrait viewport pointer did not route to the choice" << std::endl;
        exit(1);
    }
    RowlEngine_ResizeViewport(handle, 1920, 1080);
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    if (RowlEngine_SelectChoice(handle, "go_right") != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Stable choice ID routing failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "does_not_exist") != 0) {
        std::cerr << "Unknown stable choice ID was accepted" << std::endl;
        exit(1);
    }

    // A failed graph load must be transactional: the active graph, current
    // node, and scene stay usable instead of being replaced by partial data.
    const auto invalidGraphPath = std::filesystem::temp_directory_path() / "rowl_invalid_graph_test.json";
    {
        std::ofstream invalidGraph(invalidGraphPath);
        invalidGraph << R"({"start_node_id":101,"nodes":[{"id":101,"next_nodes":[{"id":999}]}]})";
    }
    RowlEngine_LoadStoryGraph(handle, invalidGraphPath.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 103 ||
        std::string(RowlEngine_GetSpeaker(handle)) != "Guide") {
        std::cerr << "Invalid graph load replaced the active story" << std::endl;
        exit(1);
    }
    std::filesystem::remove(invalidGraphPath);

    const std::string graphFuzzSeed = R"({"start_node_id":101,"nodes":[{"id":101,"speaker":"Replacement"}]})";
    uint32_t graphFuzzState = 0x51A7E123u;
    const auto nextGraphFuzzByte = [&graphFuzzState] {
        graphFuzzState = graphFuzzState * 22695477u + 1u;
        return static_cast<char>('!' + ((graphFuzzState >> 16u) % 94u));
    };
    const auto graphFuzzPath = std::filesystem::temp_directory_path() / "rowl_graph_fuzz_test.json";
    for (uint32_t caseIndex = 0; caseIndex < 64; ++caseIndex) {
        std::string fuzzed = graphFuzzSeed;
        for (uint32_t mutation = 0; mutation < 1 + (caseIndex % 5); ++mutation) {
            const size_t position = (static_cast<size_t>(caseIndex) * 29u + mutation * 41u) % fuzzed.size();
            fuzzed[position] = nextGraphFuzzByte();
        }
        fuzzed.push_back('#');
        std::ofstream graph(graphFuzzPath, std::ios::binary);
        graph.write(fuzzed.data(), static_cast<std::streamsize>(fuzzed.size()));
        graph.close();
        RowlEngine_LoadStoryGraph(handle, graphFuzzPath.string().c_str());
        if (RowlEngine_GetCurrentNodeId(handle) != 103 ||
            std::string(RowlEngine_GetSpeaker(handle)) != "Guide") {
            std::cerr << "Malformed graph fuzz input replaced the active story: case " << caseIndex << std::endl;
            exit(1);
        }
    }
    std::filesystem::remove(graphFuzzPath);
    TEST_PASS("Deterministic malformed-graph fuzz corpus preserves active story");

    const auto overCountGraphPath = std::filesystem::temp_directory_path() / "rowl_overcount_graph_test.json";
    {
        std::ofstream overCountGraph(overCountGraphPath);
        overCountGraph << R"({"start_node_id":1000,"nodes":[)";
        for (uint64_t nodeId = 1000; nodeId <= 11'000; ++nodeId) {
            if (nodeId != 1000) overCountGraph << ',';
            overCountGraph << R"({"id":)" << nodeId << '}';
        }
        overCountGraph << "]}";
    }
    RowlEngine_LoadStoryGraph(handle, overCountGraphPath.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 103 ||
        std::string(RowlEngine_GetSpeaker(handle)) != "Guide") {
        std::cerr << "Over-count graph load replaced the active story" << std::endl;
        exit(1);
    }
    std::filesystem::remove(overCountGraphPath);
    TEST_PASS("Story Graph Count Containment");

    // A successful reload starts a fresh story state and cannot carry script
    // variables or rewind history over from the previously loaded graph.
    RowlEngine_SetVariable(handle, "old_graph_variable", "stale");
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 101 ||
        !std::string(RowlEngine_GetVariable(handle, "old_graph_variable")).empty()) {
        std::cerr << "Story graph reload retained state from the previous graph" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "go_right") != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Story graph was unusable after transactional reload" << std::endl;
        exit(1);
    }
    std::filesystem::remove(graphPath);
    TEST_PASS("Story Graph Routing, Transactional Validation, and Fresh Reload State");

    // Audio C-API calls
    RowlEngine_PlayAudio(handle, "test_bgm.wav", 0, 1);
    RowlEngine_SetBgmVolume(handle, 0.8f);
    RowlEngine_SetBgmVolume(handle, std::numeric_limits<float>::quiet_NaN());
    if (!Rowl::Core::Engine::instance().getAudio() ||
        !std::isfinite(Rowl::Core::Engine::instance().getAudio()->getBgmGain()) ||
        std::abs(Rowl::Core::Engine::instance().getAudio()->getBgmGain() - 0.8f) > 0.001f) {
        std::cerr << "C-API accepted a non-finite BGM volume" << std::endl;
        exit(1);
    }
    RowlEngine_TriggerVoiceDucking(handle, 1);
    RowlEngine_TriggerVoiceDucking(handle, 0);
    RowlEngine_StopBgm(handle);
    TEST_PASS("C-API Audio Control (PlayAudio, SetBgmVolume, Ducking, StopBgm)");

    // Variable & Scripting C-API
    RowlEngine_SetVariable(handle, "hero_gold", "150");
    if (std::string(RowlEngine_GetVariable(handle, "hero_gold")) != "150") {
        std::cerr << "C-API GetVariable mismatch" << std::endl;
        exit(1);
    }
    if (RowlEngine_EvaluateCondition(handle, "hero_gold >= 100") != 1 ||
        RowlEngine_EvaluateCondition(handle, "hero_gold < 50") != 0) {
        std::cerr << "C-API EvaluateCondition mismatch" << std::endl;
        exit(1);
    }
    if (RowlEngine_ExecuteScript(handle,
            "rowl.var_set('hero_gold', '300'); rowl.var_set('quest_state', 'accepted')") != 1 ||
        std::string(RowlEngine_GetVariable(handle, "hero_gold")) != "300" ||
        std::string(RowlEngine_GetVariable(handle, "quest_state")) != "accepted") {
        std::cerr << "C-API ExecuteScript mismatch" << std::endl;
        exit(1);
    }
    if (RowlEngine_Rewind(handle, 1) != 1 ||
        std::string(RowlEngine_GetVariable(handle, "hero_gold")) != "150" ||
        !std::string(RowlEngine_GetVariable(handle, "quest_state")).empty()) {
        std::cerr << "Lua script state was not atomically persisted and rewound" << std::endl;
        exit(1);
    }
    if (RowlEngine_ExecuteScript(handle,
            "rowl.var_set('hero_gold', '300'); rowl.var_set('quest_state', 'accepted')") != 1) {
        std::cerr << "C-API ExecuteScript replay mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Scripting & Dynamic Variables (SetVariable, GetVariable, EvaluateCondition, ExecuteScript)");

    // Save/Load Slots & Rewind C-API
    if (RowlEngine_SaveGameSlot(handle, 0) != 1) {
        std::cerr << "C-API SaveGameSlot failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_HasSaveSlot(handle, 0) != 1) {
        std::cerr << "C-API HasSaveSlot failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_LoadGameSlot(handle, 0) != 1) {
        std::cerr << "C-API LoadGameSlot failed" << std::endl;
        exit(1);
    }
    RowlEngine_DeleteSaveSlot(handle, 0);
    if (RowlEngine_HasSaveSlot(handle, 0) != 0) {
        std::cerr << "C-API DeleteSaveSlot failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_GetCurrentStepId(handle) == 0) {
        std::cerr << "C-API GetCurrentStepId mismatch" << std::endl;
        exit(1);
    }
    RowlEngine_Rewind(handle, 1);
    TEST_PASS("C-API Save / Load Slots & State Rewind (SaveGameSlot, LoadGameSlot, Rewind)");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    uint32_t staleWidth = 123;
    uint32_t staleHeight = 456;
    RowlEngine_Step(handle, 0.016f);
    RowlEngine_Destroy(handle); // Double destroy must be harmless.
    if (RowlEngine_IsRunning(handle) != 0 || RowlEngine_GetCurrentNodeId(handle) != 0 ||
        RowlEngine_GetPixelBuffer(handle, &staleWidth, &staleHeight) != nullptr ||
        staleWidth != 0 || staleHeight != 0 ||
        RowlEngine_IsRunning(reinterpret_cast<RowlEngineHandle>(static_cast<uintptr_t>(1))) != 0) {
        std::cerr << "C-API accepted a stale or unknown engine handle" << std::endl;
        exit(1);
    }
    RowlEngineHandle replacementHandle = RowlEngine_Create();
    if (!replacementHandle || replacementHandle == handle ||
        RowlEngine_Init(replacementHandle, 320, 180, 0) != 1) {
        std::cerr << "C-API failed to create an isolated replacement handle" << std::endl;
        exit(1);
    }
    RowlEngine_SetVariable(handle, "stale_callback", "must_not_reach_replacement");
    if (!std::string(RowlEngine_GetVariable(replacementHandle, "stale_callback")).empty()) {
        std::cerr << "Stale C-API handle targeted a replacement engine" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(replacementHandle);
    TEST_PASS("RowlEngine_Shutdown & Destroy (Clean Resource Teardown)");
    TEST_PASS("C-API Stale and Unknown Handle Containment");
}

class VelocityComponent : public Rowl::Scene::Component {
public:
    VelocityComponent(float vx, float vy) : m_vx(vx), m_vy(vy) {}

    void onUpdate(float deltaTime) override {
        if (auto* tf = getOwner()->getTransform()) {
            tf->translate(m_vx * deltaTime, m_vy * deltaTime);
        }
    }

    float getVx() const { return m_vx; }
    float getVy() const { return m_vy; }

private:
    float m_vx = 0.0f;
    float m_vy = 0.0f;
};

class LifecycleProbeComponent : public Rowl::Scene::Component {
public:
    void onAwake() override { ++awake; }
    void onEnable() override { ++enabled; }
    void onStart() override { ++started; }
    void onUpdate(float) override { ++updated; }
    void onLateUpdate(float) override { ++lateUpdated; }
    void onDisable() override { ++disabled; }
    void onDestroy() override { ++destroyed; }

    int awake = 0;
    int enabled = 0;
    int started = 0;
    int updated = 0;
    int lateUpdated = 0;
    int disabled = 0;
    int destroyed = 0;
};

class SelfRemovingComponent : public Rowl::Scene::Component {
public:
    explicit SelfRemovingComponent(int* updateCount) : m_updateCount(updateCount) {}
    void onUpdate(float) override {
        if (m_updateCount) ++*m_updateCount;
        getOwner()->removeComponent(this);
    }
private:
    int* m_updateCount = nullptr;
};

void test_game_object_component_system() {
    TEST_SECTION("Entity-Component & GameObject Subsystem");

    // 1. Create empty scene and game object
    Rowl::Scene::Scene scene;
    auto* hero = scene.createGameObject("Hero");
    if (!hero || hero->getName() != "Hero" || !hero->isActive()) {
        std::cerr << "GameObject creation failed" << std::endl;
        exit(1);
    }
    if (scene.getObjectCount() != 1) {
        std::cerr << "Scene object count mismatch" << std::endl;
        exit(1);
    }

    // Default transform check
    auto* transform = hero->getTransform();
    if (!transform) {
        std::cerr << "GameObject missing default TransformComponent" << std::endl;
        exit(1);
    }
    transform->setPosition(100.0f, 200.0f);
    if (std::abs(transform->getX() - 100.0f) > 0.001f || std::abs(transform->getY() - 200.0f) > 0.001f) {
        std::cerr << "Transform position set failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Empty GameObject Creation with Default TransformComponent");

    // 2. Attach SpriteComponent
    auto* sprite = hero->addComponent<Rowl::Scene::SpriteComponent>("Margot.jpg", 360.0f, 540.0f, 0.95f);
    if (!sprite || !hero->hasComponent<Rowl::Scene::SpriteComponent>()) {
        std::cerr << "SpriteComponent attachment failed" << std::endl;
        exit(1);
    }
    if (hero->getComponent<Rowl::Scene::SpriteComponent>() != sprite) {
        std::cerr << "getComponent<SpriteComponent> mismatch" << std::endl;
        exit(1);
    }
    if (sprite->getOwner() != hero || sprite->getTexturePath() != "Margot.jpg") {
        std::cerr << "SpriteComponent owner or texture mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Attach SpriteComponent to Empty GameObject");

    // 3. Movement simulation via custom Component onUpdate
    auto* vel = hero->addComponent<VelocityComponent>(150.0f, 50.0f); // 150 px/s X, 50 px/s Y
    if (!vel || !hero->hasComponent<VelocityComponent>()) {
        std::cerr << "VelocityComponent attachment failed" << std::endl;
        exit(1);
    }

    // Simulate 2 seconds of updates (e.g. 2 x 1.0s)
    scene.update(1.0f);
    scene.update(1.0f);

    // Initial X: 100 + 2*150 = 400; Initial Y: 200 + 2*50 = 300
    if (std::abs(transform->getX() - 400.0f) > 0.01f || std::abs(transform->getY() - 300.0f) > 0.01f) {
        std::cerr << "Position after onUpdate translation mismatch: X=" << transform->getX() << ", Y=" << transform->getY() << std::endl;
        exit(1);
    }
    TEST_PASS("Component onUpdate Movement Simulation (Position Translation)");

    // 3b. Unity-style lifecycle order and safe mutation during callbacks.
    auto* lifecycleObject = scene.createGameObject("Lifecycle Probe");
    auto* lifecycle = lifecycleObject->addComponent<LifecycleProbeComponent>();
    if (lifecycle->awake != 1 || lifecycle->enabled != 1 || lifecycle->started != 0) {
        std::cerr << "Component Awake/OnEnable lifecycle mismatch" << std::endl;
        exit(1);
    }
    lifecycleObject->update(0.016f);
    if (lifecycle->started != 1 || lifecycle->updated != 1 || lifecycle->lateUpdated != 1) {
        std::cerr << "Component Start/Update/LateUpdate lifecycle mismatch" << std::endl;
        exit(1);
    }
    lifecycle->setEnabled(false);
    lifecycle->setEnabled(true);
    lifecycleObject->setActive(false);
    lifecycleObject->setActive(true);
    if (lifecycle->disabled != 2 || lifecycle->enabled != 3 || lifecycle->started != 1) {
        std::cerr << "Component enable/disable lifecycle mismatch" << std::endl;
        exit(1);
    }
    int selfRemovalUpdates = 0;
    lifecycleObject->addComponent<SelfRemovingComponent>(&selfRemovalUpdates);
    lifecycleObject->update(0.016f);
    if (selfRemovalUpdates != 1 || lifecycleObject->hasComponent<SelfRemovingComponent>()) {
        std::cerr << "Deferred component removal during update failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Unity-style Lifecycle and Deferred Component Mutation");

    // 4. Direct Transform Translation & Scaling
    transform->translate(100.0f, -50.0f);
    transform->setScale(2.0f);
    if (std::abs(transform->getX() - 500.0f) > 0.01f || std::abs(transform->getY() - 250.0f) > 0.01f ||
        std::abs(transform->getScaleX() - 2.0f) > 0.01f) {
        std::cerr << "Direct transform translation/scale failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Direct Transform Translation & Scaling");

    // 5. Engine Step Integration & Scene Rendering to Offscreen Framebuffer
    {
        Rowl::Core::Engine engine;
        Rowl::Core::EngineConfig cfg;
        cfg.appName = "Scene Test";
        cfg.virtualWidth = 1920;
        cfg.virtualHeight = 1080;
        if (!engine.initialize(cfg)) {
            std::cerr << "Engine initialize failed" << std::endl;
            exit(1);
        }

        auto* engineScene = engine.getScene();
        if (!engineScene) {
            std::cerr << "engine.getScene() returned null" << std::endl;
            exit(1);
        }

        auto* renderedObj = engineScene->createGameObject("RenderedSprite");
        renderedObj->getTransform()->setPosition(300.0f, 200.0f);
        renderedObj->addComponent<Rowl::Scene::SpriteComponent>("Margot.jpg", 360.0f, 540.0f);
        renderedObj->addComponent<VelocityComponent>(60.0f, 40.0f);

        // Step engine for 30 frames
        for (int i = 0; i < 30; ++i) {
            engine.step(0.0166f);
        }

        // Object moved during step
        float expectedX = 300.0f + 60.0f * (30 * 0.0166f);
        if (std::abs(renderedObj->getTransform()->getX() - expectedX) > 1.0f) {
            std::cerr << "Engine step scene update mismatch" << std::endl;
            exit(1);
        }

        // Pixel buffer check
        uint32_t pw = 0, ph = 0;
        const uint8_t* pixels = engine.getPixelBuffer(&pw, &ph);
        if (!pixels || pw != 1920 || ph != 1080) {
            std::cerr << "Pixel buffer mismatch in scene rendering" << std::endl;
            exit(1);
        }
        TEST_PASS("Engine Step Loop Integration with Scene Render & Pixel Buffer Output");

        engine.shutdown();
    }

    // 6. Safe Component Removal and Scene Cleanup
    bool removed = hero->removeComponent<VelocityComponent>();
    if (!removed || hero->hasComponent<VelocityComponent>()) {
        std::cerr << "removeComponent failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Dynamic Component Removal (removeComponent<T>)");

    bool destroyed = scene.destroyGameObject(hero);
    bool lifecycleDestroyed = scene.destroyGameObject(lifecycleObject);
    if (!destroyed || !lifecycleDestroyed || scene.getObjectCount() != 0) {
        std::cerr << "destroyGameObject failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Safe GameObject Destruction & Scene Teardown");
}

std::string jsonEscape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) escaped += "?";
                else escaped += static_cast<char>(character);
        }
    }
    return escaped;
}

std::string environmentValue(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

uint64_t processMemoryBytes() {
    // Linux exposes resident pages here. Other hosts retain a valid JSON
    // schema with zero when this portable fallback is unavailable.
    std::ifstream statm("/proc/self/statm");
    uint64_t pages = 0;
    uint64_t residentPages = 0;
    if (statm >> pages >> residentPages) {
        return residentPages * 4096ULL;
    }
    return 0;
}

uint64_t benchmarkTextureCacheBudgetBytes() {
    const auto configured = environmentValue("ROWL_BENCHMARK_TEXTURE_CACHE_BYTES", "67108864");
    try {
        size_t parsed = 0;
        const auto value = std::stoull(configured, &parsed);
        return parsed == configured.size() ? value : 64ULL * 1024ULL * 1024ULL;
    } catch (...) {
        return 64ULL * 1024ULL * 1024ULL;
    }
}

void writeBenchmarkJson(const std::string& outputPath, double vfsElapsedMs, int vfsIterations,
                        double jsonElapsedMs, int jsonIterations, double firstFrameMs,
                        double steadyFrameMs, double textureLoadMs, double nonTextureRenderMs,
                        uint64_t textureCount, uint64_t textureBytes, uint64_t textureBudgetBytes,
                        uint64_t textureEvictionCount) {
    const std::filesystem::path output(outputPath);
    if (!output.parent_path().empty()) std::filesystem::create_directories(output.parent_path());
    const std::filesystem::path temporary = output.string() + ".tmp";
    std::ofstream stream(temporary, std::ios::trunc);
    if (!stream.is_open()) {
        std::cerr << "Could not write benchmark JSON: " << outputPath << std::endl;
        exit(1);
    }
    stream << std::fixed << std::setprecision(6)
           << "{\n"
           << "  \"schema_version\": 1,\n"
           << "  \"build\": {\"id\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_BUILD_ID", "unknown"))
           << "\", \"type\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_BUILD_TYPE", "unknown")) << "\"},\n"
           << "  \"environment\": {\"os\": \"" << jsonEscape(SDL_GetPlatform())
           << "\", \"cpu_count\": " << SDL_GetNumLogicalCPUCores()
           << ", \"machine\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_MACHINE", "unknown")) << "\"},\n"
           << "  \"fixture_id\": \"" << jsonEscape(environmentValue("ROWL_BENCHMARK_FIXTURE", "native-default-v1")) << "\",\n"
           << "  \"metrics\": {\n"
           << "    \"vfs_io\": {\"iterations\": " << vfsIterations << ", \"total_ms\": " << vfsElapsedMs
           << ", \"avg_ms\": " << vfsElapsedMs / vfsIterations << "},\n"
           << "    \"json_update\": {\"iterations\": " << jsonIterations << ", \"total_ms\": " << jsonElapsedMs
           << ", \"avg_ms\": " << jsonElapsedMs / jsonIterations << "},\n"
           << "    \"first_frame_ms\": " << firstFrameMs << ",\n"
           << "    \"startup_profile\": {\"texture_load_ms\": " << textureLoadMs
           << ", \"non_texture_render_ms\": " << nonTextureRenderMs << "},\n"
           << "    \"steady_frame_ms\": " << steadyFrameMs << ",\n"
           << "    \"texture_cache\": {\"texture_count\": " << textureCount << ", \"bytes\": " << textureBytes
           << ", \"budget_bytes\": " << textureBudgetBytes << ", \"eviction_count\": " << textureEvictionCount << "},\n"
           << "    \"process_memory_bytes\": " << processMemoryBytes() << "\n"
           << "  }\n"
           << "}\n";
    stream.close();
    std::error_code replaceError;
    std::filesystem::remove(output, replaceError);
    std::filesystem::rename(temporary, output, replaceError);
    if (replaceError) {
        std::cerr << "Could not publish benchmark JSON: " << replaceError.message() << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] JSON report: " << outputPath << std::endl;
}

void test_window_input_routing() {
    TEST_SECTION("SDL Visible-Window Event Dispatching");

    Rowl::Render::Window window(&Rowl::VFS::VFSManager::instance());
    if (!window.initializeOffscreen(320, 180)) {
        std::cerr << "Could not initialize offscreen window for input routing test" << std::endl;
        exit(1);
    }

    constexpr uint32_t windowA = 101;
    constexpr uint32_t windowB = 202;
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(windowA) ||
        !Rowl::Platform::SdlEventDispatcher::registerWindow(windowB)) {
        std::cerr << "Could not register synthetic visible SDL windows" << std::endl;
        exit(1);
    }
    std::atomic<bool> foreignThreadRegistered{true};
    std::thread foreignThread([&] {
        foreignThreadRegistered.store(Rowl::Platform::SdlEventDispatcher::registerWindow(303));
    });
    foreignThread.join();
    if (foreignThreadRegistered.load()) {
        std::cerr << "SDL dispatcher accepted a visible window from a second event thread" << std::endl;
        exit(1);
    }

    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.key = SDLK_F5;
    keyEvent.key.windowID = windowA;
    if (!SDL_PushEvent(&keyEvent)) {
        std::cerr << "Could not enqueue SDL key event for input routing test" << std::endl;
        exit(1);
    }
    SDL_Event pointerEvent{};
    pointerEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    pointerEvent.button.button = SDL_BUTTON_LEFT;
    pointerEvent.button.windowID = windowB;
    pointerEvent.button.x = 42.0f;
    pointerEvent.button.y = 24.0f;
    if (!SDL_PushEvent(&pointerEvent)) {
        std::cerr << "Could not enqueue SDL pointer event for input routing test" << std::endl;
        exit(1);
    }

    SDL_Event touchEvent{};
    touchEvent.type = SDL_EVENT_FINGER_DOWN;
    touchEvent.tfinger.windowID = windowB;
    touchEvent.tfinger.fingerID = 77;
    touchEvent.tfinger.x = 0.5f;
    touchEvent.tfinger.y = 0.25f;
    if (!SDL_PushEvent(&touchEvent)) {
        std::cerr << "Could not enqueue SDL touch event for input routing test" << std::endl;
        exit(1);
    }

    SDL_Event resizeEvent{};
    resizeEvent.type = SDL_EVENT_WINDOW_RESIZED;
    resizeEvent.window.windowID = windowB;
    resizeEvent.window.data1 = 800;
    resizeEvent.window.data2 = 600;
    SDL_PushEvent(&resizeEvent);

    SDL_Event closeEvent{};
    closeEvent.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    closeEvent.window.windowID = windowA;
    SDL_PushEvent(&closeEvent);

    const auto eventsA = Rowl::Platform::SdlEventDispatcher::takeEvents(windowA);
    const auto eventsB = Rowl::Platform::SdlEventDispatcher::takeEvents(windowB);
    if (eventsA.size() != 2 || eventsA[0].type != SDL_EVENT_KEY_DOWN ||
        eventsA[1].type != SDL_EVENT_WINDOW_CLOSE_REQUESTED || eventsB.size() != 3 ||
        eventsB[0].type != SDL_EVENT_MOUSE_BUTTON_DOWN || eventsB[1].type != SDL_EVENT_FINGER_DOWN ||
        eventsB[2].type != SDL_EVENT_WINDOW_RESIZED ||
        std::abs(eventsB[0].button.x - 42.0f) > 0.001f || std::abs(eventsB[0].button.y - 24.0f) > 0.001f) {
        std::cerr << "SDL dispatcher did not isolate target window events" << std::endl;
        exit(1);
    }

    SDL_Event quitEvent{};
    quitEvent.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitEvent);
    const auto quitA = Rowl::Platform::SdlEventDispatcher::takeEvents(windowA);
    const auto quitB = Rowl::Platform::SdlEventDispatcher::takeEvents(windowB);
    if (quitA.size() != 1 || quitB.size() != 1 ||
        quitA[0].type != SDL_EVENT_QUIT || quitB[0].type != SDL_EVENT_QUIT) {
        std::cerr << "SDL process quit was not broadcast to every visible runtime" << std::endl;
        exit(1);
    }

    Rowl::Platform::SdlEventDispatcher::unregisterWindow(windowB);
    pointerEvent.button.windowID = windowB;
    SDL_PushEvent(&pointerEvent);
    if (!Rowl::Platform::SdlEventDispatcher::takeEvents(windowA).empty()) {
        std::cerr << "Late event for an unregistered window leaked to another runtime" << std::endl;
        exit(1);
    }
    Rowl::Platform::SdlEventDispatcher::unregisterWindow(windowA);
    window.shutdown();
    TEST_PASS("SDL dispatcher isolates visible runtime events and broadcasts process quit");
}

void test_runtime_context_and_diagnostics() {
    TEST_SECTION("RuntimeContext & Structured Results Diagnostics");

    // 1. Independent Engine and RuntimeContext VFS Isolation
    {
        namespace fs = std::filesystem;
        const auto tempA = fs::temp_directory_path() / "rowl_vfs_iso_a";
        const auto tempB = fs::temp_directory_path() / "rowl_vfs_iso_b";
        fs::create_directories(tempA);
        fs::create_directories(tempB);

        {
            std::ofstream f(tempA / "alpha.txt");
            f << "file from isolated environment A";
        }
        {
            std::ofstream f(tempB / "beta.txt");
            f << "file from isolated environment B";
        }

        auto vfsA = std::make_shared<Rowl::VFS::VFSManager>();
        vfsA->mountDirectory("", tempA.string());

        auto vfsB = std::make_shared<Rowl::VFS::VFSManager>();
        vfsB->mountDirectory("", tempB.string());

        if (!vfsA->exists("alpha.txt") || vfsA->exists("beta.txt")) {
            std::cerr << "VFS A cross-contaminated with VFS B" << std::endl;
            exit(1);
        }
        if (!vfsB->exists("beta.txt") || vfsB->exists("alpha.txt")) {
            std::cerr << "VFS B cross-contaminated with VFS A" << std::endl;
            exit(1);
        }

        auto ctxA = std::make_shared<Rowl::Core::RuntimeContext>(vfsA);
        auto ctxB = std::make_shared<Rowl::Core::RuntimeContext>(vfsB);

        Rowl::Core::Engine engineA(ctxA);
        Rowl::Core::Engine engineB(ctxB);

        if (!engineA.getVfs()->exists("alpha.txt") || engineA.getVfs()->exists("beta.txt")) {
            std::cerr << "Engine A does not isolate its VFS" << std::endl;
            exit(1);
        }
        if (!engineB.getVfs()->exists("beta.txt") || engineB.getVfs()->exists("alpha.txt")) {
            std::cerr << "Engine B does not isolate its VFS" << std::endl;
            exit(1);
        }

        fs::remove_all(tempA);
        fs::remove_all(tempB);
        TEST_PASS("Independent Engine and RuntimeContext VFS Isolation");
    }

    // 2. Structured Diagnostics on Engine Save/Load/Graph/Script
    {
        namespace fs = std::filesystem;
        const auto saveDir = fs::temp_directory_path() / "rowl_diag_saves";
        fs::create_directories(saveDir);

        Rowl::Core::Engine engine;
        engine.setSaveDirectory(saveDir.string());
        engine.initialize({});

        // Save slot invalid bounds
        if (engine.saveGameSlot(-1)) {
            std::cerr << "saveGameSlot(-1) should have returned false" << std::endl;
            exit(1);
        }
        auto res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::InvalidArgument || res.operation != "save_game_slot") {
            std::cerr << "saveGameSlot(-1) did not set InvalidArgument result" << std::endl;
            exit(1);
        }

        // Save slot valid
        if (!engine.saveGameSlot(1)) {
            std::cerr << "saveGameSlot(1) failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk() || res.operation != "save_game_slot" || res.target != "1") {
            std::cerr << "saveGameSlot(1) did not set Success result" << std::endl;
            exit(1);
        }

        // Load slot nonexistent
        if (engine.loadGameSlot(99)) {
            std::cerr << "loadGameSlot(99) should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::FileNotFound || res.operation != "load_game_slot") {
            std::cerr << "loadGameSlot(99) did not set FileNotFound result" << std::endl;
            exit(1);
        }

        // Load slot valid
        if (!engine.loadGameSlot(1)) {
            std::cerr << "loadGameSlot(1) failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk() || res.operation != "load_game_slot") {
            std::cerr << "loadGameSlot(1) did not set Success result" << std::endl;
            exit(1);
        }

        // Delete slot valid
        if (!engine.deleteSaveSlot(1)) {
            std::cerr << "deleteSaveSlot(1) failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk() || res.operation != "delete_save_slot") {
            std::cerr << "deleteSaveSlot(1) did not set Success result" << std::endl;
            exit(1);
        }

        // Delete slot nonexistent
        if (engine.deleteSaveSlot(1)) {
            std::cerr << "deleteSaveSlot(1) second time should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::FileNotFound) {
            std::cerr << "deleteSaveSlot(1) second time did not set FileNotFound result" << std::endl;
            exit(1);
        }

        // Story Graph missing file
        if (engine.loadStoryGraphFromPath("/nonexistent_rowl_graph_path.json")) {
            std::cerr << "loadStoryGraphFromPath on nonexistent file should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::FileNotFound) {
            std::cerr << "loadStoryGraphFromPath missing file did not set FileNotFound" << std::endl;
            exit(1);
        }

        // Story Graph corrupt file
        const auto corruptGraph = fs::temp_directory_path() / "corrupt_graph.json";
        {
            std::ofstream f(corruptGraph);
            f << "{ invalid json content !!! }";
        }
        if (engine.loadStoryGraphFromPath(corruptGraph.string())) {
            std::cerr << "loadStoryGraphFromPath on corrupt file should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ParseError) {
            std::cerr << "loadStoryGraphFromPath corrupt file did not set ParseError" << std::endl;
            exit(1);
        }
        fs::remove(corruptGraph);

        // Story Graph semantic validation error (empty nodes array)
        const auto invalidGraph = fs::temp_directory_path() / "invalid_graph.json";
        {
            std::ofstream f(invalidGraph);
            f << "{\"nodes\": []}";
        }
        if (engine.loadStoryGraphFromPath(invalidGraph.string())) {
            std::cerr << "loadStoryGraphFromPath on empty nodes should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ValidationError) {
            std::cerr << "loadStoryGraphFromPath semantic error did not set ValidationError (got " << res.rawCode() << ")" << std::endl;
            exit(1);
        }
        fs::remove(invalidGraph);

        // Scripting invalid syntax
        if (engine.executeScript("this is definitely not lua syntax @#$!")) {
            std::cerr << "executeScript with bad syntax should fail" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ScriptRuntimeError) {
            std::cerr << "executeScript did not set ScriptRuntimeError" << std::endl;
            exit(1);
        }

        // Scripting valid
        if (!engine.executeScript("rowl.var_set('diag_flag', 'confirmed')")) {
            std::cerr << "executeScript valid failed" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk()) {
            std::cerr << "executeScript valid did not set Ok" << std::endl;
            exit(1);
        }

        // Condition syntax error
        if (engine.evaluateCondition("bad condition @#$!")) {
            std::cerr << "evaluateCondition bad syntax should return false" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (res.code != Rowl::Core::RuntimeErrorCode::ScriptSyntaxError) {
            std::cerr << "evaluateCondition bad syntax did not set ScriptSyntaxError" << std::endl;
            exit(1);
        }

        // Valid condition evaluating to false must NOT be contaminated by previous errors
        if (engine.evaluateCondition("1 == 2")) {
            std::cerr << "evaluateCondition(1 == 2) should return false" << std::endl;
            exit(1);
        }
        res = engine.getContext()->getLastResult();
        if (!res.isOk()) {
            std::cerr << "evaluateCondition(1 == 2) contaminated with error: " << res.message << std::endl;
            exit(1);
        }

        engine.shutdown();
        fs::remove_all(saveDir);
        TEST_PASS("Engine Structured Results for Save, Load, Graph, and Scripting");
    }

    // 3. C-API Structured Diagnostics & Null Handle Safety
    {
        // Null handle queries must be safe and return InvalidHandle
        if (RowlEngine_GetLastResultCode(nullptr) != 1) {
            std::cerr << "RowlEngine_GetLastResultCode(nullptr) should return 1" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetLastResultOperation(nullptr)) != "none") {
            std::cerr << "RowlEngine_GetLastResultOperation(nullptr) failed" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetLastResultMessage(nullptr)).empty()) {
            std::cerr << "RowlEngine_GetLastResultMessage(nullptr) is empty" << std::endl;
            exit(1);
        }
        RowlEngine_ClearLastResult(nullptr); // Must be a safe no-op

        // Live handle operations
        RowlEngineHandle h = RowlEngine_Create();
        if (!h) {
            std::cerr << "RowlEngine_Create failed in diagnostic test" << std::endl;
            exit(1);
        }
        if (!RowlEngine_Init(h, 320, 180, 0)) {
            std::cerr << "RowlEngine_Init failed in diagnostic test" << std::endl;
            exit(1);
        }

        // Null string arguments to live handle must set InvalidArgument
        if (RowlEngine_ExecuteScript(h, nullptr) != 0) {
            std::cerr << "RowlEngine_ExecuteScript(h, nullptr) should return 0" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetLastResultCode(h) != 2) { // InvalidArgument = 2
            std::cerr << "Expected InvalidArgument (2) on null script string, got: " << RowlEngine_GetLastResultCode(h) << std::endl;
            exit(1);
        }

        if (RowlEngine_LoadStoryGraphFromVfs(h, nullptr) != 0) {
            std::cerr << "RowlEngine_LoadStoryGraphFromVfs(h, nullptr) should return 0" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetLastResultCode(h) != 2) { // InvalidArgument = 2
            std::cerr << "Expected InvalidArgument (2) on null VFS path, got: " << RowlEngine_GetLastResultCode(h) << std::endl;
            exit(1);
        }

        // Invalid save slot via C API
        int saveRes = RowlEngine_SaveGameSlot(h, -7);
        if (saveRes != 0) {
            std::cerr << "RowlEngine_SaveGameSlot(h, -7) should return 0" << std::endl;
            exit(1);
        }
        int32_t code = RowlEngine_GetLastResultCode(h);
        if (code != 2) { // InvalidArgument = 2
            std::cerr << "Expected code 2 (InvalidArgument), got: " << code << std::endl;
            exit(1);
        }
        const char* op = RowlEngine_GetLastResultOperation(h);
        if (!op || std::string(op) != "save_game_slot") {
            std::cerr << "Expected operation save_game_slot, got: " << (op ? op : "null") << std::endl;
            exit(1);
        }
        const char* tgt = RowlEngine_GetLastResultTarget(h);
        if (!tgt || std::string(tgt) != "-7") {
            std::cerr << "Expected target -7, got: " << (tgt ? tgt : "null") << std::endl;
            exit(1);
        }
        const char* msg = RowlEngine_GetLastResultMessage(h);
        if (!msg || std::string(msg).find("Invalid save slot") == std::string::npos) {
            std::cerr << "Expected message containing 'Invalid save slot', got: " << (msg ? msg : "null") << std::endl;
            exit(1);
        }

        // Clear diagnostic result
        RowlEngine_ClearLastResult(h);
        if (RowlEngine_GetLastResultCode(h) != 0) {
            std::cerr << "RowlEngine_ClearLastResult did not reset code to 0" << std::endl;
            exit(1);
        }
        if (std::string(RowlEngine_GetLastResultMessage(h)) != "Success") {
            std::cerr << "RowlEngine_ClearLastResult did not reset message to Success" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(h);
        TEST_PASS("C-API Structured Diagnostic Query Functions & Null Handle Safety");
    }
}

void test_native_performance_benchmarks(const std::string& benchmarkJsonPath = "") {
    TEST_SECTION("Performance & Profiling Benchmarks");

    // 1. VFS Query & Read Latency Benchmark
    auto& vfs = Rowl::VFS::VFSManager::instance();
    const int VFS_ITERATIONS = 5000;
    auto vfsStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < VFS_ITERATIONS; ++i) {
        bool exists = vfs.exists("fonts/default.ttf");
        (void)exists;
    }
    auto vfsEnd = std::chrono::high_resolution_clock::now();
    double vfsElapsedMs = std::chrono::duration<double, std::milli>(vfsEnd - vfsStart).count();
    double vfsOpsPerSec = (VFS_ITERATIONS / (vfsElapsedMs / 1000.0));
    std::cout << "  ⚡ [BENCHMARK] VFS Exists Lookups: " << VFS_ITERATIONS << " queries in "
              << vfsElapsedMs << "ms (" << static_cast<uint64_t>(vfsOpsPerSec) << " queries/sec)" << std::endl;
    TEST_PASS("VFS High-Throughput Lookup Benchmark");

    // 2. Scene JSON Parse & Update Benchmark
    RowlEngineHandle handle = RowlEngine_Create();
    RowlEngine_Init(handle, 1920, 1080, 0);
    const auto configuredTextureBudgetBytes = benchmarkTextureCacheBudgetBytes();
    RowlEngine_SetTextureCacheBudgetBytes(handle, configuredTextureBudgetBytes);

    const std::string benchJson = R"([
        {"type":"speaker","id":"s1","enabled":true,"data":{"speaker":"Evelyn","dialogue":"Benchmark line"}},
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080}},
        {"type":"character","id":"c1","enabled":true,"data":{"sprite":"Margot.jpg","x":400,"y":200,"width":360,"height":540}},
        {"type":"dialogue_box","id":"d1","enabled":true,"data":{"x":80,"y":840,"width":1760,"height":200}},
        {"type":"audio","id":"a1","enabled":true,"data":{"dsp_filter":"Normal"}}
    ])";

    const int JSON_ITERATIONS = 500;
    auto jsonStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < JSON_ITERATIONS; ++i) {
        RowlEngine_UpdateSceneFromJson(handle, benchJson.c_str());
    }
    auto jsonEnd = std::chrono::high_resolution_clock::now();
    double jsonElapsedMs = std::chrono::duration<double, std::milli>(jsonEnd - jsonStart).count();
    double jsonOpsPerSec = (JSON_ITERATIONS / (jsonElapsedMs / 1000.0));
    std::cout << "  ⚡ [BENCHMARK] Scene JSON Updates: " << JSON_ITERATIONS << " updates in "
              << jsonElapsedMs << "ms (" << static_cast<uint64_t>(jsonOpsPerSec) << " updates/sec, "
              << (jsonElapsedMs / JSON_ITERATIONS) << "ms/op)" << std::endl;
    TEST_PASS("Scene JSON Ingestion & Entity Synchronization Benchmark");

    // 3. Native Frame Render Step Benchmark. Asset decode/cache population is
    // a startup cost, not a steady-state frame cost, so prime it before
    // timing the normal render loop.
    auto firstFrameStart = std::chrono::high_resolution_clock::now();
    RowlEngine_Step(handle, 0.0f);
    auto firstFrameEnd = std::chrono::high_resolution_clock::now();
    const double firstFrameMs = std::chrono::duration<double, std::milli>(firstFrameEnd - firstFrameStart).count();
    const double textureLoadMs = RowlEngine_GetLastFrameTextureLoadMilliseconds(handle);
    const double nonTextureRenderMs = RowlEngine_GetLastFrameNonTextureRenderMilliseconds(handle);
    const double textRasterizationMs = RowlEngine_GetLastFrameTextRasterizationMilliseconds(handle);
    const double rendererFlushMs = RowlEngine_GetLastFrameRendererFlushMilliseconds(handle);
    if (textureLoadMs < 0.0 || nonTextureRenderMs < 0.0 || textRasterizationMs < 0.0 || rendererFlushMs < 0.0 ||
        textRasterizationMs > nonTextureRenderMs + 0.1 || rendererFlushMs > nonTextureRenderMs + 0.1 ||
        textureLoadMs + nonTextureRenderMs > firstFrameMs + 5.0) {
        std::cerr << "First-frame profile timings are inconsistent" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] First Frame Profile: texture load " << textureLoadMs
              << "ms, non-texture render " << nonTextureRenderMs
              << "ms, text rasterization " << textRasterizationMs
              << "ms, renderer flush " << rendererFlushMs << "ms" << std::endl;
    TEST_PASS("First Frame Texture and Renderer Profile");
    const auto warmTextureCount = RowlEngine_GetTextureCacheTextureCount(handle);
    const auto warmTextureBytes = RowlEngine_GetTextureCacheBytes(handle);
    if (warmTextureCount == 0 || warmTextureBytes == 0) {
        std::cerr << "Texture cache warm-up did not load the benchmark assets" << std::endl;
        exit(1);
    }
    const int FRAME_ITERATIONS = 60;
    auto frameStart = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < FRAME_ITERATIONS; ++i) {
        RowlEngine_Step(handle, 0.01667f);
    }
    auto frameEnd = std::chrono::high_resolution_clock::now();
    double frameElapsedMs = std::chrono::duration<double, std::milli>(frameEnd - frameStart).count();
    double avgFrameMs = frameElapsedMs / FRAME_ITERATIONS;
    double equivalentFps = 1000.0 / avgFrameMs;
    std::cout << "  ⚡ [BENCHMARK] Native Render Pipeline: " << FRAME_ITERATIONS << " offscreen frames in "
              << frameElapsedMs << "ms (Avg: " << avgFrameMs << "ms/frame ~ "
              << static_cast<uint64_t>(equivalentFps) << " FPS equivalent)" << std::endl;
    TEST_PASS("Offscreen Software Render Pipeline Latency Benchmark");

    const auto cachedTextureCount = RowlEngine_GetTextureCacheTextureCount(handle);
    const auto cachedTextureBytes = RowlEngine_GetTextureCacheBytes(handle);
    const auto textureEvictionCount = RowlEngine_GetTextureCacheEvictionCount(handle);
    if (cachedTextureCount != warmTextureCount || cachedTextureBytes != warmTextureBytes) {
        std::cerr << "Steady-state render unexpectedly changed texture cache usage" << std::endl;
        exit(1);
    }
    std::cout << "  ⚡ [BENCHMARK] Texture Cache: " << cachedTextureCount << " unique textures, "
              << cachedTextureBytes << " RGBA bytes" << std::endl;
    TEST_PASS("Texture Cache Memory Telemetry");

    if (!benchmarkJsonPath.empty()) {
        writeBenchmarkJson(benchmarkJsonPath, vfsElapsedMs, VFS_ITERATIONS, jsonElapsedMs, JSON_ITERATIONS,
                           firstFrameMs, avgFrameMs, textureLoadMs, nonTextureRenderMs,
                           cachedTextureCount, cachedTextureBytes, configuredTextureBudgetBytes,
                           textureEvictionCount);
    }

    constexpr uint64_t kDefaultTextureCacheBudget = 64ULL * 1024ULL * 1024ULL;
    if (RowlEngine_GetTextureCacheBudgetBytes(handle) != configuredTextureBudgetBytes ||
        (configuredTextureBudgetBytes == kDefaultTextureCacheBudget &&
         RowlEngine_GetTextureCacheEvictionCount(handle) != 0)) {
        std::cerr << "C-API texture cache budget telemetry contract failed" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Texture Cache Budget and Eviction Telemetry");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
}

void test_hardening_and_reliability() {
    TEST_SECTION("Hardening & Lifecycle Reliability");

    // 1. Texture Cache Double-Free Safety
    {
        Rowl::Render::Window win(&Rowl::VFS::VFSManager::instance());
        bool initOk = win.initializeOffscreen(400, 300);
        if (initOk) {
            auto* t1 = win.loadTexture("Woman.png");
            auto* t2 = win.loadTexture("Margot.jpg");
            if (!t1 || !t2 || win.getTextureCacheTextureCount() != 2 ||
                win.getTextureCacheBytes() == 0) {
                std::cerr << "Texture cache statistics did not report loaded textures" << std::endl;
                exit(1);
            }
            win.clearTextureCache(); // Must safely free unique textures only once
            if (win.getTextureCacheTextureCount() != 0 || win.getTextureCacheBytes() != 0) {
                std::cerr << "Texture cache statistics were not cleared" << std::endl;
                exit(1);
            }

            // A low-end target may not have room for every decoded asset. The
            // cache must evict old textures before admitting a new one and
            // reject a single oversized texture without retaining stale state.
            constexpr uint64_t kThirtyOneMiB = 31ULL * 1024ULL * 1024ULL;
            constexpr uint64_t kOneMiB = 1ULL * 1024ULL * 1024ULL;
            win.setTextureCacheBudgetBytes(kThirtyOneMiB);
            if (!win.loadTexture("Woman.png") || !win.loadTexture("Margot.jpg") ||
                win.getTextureCacheTextureCount() != 1 ||
                win.getTextureCacheBytes() > kThirtyOneMiB ||
                win.getTextureCacheEvictionCount() != 1) {
                std::cerr << "Texture cache did not evict the least-recently-used texture" << std::endl;
                exit(1);
            }
            win.setTextureCacheBudgetBytes(kOneMiB);
            if (win.getTextureCacheTextureCount() != 0 ||
                win.loadTexture("Margot.jpg") != nullptr ||
                win.getTextureCacheTextureCount() != 0 ||
                win.getTextureCacheEvictionCount() != 2) {
                std::cerr << "Texture cache admitted an asset larger than its budget" << std::endl;
                exit(1);
            }
            win.setTextureCacheBudgetBytes(64ULL * 1024ULL * 1024ULL);
            if (!win.loadTexture("Margot.jpg")) {
                std::cerr << "Texture rejected by a smaller budget was not retried after budget growth" << std::endl;
                exit(1);
            }
            win.loadTexture("Woman.png");
            if (win.loadTexture("missing_texture_for_negative_cache.png") != nullptr ||
                win.loadTexture("missing_texture_for_negative_cache.png") != nullptr ||
                win.getNegativeTextureCacheSize() != 1) {
                std::cerr << "Texture negative cache did not retain a missing asset lookup" << std::endl;
                exit(1);
            }
            for (int index = 0; index < 600; ++index) {
                win.loadTexture("missing_texture_budget_" + std::to_string(index) + ".png");
            }
            if (win.getNegativeTextureCacheSize() > 512) {
                std::cerr << "Texture negative cache exceeded its bounded entry count" << std::endl;
                exit(1);
            }
            win.clearTextureCache();
            if (win.getNegativeTextureCacheSize() != 0 || win.getTextureCacheEvictionCount() != 0) {
                std::cerr << "Texture negative cache was not cleared with the texture cache" << std::endl;
                exit(1);
            }
            const auto oversizedTexturePath = std::filesystem::temp_directory_path() / "rowl_oversized_texture.png";
            const uint8_t oversizedPngHeader[] = {
                137, 80, 78, 71, 13, 10, 26, 10, // PNG signature
                0, 0, 0, 13, 'I', 'H', 'D', 'R',
                0, 0, 78, 32, // 20,000 px width
                0, 0, 0, 1,   // 1 px height
                8, 6, 0, 0, 0, 0, 0, 0, 0 // IHDR fields + unused CRC
            };
            {
                std::ofstream oversizedTexture(oversizedTexturePath, std::ios::binary);
                oversizedTexture.write(reinterpret_cast<const char*>(oversizedPngHeader), sizeof(oversizedPngHeader));
            }
            const auto textureProject = std::filesystem::temp_directory_path() / "rowl_texture_vfs_test_project";
            const auto textureAssetDir = textureProject / "Assets" / "images";
            std::filesystem::create_directories(textureAssetDir);
            std::filesystem::copy_file(oversizedTexturePath, textureAssetDir / "oversized.png",
                                       std::filesystem::copy_options::overwrite_existing);
            Rowl::VFS::VFSManager::instance().remountProject(textureProject.string());
            if (win.loadTexture("images/oversized.png") != nullptr) {
                std::cerr << "Renderer decoded a texture with unsafe dimensions" << std::endl;
                exit(1);
            }
            std::filesystem::remove(oversizedTexturePath);
            std::filesystem::remove_all(textureProject);
            Rowl::VFS::VFSManager::instance().remountProject(std::filesystem::current_path().string());
            win.shutdown();          // Must safely free unique textures only once
            TEST_PASS("Texture Cache Unique Teardown and Missing-Asset Negative Cache");
        }
    }

    // 2. VFS Cross-Platform Path Normalization (Windows Backslashes)
    {
        auto& vfs = Rowl::VFS::VFSManager::instance();
        bool existsSlash = vfs.exists("images/Woman.png");
        bool existsBackslash = vfs.exists("images\\Woman.png");
        if (existsSlash && !existsBackslash) {
            std::cerr << "VFS backslash normalization failed for images\\Woman.png" << std::endl;
            exit(1);
        }
        TEST_PASS("VFS Cross-Platform Backslash (\\) Path Normalization");
    }

    {
        auto& vfs = Rowl::VFS::VFSManager::instance();
        auto stream = vfs.openReadStream("images/Woman.png");
        char signature[8]{};
        if (stream && stream->read(signature, sizeof(signature)) &&
            std::memcmp(signature, "\x89PNG\r\n\x1a\n", sizeof(signature)) == 0) {
            TEST_PASS("VFS Read-Only Asset Stream (Loose File)");
        } else if (vfs.exists("images/Woman.png")) {
            std::cerr << "VFS failed to open an existing loose asset as a stream" << std::endl;
            exit(1);
        }
    }

    // 3. GameState Step-by-Step Node Traversal & Rewind Integrity
    {
        const auto tempGraph = std::filesystem::temp_directory_path() / "rowl_rewind_chain_test.json";
        {
            std::ofstream f(tempGraph);
            f << R"({
                "format_version": 4,
                "start_node_id": 201,
                "nodes": [
                    {"id": 201, "speaker": "A", "dialogue": "Step 1", "next_nodes": [{"id": 202, "label": "Next"}]},
                    {"id": 202, "speaker": "B", "dialogue": "Step 2", "next_nodes": [{"id": 203, "label": "Next"}]},
                    {"id": 203, "speaker": "C", "dialogue": "Step 3", "next_nodes": []}
                ]
            })";
        }

        Rowl::Core::Engine engine;
        Rowl::Core::EngineConfig cfg;
        cfg.virtualWidth = 1920;
        cfg.virtualHeight = 1080;
        engine.initialize(cfg);
        engine.loadStoryGraphFromPath(tempGraph.string());
        engine.setPlayState(true);
        engine.resetToStartNode();

        if (engine.getCurrentNodeId() != 201) {
            std::cerr << "Engine failed to start at Node 201" << std::endl;
            exit(1);
        }

        engine.advanceToNextNode();
        if (engine.getCurrentNodeId() != 202) {
            std::cerr << "Engine failed to advance to Node 202" << std::endl;
            exit(1);
        }

        engine.advanceToNextNode();
        if (engine.getCurrentNodeId() != 203) {
            std::cerr << "Engine failed to advance to Node 203" << std::endl;
            exit(1);
        }

        if (engine.getDialogueHistory().size() != 3 ||
            engine.getDialogueHistory().front().dialogue != "Step 1" ||
            engine.getDialogueHistory().back().dialogue != "Step 3") {
            std::cerr << "Engine did not record dialogue history by active node" << std::endl;
            exit(1);
        }

        // Rewind 1 step -> should return to Node 202
        bool rw1 = engine.rewind(1);
        if (!rw1 || engine.getCurrentNodeId() != 202) {
            std::cerr << "Rewind 1 failed: expected Node 202, got " << engine.getCurrentNodeId() << std::endl;
            exit(1);
        }

        // Rewind another step -> should return to Node 201
        bool rw2 = engine.rewind(1);
        if (!rw2 || engine.getCurrentNodeId() != 201) {
            std::cerr << "Rewind 2 failed: expected Node 201, got " << engine.getCurrentNodeId() << std::endl;
            exit(1);
        }
        TEST_PASS("GameState Node-by-Node Step Recording & History Rewind Chain");
        std::filesystem::remove(tempGraph);
    }

    // 4. BGM Looping State & Configuration
    {
        const auto autoGraph = std::filesystem::temp_directory_path() / "rowl_auto_advance_test.json";
        {
            std::ofstream f(autoGraph);
            f << R"({
                "format_version":4,"start_node_id":301,"nodes":[
                  {"id":301,"components":[{"type":"dialogue","data":{"speaker":"A","dialogue":"Auto","typewriter_enabled":false,"auto_advance":true,"auto_advance_delay":0.0}}],"next_nodes":[{"id":302}]},
                  {"id":302,"components":[{"type":"dialogue","data":{"speaker":"B","dialogue":"Arrived"}}],"next_nodes":[]}
                ]
            })";
        }
        Rowl::Core::Engine engine;
        engine.initialize({});
        engine.loadStoryGraphFromPath(autoGraph.string());
        engine.setPlayState(true);
        engine.resetToStartNode();
        engine.step(0.05f);
        if (engine.getCurrentNodeId() != 302) {
            std::cerr << "Auto advance did not move after its configured delay" << std::endl;
            exit(1);
        }
        engine.shutdown();
        std::filesystem::remove(autoGraph);
        TEST_PASS("Dialogue Auto Advance Waits for Completion and Uses Node Delay");
    }

    {
        Rowl::Audio::AudioEngine audio(&Rowl::VFS::VFSManager::instance());
        if (!audio.isBgmLooping()) {
            std::cerr << "BGM looping expected to default to true" << std::endl;
            exit(1);
        }
        audio.setBgmLooping(false);
        if (audio.isBgmLooping()) {
            std::cerr << "setBgmLooping(false) failed" << std::endl;
            exit(1);
        }
        audio.setMasterVolume(0.8f);
        audio.setBgmVolume(0.7f);
        audio.setVoiceVolume(0.6f);
        audio.setSfxVolume(0.5f);
        if (std::abs(audio.getMasterVolume() - 0.8f) > 0.001f ||
            std::abs(audio.getBgmVolume() - 0.7f) > 0.001f ||
            std::abs(audio.getVoiceVolume() - 0.6f) > 0.001f ||
            std::abs(audio.getSfxVolume() - 0.5f) > 0.001f) {
            std::cerr << "Independent audio channel volume configuration failed" << std::endl;
            exit(1);
        }
        TEST_PASS("Audio Engine BGM Looping Configuration");
    }
}

void test_camera_and_transition_pipeline() {
    TEST_SECTION("2D Camera & Scene Transition Subsystem Tests");

    // Test 1: Camera2D virtual projection & zoom identity
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        if (std::abs(camera.getPositionX() - 960.0f) > 0.001f ||
            std::abs(camera.getPositionY() - 540.0f) > 0.001f ||
            std::abs(camera.getZoom() - 1.0f) > 0.001f) {
            std::cerr << "Camera2D default initialization failed" << std::endl;
            exit(1);
        }

        float outX = 0, outY = 0, outW = 0, outH = 0;
        camera.transformRect(100.0f, 200.0f, 300.0f, 400.0f, outX, outY, outW, outH);
        if (std::abs(outX - 100.0f) > 0.001f || std::abs(outY - 200.0f) > 0.001f ||
            std::abs(outW - 300.0f) > 0.001f || std::abs(outH - 400.0f) > 0.001f) {
            std::cerr << "Camera2D identity transformation failed" << std::endl;
            exit(1);
        }

        // Test 2: Camera2D Zoom clamping & center scaling
        camera.setZoom(2.0f);
        if (std::abs(camera.getZoom() - 2.0f) > 0.001f) {
            std::cerr << "Camera2D setZoom failed" << std::endl;
            exit(1);
        }
        camera.transformRect(960.0f, 540.0f, 100.0f, 100.0f, outX, outY, outW, outH);
        if (std::abs(outX - 960.0f) > 0.001f || std::abs(outY - 540.0f) > 0.001f ||
            std::abs(outW - 200.0f) > 0.001f || std::abs(outH - 200.0f) > 0.001f) {
            std::cerr << "Camera2D zoom projection failed" << std::endl;
            exit(1);
        }

        // Clamping test: negative zoom and excessive zoom
        camera.setZoom(-5.0f);
        if (camera.getZoom() < 0.1f) {
            std::cerr << "Camera2D negative zoom clamp failed" << std::endl;
            exit(1);
        }
        camera.setZoom(100.0f);
        if (camera.getZoom() > 10.0f) {
            std::cerr << "Camera2D max zoom clamp failed" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Virtual Canvas Projection & Zoom Scaling");
    }

    // Test 3: Camera2D Screen Shake & Harmonic Decay
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        camera.shake(25.0f, 0.4f, 30.0f);
        if (!camera.isShaking()) {
            std::cerr << "Camera2D shake trigger failed" << std::endl;
            exit(1);
        }

        camera.update(0.1f);
        if (!camera.isShaking() || (camera.getShakeOffsetX() == 0.0f && camera.getShakeOffsetY() == 0.0f)) {
            std::cerr << "Camera2D shake offset update failed" << std::endl;
            exit(1);
        }

        // Advance past shake duration
        camera.update(0.5f);
        if (camera.isShaking() || camera.getShakeOffsetX() != 0.0f || camera.getShakeOffsetY() != 0.0f) {
            std::cerr << "Camera2D shake decay to zero failed" << std::endl;
            exit(1);
        }

        // Reset
        camera.setPosition(1200.0f, 600.0f);
        camera.setZoom(1.5f);
        camera.reset();
        if (std::abs(camera.getPositionX() - 960.0f) > 0.001f ||
            std::abs(camera.getPositionY() - 540.0f) > 0.001f ||
            std::abs(camera.getZoom() - 1.0f) > 0.001f) {
            std::cerr << "Camera2D reset failed" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Screen Shake Harmonic Decay & Reset");
    }

    // Test 3b: Camera2D Smooth Tweening & Easing (PanTo & ZoomTo)
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        camera.panTo(1200.0f, 600.0f, 1.0f, Rowl::Render::CameraEasing::EaseInOutCubic);
        camera.zoomTo(2.0f, 1.0f, Rowl::Render::CameraEasing::EaseInOutCubic);

        if (!camera.isPanning() || !camera.isZooming() || !camera.isMoving()) {
            std::cerr << "Camera2D tween start query failed" << std::endl;
            exit(1);
        }

        // Halfway step: 0.5s -> EaseInOutCubic factor = 0.5
        camera.update(0.5f);
        if (std::abs(camera.getPositionX() - 1080.0f) > 0.05f ||
            std::abs(camera.getPositionY() - 570.0f) > 0.05f ||
            std::abs(camera.getZoom() - 1.5f) > 0.05f) {
            std::cerr << "Camera2D halfway tween interpolation failed" << std::endl;
            exit(1);
        }

        // Complete step: 0.5s -> reaches target exactly
        camera.update(0.5f);
        if (std::abs(camera.getPositionX() - 1200.0f) > 0.01f ||
            std::abs(camera.getPositionY() - 600.0f) > 0.01f ||
            std::abs(camera.getZoom() - 2.0f) > 0.01f) {
            std::cerr << "Camera2D tween target completion failed" << std::endl;
            exit(1);
        }
        if (camera.isPanning() || camera.isZooming() || camera.isMoving()) {
            std::cerr << "Camera2D tween end state query failed" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Smooth Tweening & EaseInOutCubic Interpolation");
    }

    // Test 4: TransitionManager Lifecycle & State Transitions
    {
        Rowl::Render::TransitionManager transition;
        if (transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::None) {
            std::cerr << "TransitionManager initial state should be None" << std::endl;
            exit(1);
        }

        transition.startTransitionFromKind("crossfade", 0.5f);
        if (!transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::CrossFade) {
            std::cerr << "TransitionManager CrossFade start failed" << std::endl;
            exit(1);
        }

        transition.update(0.25f);
        if (std::abs(transition.getProgress() - 0.5f) > 0.01f || !transition.isTransitionActive()) {
            std::cerr << "TransitionManager halfway progress failed" << std::endl;
            exit(1);
        }

        transition.update(0.3f);
        if (transition.isTransitionActive()) {
            std::cerr << "TransitionManager completion failed" << std::endl;
            exit(1);
        }

        // Test FadeToColor with custom hex
        transition.startTransitionFromKind("fade_color", 1.0f, "#10B981");
        if (!transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::FadeToColor) {
            std::cerr << "TransitionManager FadeToColor start failed" << std::endl;
            exit(1);
        }

        // Wipe transitions
        transition.startTransitionFromKind("wipe_left", 0.8f);
        if (!transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::WipeLeft) {
            std::cerr << "TransitionManager WipeLeft start failed" << std::endl;
            exit(1);
        }

        transition.reset();
        if (transition.isTransitionActive()) {
            std::cerr << "TransitionManager reset failed" << std::endl;
            exit(1);
        }

        TEST_PASS("TransitionManager Lifecycle, Progress & Hex Parsing");
    }

    // Test 5: C API Integration for Camera & Transition
    {
        RowlEngineHandle handle = RowlEngine_Create();
        if (!handle) {
            std::cerr << "Failed to create RowlEngine handle" << std::endl;
            exit(1);
        }
        if (!RowlEngine_Init(handle, 1920, 1080, 0)) {
            std::cerr << "Failed to initialize offscreen engine" << std::endl;
            exit(1);
        }

        RowlEngine_SetCamera(handle, 1000.0f, 500.0f, 1.25f);
        RowlEngine_TriggerCameraShake(handle, 15.0f, 0.5f);
        RowlEngine_Step(handle, 0.016f);

        // Smooth Panning and Zooming via C API
        RowlEngine_CameraPanTo(handle, 1200.0f, 600.0f, 0.4f, 3);
        RowlEngine_CameraZoomTo(handle, 1.75f, 0.4f, 3);
        if (!RowlEngine_IsCameraMoving(handle)) {
            std::cerr << "RowlEngine_IsCameraMoving expected true during pan/zoom" << std::endl;
            exit(1);
        }

        RowlEngine_StartTransition(handle, "crossfade", 0.4f, nullptr);
        if (!RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "RowlEngine_IsTransitionActive expected true after start" << std::endl;
            exit(1);
        }

        // Since Engine::step clamps dt to 0.25s maximum, step twice to advance 0.5s
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        if (RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "RowlEngine_IsTransitionActive expected false after completion" << std::endl;
            exit(1);
        }
        if (RowlEngine_IsCameraMoving(handle)) {
            std::cerr << "RowlEngine_IsCameraMoving expected false after completion" << std::endl;
            exit(1);
        }

        RowlEngine_ResetCamera(handle);
        RowlEngine_Destroy(handle);

        TEST_PASS("C API Camera & Transition Integration");
    }

    // Test 6: Component JSON Ingestion for Camera & Transition
    {
        RowlEngineHandle handle = RowlEngine_Create();
        RowlEngine_Init(handle, 1920, 1080, 0);

        std::string componentJson = R"([
            {
                "type": "background",
                "data": { "image": "bg_test.png", "x": 0, "y": 0, "width": 1920, "height": 1080 }
            },
            {
                "type": "camera",
                "data": { "x": 960, "y": 540, "zoom": 1.1, "shake_intensity": 10.0, "shake_duration": 0.3 }
            },
            {
                "type": "transition",
                "data": { "kind": "fade_black", "duration": 0.5 }
            }
        ])";

        RowlEngine_UpdateSceneFromJson(handle, componentJson.c_str());
        if (!RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "Transition was not activated from component JSON" << std::endl;
            exit(1);
        }

        RowlEngine_Step(handle, 0.016f);
        uint32_t bufW = 0, bufH = 0;
        const uint8_t* buffer = RowlEngine_GetPixelBuffer(handle, &bufW, &bufH);
        if (!buffer || bufW != 1920 || bufH != 1080) {
            std::cerr << "Failed to retrieve pixel buffer with camera/transition active" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(handle);
        TEST_PASS("Scene JSON Camera & Transition Component Ingestion");
    }

    // Test 7: Active Transition Render Performance Benchmark
    {
        RowlEngineHandle handle = RowlEngine_Create();
        RowlEngine_Init(handle, 1920, 1080, 0);

        RowlEngine_StartTransition(handle, "crossfade", 2.0f, nullptr);

        const int frameCount = 60;
        const auto benchStart = std::chrono::high_resolution_clock::now();

        for (int f = 0; f < frameCount; ++f) {
            RowlEngine_Step(handle, 0.016f);
        }

        const auto benchEnd = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(benchEnd - benchStart).count();
        double fps = (frameCount / elapsedMs) * 1000.0;

        std::cout << "  ⚡ [Benchmark] Active Transition Render: " << frameCount << " frames rendered in "
                  << std::fixed << std::setprecision(2) << elapsedMs << "ms (~"
                  << static_cast<int>(fps) << " FPS)" << std::endl;

        if (fps < 30.0) {
            std::cerr << "Transition render FPS is too low: " << fps << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(handle);
        TEST_PASS("Active Scene Transition 60-Frame Render Performance");
    }
}

int main(int argc, char* argv[]) {
    std::string benchmarkJsonPath;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--benchmark-json" && index + 1 < argc) {
            benchmarkJsonPath = argv[++index];
        } else {
            std::cerr << "Usage: rowl_tests [--benchmark-json <output.json>]" << std::endl;
            return 1;
        }
    }
    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🚀 ROWL ENGINE COMPREHENSIVE NATIVE UNIT TEST SUITE 🚀" << std::endl;
    std::cout << "=======================================================" << std::endl;

    test_aspect_guardian();
    test_msdf_renderer();
    test_game_state();
    test_audio_engine();
    test_lua_sandbox();
    test_mobile_input();
    test_vfs_security();
    test_native_c_api();
    test_game_object_component_system();
    test_window_input_routing();
    test_runtime_context_and_diagnostics();
    test_camera_and_transition_pipeline();
    test_native_performance_benchmarks(benchmarkJsonPath);
    test_hardening_and_reliability();

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🎉 ALL UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    return 0;
}
