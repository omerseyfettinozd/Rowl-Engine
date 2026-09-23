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
option(ROWL_BUILD_TOOLS "Build host converter tools (rowl_oggenc, rowl_webp2png)" ${ROWL_RUNTIME_TESTS_DEFAULT})

if(ROWL_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(ROWL_BUILD_RUNTIME_TESTS)
    add_subdirectory(tests)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        # -u on every python gate (tur-7): ctest captures through a pipe,
        # where stdout is block-buffered; on a TIMEOUT kill the buffer dies
        # with the process and the hang point is lost. Unbuffered output
        # keeps every section marker visible exactly where the run stopped.
        # Pass/fail behavior is unchanged.
        add_test(
            NAME rowl_benchmark_tool_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_benchmark_tools.py
        )
        set_tests_properties(rowl_benchmark_tool_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_abi_tool_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_abi_tools.py
        )
        set_tests_properties(rowl_abi_tool_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_platform_gate_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_platform_gates.py
        )
        set_tests_properties(rowl_platform_gate_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_story_gate_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_story_gate.py
        )
        set_tests_properties(rowl_story_gate_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_aux_gate_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_aux_gate.py
        )
        set_tests_properties(rowl_aux_gate_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_release_package_tool_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_release_package.py
        )
        set_tests_properties(rowl_release_package_tool_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        add_test(
            NAME rowl_d18a_verify_depth_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_d18a_verify_depth.py
        )
        set_tests_properties(rowl_d18a_verify_depth_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        # D18b-i i18n docs-sync probu: kod gercegi (sema 1|2, zincir-bilincli
        # matchSupported, kirpmasiz etiket) ile BASELINE + LOCALIZATION_CONTRACT
        # iddialarini kilitler. Salt dosya okur, derleme gerektirmez.
        add_test(
            NAME rowl_d18bi_i18n_sync_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_d18bi_i18n_sync.py
        )
        set_tests_properties(rowl_d18bi_i18n_sync_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        # W8-b localization docs-sync probu: 8 belgelik sevk-gercegi
        # (Dilim-4 desk/pseudo/exchange, cogul-tablo, etiket kurali,
        # FILE_NOT_FOUND, desk delegasyonu, taraf kapsamlari, BASELINE
        # hucresi) ile kod capalarini kilitler. Salt dosya okur.
        add_test(
            NAME rowl_w8b_docs_sync_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_w8b_docs_sync.py
        )
        set_tests_properties(rowl_w8b_docs_sync_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        # D18b-ii platform/paket kilit problari: salt dosya okur (+ /tmp
        # fixture), derleme gerektirmez. Siralama alfabetiktir.
        add_test(
            NAME rowl_d18bii_abi_pin_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_d18bii_abi_pin.py
        )
        set_tests_properties(rowl_d18bii_abi_pin_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        add_test(
            NAME rowl_d18bii_packaging_guard_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_d18bii_packaging_guard.py
        )
        set_tests_properties(rowl_d18bii_packaging_guard_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )
        add_test(
            NAME rowl_d18bii_platform_lock_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_d18bii_platform_lock.py
        )
        set_tests_properties(rowl_d18bii_platform_lock_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        add_test(
            NAME rowl_package_determinism_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_package_determinism.py
        )
        set_tests_properties(rowl_package_determinism_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )
        # W8-c bulgu-1 kilidi: expansion-ratio gate probu (pack RAW-karari +
        # verifier 2048x-forge reddi). D18b-ii blogu emsali; package_* icinde
        # alfabetik (determinism < ratio_gate). zstd'siz yorumlayicida yalniz
        # pack-karari SKIP olur; verifier-aynasi sentetik tabanda kosar.
        add_test(
            NAME rowl_package_ratio_gate_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_package_ratio_gate.py
        )
        set_tests_properties(rowl_package_ratio_gate_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )
        # W8-f2 verifier-mirror kilidi: reader fail-closed aynasi (4 is:
        # overlapping-payload, 4096 path cap, kanonik duplicate,
        # manifest capraz-kontrol). Sentetik taban, derleme gerektirmez.
        add_test(
            NAME rowl_verifier_mirror_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_verifier_mirror.py
        )
        set_tests_properties(rowl_verifier_mirror_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )
        add_test(
            NAME rowl_media_format_gate_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_media_format_gate.py
        )
        set_tests_properties(rowl_media_format_gate_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )
        # W8-c bulgu-4 kilidi: dinamik tabloda RowlEngine_-disi T sembol
        # yok (9 internal helper gizli). Derlenmis lib yoksa prob SKIP verir.
        # ROWL_LIB_DIR derlenen agacin lib dizinini isaret eder (bayat
        # build/ golgelemesi olmaz).
        add_test(
            NAME rowl_no_internal_exports_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_no_internal_exports.py
        )
        set_tests_properties(rowl_no_internal_exports_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            ENVIRONMENT "ROWL_LIB_DIR=$<TARGET_FILE_DIR:RowlEngineCore>"
            TIMEOUT 60
        )
        if(ROWL_BUILD_TOOLS)
            add_test(
                NAME rowl_converter_tool_tests
                COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_converters.py
                    $<TARGET_FILE:rowl_oggenc>
                    $<TARGET_FILE:rowl_webp2png>
            )
            set_tests_properties(rowl_converter_tool_tests PROPERTIES
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                TIMEOUT 180
            )
        endif()
        add_test(
            NAME rowl_export_tool_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_export_game.py
        )
        set_tests_properties(rowl_export_tool_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_self_extracting_installer_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_self_extracting_installer.py
        )
        set_tests_properties(rowl_self_extracting_installer_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
        )
        add_test(
            NAME rowl_player_guide_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_player_guide.py
        )
        set_tests_properties(rowl_player_guide_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
        )
        add_test(
            NAME rowl_author_guide_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_author_guide.py
        )
        set_tests_properties(rowl_author_guide_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 180
        )
        add_test(
            NAME rowl_golden_manifest_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_golden_manifest.py
        )
        set_tests_properties(rowl_golden_manifest_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 15
        )
        add_test(
            NAME rowl_editor_scale_fixture_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_editor_scale_fixture.py
        )
        set_tests_properties(rowl_editor_scale_fixture_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 30
        )
        add_test(
            NAME rowl_font_license_glyph_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_font_license_glyph.py
        )
        set_tests_properties(rowl_font_license_glyph_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 60
        )
        add_test(
            NAME rowl_third_party_license_tests
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_third_party_licenses.py
        )
        set_tests_properties(rowl_third_party_license_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
        )
    endif()

    find_program(DOTNET_EXECUTABLE dotnet)
    if(DOTNET_EXECUTABLE)
        add_test(
            NAME rowl_editor_headless_tests
            COMMAND ${DOTNET_EXECUTABLE} test
                ${CMAKE_SOURCE_DIR}/editor/Tests/RowlEngine.Editor.Tests.csproj
                --configuration Debug
                --logger "console;verbosity=normal"
        )
        set_tests_properties(rowl_editor_headless_tests PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
            # The editor drives the native engine for audio-observer checks;
            # without a dummy driver headless machines report no device and
            # edge-triggered device tests observe a stale transition. The
            # video driver is forced for the same reason: the offscreen
            # engine must initialise with no display server present.
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy;MSBuildEnableWorkloadResolver=false"
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
    add_test(
        NAME rowl_player_cli_invalid_slot
        COMMAND $<TARGET_FILE:rowl_player> --slot 99
    )

    # The First Light sample packaged as a portable release and played
    # through the release contract (packaged VFS, offscreen smoke frame).
    # Runs everywhere: no window, no GPU, no installed toolchain needed.
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME rowl_demo_first_light_packaged
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_demo_packaged.py
                $<TARGET_FILE:rowl_player>
                $<TARGET_FILE:RowlEngineCore>
                ${CMAKE_SOURCE_DIR}/samples/first_light
        )
        set_tests_properties(rowl_demo_first_light_packaged PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
            # Offscreen smoke frame: must initialise with no display server.
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
        )
        add_test(
            NAME rowl_demo_second_signal_packaged
            COMMAND ${Python3_EXECUTABLE} -u ${CMAKE_SOURCE_DIR}/tests/test_demo_packaged.py
                $<TARGET_FILE:rowl_player>
                $<TARGET_FILE:RowlEngineCore>
                ${CMAKE_SOURCE_DIR}/samples/second_signal
        )
        set_tests_properties(rowl_demo_second_signal_packaged PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            TIMEOUT 120
            # Offscreen smoke frame: must initialise with no display server.
            ENVIRONMENT "SDL_AUDIODRIVER=dummy;SDL_VIDEODRIVER=dummy"
        )
    endif()

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
        rowl_player_cli_invalid_slot
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            WILL_FAIL TRUE
            TIMEOUT 10
    )
endif()
