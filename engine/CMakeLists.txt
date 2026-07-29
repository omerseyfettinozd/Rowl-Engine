cmake_minimum_required(VERSION 3.25)
project(RowlEngineCore VERSION 1.0.0 LANGUAGES CXX)

# Find zstd library
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
        message(FATAL_ERROR "zstd library not found on system! Please install zstd or libzstd-dev.")
    endif()
else()
    set(ZSTD_TARGET zstd::libzstd)
endif()

# Find SDL3 library
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
        message(FATAL_ERROR "SDL3 library not found on system! Please install SDL3.")
    endif()
else()
    set(SDL3_TARGET SDL3::SDL3)
endif()

# Find Lua library
find_package(Lua REQUIRED)

# Core engine static library
add_library(RowlEngineCore STATIC
    src/core/logger.cpp
    src/core/engine.cpp
    src/vfs/vfs.cpp
    src/vfs/rowlpkg_reader.cpp
    src/render/window.cpp
    src/render/msdf_renderer.cpp
    src/render/aspect_guardian.cpp
    src/ipc/ipc_server.cpp
    src/state/game_state.cpp
    src/scripting/lua_sandbox.cpp
    src/audio/audio_engine.cpp
    src/platform/mobile_input.cpp
)

target_include_directories(RowlEngineCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(RowlEngineCore PRIVATE ${ZSTD_TARGET} ${SDL3_TARGET} ${LUA_LIBRARIES})

# Engine main executable
add_executable(rowl_engine
    src/main.cpp
)

target_link_libraries(rowl_engine PRIVATE RowlEngineCore)

message(STATUS "RowlEngineCore library linked with Mobile Input Abstraction. Target configured.")
