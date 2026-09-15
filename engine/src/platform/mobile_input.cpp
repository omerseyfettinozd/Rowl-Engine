#include "rowl/platform/mobile_input.hpp"

#include <algorithm>
#include <cmath>

namespace Rowl::Platform {

bool MobileInput::isTouchTargetValid(float widthDp, float heightDp) {
    // Mobile accessibility: Minimum touch target size is 48x48 dp
    constexpr float MIN_TARGET_DP = 48.0f;
    return (widthDp >= MIN_TARGET_DP) && (heightDp >= MIN_TARGET_DP);
}

InputEventType MobileInput::classifyTouchGesture(float startX, float startY,
                                                  float endX, float endY,
                                                  float viewportWidth, float viewportHeight) {
    const float deltaX = endX - startX;
    const float deltaY = endY - startY;
    const float threshold = std::max(48.0f, std::min(viewportWidth, viewportHeight) * 0.05f);
    if (std::abs(deltaX) >= threshold && std::abs(deltaX) > std::abs(deltaY)) {
        return deltaX < 0.0f ? InputEventType::SwipeForward : InputEventType::SwipeBack;
    }
    return InputEventType::Tap;
}

} // namespace Rowl::Platform
