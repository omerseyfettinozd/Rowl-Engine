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
# B1: shader-varlığı tests/ alt-dizininden de okunur (capability-bit varlık
# testi için; CACHE'e yazılır, normal değişken alt-dizine yayılmaz).
set(ROWL_GPU_MSDF_SHADER_AVAILABLE "${ROWL_GPU_MSDF_SHADER_AVAILABLE}" CACHE INTERNAL "GPU MSDF shader artifact available")

# ── nlohmann/json (header-only JSON library) ──────────────────────────────
# Prefer a package supplied by the active toolchain (for example vcpkg or an
# Android/iOS sysroot).  FetchContent remains a pinned desktop fallback, not a
# requirement for a cross-build with an already provisioned dependency set.
#
# Offline builds (notably a -DROWL_ENABLE_TEXT_SHAPING=OFF fallback configure
# without network access) must not re-download the dependency: reuse the
# source tree already populated by the primary build directory before
# falling back to the network.  (The FetchContent build directory's
# generated config is deliberately NOT used here: it is only complete
# after the sub-build finishes.)
if(NOT TARGET nlohmann_json::nlohmann_json
    AND NOT DEFINED FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON)
    set(_rowl_json_local_src "${CMAKE_SOURCE_DIR}/build/_deps/nlohmann_json-src")
    if(EXISTS "${_rowl_json_local_src}/CMakeLists.txt")
        set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${_rowl_json_local_src}"
            CACHE PATH "Local nlohmann/json source reused for offline builds")
        message(STATUS "Reusing local nlohmann/json source: ${FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON}")
    endif()
    unset(_rowl_json_local_src)
endif()
find_package(nlohmann_json 3.11.3 CONFIG QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
    include(FetchContent)
    if(DEFINED FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON)
        FetchContent_Declare(nlohmann_json
            SOURCE_DIR "${FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON}")
    else()
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY https://github.com/nlohmann/json.git
            GIT_TAG v3.11.3
            GIT_SHALLOW TRUE
        )
    endif()
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

# ── Unicode text shaping (optional, stb_truetype remains the fallback) ─────
option(ROWL_ENABLE_TEXT_SHAPING
    "Enable FreeType/HarfBuzz/FriBidi/libunibreak text shaping" ON)
set(ROWL_TEXT_SHAPING_AVAILABLE 0)
set(ROWL_TEXT_SHAPING_TARGET "")
if(ROWL_ENABLE_TEXT_SHAPING AND PkgConfig_FOUND)
    pkg_check_modules(ROWL_TEXT_SHAPING QUIET IMPORTED_TARGET
        freetype2 harfbuzz fribidi libunibreak)
    if(TARGET PkgConfig::ROWL_TEXT_SHAPING)
        set(ROWL_TEXT_SHAPING_TARGET PkgConfig::ROWL_TEXT_SHAPING)
    endif()
