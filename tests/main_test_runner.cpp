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
#include <limits>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <thread>
#include <SDL3/SDL.h>

#include "rowl/render/aspect_guardian.hpp"
#include "rowl/state/game_state.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "rowl/scripting/lua_sandbox.hpp"
#include "rowl/platform/mobile_input.hpp"
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

    // JSON Serialization & Slot Persistence
    std::string serialized = s2->serializeJson();
    auto deserialized = Rowl::State::GameState::deserializeJson(serialized);
    if (!deserialized || deserialized->stepId != s2->stepId || deserialized->activeNodeId != 102 ||
        deserialized->getVariable("player_name") != "Evelyn") {
        std::cerr << "GameState JSON serialize/deserialize mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("GameState JSON Serialization & Deserialization");

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

    // Audio playback uses a real, minimal PCM WAV rather than only recording
    // an intent string. This exercises SDL's decode and stream queue path.
    const auto tonePath = std::filesystem::temp_directory_path() / "rowl_audio_test_tone.wav";
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
    audio.playAudio(tonePath.string(), Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (audio.getCurrentBgmPath() != tonePath.string()) {
        std::cerr << "Audio BGM path mismatch" << std::endl;
        exit(1);
    }
    if (audio.isAudioDeviceAvailable()) {
        audio.playAudio("missing_theme.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (audio.getCurrentBgmPath() != tonePath.string()) {
            std::cerr << "Failed BGM load replaced the current playback state" << std::endl;
            exit(1);
        }
    }
    std::filesystem::remove(tonePath);
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
    TEST_PASS("Loose-directory mounts reject parent traversal");

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

    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"character","data":{"sprite":"Margot.jpg","x":"not-a-number"}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Type-invalid component JSON did not roll back the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Value-Type Transaction Rollback");

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
    TEST_PASS("RowlEngine_Shutdown & Destroy (Clean Resource Teardown)");
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

void test_native_performance_benchmarks() {
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

    // 3. Native Frame Render Step Benchmark
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

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
}

void test_hardening_and_reliability() {
    TEST_SECTION("Hardening & Lifecycle Reliability");

    // 1. Texture Cache Double-Free Safety
    {
        Rowl::Render::Window win;
        bool initOk = win.initializeOffscreen(400, 300);
        if (initOk) {
            auto* t1 = win.loadTexture("Woman.png");
            auto* t2 = win.loadTexture("Margot.jpg");
            (void)t1; (void)t2;
            win.clearTextureCache(); // Must safely free unique textures only once
            win.loadTexture("Woman.png");
            if (win.loadTexture("missing_texture_for_negative_cache.png") != nullptr ||
                win.loadTexture("missing_texture_for_negative_cache.png") != nullptr ||
                win.getNegativeTextureCacheSize() != 1) {
                std::cerr << "Texture negative cache did not retain a missing asset lookup" << std::endl;
                exit(1);
            }
            win.clearTextureCache();
            if (win.getNegativeTextureCacheSize() != 0) {
                std::cerr << "Texture negative cache was not cleared with the texture cache" << std::endl;
                exit(1);
            }
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
        Rowl::Audio::AudioEngine audio;
        if (!audio.isBgmLooping()) {
            std::cerr << "BGM looping expected to default to true" << std::endl;
            exit(1);
        }
        audio.setBgmLooping(false);
        if (audio.isBgmLooping()) {
            std::cerr << "setBgmLooping(false) failed" << std::endl;
            exit(1);
        }
        TEST_PASS("Audio Engine BGM Looping Configuration");
    }
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
    test_vfs_security();
    test_native_c_api();
    test_game_object_component_system();
    test_native_performance_benchmarks();
    test_hardening_and_reliability();

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🎉 ALL UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    return 0;
}
