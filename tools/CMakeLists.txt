# Rowl converter host tools (Faz 5 Dilim 5).
#
# rowl_oggenc / rowl_webp2png are HOST-build utilities: they convert source
# media into deterministic runtime formats on the developer machine. They
# never ship in the game package, never link the engine runtime, and never
# touch Rowl::Audio. Guarded by ROWL_BUILD_TOOLS so cross-builds can skip
# host tooling exactly like ROWL_BUILD_RUNTIME_TESTS skips host tests.

find_package(PkgConfig REQUIRED)
pkg_check_modules(OGGENC_DEPS REQUIRED IMPORTED_TARGET vorbisenc vorbis ogg)
pkg_check_modules(WEBP2PNG_DEPS REQUIRED IMPORTED_TARGET libwebpdecoder libpng)

add_executable(rowl_oggenc rowl_oggenc.c)
target_link_libraries(rowl_oggenc PRIVATE PkgConfig::OGGENC_DEPS)
set_target_properties(rowl_oggenc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    C_STANDARD 11
)

add_executable(rowl_webp2png rowl_webp2png.c)
target_link_libraries(rowl_webp2png PRIVATE PkgConfig::WEBP2PNG_DEPS)
set_target_properties(rowl_webp2png PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    C_STANDARD 11
)

if(NOT MSVC)
    target_compile_options(rowl_oggenc PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(rowl_webp2png PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tur-8 (Windows CI: converter/author TIMEOUT): the tools link vcpkg DLLs
# (libwebp/libpng/zlib, vorbis/ogg). When those DLLs are not on PATH, the
# Windows loader parks on a missing-DLL dialog and the headless CI run
# hangs with zero output until the ctest timeout kills it. Host tools must
# be self-contained (the same rule test_converters.py enforces on their
# sources), so stage every runtime DLL next to the exe at build time.
if(WIN32)
    foreach(ROWL_TOOL rowl_oggenc rowl_webp2png)
        add_custom_command(TARGET ${ROWL_TOOL} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:${ROWL_TOOL}>"
                "$<TARGET_FILE_DIR:${ROWL_TOOL}>"
            COMMAND_EXPAND_LISTS)
    endforeach()
endif()
