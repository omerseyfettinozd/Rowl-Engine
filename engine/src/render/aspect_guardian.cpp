#include "rowl/render/aspect_guardian.hpp"

namespace Rowl::Render {

ViewportMetrics AspectGuardian::calculateViewport(
    uint32_t physicalWidth, uint32_t physicalHeight,
    uint32_t virtualWidth, uint32_t virtualHeight) {

    ViewportMetrics result;
    if (physicalWidth == 0 || physicalHeight == 0) return result;

    float virtualAspect = static_cast<float>(virtualWidth) / static_cast<float>(virtualHeight);
    float physicalAspect = static_cast<float>(physicalWidth) / static_cast<float>(physicalHeight);

    if (physicalAspect > virtualAspect) {
        // Wide display: Pillarbox (side bars)
        result.height = static_cast<int>(physicalHeight);
        result.width = static_cast<int>(physicalHeight * virtualAspect);
        result.x = (static_cast<int>(physicalWidth) - result.width) / 2;
        result.y = 0;
        result.scaleFactor = static_cast<float>(result.height) / static_cast<float>(virtualHeight);
        result.isPillarbox = true;
    } else {
        // Tall display: Letterbox (top/bottom bars)
        result.width = static_cast<int>(physicalWidth);
        result.height = static_cast<int>(physicalWidth / virtualAspect);
        result.x = 0;
        result.y = (static_cast<int>(physicalHeight) - result.height) / 2;
        result.scaleFactor = static_cast<float>(result.width) / static_cast<float>(virtualWidth);
        result.isPillarbox = false;
    }

    return result;
}

void AspectGuardian::virtualToPhysical(
    float virtX, float virtY, const ViewportMetrics& metrics,
    float& outPhysX, float& outPhysY) {
    outPhysX = static_cast<float>(metrics.x) + (virtX * metrics.scaleFactor);
    outPhysY = static_cast<float>(metrics.y) + (virtY * metrics.scaleFactor);
}

} // namespace Rowl::Render
