#include "rowl/platform/mobile_input.hpp"
#include "rowl/core/logger.hpp"
#include <SDL3/SDL.h>

namespace Rowl::Platform {

bool MobileInput::processSdlEvent(const SDL_Event& sdlEvent, InputEvent& outEvent) {
    switch (sdlEvent.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            outEvent.type = InputEventType::TapDown;
            outEvent.x = static_cast<float>(sdlEvent.button.x);
            outEvent.y = static_cast<float>(sdlEvent.button.y);
            outEvent.touchId = UINT32_MAX;  // Mouse uses special ID to distinguish from touch
            return true;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            outEvent.type = InputEventType::TapUp;
            outEvent.x = static_cast<float>(sdlEvent.button.x);
            outEvent.y = static_cast<float>(sdlEvent.button.y);
            outEvent.touchId = UINT32_MAX;
            return true;

        case SDL_EVENT_FINGER_DOWN:
            outEvent.type = InputEventType::TapDown;
            // SDL3 touch coordinates are normalized [0,1] - convert to virtual canvas (1920x1080)
            outEvent.x = sdlEvent.tfinger.x * 1920.0f;
            outEvent.y = sdlEvent.tfinger.y * 1080.0f;
            outEvent.touchId = static_cast<uint32_t>(sdlEvent.tfinger.fingerID);
            ROWL_LOG_TRACE("Unified Mobile Touch Tap Down at (" + std::to_string(outEvent.x) + ", " + std::to_string(outEvent.y) + ")");
            return true;

        case SDL_EVENT_FINGER_UP:
            outEvent.type = InputEventType::TapUp;
            outEvent.x = sdlEvent.tfinger.x * 1920.0f;
            outEvent.y = sdlEvent.tfinger.y * 1080.0f;
            outEvent.touchId = static_cast<uint32_t>(sdlEvent.tfinger.fingerID);
            return true;

        case SDL_EVENT_FINGER_MOTION:
            outEvent.type = InputEventType::DragMotion;
            outEvent.x = sdlEvent.tfinger.x * 1920.0f;
            outEvent.y = sdlEvent.tfinger.y * 1080.0f;
            outEvent.deltaX = sdlEvent.tfinger.dx * 1920.0f;
            outEvent.deltaY = sdlEvent.tfinger.dy * 1080.0f;
            outEvent.touchId = static_cast<uint32_t>(sdlEvent.tfinger.fingerID);
            return true;

        default:
            return false;
    }
}

bool MobileInput::isTouchTargetValid(float widthDp, float heightDp) {
    // Mobile accessibility: Minimum touch target size is 48x48 dp
    constexpr float MIN_TARGET_DP = 48.0f;
    return (widthDp >= MIN_TARGET_DP) && (heightDp >= MIN_TARGET_DP);
}

} // namespace Rowl::Platform