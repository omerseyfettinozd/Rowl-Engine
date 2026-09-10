#pragma once

#include <cstdint>
#include <string>

union SDL_Event;

namespace Rowl::Platform {

enum class InputEventType {
    TapDown,
    TapUp,
    DragMotion,
    Tap,
    SwipeForward,
    SwipeBack
};

struct InputEvent {
    InputEventType type;
    float x = 0.0f;
    float y = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    uint32_t touchId = 0;
};

class MobileInput {
public:
    static bool processSdlEvent(const SDL_Event& sdlEvent, InputEvent& outEvent);
    static bool isTouchTargetValid(float widthDp, float heightDp);
    /// Classifies a completed touch in physical window coordinates. Horizontal
    /// swipes deliberately win over taps, so a story never advances twice.
    static InputEventType classifyTouchGesture(float startX, float startY,
                                               float endX, float endY,
                                               float viewportWidth, float viewportHeight);
};

} // namespace Rowl::Platform
