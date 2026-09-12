/**
 * test_demo_second_signal.cpp — the samples/second_signal game project played
 * through the real pipeline: story-driven Lua variables, BGM audio, choice
 * branching, and live condition evaluation.
 */
#include "rowl_test_harness.hpp"

void test_demo_second_signal() {
    TEST_SECTION("Second-Signal Sample Project (audio + Lua, real pipeline)");

    namespace fs = std::filesystem;
    const auto sourceProject = fs::path("samples/second_signal");
    const auto projectRoot = fs::temp_directory_path() /
        ("rowl_golden_project_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code copyError;
    fs::copy(sourceProject, projectRoot, fs::copy_options::recursive, copyError);
    if (copyError) {
        std::cerr << "Could not isolate the Golden Project fixture: "
                  << copyError.message() << std::endl;
        exit(1);
    }

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1) {
        std::cerr << "C-API engine init failed for the second demo project" << std::endl;
        exit(1);
    }

    RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 1) {
        std::cerr << "Second demo story graph did not auto-load on project mount: "
                  << RowlEngine_GetLastStoryGraphError(handle) << std::endl;
        exit(1);
    }
    // The opening node sets this itself via a variable component — no C-API
    // SetVariable call is involved, so this proves story-driven Lua state.
    if (std::string(RowlEngine_GetVariable(handle, "station_awake")) != "yes") {
        std::cerr << "Second demo opening node did not set its story variable" << std::endl;
        exit(1);
    }
    if (std::string(RowlEngine_GetVariable(handle, "golden_script_entered")) != "yes" ||
        RowlEngine_IsTransitionActive(handle) != 1) {
        std::cerr << "Golden Project did not execute its script/cinematic components" << std::endl;
        exit(1);
    }
    if (RowlEngine_EvaluateCondition(handle, "signal_count >= 1") != 0) {
        std::cerr << "Lua condition true before the story earned it" << std::endl;
        exit(1);
    }
    TEST_PASS("Second demo opens with story-set Lua state");

    RowlEngine_Step(handle, 0.016f);
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsRunning(handle) != 1 || RowlEngine_IsBgmPlaying(handle) != 1) {
        std::cerr << "Second demo BGM is not playing after stepping" << std::endl;
        exit(1);
    }
    uint32_t frameW = 0;
    uint32_t frameH = 0;
    if (!RowlEngine_GetPixelBuffer(handle, &frameW, &frameH) || frameW == 0 || frameH == 0 ||
        RowlEngine_GetTextureCacheTextureCount(handle) < 2) {
        std::cerr << "Second demo opening node produced no rendered frame with assets" << std::endl;
        exit(1);
    }
    TEST_PASS("Second demo plays BGM and renders its opening frame");

    if (RowlEngine_SelectChoice(handle, "go_answer") != 1 ||
        RowlEngine_GetCurrentNodeId(handle) != 2) {
        std::cerr << "Second demo choice did not reach the answer node" << std::endl;
        exit(1);
    }
    if (RowlEngine_SaveGameSlot(handle, 0) != 1 || RowlEngine_HasSaveSlot(handle, 0) != 1) {
        std::cerr << "Golden Project could not persist its answer-node state" << std::endl;
        exit(1);
    }
    // Node 2 adds signal_count via a variable component; the gated choice on
    // this node requires signal_count >= 1 in Lua.
    // Contract lock: the add operation stores engine-canonical double
    // formatting ("1.000000", never "1"); numeric consumers must compare
    // numerically. See RowlEngine_GetVariable docs.
    if (std::string(RowlEngine_GetVariable(handle, "signal_count")) != "1.000000" ||
        std::stod(RowlEngine_GetVariable(handle, "signal_count")) != 1.0 ||
        RowlEngine_EvaluateCondition(handle, "signal_count >= 1") != 1) {
        std::cerr << "Second demo answer node did not accumulate Lua state" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "go_code") != 1 ||
        RowlEngine_GetCurrentNodeId(handle) != 4 ||
        std::string(RowlEngine_GetSpeaker(handle)) != "Margot") {
        std::cerr << "Second demo gated choice did not reach the code ending" << std::endl;
        exit(1);
    }
    TEST_PASS("Second demo Lua-gated choice reaches the code ending (node #4)");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);

    handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1) {
        std::cerr << "C-API engine restart failed for the Golden Project" << std::endl;
        exit(1);
    }
    RowlEngine_SetProjectDirectory(handle, projectRoot.string().c_str());
    if (RowlEngine_LoadGameSlot(handle, 0) != 1 ||
        RowlEngine_GetCurrentNodeId(handle) != 2 ||
        std::stod(RowlEngine_GetVariable(handle, "signal_count")) != 1.0) {
        std::cerr << "Golden Project did not restore its saved state after process restart" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "go_code") != 1 ||
        RowlEngine_Rewind(handle, 1) != 1 ||
        RowlEngine_GetCurrentNodeId(handle) != 2) {
        std::cerr << "Golden Project rewind did not restore the saved answer node" << std::endl;
        exit(1);
    }
    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    fs::remove_all(projectRoot, copyError);
    if (copyError) {
        std::cerr << "Could not clean the isolated Golden Project fixture: "
                  << copyError.message() << std::endl;
        exit(1);
    }
    TEST_PASS("Golden Project save/load/restart/rewind contract passes through the C API");
}
