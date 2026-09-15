#pragma once

namespace Rowl::Platform {

enum class InputEventType {
    TapDown,
    TapUp,
    DragMotion,
    Tap,
    SwipeForward,
    SwipeBack
};

/// Faz 4.5 Dilim 4 touch decision: DELETE the dead path.
/// The former MobileInput::processSdlEvent per-event SDL switch (plus its
/// InputEvent payload) was dead — no shipping code called it; the only caller
/// was the unit test — and it duplicated coordinate normalization with a
/// hardcoded 1920x1080 canvas, so a future mobile integration calling it on
/// any other surface would have mis-mapped every touch. It is deleted rather
/// than unified because the live path below already owns DOWN/UP pairing and
/// viewport-relative mapping (Window::pollEvents + touchCoordinateToPhysical);
/// merging a stateless per-event translator into it would have recreated the
/// second path under a shared name.
/// Single live touch path: Window::pollEvents pairs FINGER_DOWN/UP per
/// fingerID, maps to physical pixels viewport-relatively, then classifies here.
/// The TapDown/TapUp/DragMotion enumerators stay as stable vocabulary for the
/// Faz 6/7 mobile host gate (see docs/PLATFORM_SUPPORT.md); they cost nothing
/// and block no future integration.
class MobileInput {
public:
    static bool isTouchTargetValid(float widthDp, float heightDp);
    /// Classifies a completed touch in physical window coordinates. Horizontal
    /// swipes deliberately win over taps, so a story never advances twice.
    static InputEventType classifyTouchGesture(float startX, float startY,
                                               float endX, float endY,
                                               float viewportWidth, float viewportHeight);
};

} // namespace Rowl::Platform