endif()
if(ROWL_ENABLE_TEXT_SHAPING AND NOT ROWL_TEXT_SHAPING_TARGET)
    # Per-library fallback for the umbrella above: a single module without
    # a .pc file sinks the whole stack — vcpkg's libunibreak port ships
    # none, which left Windows on the stb_truetype fallback while the other
    # three resolved (tur-6). Each library resolves independently
    # (pkg-config module first, then find_library/find_path) and the
    # winners combine into one interface target. vcpkg's default triplet
    # is dynamic, so these are DLL import libraries: safe to link into any
    # CRT configuration (static triplets would need per-config selection
    # and are out of scope).
    set(_rowl_shaping_tgts "")
    set(_rowl_shaping_libs "")
    set(_rowl_shaping_incs "")

    # FreeType (<ft2build.h> + FT_FREETYPE_H): the two headers live in
    # different prefixes on some layouts (Debian: freetype2/; upstream or
    # vcpkg: include/ + include/freetype2/), so both join the interface.
    if(PkgConfig_FOUND)
        pkg_check_modules(ROWL_SHAPING_FT QUIET IMPORTED_TARGET freetype2)
    endif()
    if(TARGET PkgConfig::ROWL_SHAPING_FT)
        list(APPEND _rowl_shaping_tgts PkgConfig::ROWL_SHAPING_FT)
        set(_ROWL_SHAPING_FREETYPE_OK TRUE)
    else()
        find_library(ROWL_SHAPING_FREETYPE_LIB NAMES freetype libfreetype)
        find_path(ROWL_SHAPING_FT2_DIR NAMES ft2build.h)
        find_path(ROWL_SHAPING_FT_H_DIR NAMES freetype/freetype.h)
        if(ROWL_SHAPING_FREETYPE_LIB AND ROWL_SHAPING_FT2_DIR
                AND ROWL_SHAPING_FT_H_DIR)
            list(APPEND _rowl_shaping_libs "${ROWL_SHAPING_FREETYPE_LIB}")
            list(APPEND _rowl_shaping_incs
                "${ROWL_SHAPING_FT2_DIR}" "${ROWL_SHAPING_FT_H_DIR}")
            set(_ROWL_SHAPING_FREETYPE_OK TRUE)
        else()
            set(_ROWL_SHAPING_FREETYPE_OK FALSE)
        endif()
    endif()

    # HarfBuzz (<harfbuzz/hb.h> + <harfbuzz/hb-ft.h>, the latter used by
    # text_shaper.cpp for the FreeType bridge).
    if(PkgConfig_FOUND)
        pkg_check_modules(ROWL_SHAPING_HB QUIET IMPORTED_TARGET harfbuzz)
    endif()
    if(TARGET PkgConfig::ROWL_SHAPING_HB)
        list(APPEND _rowl_shaping_tgts PkgConfig::ROWL_SHAPING_HB)
        set(_ROWL_SHAPING_HARFBUZZ_OK TRUE)
    else()
        find_library(ROWL_SHAPING_HARFBUZZ_LIB NAMES harfbuzz libharfbuzz)
        find_path(ROWL_SHAPING_HARFBUZZ_DIR NAMES harfbuzz/hb.h)
        if(ROWL_SHAPING_HARFBUZZ_LIB AND ROWL_SHAPING_HARFBUZZ_DIR
                AND EXISTS "${ROWL_SHAPING_HARFBUZZ_DIR}/harfbuzz/hb-ft.h")
            list(APPEND _rowl_shaping_libs "${ROWL_SHAPING_HARFBUZZ_LIB}")
            list(APPEND _rowl_shaping_incs "${ROWL_SHAPING_HARFBUZZ_DIR}")
            set(_ROWL_SHAPING_HARFBUZZ_OK TRUE)
        else()
            set(_ROWL_SHAPING_HARFBUZZ_OK FALSE)
        endif()
    endif()

    # FriBidi (<fribidi.h>): shipped as fribidi/fribidi.h on most layouts
    # (with the fribidi subdir on the path) or top-level elsewhere; both
    # spellings are probed and every existing prefix joins the interface.
    if(PkgConfig_FOUND)
        pkg_check_modules(ROWL_SHAPING_FR QUIET IMPORTED_TARGET fribidi)
    endif()
    if(TARGET PkgConfig::ROWL_SHAPING_FR)
        list(APPEND _rowl_shaping_tgts PkgConfig::ROWL_SHAPING_FR)
        set(_ROWL_SHAPING_FRIBIDI_OK TRUE)
    else()
        find_library(ROWL_SHAPING_FRIBIDI_LIB NAMES fribidi libfribidi)
        find_path(ROWL_SHAPING_FRIBIDI_DIR NAMES fribidi/fribidi.h fribidi.h)
        if(ROWL_SHAPING_FRIBIDI_LIB AND ROWL_SHAPING_FRIBIDI_DIR)
            list(APPEND _rowl_shaping_libs "${ROWL_SHAPING_FRIBIDI_LIB}")
            list(APPEND _rowl_shaping_incs "${ROWL_SHAPING_FRIBIDI_DIR}")
            if(EXISTS "${ROWL_SHAPING_FRIBIDI_DIR}/fribidi")
                list(APPEND _rowl_shaping_incs
                    "${ROWL_SHAPING_FRIBIDI_DIR}/fribidi")
            endif()
            set(_ROWL_SHAPING_FRIBIDI_OK TRUE)
        else()
            set(_ROWL_SHAPING_FRIBIDI_OK FALSE)
        endif()
    endif()

    # libunibreak (<graphemebreak.h> + <linebreak.h>): ships no .pc file on
    # several distributions, so the manual path is the common one here.
    if(PkgConfig_FOUND)
        pkg_check_modules(ROWL_SHAPING_UB QUIET IMPORTED_TARGET libunibreak)
    endif()
    if(TARGET PkgConfig::ROWL_SHAPING_UB)
        list(APPEND _rowl_shaping_tgts PkgConfig::ROWL_SHAPING_UB)
        set(_ROWL_SHAPING_UNIBREAK_OK TRUE)
    else()
        find_library(ROWL_SHAPING_UNIBREAK_LIB NAMES unibreak libunibreak)
        find_path(ROWL_SHAPING_UNIBREAK_DIR
            NAMES unibreak/graphemebreak.h graphemebreak.h)
        set(_ROWL_SHAPING_UNIBREAK_OK FALSE)
        if(ROWL_SHAPING_UNIBREAK_LIB AND ROWL_SHAPING_UNIBREAK_DIR)
            set(_rowl_ub_incs "${ROWL_SHAPING_UNIBREAK_DIR}")
            if(EXISTS "${ROWL_SHAPING_UNIBREAK_DIR}/unibreak")
                list(APPEND _rowl_ub_incs
                    "${ROWL_SHAPING_UNIBREAK_DIR}/unibreak")
            endif()
            # Both headers ship in the same directory upstream; confirm the
            # co-header resolves from the chosen prefixes so the build —
            # not configure — is never the first to complain.
            foreach(_rowl_ub_dir IN LISTS _rowl_ub_incs)
                if(EXISTS "${_rowl_ub_dir}/linebreak.h")
                    list(APPEND _rowl_shaping_libs
                        "${ROWL_SHAPING_UNIBREAK_LIB}")
                    list(APPEND _rowl_shaping_incs ${_rowl_ub_incs})
                    set(_ROWL_SHAPING_UNIBREAK_OK TRUE)
                    break()
                endif()
            endforeach()
        endif()
    endif()

    if(_ROWL_SHAPING_FREETYPE_OK AND _ROWL_SHAPING_HARFBUZZ_OK
            AND _ROWL_SHAPING_FRIBIDI_OK AND _ROWL_SHAPING_UNIBREAK_OK)
        list(REMOVE_DUPLICATES _rowl_shaping_incs)
        set(_rowl_shaping_link ${_rowl_shaping_tgts} ${_rowl_shaping_libs})
        add_library(RowlShapingFallback INTERFACE IMPORTED)
        set_target_properties(RowlShapingFallback PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${_rowl_shaping_incs}"
            INTERFACE_LINK_LIBRARIES "${_rowl_shaping_link}")
        set(ROWL_TEXT_SHAPING_TARGET RowlShapingFallback)
    endif()
