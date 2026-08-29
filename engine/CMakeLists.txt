cmake_minimum_required(VERSION 3.25)
project(RowlEngineCore VERSION 1.0.0 LANGUAGES CXX C)

# ── nlohmann/json (header-only JSON library) ──────────────────────────────
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# ── zstd ──────────────────────────────────────────────────────────────────
find_package(zstd QUIET)
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
add_library(RowlEngineCore SHARED
    # Core subsystems
    src/core/logger.cpp
    src/core/engine.cpp
    # Rendering
    src/render/window.cpp
    src/render/msdf_renderer.cpp
    src/render/aspect_guardian.cpp
    # VFS
    src/vfs/vfs.cpp
    src/vfs/rowlpkg_reader.cpp
    # Game state
    src/state/game_state.cpp
    # Scripting
    src/scripting/lua_sandbox.cpp
    # Audio
    src/audio/audio_engine.cpp
    # Platform abstraction
    src/platform/mobile_input.cpp
    # C-API bridge (consumed by C# P/Invoke)
    src/c_api.cpp
)

# Mark all C-API symbols as exported on all platforms
target_compile_definitions(RowlEngineCore PRIVATE ROWL_BUILDING_DLL)

# Symbol visibility: only explicitly marked symbols are exported
if(NOT MSVC)
    target_compile_options(RowlEngineCore PRIVATE -fvisibility=hidden)
endif()

target_include_directories(RowlEngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(RowlEngineCore PUBLIC
    ${ZSTD_TARGET}
    ${SDL3_TARGET}
    ${LUA_LIBRARIES}
    nlohmann_json::nlohmann_json
)

# ── Install rules (optional but useful for packaging) ─────────────────────
install(TARGETS RowlEngineCore
    LIBRARY  DESTINATION lib
    RUNTIME  DESTINATION bin   # Windows DLL goes here
    ARCHIVE  DESTINATION lib
)
install(DIRECTORY include/ DESTINATION include)

message(STATUS "RowlEngineCore configured as SHARED library (Embedded Library mode — IPC removed).")
