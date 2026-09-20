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
#include <sstream>  // A3-tur6: frameHashFail hex-formatı harness-helper'a taşındı
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
#include "rowl/platform/user_data_directories.hpp"
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

// A3-tur6 (hygiene): kilit-testlerinin fail-helper ikizlenmesi paylaşıma
// alındı (cerr + exit(1) tek-noktada). Fixture'lar farklı (hash statik,
// reuse pencereli) o yüzden yalnız helper paylaşılır; her test kendi
// thin-wrapper'ını ya da doğrudan bunu kullanır.
[[noreturn]] inline void rowlLockFail(const char* lockName, const std::string& message) {
    std::cerr << lockName << " lock failure: " << message << std::endl;
    std::exit(1);
}

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
void test_logger_concurrency();
void test_aspect_guardian();
void test_msdf_renderer();
void test_game_state();
void test_audio_engine();
void test_audio_device_recovery();
void test_audio_streaming();
void test_audio_seek_forward_no_reset();
void test_audio_mixer();
void test_audio_lock_sanitize_nan();
void test_audio_lock_underwater_clamp();
void test_audio_lock_bgm_miss_guard();
void test_audio_lock_queue_fail_atomic();
void test_audio_lock_bgm_transactional();
void test_audio_lock_bgm_cap_fail_closed();
void test_audio_lock_outage_bgm_intent_data_sync();
void test_audio_lock_outage_pending_no_stale_replay();
void test_audio_lock_outage_pending_fail_preserved();
void test_audio_lock_dead_handle_spectrum_zero_fill();
void test_audio_lock_dead_handle_guards_watcher();
void test_audio_lock_offscreen_global_pump();
void test_audio_lock_scene_restore_audio_snapshot();
void test_audio_lock_mixer_persistence();
void test_audio_lock_save_format_v4_mixer();
void test_audio_lock_voice_blip_concurrent_counts();
void test_lua_sandbox();
void test_mobile_input();
void test_platform_host();
void test_c_api_contract();
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
void test_pixel_pitch();
void test_window_input_routing();
void test_frame_hash_lock();
void test_frame_reuse_lock();  // A3-tur5: identical-frame reuse D2 kilidi
void test_cache_hygiene();  // A3-tur6: negatif-onbellek hijyen kilidi
void test_font_hardening();  // A3-tur7: font-yukleme fail-closed kilidi
void test_handle_hygiene();  // A4-tur1: handle-once siralama kilidi
void test_lifecycle_init_guards();  // B1a: fail-loud init kilitleri
void test_lifecycle_shutdown_sweep();  // D2: shutdown-supurme + re-init kilitleri
void test_lifecycle_thread_lease();  // D3: thread/lease + dispatch-pin kilitleri
void test_save_lock();  // D4: save/load/rewind kilitleri (#43-#53, #70)
void test_numeric_guards();  // B6: sayisal-uc/native guard dilimi bilerek-boz kilitleri
void test_lua_hardening();  // B7: lua-sandbox sertlestirme dilimi bilerek-boz kilitleri
void test_runtime_context_and_diagnostics();
void test_crash_log();
void test_story_graph_parser();
void test_graph_vnext();
void test_localization();
void test_markup_parser();
void test_text_shaping();
void test_utf8_decoder();  // A3-tur4: paylasimli strict UTF-8 decoder kilidi
void test_accessibility();
void test_character_layers();
void test_character_layer_benchmarks();
void test_prefetch_chapters();
void test_converter_provenance();
void test_native_performance_benchmarks(const std::string& benchmarkJsonPath);
void test_golden_project_benchmarks(const std::string& goldenJsonPath);
void test_hardening_and_reliability();
void test_rc_soak_and_data_safety();
void test_save_slot_and_package_fuzz();
void test_camera_and_transition_pipeline();
