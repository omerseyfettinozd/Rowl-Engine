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
    test_audio_streaming.cpp
    test_audio_mixer.cpp
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
    test_graph_vnext.cpp
    test_localization.cpp
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
    test_lifecycle_handle_reclamation.cpp
    test_save_lock.cpp
    test_save_slot_concurrency.cpp
    test_numeric_guards.cpp
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
if(WIN32)
    # MSVC Debug runs the full native suite far slower than optimized
    # compilers; the CI --timeout 300 turns that into a zero-output kill.
    # 1500 gives a Debug run room to finish while staying a fast pointer
    # on a real hang (assert dialogs are suppressed in test_main.cpp).
    # Other hosts keep the CLI default: do not touch, they are green.
    set_tests_properties(rowl_native_tests PROPERTIES TIMEOUT 1500)
endif()
