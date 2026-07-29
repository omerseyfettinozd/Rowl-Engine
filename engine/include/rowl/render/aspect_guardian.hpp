#pragma once

#include <cstdint>

namespace Rowl::Render {

struct ViewportMetrics {
    int x = 0;
    int y = 0;
    int width = 1920;
    int height = 1080;
    float scaleFactor = 1.0f;
    bool isPillarbox = false;
};

struct NineSliceMetrics {
    int leftMargin;
    int rightMargin;
    int topMargin;
    int bottomMargin;
};

class AspectGuardian {
public:
    static ViewportMetrics calculateViewport(
        uint32_t physicalWidth, uint32_t physicalHeight,
        uint32_t virtualWidth = 1920, uint32_t virtualHeight = 1080
    );

    static void virtualToPhysical(
        float virtX, float virtY, const ViewportMetrics& metrics,
        float& outPhysX, float& outPhysY
    );
};

} // namespace Rowl::Render
