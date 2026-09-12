cmake_minimum_required(VERSION 3.25)

add_executable(rowl_tests
    test_main.cpp
    test_logger.cpp
    test_aspect_guardian.cpp
    test_msdf_renderer.cpp
    test_game_state.cpp
    test_audio_engine.cpp
    test_audio_device_recovery.cpp
    test_lua_sandbox.cpp
    test_mobile_input.cpp
    test_vfs_security.cpp
    test_native_c_api.cpp
    test_game_object_component_system.cpp
    test_window_input_routing.cpp
    test_runtime_context_and_diagnostics.cpp
    test_native_performance_benchmarks.cpp
    test_hardening_and_reliability.cpp
    test_camera_and_transition_pipeline.cpp
    test_single_engine_image.cpp
    test_demo_first_light.cpp
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

# The test binary is the exporting side of its own engine objects, matching
# how RowlEngineCore builds them.
target_compile_definitions(rowl_tests PRIVATE ROWL_BUILDING_DLL)

add_test(NAME rowl_native_tests COMMAND rowl_tests)
set_tests_properties(rowl_native_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy"
)
