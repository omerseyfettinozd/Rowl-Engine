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

    // MS-6: SDL-key to player-action translation is a pure function, pinned
    // here without a visible window. Escape/P pause (never quit), digits pick
    // quick slots, arrows drive menu nav, Space/Enter advance.
    {
        using Event = Rowl::Platform::RuntimeInputEvent;
        using Type = Rowl::Platform::RuntimeInputEvent::Type;
        auto expect = [&](uint32_t key, Type type, int32_t slot = 0) {
            Event event{Type::Advance};
            event.slot = -1;
            if (!Rowl::Render::Window::mapKeyToRuntimeInput(key, event) ||
                event.type != type || event.slot != slot) {
                std::cerr << "MS-6: key mapping mismatch for key " << key << std::endl;
                exit(1);
            }
        };
        expect(SDLK_ESCAPE, Type::PauseToggle);
        expect(SDLK_P, Type::PauseToggle);
        expect(SDLK_SPACE, Type::Advance);
        expect(SDLK_RETURN, Type::Advance);
        expect(SDLK_KP_ENTER, Type::Advance);
        expect(SDLK_UP, Type::MenuUp);
        expect(SDLK_DOWN, Type::MenuDown);
        expect(SDLK_LEFT, Type::MenuLeft);
        expect(SDLK_RIGHT, Type::MenuRight);
        expect(SDLK_F5, Type::QuickSave);
        expect(SDLK_F9, Type::QuickLoad);
        expect(SDLK_BACKSPACE, Type::Rewind);
        expect(SDLK_Z, Type::Rewind);
        expect(SDLK_0, Type::SelectSlot, 0);
        expect(SDLK_3, Type::SelectSlot, 3);
        expect(SDLK_9, Type::SelectSlot, 9);
        Event unmapped{Type::Advance};
        if (Rowl::Render::Window::mapKeyToRuntimeInput(SDLK_F1, unmapped) ||
            Rowl::Render::Window::mapKeyToRuntimeInput(SDLK_A, unmapped)) {
            std::cerr << "MS-6: unrelated keys must not map to player actions" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("MS-6 Player Key Map (pause, slots, menu nav, advance)");
}

// #16 [HIGH] dispatcher+consumer gap: MOUSE_MOTION/MOUSE_WHEEL/TEXT_INPUT
// never routed (targetWindowId nullopt-drop) and KEY_UP/BUTTON_UP/
// FINGER_MOTION queued but branchless in Window::pollEvents. End-to-end pin:
// a real visible window, synthetic SDL events behind its dispatch id, one
// pollEvents step each — every event must surface observably through the
// existing input-handler channel with its payload intact.
void test_window_input_routing_release_motion_wheel_text() {
    TEST_SECTION("Input Release/Motion/Wheel/Text Conscious Consumption (#16)");

    Rowl::VFS::VFSManager inputVfs;
    inputVfs.remountProject(std::filesystem::current_path().string());
    Rowl::Render::Window window(&inputVfs);
    if (!window.initialize("rowl-input-lock-16", 320, 180)) {
        std::cerr << "Could not create visible window for release/motion/wheel/text test" << std::endl;
        exit(1);
    }
    const uint32_t wid = window.eventWindowId();
    if (wid == 0) {
        std::cerr << "Visible window owns no dispatch registration" << std::endl;
        exit(1);
    }

    using Event = Rowl::Platform::RuntimeInputEvent;
    using Type = Rowl::Platform::RuntimeInputEvent::Type;
    std::vector<Event> seen;
    window.setInputHandler([&](const Event& event) { seen.push_back(event); });

    auto step = [&]() {
        bool shouldQuit = false;
        window.pollEvents(shouldQuit);
        if (shouldQuit) {
            std::cerr << "Unexpected quit while pumping synthetic input events" << std::endl;
            exit(1);
        }
    };
    auto pushOrDie = [&](SDL_Event event, const char* what) {
        if (!SDL_PushEvent(&event)) {
            std::cerr << "Could not push synthetic " << what << std::endl;
            exit(1);
        }
    };
    // Drain real window-show/resize noise before pinning expectations.
    step();
    seen.clear();

    // Payload-exact "contains" (not bare counts): a live desktop may interleave
    // genuine cursor motion while the suite runs; under xvfb there is none.
    auto contains = [&](Type type, float x, float y, uint32_t key, const std::string& text) {
        for (const auto& event : seen) {
            if (event.type != type) continue;
            if (type == Type::KeyUp && event.key != key) continue;
            if (type == Type::TextInput && event.text != text) continue;
            if ((type == Type::PointerUp || type == Type::PointerMotion || type == Type::Scroll) &&
                (std::abs(event.x - x) > 0.001f || std::abs(event.y - y) > 0.001f)) continue;
            return true;
        }
        return false;
    };
    auto containsType = [&](Type type) {
        for (const auto& event : seen) {
            if (event.type == type) return true;
        }
        return false;
    };

    // 1. KEY_UP (mapped press-key Space): release is observable with keycode.
    {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_UP;
        event.key.key = SDLK_SPACE;
        event.key.windowID = wid;
        pushOrDie(event, "KEY_UP");
        seen.clear();
        step();
        if (!contains(Type::KeyUp, 0.0f, 0.0f, static_cast<uint32_t>(SDLK_SPACE), "")) {
            std::cerr << "Synthetic KEY_UP was not consciously consumed" << std::endl;
            exit(1);
        }
    }

    // 2. BUTTON_UP left: PointerUp with position.
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.windowID = wid;
        event.button.x = 10.0f;
        event.button.y = 20.0f;
        pushOrDie(event, "BUTTON_UP");
        seen.clear();
        step();
        if (!contains(Type::PointerUp, 10.0f, 20.0f, 0, "")) {
            std::cerr << "Synthetic BUTTON_UP was not consciously consumed" << std::endl;
            exit(1);
        }
    }

    // 3. BUTTON_UP non-left stays unconsumed (mirrors the BUTTON_DOWN guard).
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_RIGHT;
        event.button.windowID = wid;
        event.button.x = 10.0f;
        event.button.y = 20.0f;
        pushOrDie(event, "BUTTON_UP/right");
        seen.clear();
        step();
        if (containsType(Type::PointerUp)) {
            std::cerr << "Non-left BUTTON_UP must not produce PointerUp" << std::endl;
            exit(1);
        }
    }

    // 4. MOUSE_MOTION: hover position through the input channel.
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = wid;
        event.motion.x = 30.0f;
        event.motion.y = 40.0f;
        pushOrDie(event, "MOUSE_MOTION");
        seen.clear();
        step();
        if (!contains(Type::PointerMotion, 30.0f, 40.0f, 0, "")) {
            std::cerr << "Synthetic MOUSE_MOTION was not consciously consumed" << std::endl;
            exit(1);
        }
    }

    // 5. MOUSE_WHEEL normal: scroll deltas pass through unchanged.
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = wid;
        event.wheel.x = 0.0f;
        event.wheel.y = 1.0f;
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        pushOrDie(event, "MOUSE_WHEEL");
        seen.clear();
        step();
        if (!contains(Type::Scroll, 0.0f, 1.0f, 0, "")) {
            std::cerr << "Synthetic MOUSE_WHEEL was not consciously consumed" << std::endl;
            exit(1);
        }
    }

    // 6. MOUSE_WHEEL flipped (natural): deltas are normalized back.
    {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = wid;
        event.wheel.x = 0.0f;
        event.wheel.y = 1.0f;
        event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        pushOrDie(event, "MOUSE_WHEEL/flipped");
        seen.clear();
        step();
        if (!contains(Type::Scroll, 0.0f, -1.0f, 0, "")) {
            std::cerr << "Flipped MOUSE_WHEEL deltas were not normalized" << std::endl;
            exit(1);
        }
    }

    // 7. TEXT_INPUT: committed UTF-8 text rides along.
    {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.windowID = wid;
        event.text.text = "a";
        pushOrDie(event, "TEXT_INPUT");
        seen.clear();
        step();
        if (!contains(Type::TextInput, 0.0f, 0.0f, 0, "a")) {
            std::cerr << "Synthetic TEXT_INPUT was not consciously consumed" << std::endl;
            exit(1);
        }
    }

    // 8. FINGER_MOTION: viewport-relative physical pixels, like DOWN/UP.
    {
        SDL_Event down{};
        down.type = SDL_EVENT_FINGER_DOWN;
        down.tfinger.windowID = wid;
        down.tfinger.fingerID = 9;
        down.tfinger.x = 0.5f;
        down.tfinger.y = 0.25f;
        pushOrDie(down, "FINGER_DOWN");
        seen.clear();
        step();

        SDL_Event motion{};
        motion.type = SDL_EVENT_FINGER_MOTION;
        motion.tfinger.windowID = wid;
        motion.tfinger.fingerID = 9;
        motion.tfinger.x = 0.5f;
        motion.tfinger.y = 0.25f;
        pushOrDie(motion, "FINGER_MOTION");
        seen.clear();
        step();
        // Viewport-relative physical pixels against the LIVE surface size
        // (a real window manager may resize after creation — see the
        // initial drain above — so never hardcode 320x180 here).
        const float wantX = 0.5f * static_cast<float>(window.getWidth());
        const float wantY = 0.25f * static_cast<float>(window.getHeight());
        if (!contains(Type::PointerMotion, wantX, wantY, 0, "")) {
            std::cerr << "Synthetic FINGER_MOTION was not consciously consumed" << std::endl;
            exit(1);
        }

        // Clean the tracked touch without emitting (CANCELED path, no input).
        SDL_Event cancel{};
        cancel.type = SDL_EVENT_FINGER_CANCELED;
        cancel.tfinger.windowID = wid;
        cancel.tfinger.fingerID = 9;
        pushOrDie(cancel, "FINGER_CANCELED");
        seen.clear();
        step();
        if (!seen.empty()) {
            std::cerr << "FINGER_CANCELED must stay input-silent" << std::endl;
            exit(1);
        }
    }

    window.shutdown();
    TEST_PASS("Releases, motion, wheel and text are consciously consumed (#16)");
}
