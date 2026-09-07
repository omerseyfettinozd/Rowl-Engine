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

add_test(
    NAME rowl_player_cli_help
    COMMAND $<TARGET_FILE:rowl_player> --help
)
set_tests_properties(rowl_player_cli_help PROPERTIES
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    TIMEOUT 10
)
