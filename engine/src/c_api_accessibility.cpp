/**
 * c_api_accessibility.cpp
 *
 * Faz 3 Dilim 5 — C-API accessibility surface: text scale, high contrast
 * and reduced motion. State lives in FontRenderer (display) and Camera2D
 * (motion); this unit only routes handles. engine.cpp and window.cpp are
 * untouched: dialogue/typewriter paths pick the settings up through the
 * renderer and camera they already use.
 */

#include "c_api_internal.hpp"
#include "rowl/render/window.hpp"
extern "C" {

void RowlEngine_SetTextScale(RowlEngineHandle handle, float scale) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* win = toEngine(handle)->getWindow();
        if (win) win->forEachFontRenderer([&](Rowl::Render::FontRenderer& renderer) {
            renderer.setTextScale(scale);
        });
    });
}

void RowlEngine_SetHighContrast(RowlEngineHandle handle, int enabled) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* win = toEngine(handle)->getWindow();
        if (win) win->forEachFontRenderer([&](Rowl::Render::FontRenderer& renderer) {
            renderer.setHighContrast(enabled != 0);
        });
    });
}

void RowlEngine_SetReducedMotion(RowlEngineHandle handle, int enabled) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) cam->setReducedMotion(enabled != 0);
    });
}

float RowlEngine_GetTextScale(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1.0f;
    return invokeNoexcept<float>([&] {
        auto* win = toEngine(handle)->getWindow();
        auto* renderer = win ? win->getFontRenderer() : nullptr;
        return renderer ? renderer->textScale() : 1.0f;
    }, 1.0f);
}

int RowlEngine_IsHighContrast(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto* win = toEngine(handle)->getWindow();
        auto* renderer = win ? win->getFontRenderer() : nullptr;
        return (renderer && renderer->highContrast()) ? 1 : 0;
    }, 0);
}

int RowlEngine_IsReducedMotion(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* cam = toEngine(handle)->getCamera();
        return (cam && cam->reducedMotion()) ? 1 : 0;
    }, 0);
}

} // extern "C"
