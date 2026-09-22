cmake_minimum_required(VERSION 3.25)

add_executable(rowl_tests
    test_c_api_header.c
    test_main.cpp
    test_logger.cpp
    test_aspect_guardian.cpp
    test_msdf_renderer.cpp
    test_game_state.cpp
    test_audio_engine.cpp
    test_audio_device_recovery.cpp
    test_audio_error_channel.cpp
    test_audio_streaming.cpp
    test_audio_mixer.cpp
    test_audio_ui_gain.cpp
    test_audio_lock.cpp
    test_lua_sandbox.cpp
    test_mobile_input.cpp
    test_platform_host.cpp
    test_c_api_contract.cpp
    test_vfs_security.cpp
    test_native_c_api.cpp
    test_game_object_component_system.cpp
    test_pixel_pitch.cpp
    test_window_input_routing.cpp
    test_runtime_context_and_diagnostics.cpp
    test_crash_log.cpp
    test_save_slot_and_package_fuzz.cpp
    test_story_graph_parser.cpp
    test_boot_story_diagnostics.cpp
    test_graph_vnext.cpp
    test_localization.cpp
    test_locale_cluster.cpp
    test_markup_parser.cpp
    test_text_shaping.cpp
    test_utf8_decoder.cpp
    test_accessibility.cpp
    test_character_layers.cpp
    test_character_layer_benchmarks.cpp
    test_prefetch_chapters.cpp
    test_converter_provenance.cpp
    test_native_performance_benchmarks.cpp
    test_hardening_and_reliability.cpp
    test_rc_soak_and_data_safety.cpp
    test_camera_and_transition_pipeline.cpp
    test_frame_hash_lock.cpp
    test_frame_reuse_lock.cpp
    test_cache_hygiene.cpp
    test_font_hardening.cpp
    test_handle_hygiene.cpp
    test_lifecycle_init_guards.cpp
    test_lifecycle_shutdown_sweep.cpp
    test_lifecycle_thread_lease.cpp
    test_lifecycle_run_guard.cpp
    test_lifecycle_run_abort.cpp
    test_typewriter_gate.cpp
    test_autoadvance_freeze.cpp
    test_lifecycle_handle_reclamation.cpp
    test_save_lock.cpp
    test_save_slot_concurrency.cpp
    test_scene_update_rollback.cpp    test_numeric_guards.cpp
    test_render_transition_fixes.cpp
    test_lua_hardening.cpp
    test_single_engine_image.cpp
    test_demo_first_light.cpp
    test_demo_second_signal.cpp
)

target_include_directories(rowl_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)

# rowl_tests consumes the same rowl_engine_objects as RowlEngineCore, so the
# engine exists exactly once in this binary: C++ calls and C-API calls share
# one copy of every process-wide static (e.g. the SDL event dispatcher).
# RowlEngineCore itself is deliberately NOT linked here; rowl_engine_thirdparty
# carries only the third-party include/link requirements. This replaces the
# old double compilation plus /FORCE:MULTIPLE workaround.
target_link_libraries(rowl_tests PRIVATE
    rowl_engine_objects
    rowl_engine_thirdparty
)

# test_c_api_header.c uses _Static_assert (C11): MSVC only provides the
# keyword in an explicit C11 mode (default C mode rejects it with C2143),
# while GCC/Clang accept it as an extension. tools/ targets already pin
# C_STANDARD 11 for the same reason; keep the test binary on the same rule.
set_target_properties(rowl_tests PROPERTIES C_STANDARD 11)

if(WIN32)
    # Audio soak RSS gauge uses GetProcessMemoryInfo on Windows.
    target_link_libraries(rowl_tests PRIVATE psapi)
endif()

# The test binary is the exporting side of its own engine objects, matching
# how RowlEngineCore builds them.
target_compile_definitions(rowl_tests PRIVATE ROWL_BUILDING_DLL)
# B1: MSDF capability-bit varlık testi bu build'in shader-durumunu bilir
# (CACHE'den; shaderless-fallback'ta bit 0 beklenir).
target_compile_definitions(rowl_tests PRIVATE
    ROWL_TEST_GPU_MSDF_EXPECTED=${ROWL_GPU_MSDF_SHADER_AVAILABLE})

add_test(NAME rowl_native_tests COMMAND rowl_tests)
set_tests_properties(rowl_native_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    # Dummy drivers keep the suite display-/audio-server free on headless
    # CI and sandbox machines; the offscreen engine needs neither.
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
)

