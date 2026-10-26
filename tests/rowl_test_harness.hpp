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
#include <ctime>
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

// D-K2: pins the process time zone for the scope (TZ + tzset) and restores the
// previous value, so local-time displays are deterministic on any machine.
struct ScopedTimeZone {
    explicit ScopedTimeZone(const char* tz) {
        const char* old = std::getenv("TZ");
        hadOld = old != nullptr;
        if (old) oldValue = old;
        apply(tz);
    }
    ~ScopedTimeZone() { apply(hadOld ? oldValue.c_str() : nullptr); }
    ScopedTimeZone(const ScopedTimeZone&) = delete;
    ScopedTimeZone& operator=(const ScopedTimeZone&) = delete;

private:
    static void apply(const char* tz) {
#ifdef _WIN32
        _putenv_s("TZ", tz ? tz : "");
        _tzset();
#else
        if (tz) ::setenv("TZ", tz, 1);
        else ::unsetenv("TZ");
        ::tzset();
#endif
    }
    bool hadOld = false;
    std::string oldValue;
};

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
void test_gpu_truetype_text();
void test_pause_overlay_visible_window();  // C02 BUG-2: gorunur pencerede pause overlay
void test_game_state();
void test_audio_engine();
void test_audio_device_recovery();
void test_audio_error_channel();
void test_audio_streaming();
void test_audio_seek_forward_no_reset();
void test_audio_mixer();
void test_audio_ui_gain();
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
void test_audio_lock_spectrum_foreign_thread();
void test_audio_lock_spectrum_null_guard_lock();
void test_audio_lock_spectrum_nonpositive_guard_watcher();
void test_audio_lock_offscreen_global_pump();
void test_audio_lock_scene_restore_no_restart_on_clean_throw();
void test_audio_lock_scene_restore_stopped_bgm_stays_stopped();
void test_audio_lock_mixer_persistence();
void test_audio_lock_save_format_v4_mixer();
void test_audio_lock_voice_blip_concurrent_counts();
void test_audio_lock_voice_blip_sanitize_finiteness();
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
// R1 #7 test-only sayaçları (c_api_lifecycle.cpp): slot havuzu boyutu +
// canlı handle sayısı. Davranış-nötr gözlem.
uint64_t RowlTest_HandleSlotCount();
uint64_t RowlTest_LiveHandleCount();
// P2-5 test-only sayaçları: TU-local per-handle aux-map girdi sayıları
// (c_api_prefetch_chapters.cpp / c_api_character_layers.cpp). Shutdown'un
// aux haritasını boşalttığını ÖLÇMEK için; davranış-nötr gözlem.
uint64_t RowlTest_PrefetchAuxEntryCount();
uint64_t RowlTest_CharacterAuxEntryCount();
}
void test_game_object_component_system();
void test_pixel_pitch();
void test_window_input_routing();
void test_window_input_routing_release_motion_wheel_text();  // #16: release/motion/wheel/text tuketim kilidi
void test_frame_hash_lock();
void test_frame_reuse_lock();  // A3-tur5: identical-frame reuse D2 kilidi
void test_cache_hygiene();  // A3-tur6: negatif-onbellek hijyen kilidi
void test_font_hardening();  // A3-tur7: font-yukleme fail-closed kilidi
void test_handle_hygiene();  // A4-tur1: handle-once siralama kilidi
void test_observability_c_api();  // RowlEye gozlem C API: sahip-thread + boyut + log kilidi
void test_lifecycle_init_guards();  // B1a: fail-loud init kilitleri
void test_lifecycle_shutdown_sweep();  // D2: shutdown-supurme + re-init kilitleri
void test_lifecycle_thread_lease();  // D3: thread/lease + dispatch-pin kilitleri
void test_lifecycle_run_guard();  // #14 [HIGH]: offscreen Run fail-closed kilidi
void test_lifecycle_run_abort();  // #123 [HIGH]: run-loop istisnada fail-closed abort kilidi
void test_typewriter_gate();  // #40 [HIGH]: reveal-tabanli tamamlanma kapisi kilidi
void test_autoadvance_freeze();  // #61 [HIGH]: gecis sayaci dondurur, silmez kilidi
void test_lifecycle_handle_reclamation();  // R1 #7: nesilli slot havuzu + ABA kilidi
void test_save_lock();  // D4: save/load/rewind kilitleri (#43-#53, #70)
void test_save_slot_concurrency();  // R1 #3: eszamanli slot yazarlarinda kazanan hep tam payload
void test_save_slot_transient_retry();  // Windows paylasim yarisinda sinirli retry + fail-closed
void test_numeric_guards();  // B6: sayisal-uc/native guard dilimi bilerek-boz kilitleri
void test_lua_hardening();  // B7: lua-sandbox sertlestirme dilimi bilerek-boz kilitleri
void test_lua_module_rowl_impostor();  // D6 #158: module-env rowl impostor RED kilidi
void test_lua_sandbox_dos_probe();
void test_runtime_context_and_diagnostics();
void test_crash_log();
void test_story_graph_parser();
void test_story_graph_format_version();
void test_game_identity();
void test_game_data_layout();
void test_native_player_profile();  // D01c-1: player profile v1 (preferences + read set)
void test_boot_story_diagnostics();  // #118/#119/#120: grafsiz/bozuk boot ayirt edilebilirlik kilidi
void test_project_mount_cwd();  // cwd = proje koku iken oynatici VFS baglamasi kilidi
void test_title_screen();  // D02: native baslik ekrani / Devam Et / Tercihler
void test_backlog_replay();  // D03-1: gecmis satirina ses + guvenli ses tekrari
void test_f2_audit_fixes();  // F2: girdi korumalari, kayit/yukle geri bildirimi, yuva onbellegi, secim menusu, kosullar
void test_f2_visible_window();  // F2 B-1: gorunur pencerede kayit kucuk resmi (pause-overlay gorunur testinden hemen sonra kosar)
void test_f1_audit_fixes();  // F1: dugum geri sarma, kalici gecmis, giris sesleri, gecmis ekrani
void test_backlog_screen();  // D03-2: oyuncuda Gecmis (backlog) ekrani
void test_auto_skip();  // D04-1: oyuncu Otomatik + okunana duyarli Atla surucusu
void test_chapter_boundary_persistence();  // D06-3: bolum siniri kayit/yukle/geri sarma + muzik + oyuncu prefetch surucusu
void test_ui_skin_visible_window();  // T2: gorunur pencerede skin + tema imleci (pause-overlay gorunur testinden hemen sonra kosar)
void test_menu_layout();  // T2b: sayfa yerlesimi (screens.<sayfa>.layout) + yuva sayfalari
void test_ui_skin();    // T2: skin (gorsel cerceve) — her oyuncu arayuzu yuzeyi: ayristirici, 9-dilim, durum, kaydirici, tani
void test_ui_theme();  // T1: oyuncu arayuzu temasi (ui_theme.json): ayristirici, varsayilan == eski sabitler, temali pikseller, oncelik, yazi tipi, reload
void test_extras_screens();  // D07-2: Ekstralar sayfalari (Galeri / Muzik Odasi / Basarimlar) + basarim bildirimi
void test_extras_unlocks();  // D07-1: acilabilir icerik (extras.json, profil unlocks, tetikleyiciler, C API)
void test_condition_component();  // D06-1: dugum 'condition' bileseni (giriste degerlendir / hedefe yonlendir)
void test_auto_skip_ui();  // D04-2: Tercihler Otomatik/Atlama satirlari + AUTO/SKIP gostergesi
void test_player_preferences_menu();  // D05-2: Tercihler pause'dan + dil / metin boyutu / kontrast / hareket satirlari, sigma
void test_player_ui_language();  // D05-1: oyuncu arayuz dili (tr/en), UTF-8 menu yolu, profil dili
void test_choice_input_guard();  // secim menusunu duz ilerleme yanitlamaz kilidi
void test_choice_keyboard();  // D-K3: secim menusunde klavye / fare odagi kilidi
void test_pause_subpage_back();  // D-K3: Esc duraklatma alt sayfalarinda Geri kilidi
void test_ui_text_fit();  // D-K3: konusmaci etiketi + secenek dugmesi yazi boyutuyla sigar kilidi
void test_bgm_continuity();  // D-K3: geri sarma / yukleme calan muzigi bastan baslatmaz kilidi
void test_end_of_story_title();  // D-K3: hikaye bitince baslik ekrani kilidi
void test_graph_vnext();
void test_localization();
void test_locale_cluster();  // Dilim Locale kümesi kilitleri (T1-T7)
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
void test_scene_update_rollback();  // #20/#21: scene-update atomic-rollback hybrid probe
void test_render_transition_fixes();  // #160/#162/#163/#164: render/transition lock tests
