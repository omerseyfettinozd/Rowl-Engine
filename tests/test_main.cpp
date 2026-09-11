/**
 * test_main.cpp — Runner entry point; executes subsystems in order.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

int main(int argc, char* argv[]) {
    std::string benchmarkJsonPath;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--benchmark-json" && index + 1 < argc) {
            benchmarkJsonPath = argv[++index];
        } else {
            std::cerr << "Usage: rowl_tests [--benchmark-json <output.json>]" << std::endl;
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
    test_lua_sandbox();
    test_mobile_input();
    test_vfs_security();
    test_native_c_api();
    test_game_object_component_system();
    test_window_input_routing();
    test_runtime_context_and_diagnostics();
    test_camera_and_transition_pipeline();
    test_native_performance_benchmarks(benchmarkJsonPath);
    test_hardening_and_reliability();

    std::cout << "\n=======================================================" << std::endl;
    std::cout << "🎉 ALL UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY! 🎉" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
    return 0;
}
