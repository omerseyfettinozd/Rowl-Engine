/**
 * test_main.cpp — Runner entry point; executes subsystems in order.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"
#ifdef _WIN32
// Debug CRT + loader faults park on a modal dialog by default; on headless
// CI that burns the whole ctest timeout with zero output (tur-6). Route
// every report to stderr and disable the fault dialog instead.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <crtdbg.h>
#include <cstdint>
#include <cstdio>
#include <eh.h>
#include <exception>

// Tur-7: a bare "abort() has been called" with no report text tells us
// nothing about WHERE. These test-only handlers name the mechanism on
// stderr before dying: Watson (invalid CRT parameter), terminate
// (exception escaping noexcept / unhandled), or SEH (access violation,
// breakpoint). Production code paths are untouched.
static void RowlTestInvalidParameterHandler(const wchar_t* expression,
                                            const wchar_t* function,
                                            const wchar_t* file,
                                            unsigned int line,
                                            uintptr_t /*reserved*/) {
    std::fwprintf(stderr, L"\n[ROWL-TEST-DIAG] invalid CRT parameter: expr='%ls' function='%ls' file='%ls' line=%u\n",
                  expression ? expression : L"?", function ? function : L"?",
                  file ? file : L"?", line);
    std::fflush(stderr);
    abort();
}

static void RowlTestTerminateHandler() {
    std::fprintf(stderr, "\n[ROWL-TEST-DIAG] std::terminate called (exception escaped noexcept/unhandled)\n");
    std::fflush(stderr);
    abort();
}

static LONG WINAPI RowlTestSehFilter(EXCEPTION_POINTERS* info) {
    std::fprintf(stderr, "\n[ROWL-TEST-DIAG] unhandled SEH 0x%08lX at %p\n",
                 (unsigned long)info->ExceptionRecord->ExceptionCode,
                 info->ExceptionRecord->ExceptionAddress);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    _set_invalid_parameter_handler(RowlTestInvalidParameterHandler);
    std::set_terminate(RowlTestTerminateHandler);
    SetUnhandledExceptionFilter(RowlTestSehFilter);
#endif
    // Flush every insertion: on a timeout kill, ctest prints what the pipe
    // captured, so progressive output turns a zero-output kill into a
    // pointer at the hanging section.
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::string benchmarkJsonPath;
    std::string goldenJsonPath;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--benchmark-json" && index + 1 < argc) {
            benchmarkJsonPath = argv[++index];
        } else if (argument == "--golden-benchmark-json" && index + 1 < argc) {
            goldenJsonPath = argv[++index];
        } else {
            std::cerr << "Usage: rowl_tests [--benchmark-json <output.json>] [--golden-benchmark-json <output.json>]"
                      << std::endl;
            return 1;
        }
    }
    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🚀 ROWL ENGINE COMPREHENSIVE NATIVE UNIT TEST SUITE 🚀" << std::endl;
    std::cout << "=======================================================" << std::endl;

    test_logger_timestamp();
    test_aspect_guardian();
    test_msdf_renderer();
    test_game_state();
    test_audio_engine();
    test_audio_device_recovery();
    test_audio_streaming();
    test_audio_mixer();
    test_lua_sandbox();
    test_mobile_input();
    test_platform_host();
    test_c_api_contract();
    test_vfs_security();
    test_native_c_api();
    test_single_engine_image();
    test_game_object_component_system();
    test_pixel_pitch();
    test_window_input_routing();
    test_runtime_context_and_diagnostics();
    test_crash_log();
    test_story_graph_parser();
    test_graph_vnext();
    test_localization();
    test_markup_parser();
    test_text_shaping();
    test_accessibility();
    test_character_layers();
    test_prefetch_chapters();
    test_converter_provenance();
    test_camera_and_transition_pipeline();
    test_native_performance_benchmarks(benchmarkJsonPath);
    test_character_layer_benchmarks();
    test_golden_project_benchmarks(goldenJsonPath);
    test_hardening_and_reliability();
    test_rc_soak_and_data_safety();
    test_save_slot_and_package_fuzz();
    test_demo_first_light();
    test_demo_second_signal();

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🎉 ALL UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    return 0;
}
