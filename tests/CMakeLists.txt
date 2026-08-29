cmake_minimum_required(VERSION 3.25)

add_executable(rowl_tests
    main_test_runner.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/core/logger.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/aspect_guardian.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/font_renderer.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/msdf_renderer.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/render/window.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/state/game_state.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/audio/audio_engine.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/scripting/lua_sandbox.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/platform/mobile_input.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/vfs/vfs.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/vfs/rowlpkg_reader.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/core/engine.cpp
    ${CMAKE_SOURCE_DIR}/engine/src/c_api.cpp
)

target_include_directories(rowl_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/engine/include
)

target_link_libraries(rowl_tests PRIVATE
    RowlEngineCore
)
