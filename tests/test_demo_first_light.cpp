/**
 * test_demo_first_light.cpp — the samples/first_light game project played
 * through the real pipeline: VFS mount, story-graph load, stepping,
 * rendering, and choice input reaching both endings.
 */
#include "rowl_test_harness.hpp"

void test_demo_first_light() {
    TEST_SECTION("First-Light Sample Project (real game content, real pipeline)");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 1280, 720, 0) != 1) {
        std::cerr << "C-API engine init failed for the demo project" << std::endl;
        exit(1);
    }

    // Standard project layout: manifest + Assets/json/full_story_graph.json
    // auto-load on mount, no explicit story path needed.
    RowlEngine_SetProjectDirectory(handle, "samples/first_light");
    if (RowlEngine_GetCurrentNodeId(handle) != 1) {
        std::cerr << "Demo story graph did not auto-load on project mount: "
                  << RowlEngine_GetLastStoryGraphError(handle) << std::endl;
        exit(1);
    }
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Evelyn" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Demo opening node has no speaker or dialogue" << std::endl;
        exit(1);
    }
    TEST_PASS("Demo project mounts and opens on node #1");

    RowlEngine_Step(handle, 0.016f);
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "Demo engine stopped while stepping the opening node" << std::endl;
        exit(1);
    }
    uint32_t frameW = 0;
    uint32_t frameH = 0;
    if (!RowlEngine_GetPixelBuffer(handle, &frameW, &frameH) || frameW == 0 || frameH == 0) {
        std::cerr << "Demo opening node produced no rendered frame" << std::endl;
        exit(1);
    }
    // Background + character textures must actually resolve through the
    // sample project's VFS mounts — a frame with no assets is a false pass.
    if (RowlEngine_GetTextureCacheTextureCount(handle) < 2) {
        std::cerr << "Demo opening node did not load its background/character textures" << std::endl;
        exit(1);
    }
    TEST_PASS("Demo opening node steps and renders a frame");

    // An unknown choice must be rejected without leaving the node.
    if (RowlEngine_SelectChoice(handle, "go_nowhere") != 0 ||
        RowlEngine_GetCurrentNodeId(handle) != 1) {
        std::cerr << "Demo accepted an invalid choice" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "go_ridge") != 1 ||
        RowlEngine_GetCurrentNodeId(handle) != 3) {
        std::cerr << "Demo choice did not reach the ridge ending" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Margot") {
        std::cerr << "Ridge ending has the wrong speaker" << std::endl;
        exit(1);
    }
    TEST_PASS("Demo choice input reaches the ridge ending (node #3)");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    TEST_PASS("First-Light demo plays end to end through the C API");
}
