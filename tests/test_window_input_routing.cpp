/**
 * test_window_input_routing.cpp — SDL visible-window event dispatching.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_window_input_routing() {
    TEST_SECTION("SDL Visible-Window Event Dispatching");

    // Explicit local VFS mounted at the repo root, mirroring what the removed
    // process-global singleton carried at this point in the suite.
    Rowl::VFS::VFSManager routingVfs;
    routingVfs.remountProject(std::filesystem::current_path().string());
    Rowl::Render::Window window(&routingVfs);
    if (!window.initializeOffscreen(320, 180)) {
        std::cerr << "Could not initialize offscreen window for input routing test" << std::endl;
        exit(1);
    }

    constexpr uint32_t windowA = 101;
    constexpr uint32_t windowB = 202;
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(windowA) ||
        !Rowl::Platform::SdlEventDispatcher::registerWindow(windowB)) {
        std::cerr << "Could not register synthetic visible SDL windows" << std::endl;
        exit(1);
    }
    std::atomic<bool> foreignThreadRegistered{true};
    std::thread foreignThread([&] {
        foreignThreadRegistered.store(Rowl::Platform::SdlEventDispatcher::registerWindow(303));
    });
    foreignThread.join();
    if (foreignThreadRegistered.load()) {
        std::cerr << "SDL dispatcher accepted a visible window from a second event thread" << std::endl;
        exit(1);
    }

    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.key = SDLK_F5;
    keyEvent.key.windowID = windowA;
    if (!SDL_PushEvent(&keyEvent)) {
        std::cerr << "Could not enqueue SDL key event for input routing test" << std::endl;
        exit(1);
    }
    SDL_Event pointerEvent{};
    pointerEvent.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    pointerEvent.button.button = SDL_BUTTON_LEFT;
    pointerEvent.button.windowID = windowB;
    pointerEvent.button.x = 42.0f;
    pointerEvent.button.y = 24.0f;
    if (!SDL_PushEvent(&pointerEvent)) {
        std::cerr << "Could not enqueue SDL pointer event for input routing test" << std::endl;
        exit(1);
    }

    SDL_Event touchEvent{};
    touchEvent.type = SDL_EVENT_FINGER_DOWN;
    touchEvent.tfinger.windowID = windowB;
    touchEvent.tfinger.fingerID = 77;
    touchEvent.tfinger.x = 0.5f;
    touchEvent.tfinger.y = 0.25f;
    if (!SDL_PushEvent(&touchEvent)) {
        std::cerr << "Could not enqueue SDL touch event for input routing test" << std::endl;
        exit(1);
    }

    SDL_Event resizeEvent{};
    resizeEvent.type = SDL_EVENT_WINDOW_RESIZED;
    resizeEvent.window.windowID = windowB;
    resizeEvent.window.data1 = 800;
    resizeEvent.window.data2 = 600;
    SDL_PushEvent(&resizeEvent);

    SDL_Event closeEvent{};
    closeEvent.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    closeEvent.window.windowID = windowA;
    SDL_PushEvent(&closeEvent);

    const auto eventsA = Rowl::Platform::SdlEventDispatcher::takeEvents(windowA);
    const auto eventsB = Rowl::Platform::SdlEventDispatcher::takeEvents(windowB);
    if (eventsA.size() != 2 || eventsA[0].type != SDL_EVENT_KEY_DOWN ||
        eventsA[1].type != SDL_EVENT_WINDOW_CLOSE_REQUESTED || eventsB.size() != 3 ||
        eventsB[0].type != SDL_EVENT_MOUSE_BUTTON_DOWN || eventsB[1].type != SDL_EVENT_FINGER_DOWN ||
        eventsB[2].type != SDL_EVENT_WINDOW_RESIZED ||
        std::abs(eventsB[0].button.x - 42.0f) > 0.001f || std::abs(eventsB[0].button.y - 24.0f) > 0.001f) {
        std::cerr << "SDL dispatcher did not isolate target window events" << std::endl;
        exit(1);
    }

    SDL_Event quitEvent{};
    quitEvent.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitEvent);
    const auto quitA = Rowl::Platform::SdlEventDispatcher::takeEvents(windowA);
    const auto quitB = Rowl::Platform::SdlEventDispatcher::takeEvents(windowB);
    if (quitA.size() != 1 || quitB.size() != 1 ||
        quitA[0].type != SDL_EVENT_QUIT || quitB[0].type != SDL_EVENT_QUIT) {
        std::cerr << "SDL process quit was not broadcast to every visible runtime" << std::endl;
        exit(1);
    }

    Rowl::Platform::SdlEventDispatcher::unregisterWindow(windowB);
    pointerEvent.button.windowID = windowB;
    SDL_PushEvent(&pointerEvent);
    if (!Rowl::Platform::SdlEventDispatcher::takeEvents(windowA).empty()) {
        std::cerr << "Late event for an unregistered window leaked to another runtime" << std::endl;
        exit(1);
    }
    Rowl::Platform::SdlEventDispatcher::unregisterWindow(windowA);

    // Viewport mapping lives on the render boundary: canvas taps resolve to
    // story coordinates, margin taps report a bezel hit for the caller to
    // consume without advancing the story.
    {
        float vx = 0.0f, vy = 0.0f;
        bool bezel = true;
        if (!window.mapPhysicalToVirtual(160.0f, 90.0f, 1920, 1080, vx, vy, bezel) || bezel) {
            std::cerr << "Offscreen canvas center was not mapped to the story canvas" << std::endl;
            exit(1);
        }
        if (std::abs(vx - 960.0f) > 0.5f || std::abs(vy - 540.0f) > 0.5f) {
            std::cerr << "Canvas center mapping mismatch: got (" << vx << ", " << vy << ")" << std::endl;
            exit(1);
        }

        float bx = -1.0f, by = -1.0f;
        bool bezelHit = false;
        if (!window.mapPhysicalToVirtual(5.0f, 90.0f, 1080, 1920, bx, by, bezelHit) || !bezelHit) {
            std::cerr << "Pillarbox margin tap was not reported as a bezel hit" << std::endl;
            exit(1);
        }

        float px = 0.0f, py = 0.0f;
        bool pillarBezel = true;
        if (!window.mapPhysicalToVirtual(160.0f, 90.0f, 1080, 1920, px, py, pillarBezel) || pillarBezel) {
            std::cerr << "Pillarbox canvas tap was misclassified as a bezel hit" << std::endl;
            exit(1);
        }
        // Integer-truncated pillarbox: width int(180 * 1080/1920) = 101,
        // x = (320 - 101) / 2 = 109, scale 180/1920 → (160-109)/scale = 544.
        if (std::abs(px - 544.0f) > 0.5f || std::abs(py - 960.0f) > 0.5f) {
            std::cerr << "Pillarbox canvas mapping mismatch: got (" << px << ", " << py << ")" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Render Boundary Maps Canvas Taps and Reports Bezel Hits");
    window.shutdown();
    TEST_PASS("SDL dispatcher isolates visible runtime events and broadcasts process quit");
}
