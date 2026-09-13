cmake_minimum_required(VERSION 3.25)
project(RowlEngineCore VERSION 1.0.0 LANGUAGES CXX C)

# The first GPU MSDF artifact is Linux/Vulkan SPIR-V.  Keep it optional so a
# shader compiler or an unsupported shader format can never prevent another
# platform from building the engine's SDL/font-renderer fallback.
set(ROWL_GPU_MSDF_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ROWL_GPU_MSDF_DEFAULT ON)
endif()
option(ROWL_ENABLE_GPU_MSDF "Build the GPU MSDF shader artifact when supported" ${ROWL_GPU_MSDF_DEFAULT})
set(ROWL_GPU_MSDF_SHADER_AVAILABLE 0)

if(ROWL_ENABLE_GPU_MSDF)
    find_program(GLSLC_EXECUTABLE glslc)
    if(GLSLC_EXECUTABLE)
        set(ROWL_SHADER_DIR "${CMAKE_BINARY_DIR}/shaders")
        file(MAKE_DIRECTORY "${ROWL_SHADER_DIR}")
        add_custom_command(OUTPUT "${ROWL_SHADER_DIR}/msdf_text.frag.spv"
            COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=frag
                "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/msdf_text.frag"
                -o "${ROWL_SHADER_DIR}/msdf_text.frag.spv"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/msdf_text.frag")
        add_custom_target(rowl_msdf_shader DEPENDS "${ROWL_SHADER_DIR}/msdf_text.frag.spv")
        set(ROWL_GPU_MSDF_SHADER_AVAILABLE 1)
    else()
        message(WARNING "glslc was not found; GPU MSDF is disabled and the font renderer fallback will be used.")
    endif()
endif()

# ── nlohmann/json (header-only JSON library) ──────────────────────────────
# Prefer a package supplied by the active toolchain (for example vcpkg or an
# Android/iOS sysroot).  FetchContent remains a pinned desktop fallback, not a
# requirement for a cross-build with an already provisioned dependency set.
find_package(nlohmann_json 3.11.3 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ── zstd ──────────────────────────────────────────────────────────────────
find_package(zstd QUIET)
if(zstd_FOUND AND NOT TARGET zstd::libzstd)
    # Debian/Ubuntu's libzstd-dev only ships versioned targets
    # (zstd::libzstd_shared / zstd::libzstd_static).
    if(TARGET zstd::libzstd_shared)
        add_library(zstd::libzstd ALIAS zstd::libzstd_shared)
    elseif(TARGET zstd::libzstd_static)
        add_library(zstd::libzstd ALIAS zstd::libzstd_static)
    else()
        set(zstd_FOUND FALSE)
    endif()
endif()
if(NOT zstd_FOUND)
    find_library(ZSTD_LIBRARY NAMES zstd zstd_static)
    find_path(ZSTD_INCLUDE_DIR zstd.h)
    if(ZSTD_LIBRARY AND ZSTD_INCLUDE_DIR)
        add_library(zstd::libzstd UNKNOWN IMPORTED)
        set_target_properties(zstd::libzstd PROPERTIES
            IMPORTED_LOCATION "${ZSTD_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
        )
        set(ZSTD_TARGET zstd::libzstd)
    else()
        message(FATAL_ERROR "zstd library not found! Please install libzstd-dev.")
    endif()
else()
    set(ZSTD_TARGET zstd::libzstd)
endif()

# ── Ogg/Vorbis ────────────────────────────────────────────────────────────
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(VORBISFILE QUIET IMPORTED_TARGET vorbisfile)
endif()
if(TARGET PkgConfig::VORBISFILE)
    set(VORBISFILE_TARGET PkgConfig::VORBISFILE)
else()
    find_library(VORBISFILE_LIBRARY NAMES vorbisfile libvorbisfile)
    find_path(VORBISFILE_INCLUDE_DIR vorbis/vorbisfile.h)
    if(VORBISFILE_LIBRARY AND VORBISFILE_INCLUDE_DIR)
        add_library(Rowl::Vorbisfile UNKNOWN IMPORTED)
        set_target_properties(Rowl::Vorbisfile PROPERTIES
            IMPORTED_LOCATION "${VORBISFILE_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${VORBISFILE_INCLUDE_DIR}"
        )
        set(VORBISFILE_TARGET Rowl::Vorbisfile)
    else()
        message(FATAL_ERROR "vorbisfile library not found. Supply it through pkg-config or the active toolchain.")
    endif()
endif()

# ── SDL3 ──────────────────────────────────────────────────────────────────
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    find_library(SDL3_LIBRARY NAMES SDL3)
    find_path(SDL3_INCLUDE_DIR SDL3/SDL.h)
    if(SDL3_LIBRARY AND SDL3_INCLUDE_DIR)
        add_library(SDL3::SDL3 UNKNOWN IMPORTED)
        set_target_properties(SDL3::SDL3 PROPERTIES
            IMPORTED_LOCATION "${SDL3_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SDL3_INCLUDE_DIR}"
        )
        set(SDL3_TARGET SDL3::SDL3)
    else()
        message(FATAL_ERROR "SDL3 library not found! Please install SDL3.")
    endif()
else()
    set(SDL3_TARGET SDL3::SDL3)
endif()

# ── Lua ───────────────────────────────────────────────────────────────────
find_package(Lua REQUIRED)

# ── Engine sources — compiled exactly once ──────────────────────────────────
#
#   Every engine translation unit lives in the rowl_engine_objects OBJECT
#   library. RowlEngineCore (SHARED) and rowl_tests both consume these same
#   objects, so process-wide static state (e.g. the SDL event dispatcher)
#   exists exactly once per binary. Compiling the sources a second time
#   inside the test binary used to split that state in two and was only
#   papered over with /FORCE:MULTIPLE on MSVC.
#
set(ROWL_ENGINE_SOURCES
    # Core subsystems
    src/core/logger.cpp
    src/core/runtime_context.cpp
    src/core/story_graph_parser.cpp
    src/core/story_runtime.cpp
    src/core/engine.cpp
    # Rendering
    src/render/window.cpp
    src/render/camera2d.cpp
    src/render/transition_manager.cpp
    src/render/font_renderer.cpp
    src/render/msdf_renderer.cpp
    src/render/aspect_guardian.cpp
    # VFS
    src/vfs/vfs.cpp
    src/vfs/rowlpkg_reader.cpp
    # Game state
    src/state/game_state.cpp
    src/state/session_persistence.cpp
    # Scripting
    src/scripting/lua_sandbox.cpp
    # Audio
    src/audio/audio_engine.cpp
    # Platform abstraction
    src/platform/platform_host.cpp
    src/platform/mobile_input.cpp
    src/platform/sdl_event_dispatcher.cpp
    src/platform/sdl_subsystem_lease.cpp
    # Entity-Component Scene System
    src/scene/component.cpp
    src/scene/transform_component.cpp
    src/scene/sprite_component.cpp
    src/scene/game_object.cpp
    src/scene/scene.cpp
    # C-API bridge (consumed by C# P/Invoke)
    src/c_api_lifecycle.cpp
    src/c_api_story.cpp
    src/c_api_render.cpp
    src/c_api_audio.cpp
    src/c_api_state.cpp
)
# ── Third-party dependencies as a reusable INTERFACE target ───────────────
# Both RowlEngineCore and rowl_tests link this; it carries the include/link
# usage requirements without dragging the shared library into the test binary.
add_library(rowl_engine_thirdparty INTERFACE)
target_link_libraries(rowl_engine_thirdparty INTERFACE
    ${ZSTD_TARGET}
    ${VORBISFILE_TARGET}
    ${SDL3_TARGET}
    ${LUA_LIBRARIES}
    nlohmann_json::nlohmann_json
)
target_include_directories(rowl_engine_thirdparty INTERFACE
    ${LUA_INCLUDE_DIR}
)

add_library(rowl_engine_objects OBJECT ${ROWL_ENGINE_SOURCES})
# The objects also feed the SHARED library, so they must be PIC even though
# an OBJECT target does not enable it by default.
set_target_properties(rowl_engine_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Usage requirements (notably third-party headers) flow to every consumer.
target_link_libraries(rowl_engine_objects PUBLIC rowl_engine_thirdparty)
target_compile_definitions(rowl_engine_objects PRIVATE
    ROWL_GPU_MSDF_SHADER_AVAILABLE=${ROWL_GPU_MSDF_SHADER_AVAILABLE}
    ROWL_SHADER_DIR="${ROWL_SHADER_DIR}"
)

# Mark all C-API symbols as exported on all platforms
target_compile_definitions(rowl_engine_objects PRIVATE ROWL_BUILDING_DLL)

# Symbol visibility: only explicitly marked symbols are exported
if(NOT MSVC)
    target_compile_options(rowl_engine_objects PRIVATE -fvisibility=hidden)
endif()

target_include_directories(rowl_engine_objects PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${LUA_INCLUDE_DIR}
)

# ── RowlEngineCore — SHARED library (DLL / .so / .dylib) ─────────────────
#
#   This is the primary build target. The engine is no longer a standalone
#   executable; it is a shared library consumed by the C# Avalonia editor
#   via P/Invoke (DllImport) and by native mobile host apps.
#
#   Output names per platform:
#     Linux/Android : libRowlEngineCore.so
#     Windows       : RowlEngineCore.dll
#     macOS/iOS     : libRowlEngineCore.dylib
#
add_library(RowlEngineCore SHARED)
target_link_libraries(RowlEngineCore PUBLIC
    rowl_engine_objects
    rowl_engine_thirdparty
)
if(TARGET rowl_msdf_shader)
    add_dependencies(RowlEngineCore rowl_msdf_shader)
endif()

target_include_directories(RowlEngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# ── Rowl Player — Standalone Desktop Executable ───────────────────────────
set(ROWL_PLAYER_DEFAULT ON)
if(CMAKE_CROSSCOMPILING OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(ROWL_PLAYER_DEFAULT OFF)
endif()
option(ROWL_BUILD_PLAYER "Build the standalone desktop player" ${ROWL_PLAYER_DEFAULT})

if(ROWL_BUILD_PLAYER)
    add_executable(rowl_player src/player/main.cpp)
    target_link_libraries(rowl_player PRIVATE RowlEngineCore ${SDL3_TARGET})
    set_target_properties(rowl_player PROPERTIES
        OUTPUT_NAME "rowl_player"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        BUILD_RPATH "$ORIGIN/../lib:$ORIGIN"
        INSTALL_RPATH "$ORIGIN/../lib:$ORIGIN"
    )
    if(ROWL_GPU_MSDF_SHADER_AVAILABLE)
        add_custom_command(TARGET rowl_player POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:rowl_player>/shaders"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ROWL_SHADER_DIR}/msdf_text.frag.spv"
                "$<TARGET_FILE_DIR:rowl_player>/shaders/msdf_text.frag.spv"
        )
    endif()
endif()

# ── Install rules (optional but useful for packaging) ─────────────────────
install(TARGETS RowlEngineCore
    LIBRARY  DESTINATION lib
    RUNTIME  DESTINATION bin
    ARCHIVE  DESTINATION lib
)
if(TARGET rowl_player)
    install(TARGETS rowl_player RUNTIME DESTINATION bin)
endif()
install(DIRECTORY include/ DESTINATION include)
if(ROWL_GPU_MSDF_SHADER_AVAILABLE)
    install(FILES "${ROWL_SHADER_DIR}/msdf_text.frag.spv" DESTINATION bin/shaders)
endif()

message(STATUS "RowlEngineCore and rowl_player configured successfully.")
