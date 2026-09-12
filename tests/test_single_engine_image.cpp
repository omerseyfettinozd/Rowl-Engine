/**
 * test_single_engine_image.cpp — Faz 2.1 split-brain regression guard.
 *
 * The engine must exist exactly once per binary: the C++ API and the C API
 * share one copy of every process-wide static (notably the SDL event
 * dispatcher). This test registers a window id through the C++ API, pumps
 * the queue by stepping an engine created through the C API, then reads the
 * event back through the C++ API. With two engine copies the C-API pump
 * drains the process SDL queue and drops the foreign-window event, so the
 * C++ read comes back empty and this test fails.
 */
#include "rowl_test_harness.hpp"

void test_single_engine_image() {
    TEST_SECTION("Single Engine Image (C-API and C++ Share One Static State)");

    constexpr uint32_t kProbeWindowId = 0x524F574Cu; // "ROWL", never a real SDL id
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(kProbeWindowId)) {
        std::cerr << "Could not register single-image probe window" << std::endl;
        exit(1);
    }

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 320, 180, 0) != 1) {
        std::cerr << "C-API engine init failed in single-image probe" << std::endl;
        exit(1);
    }

    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.key = SDLK_F6;
    keyEvent.key.windowID = kProbeWindowId;
    if (!SDL_PushEvent(&keyEvent)) {
        std::cerr << "Could not enqueue single-image probe event" << std::endl;
        exit(1);
    }

    // Step through the C API: its pump must route — not swallow — the event
    // addressed to the C++-registered window.
    RowlEngine_Step(handle, 0.016f);
    RowlEngine_Step(handle, 0.016f);

    bool probeDelivered = false;
    for (const SDL_Event& event : Rowl::Platform::SdlEventDispatcher::takeEvents(kProbeWindowId)) {
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F6) {
            probeDelivered = true;
        }
    }
    if (!probeDelivered || RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "C-API pump and C++ dispatcher do not share one engine image" << std::endl;
        exit(1);
    }

    Rowl::Platform::SdlEventDispatcher::unregisterWindow(kProbeWindowId);
    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    TEST_PASS("C-API pump delivers events to C++-registered windows (one engine image)");
}
