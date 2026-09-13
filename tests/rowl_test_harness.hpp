/**
 * rowl_test_harness.hpp
 *
 * Shared prelude for the per-subsystem native test translation units.
 * Split from main_test_runner.cpp; behavior unchanged.
 */

#pragma once

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
#include "rowl/platform/platform_host.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"
#include "rowl/vfs/vfs.hpp"
#include "rowl/vfs/rowlpkg_reader.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/scene/scene.hpp"
#include "rowl/scene/game_object.hpp"
#include "rowl/scene/transform_component.hpp"
#include "rowl/scene/sprite_component.hpp"
#include "rowl/c_api.h"

#define TEST_PASS(name) std::cout << "  ✅ [PASS] " << name << std::endl
#define TEST_SECTION(title) std::cout << "\n📌 === " << title << " ===" << std::endl

inline std::vector<uint8_t> decodeBase64(const std::string_view input) {
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

inline std::string environmentValue(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return value && *value ? value : fallback;
}

// Measured by test_camera_and_transition_pipeline(); consumed by the benchmark
// JSON writer. Negative means unmeasured.
extern double g_transitionFps;

void test_logger_timestamp();
void test_aspect_guardian();
void test_msdf_renderer();
void test_game_state();
void test_audio_engine();
void test_audio_device_recovery();
void test_lua_sandbox();
void test_mobile_input();
void test_platform_host();
void test_vfs_security();
void test_native_c_api();
void test_single_engine_image();
void test_demo_first_light();
void test_demo_second_signal();

// Defined in engine/src/c_api.cpp. Test-only bridge resolving the explicit
// Engine behind a C-API handle; production code must never use it.
namespace Rowl::Core {
Engine* testEngineFromHandle(RowlEngineHandle handle);
}
void test_game_object_component_system();
void test_window_input_routing();
void test_runtime_context_and_diagnostics();
void test_story_graph_parser();
void test_native_performance_benchmarks(const std::string& benchmarkJsonPath);
void test_golden_project_benchmarks(const std::string& goldenJsonPath);
void test_hardening_and_reliability();
void test_camera_and_transition_pipeline();
