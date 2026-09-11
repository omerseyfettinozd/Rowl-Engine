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
    ${CMAKE_SOURCE_DIR}/engine/src/core/logger.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/core/runtime_context.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/aspect_guardian.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/font_renderer.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/msdf_renderer.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/window.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/camera2d.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/transition_manager.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/state/game_state.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scripting/lua_sandbox.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/platform/mobile_input.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/platform/sdl_event_dispatcher.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/platform/sdl_subsystem_lease.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/vfs/vfs.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/vfs/rowlpkg_reader.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/core/engine.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scene/component.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scene/transform_component.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scene/sprite_component.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scene/game_object.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scene/scene.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/c_api.cpp
)

target_include_directories(rowl_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)

target_link_libraries(rowl_tests PRIVATE
    RowlEngineCore
)

# rowl_tests compiles the engine sources directly (the C++ classes are
# internal/hidden and not exported from the shared library). Build its
# objects as the exporting side so every engine call inside the test
# binary — C++ and C API alike — binds to the test binary's own copies.
# Without this, MSVC binds the C API (declared dllimport here) to
# RowlEngineCore.dll while the C++ calls stay local, splitting static
# state in two (e.g. the SDL event dispatcher) and breaking event tests.
target_compile_definitions(rowl_tests PRIVATE ROWL_BUILDING_DLL)

# The objects above duplicate the symbols in RowlEngineCore.lib (linked
# only for its transitive third-party deps). Keep the first — local —
# definition instead of failing with LNK2005.
if(MSVC)
    target_link_options(rowl_tests PRIVATE /FORCE:MULTIPLE)
endif()

add_test(NAME rowl_native_tests COMMAND rowl_tests)
set_tests_properties(rowl_native_tests PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    ENVIRONMENT "SDL_AUDIODRIVER=dummy"
)