endif()
if(ROWL_ENABLE_TEXT_SHAPING AND NOT ROWL_TEXT_SHAPING_TARGET)
    message(WARNING
        "FreeType/HarfBuzz/FriBidi/libunibreak were not all found; "
        "the safe stb_truetype text fallback will be used.")
endif()
if(ROWL_TEXT_SHAPING_TARGET)
    set(ROWL_TEXT_SHAPING_AVAILABLE 1)
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
    src/core/story_graph_vnext.cpp
    src/core/story_runtime.cpp
    src/core/prefetch.cpp
    src/core/chapter_loader.cpp
    src/core/engine.cpp
    src/core/engine_pause_mixer.cpp  # D15: pause-menu/mixer dörtlüsü (davranışsız taşıma)
    # Rendering
    src/render/window.cpp
    src/render/window_texture_cache.cpp  # A3-tur5: texture-cache bölünmesi
    src/render/window_text_fallback.cpp  # D10: font-fallback/MSDF çekirdek bölünmesi
    src/render/window_msdf_shaped.cpp  # D11: şekilli-MSDF plan/çizim bölünmesi
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
    src/state/save_durability.cpp
    src/state/save_metadata.cpp
    src/state/session_persistence.cpp
    # Scripting
    src/scripting/lua_sandbox.cpp
    src/scripting/lua_condition_purity.cpp  # D06: koşul ham-global saflığı (yeni TU)
    # Audio
    src/audio/audio_engine.cpp
    src/audio/audio_blip_error_channel.cpp  # D02: blip hata-kanalı Locked-yardımcıları
    src/audio/ogg_stream_source.cpp
    # Platform abstraction
    src/platform/crash_handler.cpp
    src/platform/platform_host.cpp
    src/platform/user_data_directories.cpp
    src/platform/mobile_input.cpp
    src/platform/sdl_event_dispatcher.cpp
    src/platform/sdl_subsystem_lease.cpp
    src/platform/sdl_video_serial.cpp
    # Entity-Component Scene System
    src/scene/component.cpp
    src/scene/transform_component.cpp
    src/scene/sprite_component.cpp
    src/scene/character_layers.cpp
    src/scene/game_object.cpp
    src/scene/scene.cpp
    # Internationalization (Faz 3 Dilim 1: manifest locales + catalogs)
    src/i18n/localization_manager.cpp
    # Rich text (Faz 3 Dilim 2: markup parser + token stream)
    src/text/markup_parser.cpp
    # Unicode shaping (Faz 3 Dilim 3; optional advanced backend)
    src/text/text_shaper.cpp
    # C-API bridge (consumed by C# P/Invoke)
    src/c_api_accessibility.cpp
    src/c_api_contract.cpp
    src/c_api_i18n.cpp
    src/c_api_markup.cpp
    src/c_api_text_shaping.cpp
    src/c_api_lifecycle.cpp
    src/c_api_story.cpp
    src/c_api_render.cpp
    src/c_api_embed_guards.cpp
    src/c_api_audio.cpp
    src/c_api_audio_error_stamp.cpp  # D05: M4 damga-bağlama (hata+sınıf tek snapshot)
    src/c_api_character_layers.cpp
    src/c_api_prefetch_chapters.cpp
    src/c_api_provenance.cpp
    src/c_api_state.cpp
    src/c_api_pause.cpp
    src/c_api_thread_contract_guard.cpp  # D14: legacy-mixer checked loud varyantlar
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
# Set exactly when the target below names something real: the umbrella
# PkgConfig target on pkg-config hosts, the per-library RowlShapingFallback
# interface where individual modules lack .pc files (vcpkg/Windows).
if(ROWL_TEXT_SHAPING_AVAILABLE)
    target_link_libraries(rowl_engine_thirdparty INTERFACE
        ${ROWL_TEXT_SHAPING_TARGET})
