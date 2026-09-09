cmake_minimum_required(VERSION 3.25)
project(RowlEngine VERSION 1.0.0 LANGUAGES CXX C)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable export of compile commands for clangd / IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Configure output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Compiler flags
if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "Rowl Engine Root CMake initialized. Target C++ Standard: C++${CMAKE_CXX_STANDARD}")

# Add engine and tests subdirectories
add_subdirectory(engine)

set(ROWL_RUNTIME_TESTS_DEFAULT ON)
if(CMAKE_CROSSCOMPILING)
    set(ROWL_RUNTIME_TESTS_DEFAULT OFF)
endif()
option(ROWL_BUILD_RUNTIME_TESTS "Build tests that execute on the build host" ${ROWL_RUNTIME_TESTS_DEFAULT})

if(ROWL_BUILD_RUNTIME_TESTS)
    add_subdirectory(tests)

    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE)
        add_test(
            NAME rowl_editor_headless_tests
            COMMAND ${DOTNET_EXECUTABLE} run
                --project ${CMAKE_SOURCE_DIR}/editor/RowlEngine.Editor.csproj
                -- --headless-test
        )
        set_tests_properties(rowl_editor_headless_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
        )
    endif()
endif()

if(ROWL_BUILD_RUNTIME_TESTS AND TARGET rowl_player)
    add_test(
        NAME rowl_player_cli_help
        COMMAND $<TARGET_FILE:rowl_player> --help
    )
    set_tests_properties(rowl_player_cli_help PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 10
    )

    add_test(
        NAME rowl_player_cli_invalid_width
        COMMAND $<TARGET_FILE:rowl_player> --width not-a-number
    )
    add_test(
        NAME rowl_player_cli_missing_option_value
        COMMAND $<TARGET_FILE:rowl_player> --project
    )

    # A real SDL window is needed to exercise SDL's GPU render-state path.  Keep
    # the test out of non-Linux and locally minimal configurations; CI installs
    # Xvfb and Mesa's Vulkan software driver for this gate.
    if(UNIX AND NOT APPLE)
        find_program(XVFB_RUN_EXECUTABLE xvfb-run)
        if(XVFB_RUN_EXECUTABLE)
            add_test(
                NAME rowl_player_gpu_msdf_smoke
                COMMAND ${CMAKE_COMMAND} -E env
                    SDL_VIDEODRIVER=x11
                    LIBGL_ALWAYS_SOFTWARE=1
                    SDL_GPU_DRIVER=vulkan
                    ${XVFB_RUN_EXECUTABLE} -a
                    $<TARGET_FILE:rowl_player>
                    --project ${CMAKE_SOURCE_DIR}
                    --width 960 --height 540
                    --no-vsync
                    --gpu-smoke-test
            )
            set_tests_properties(rowl_player_gpu_msdf_smoke PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 30
                PASS_REGULAR_EXPRESSION "MSDF GPU text renderer initialized"
            )
        endif()
    endif()
    set_tests_properties(
        rowl_player_cli_invalid_width
        rowl_player_cli_missing_option_value
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            WILL_FAIL TRUE
            TIMEOUT 10
    )
endif()
