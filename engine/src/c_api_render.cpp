/**
 * c_api_render.cpp
 *
 * C-API render surface: window embedding, pixel buffer, texture cache, camera, transitions, screen FX.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "rowl/render/window.hpp"
extern "C" {
/* ── Native window embedding ─────────────────────────────────────────────── */

void RowlEngine_SetExternalWindowHandle(RowlEngineHandle handle,
                                         void* nativeWindowHandle,
                                         uint32_t width,
                                         uint32_t height) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setExternalWindowHandle(nativeWindowHandle, width, height); });
}

void RowlEngine_ResizeViewport(RowlEngineHandle handle,
                                uint32_t newWidth,
                                uint32_t newHeight) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* win = toEngine(handle)->getWindow();
        if (win) win->resizeViewport(newWidth, newHeight);
    });
}

/* ── Offscreen Framebuffer & Playback Control ────────────────────────────── */

const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH) {
    if (!isLiveHandle(handle)) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return nullptr;
    }
    return invokeNoexcept<const uint8_t*>([&] { return toEngine(handle)->getPixelBuffer(outW, outH); }, nullptr);
}

uint32_t RowlEngine_GetTextureCacheTextureCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint32_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? static_cast<uint32_t>(window->getTextureCacheTextureCount()) : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheBytes(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getTextureCacheBytes() : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheBudgetBytes(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getTextureCacheBudgetBytes() : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheEvictionCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getTextureCacheEvictionCount() : 0;
    }, 0);
}

double RowlEngine_GetLastFrameTextureLoadMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getLastFrameTextureLoadMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameNonTextureRenderMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getLastFrameNonTextureRenderMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameTextRasterizationMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getLastFrameTextRasterizationMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameRendererFlushMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getLastFrameRendererFlushMilliseconds() : 0.0;
    }, 0.0);
}

void RowlEngine_SetTextureCacheBudgetBytes(RowlEngineHandle handle, uint64_t bytes) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* window = toEngine(handle)->getWindow();
        if (window) window->setTextureCacheBudgetBytes(bytes);
    });
}

void RowlEngine_SetPlayState(RowlEngineHandle handle, int isPlaying) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setPlayState(isPlaying != 0); });
}

void RowlEngine_ResetToStartNode(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->resetToStartNode(); });
}

/* ── Transitions, camera & screen FX ────────────────────────────────────────── */

void RowlEngine_StartTransition(RowlEngineHandle handle, const char* kind, float durationSeconds, const char* colorHex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        std::string k = kind ? kind : "crossfade";
        std::string c = colorHex ? colorHex : "";
        toEngine(handle)->startTransition(k, durationSeconds, c);
    });
}

int RowlEngine_IsTransitionActive(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->isTransitionActive() ? 1 : 0;
    }, 0);
}

void RowlEngine_SetCamera(RowlEngineHandle handle, float x, float y, float zoom) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->setPosition(x, y);
            cam->setZoom(zoom);
        }
    });
}

void RowlEngine_TriggerCameraShake(RowlEngineHandle handle, float intensity, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->shake(intensity, durationSeconds);
        }
    });
}

void RowlEngine_ResetCamera(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->reset();
        }
    });
}

static Rowl::Render::CameraEasing mapEasing(int easingType) {
    switch (easingType) {
        case 0: return Rowl::Render::CameraEasing::Linear;
        case 1: return Rowl::Render::CameraEasing::EaseInQuad;
        case 2: return Rowl::Render::CameraEasing::EaseOutQuad;
        case 3: return Rowl::Render::CameraEasing::EaseInOutCubic;
        case 4: return Rowl::Render::CameraEasing::SmoothStep;
        default: return Rowl::Render::CameraEasing::EaseInOutCubic;
    }
}

void RowlEngine_CameraPanTo(RowlEngineHandle handle, float targetX, float targetY, float durationSeconds, int easingType) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->panTo(targetX, targetY, durationSeconds, mapEasing(easingType));
        }
    });
}

void RowlEngine_CameraZoomTo(RowlEngineHandle handle, float targetZoom, float durationSeconds, int easingType) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->zoomTo(targetZoom, durationSeconds, mapEasing(easingType));
        }
    });
}

int RowlEngine_IsCameraMoving(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* cam = toEngine(handle)->getCamera();
        return (cam && cam->isMoving()) ? 1 : 0;
    }, 0);
}

void RowlEngine_TriggerCameraShakePreset(RowlEngineHandle handle, const char* presetName, float intensityMultiplier, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->triggerCameraShakePreset(presetName ? presetName : "subtle", intensityMultiplier, durationSeconds);
    });
}

void RowlEngine_TriggerCameraShakeProfile(RowlEngineHandle handle, float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->triggerCameraShakeProfile(intensity, durationSeconds, frequency, damping, dirX, dirY);
    });
}

float RowlEngine_GetCameraShakeOffsetX(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getCameraShakeOffsetX();
    }, 0.0f);
}

float RowlEngine_GetCameraShakeOffsetY(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getCameraShakeOffsetY();
    }, 0.0f);
}

void RowlEngine_TriggerScreenFlash(RowlEngineHandle handle, uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->triggerScreenFlash(r, g, b, durationSeconds, intensity);
    });
}

void RowlEngine_TriggerScreenFlashHex(RowlEngineHandle handle, const char* colorHex, float durationSeconds, float intensity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->triggerScreenFlashHex(colorHex ? colorHex : "#FFFFFF", durationSeconds, intensity);
    });
}

int RowlEngine_IsScreenFlashActive(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->isScreenFlashActive() ? 1 : 0;
    }, 0);
}

void RowlEngine_SetScreenTint(RowlEngineHandle handle, uint8_t r, uint8_t g, uint8_t b, float opacity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->setScreenTint(r, g, b, opacity);
    });
}

void RowlEngine_SetScreenTintHex(RowlEngineHandle handle, const char* colorHex, float opacity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->setScreenTintHex(colorHex ? colorHex : "", opacity);
    });
}

void RowlEngine_ClearScreenTint(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->clearScreenTint();
    });
}

float RowlEngine_GetScreenTintOpacity(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getScreenTintOpacity();
    }, 0.0f);
}

void RowlEngine_SetVignette(RowlEngineHandle handle, float intensity, float radius, const char* colorHex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->setVignette(intensity, radius, colorHex ? colorHex : "#000000");
    });
}

float RowlEngine_GetVignetteIntensity(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getVignetteIntensity();
    }, 0.0f);
}


} // extern "C"