endif()
if(WIN32)
    target_link_libraries(rowl_engine_thirdparty INTERFACE shell32 ole32)
endif()
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
    ROWL_TEXT_SHAPING_AVAILABLE=${ROWL_TEXT_SHAPING_AVAILABLE}
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
    # W8-c (bulgu-4): RowlCrash_* yardimcilari paylasilan kutuphaneden ihraç
    # edilmez (-fvisibility=hidden, baslikta ROWL_API yok); player'in kullandigi
    # tek ic yuzeydir. C API yolu paylasilan RowlEngineCore'dan gelmeye devam
    # eder (uretim topolojisi degismez); yalnizca crash TU'su player'a dogrudan
    # derlenir (motor ici bu sembolleri cagirmaz, .so'daki kopya oludur).
    add_executable(rowl_player src/player/main.cpp src/platform/crash_handler.cpp)
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
# W8-c (bulgu-4): internal basliklar kurulu pakete cikmaz; yalnizca public
# C ABI basliklari dagitilir (diger internal basliklar kaynak-agactan
# kullanilir, kurulmaz).
install(DIRECTORY include/ DESTINATION include
    REGEX "audio/audio_blip_error_channel\\.hpp$" EXCLUDE
    REGEX "platform/crash_handler\\.hpp$" EXCLUDE
)
if(ROWL_GPU_MSDF_SHADER_AVAILABLE)
    install(FILES "${ROWL_SHADER_DIR}/msdf_text.frag.spv" DESTINATION bin/shaders)
endif()

message(STATUS "RowlEngineCore and rowl_player configured successfully.")