# D01 (#135) embed RED/green probu: yalnızca public C API'yi kullanan
# bağımsız ikili (paylaşılan RowlEngineCore'a bağlanır; üretim
# topolojisiyle aynı). rowl_tests gövdesine gömülmez ki kırmızı-yeşil
# döngüsü tüm süiti koşmadan saniyeler içinde kanıtlansın.
add_executable(rowl_embed_red_probe test_embed_red_probe.cpp)
target_include_directories(rowl_embed_red_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_embed_red_probe PRIVATE RowlEngineCore)

add_test(NAME embed_red_probe COMMAND rowl_embed_red_probe)
set_tests_properties(embed_red_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
)

# D02 audio snapshot-struct davranışsız taşıma kilidi: yalnızca public C API'yi
# kullanan bağımsız ikili (paylaşılan RowlEngineCore'a bağlanır; üretim
# topolojisiyle aynı). rowl_tests gövdesine gömülmez ki kırmızı-yeşil döngüsü
# tüm süiti koşmadan saniyeler içinde kanıtlansın. TIMEOUT: helper'a yanlış
# çevirim (kilit alan helper çağrısı) askıya düşerse KIRMIZI verir.
add_executable(rowl_audio_snapshot_red_probe test_audio_snapshot_red_probe.cpp)
target_include_directories(rowl_audio_snapshot_red_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_audio_snapshot_red_probe PRIVATE RowlEngineCore)

