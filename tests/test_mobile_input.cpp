/**
 * test_mobile_input.cpp — Mobile touch-input mapping.
 * Split from main_test_runner.cpp; Faz 4.5 Dilim 4: the dead stateless
 * processSdlEvent SDL switch is deleted, so this TU pins the single live
 * touch path (viewport-relative classification + touch-target validity).
 * Per-event DOWN/UP pairing lives in Window::pollEvents and is covered by
 * test_window_input_routing.cpp's dispatcher isolation test.
 */
#include "rowl_test_harness.hpp"

void test_mobile_input() {
    TEST_SECTION("Mobile Multi-Touch Subsystem");

    // Touch target validity test
    if (!Rowl::Platform::MobileInput::isTouchTargetValid(48.0f, 48.0f)) exit(1);
    if (!Rowl::Platform::MobileInput::isTouchTargetValid(64.0f, 64.0f)) exit(1);
    if (Rowl::Platform::MobileInput::isTouchTargetValid(32.0f, 48.0f)) exit(1);
    TEST_PASS("Mobile Accessibility Minimum Touch Target (>= 48x48 dp)");

    // Single live path: gesture classification in physical pixels, threshold
    // viewport-relative (max 48px, 5% of the short edge).
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

    // Threshold scales with the viewport (mobile-sized surface): 5% of 360 is
    // 18px, so the 48px floor governs — a 40px flick stays a Tap, 60px swipes.
    if (Rowl::Platform::MobileInput::classifyTouchGesture(100.0f, 200.0f, 140.0f, 200.0f, 360.0f, 640.0f)
            != Rowl::Platform::InputEventType::Tap ||
        Rowl::Platform::MobileInput::classifyTouchGesture(100.0f, 200.0f, 160.0f, 200.0f, 360.0f, 640.0f)
            != Rowl::Platform::InputEventType::SwipeBack ||
        // Vertical drift beats horizontal: not a swipe, stays a Tap.
        Rowl::Platform::MobileInput::classifyTouchGesture(100.0f, 200.0f, 160.0f, 400.0f, 360.0f, 640.0f)
            != Rowl::Platform::InputEventType::Tap) {
        std::cerr << "Viewport-relative touch threshold mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Touch Threshold Scales Viewport-Relatively (Mobile-Sized Surface)");
}
