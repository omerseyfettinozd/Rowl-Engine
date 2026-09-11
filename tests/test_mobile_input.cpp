/**
 * test_mobile_input.cpp — Mobile touch-input mapping.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_mobile_input() {
    TEST_SECTION("Mobile Multi-Touch Subsystem");

    // Touch target validity test
    if (!Rowl::Platform::MobileInput::isTouchTargetValid(48.0f, 48.0f)) exit(1);
    if (!Rowl::Platform::MobileInput::isTouchTargetValid(64.0f, 64.0f)) exit(1);
    if (Rowl::Platform::MobileInput::isTouchTargetValid(32.0f, 48.0f)) exit(1);
    TEST_PASS("Mobile Accessibility Minimum Touch Target (>= 48x48 dp)");

    // Simulated SDL3 Touch Event Processing
    SDL_Event touchEvent;
    touchEvent.type = SDL_EVENT_FINGER_DOWN;
    touchEvent.tfinger.x = 0.5f; // 50% of 1920 = 960
    touchEvent.tfinger.y = 0.5f; // 50% of 1080 = 540
    touchEvent.tfinger.fingerID = 10;

    Rowl::Platform::InputEvent outEvent;
    bool procOk = Rowl::Platform::MobileInput::processSdlEvent(touchEvent, outEvent);
    if (!procOk || outEvent.type != Rowl::Platform::InputEventType::TapDown ||
        std::abs(outEvent.x - 960.0f) > 0.01f || std::abs(outEvent.y - 540.0f) > 0.01f) {
        std::cerr << "Touch event processing mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("SDL3 Touch Coordinate Normalization to 1920x1080 Canvas");

    if (Rowl::Platform::MobileInput::classifyTouchGesture(900.0f, 540.0f, 700.0f, 540.0f, 1920.0f, 1080.0f)
            != Rowl::Platform::InputEventType::SwipeForward ||
        Rowl::Platform::MobileInput::classifyTouchGesture(700.0f, 540.0f, 900.0f, 540.0f, 1920.0f, 1080.0f)
            != Rowl::Platform::InputEventType::SwipeBack ||
        Rowl::Platform::MobileInput::classifyTouchGesture(900.0f, 540.0f, 920.0f, 550.0f, 1920.0f, 1080.0f)
            != Rowl::Platform::InputEventType::Tap) {
        std::cerr << "Touch gesture classification mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Touch Tap and Horizontal Swipe Classification");
}