add_test(NAME audio_snapshot_red_probe COMMAND rowl_audio_snapshot_red_probe)
set_tests_properties(audio_snapshot_red_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

# D14 threading-contract RED/green probu: yalnizca public C API'yi kullanan
# bagimsiz ikili (paylasilan RowlEngineCore'a baglanir; uretim
# topolojisiyle ayni). rowl_tests govdesine gomulmez ki kirmizi-yesil
# dongusu tum suiti kosmadan saniyeler icinde kanitlansin.
add_executable(rowl_thread_contract_red_probe test_thread_contract_red_probe.cpp)
target_include_directories(rowl_thread_contract_red_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_thread_contract_red_probe PRIVATE RowlEngineCore)

add_test(NAME thread_contract_red_probe COMMAND rowl_thread_contract_red_probe)
set_tests_properties(thread_contract_red_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

# D06 ham-global sızıntı kilidi (evaluateCondition): yalnızca public C API'yi
# kullanan iki bağımsız ikili (paylaşılan RowlEngineCore'a bağlanır; üretim
# topolojisiyle aynı). rowl_tests gövdesine gömülmez ki kırmızı-yeşil döngüsü
# tüm süiti koşmadan saniyeler içinde kanıtlansın. TIMEOUT: olası bir kilit
# regresyonunda askı KIRMIZI verir.
#   rowl_d06_probe_x — sızıntı probu (RED: PROB-DIRTY + exit 1; fix: PROB-CLEAN + exit 0).
#   rowl_d06_probe_m — kontrol kolu (D4 ConditionPurityGuard yeşili; önce/sonra YEŞİL).
add_executable(rowl_d06_probe_x test_d06_probe_x.cpp)
target_include_directories(rowl_d06_probe_x PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_d06_probe_x PRIVATE RowlEngineCore)

add_test(NAME d06_probe_x COMMAND rowl_d06_probe_x)
set_tests_properties(d06_probe_x PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

add_executable(rowl_d06_probe_m test_d06_probe_m.cpp)
target_include_directories(rowl_d06_probe_m PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_d06_probe_m PRIVATE RowlEngineCore)

add_test(NAME d06_probe_m COMMAND rowl_d06_probe_m)
set_tests_properties(d06_probe_m PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

# D03 hacim-yarışı RED kilidi: Bacak-1 header-only StreamMixer + Bacak-3 public
# C API + Bacak-2 AudioEngine üye hammer'ı (test-köprüsü testEngineFromHandle).
# Üye-semboller paylaşılan RowlEngineCore'dan ihraç edilmez (yalnızca C API
# ROWL_API'dir); bu yüzden rowl_tests gibi rowl_engine_objects'a bağlanır —
# C++ çağrıları ve C-API çağrıları tek kopyayı paylaşır (çift-statik yok).
# rowl_tests gövdesine gömülmez ki kırmızı-yeşil döngüsü tüm süiti koşmadan
# saniyeler içinde kanıtlansın. TSan altında KIRMIZI (exit 66 + data-race
# uyarısı), TSan'sız YEŞİL (exit 0).
add_executable(rowl_audio_volume_race_red_probe test_audio_volume_race_red_probe.cpp)
target_include_directories(rowl_audio_volume_race_red_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_audio_volume_race_red_probe PRIVATE
    rowl_engine_objects
    rowl_engine_thirdparty
)
target_compile_definitions(rowl_audio_volume_race_red_probe PRIVATE ROWL_BUILDING_DLL)

add_test(NAME audio_volume_race_red_probe COMMAND rowl_audio_volume_race_red_probe)
set_tests_properties(audio_volume_race_red_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

# D08 save->load->rewind RED kilidi: ic sembolleri kullanir (Rowl::Platform /
# VFS / Core, -fvisibility=hidden ile gizli); bu yuzden rowl_tests gibi
# rowl_engine_objects'a baglanir. RED: rewind=false + step sabit + exit 1;
# YESIL: rewind=true + step-1 + exit 0.
add_executable(rowl_d08_rewind_after_load_probe test_d08_rewind_after_load_probe.cpp)
target_include_directories(rowl_d08_rewind_after_load_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_d08_rewind_after_load_probe PRIVATE
    rowl_engine_objects
    rowl_engine_thirdparty
)
target_compile_definitions(rowl_d08_rewind_after_load_probe PRIVATE ROWL_BUILDING_DLL)

add_test(NAME d08_rewind_after_load_probe COMMAND rowl_d08_rewind_after_load_probe)
set_tests_properties(d08_rewind_after_load_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

# D15 pause-menu/mixer dörtlüsü çıkarma kilidi: engine.cpp'deki
# pauseMenuVolume / setPauseMenuVolume / pauseMenuAdjustSelected (2992-3027) +
# commitMixerVolumesToGameState (3634-3642) yeni engine_pause_mixer TU'suna
# birebir taşınır (D02 deseni). Yalnızca public C API + public Engine
# okumaları (getGameState) + test-köprüsü (testEngineFromHandle) kullanır;
# iç semboller -fvisibility=hidden ile gizli olduğundan rowl_tests gibi
# rowl_engine_objects'a bağlanır. RED: yeni TU CMake'e eklenmeden tanımsız
# referans (exit != 0); GREEN: bacak gözlemleri bayt-birebir + exit 0.
add_executable(rowl_d15_pause_mixer_probe test_d15_pause_mixer_probe.cpp)
target_include_directories(rowl_d15_pause_mixer_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)
target_link_libraries(rowl_d15_pause_mixer_probe PRIVATE
    rowl_engine_objects
    rowl_engine_thirdparty
)
target_compile_definitions(rowl_d15_pause_mixer_probe PRIVATE ROWL_BUILDING_DLL)

add_test(NAME d15_pause_mixer_probe COMMAND rowl_d15_pause_mixer_probe)
set_tests_properties(d15_pause_mixer_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)

# D10 font-fallback mutual-exclusion + all-or-nothing MSDF kilidi: bağımsız
# ikili (rowl_tests gövdesine gömülmez ki kırmızı-yeşil döngüsü tüm süiti
# koşmadan saniyeler içinde kanıtlansın). D01/D02'den farklı olarak
# RowlEngineCore'a değil rowl_engine_objects'a bağlanır: Window/MsdfRenderer
# iç sembolleri -fvisibility=hidden ile gizlidir (yalnızca C API export);
# iç-davranış kilidi object-bağlantısı gerektirir (rowl_tests emsali).
add_executable(rowl_text_fallback_red_probe test_text_fallback_red_probe.cpp)
target_include_directories(rowl_text_fallback_red_probe PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
    ${CMAKE_SOURCE_DIR}/tests
)
target_link_libraries(rowl_text_fallback_red_probe PRIVATE
    rowl_engine_objects
    rowl_engine_thirdparty
)
target_compile_definitions(rowl_text_fallback_red_probe PRIVATE ROWL_BUILDING_DLL)

add_test(NAME text_fallback_red_probe COMMAND rowl_text_fallback_red_probe)
set_tests_properties(text_fallback_red_probe PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
    TIMEOUT 120
)
if(WIN32)
    # MSVC Debug runs the full native suite far slower than optimized
    # compilers; the CI --timeout 300 turns that into a zero-output kill.
    # 1500 gives a Debug run room to finish while staying a fast pointer
    # on a real hang (assert dialogs are suppressed in test_main.cpp).
    # Other hosts keep the CLI default: do not touch, they are green.
    set_tests_properties(rowl_native_tests PROPERTIES TIMEOUT 1500)
endif()
