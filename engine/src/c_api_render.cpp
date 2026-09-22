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
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        // D01 (#135): post-Init gömme sessiz-forward'dı (bogus handle
        // canlı pencereye yazılıyor, damga yoktu). Fail-closed: StateError
        // damgala, canlı pencereye dokunma. Pre-Init davranış korunur
        // (damgasız forward — Init tüketir). Ölü/yabancı handle sessiz
        // (damgalanacak motor yok / sahiplik-kanalı kuralı).
        if (checked->isInitialized()) {
            if (auto* ctx = checked->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::StateError,
                              "SetExternalWindowHandle must be called BEFORE RowlEngine_Init; "
                              "post-Init call rejected without touching the live window",
                              "set_external_window_handle", "");
            }
            return;
        }
        checked->setExternalWindowHandle(nativeWindowHandle, width, height);
    });
}

void RowlEngine_ResizeViewport(RowlEngineHandle handle,
                                uint32_t newWidth,
                                uint32_t newHeight) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        // D1 (#110): viewport null iken sessiz no-op → sinyalli no-op.
        requireEngineInitialized(checked, "resize_viewport");
        auto* win = checked->getWindow();
        if (win) win->resizeViewport(newWidth, newHeight);
    });
}

/* ── Offscreen Framebuffer & Playback Control ────────────────────────────── */

const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH) {
    return RowlEngine_GetPixelBufferEx(handle, outW, outH, nullptr);
}

const uint8_t* RowlEngine_GetPixelBufferEx(RowlEngineHandle handle,
                                            uint32_t* outW, uint32_t* outH,
                                            uint32_t* outPitch) {
    if (!isLiveHandle(handle)) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        if (outPitch) *outPitch = 0;
        return nullptr;
    }
    return invokeNoexcept<const uint8_t*>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getPixelBuffer(outW, outH, outPitch) : nullptr;
    }, nullptr);
}

uint32_t RowlEngine_GetTextureCacheTextureCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint32_t>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? static_cast<uint32_t>(window->getTextureCacheTextureCount()) : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheBytes(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getTextureCacheBytes() : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheBudgetBytes(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getTextureCacheBudgetBytes() : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheEvictionCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getTextureCacheEvictionCount() : 0;
    }, 0);
}

double RowlEngine_GetLastFrameTextureLoadMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getLastFrameTextureLoadMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameNonTextureRenderMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getLastFrameNonTextureRenderMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameTextRasterizationMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getLastFrameTextRasterizationMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameRendererFlushMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        auto checked = toEngineChecked(handle);
        const auto* window = checked ? checked->getWindow() : nullptr;
        return window ? window->getLastFrameRendererFlushMilliseconds() : 0.0;
    }, 0.0);
}

void RowlEngine_SetTextureCacheBudgetBytes(RowlEngineHandle handle, uint64_t bytes) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        // D1 (#110): pre-init sessiz-drop → sinyalli-drop (davranış korunur).
        requireEngineInitialized(checked, "set_texture_cache_budget");
        auto* window = checked->getWindow();
        if (window) window->setTextureCacheBudgetBytes(bytes);
    });
}

void RowlEngine_SetPlayState(RowlEngineHandle handle, int isPlaying) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { auto checked = toEngineChecked(handle); if (!checked) return; requireEngineInitialized(checked, "set_play_state"); checked->setPlayState(isPlaying != 0); });
}

void RowlEngine_ResetToStartNode(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto checked = toEngineChecked(handle)) checked->resetToStartNode(); });
}

/* ── Transitions, camera & screen FX ────────────────────────────────────────── */

void RowlEngine_StartTransition(RowlEngineHandle handle, const char* kind, float durationSeconds, const char* colorHex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        std::string k = kind ? kind : "crossfade";
        std::string c = colorHex ? colorHex : "";
        // D1 (#110 engine.cpp:1785-eskisi): pencere yokken düşen geçiş sinyalsizdi.
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "start_transition"); checked->startTransition(k, durationSeconds, c); }
    });
}

int RowlEngine_IsTransitionActive(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->isTransitionActive()) ? 1 : 0;
    }, 0);
}

int RowlEngine_IsPreviewFrameStatic(RowlEngineHandle handle) {
    // Fail-closed toward copying: every error path reports not-static (0) so
    // hosts fall back to the pre-MS-4 copy-every-frame behavior.
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->isPreviewFrameStatic()) ? 1 : 0;
    }, 0);
}

void RowlEngine_SetCamera(RowlEngineHandle handle, float x, float y, float zoom) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        // D1 (#110): kamera null iken sessiz no-op → sinyalli no-op.
        requireEngineInitialized(checked, "set_camera");
        auto* cam = checked->getCamera();
        if (cam) {
            cam->setPosition(x, y);
            cam->setZoom(zoom);
        }
    });
}

void RowlEngine_TriggerCameraShake(RowlEngineHandle handle, float intensity, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        requireEngineInitialized(checked, "trigger_camera_shake");
        auto* cam = checked->getCamera();
        if (cam) {
            cam->shake(intensity, durationSeconds);
        }
    });
}

void RowlEngine_ResetCamera(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        requireEngineInitialized(checked, "reset_camera");
        auto* cam = checked->getCamera();
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
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        requireEngineInitialized(checked, "camera_pan_to");
        auto* cam = checked->getCamera();
        if (cam) {
            cam->panTo(targetX, targetY, durationSeconds, mapEasing(easingType));
        }
    });
}

void RowlEngine_CameraZoomTo(RowlEngineHandle handle, float targetZoom, float durationSeconds, int easingType) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        requireEngineInitialized(checked, "camera_zoom_to");
        auto* cam = checked->getCamera();
        if (cam) {
            cam->zoomTo(targetZoom, durationSeconds, mapEasing(easingType));
        }
    });
}

int RowlEngine_IsCameraMoving(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        const auto* cam = checked ? checked->getCamera() : nullptr;
        return (cam && cam->isMoving()) ? 1 : 0;
    }, 0);
}

void RowlEngine_TriggerCameraShakePreset(RowlEngineHandle handle, const char* presetName, float intensityMultiplier, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "trigger_camera_shake_preset"); checked->triggerCameraShakePreset(presetName ? presetName : "subtle", intensityMultiplier, durationSeconds); }
    });
}

void RowlEngine_TriggerCameraShakeProfile(RowlEngineHandle handle, float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "trigger_camera_shake_profile"); checked->triggerCameraShakeProfile(intensity, durationSeconds, frequency, damping, dirX, dirY); }
    });
}

float RowlEngine_GetCameraShakeOffsetX(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getCameraShakeOffsetX() : 0.0f;
    }, 0.0f);
}

float RowlEngine_GetCameraShakeOffsetY(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getCameraShakeOffsetY() : 0.0f;
    }, 0.0f);
}

// Faz 3 Dilim 5 — reduced motion suppresses sudden full-screen effects.
// The camera owns the flag; both flash entry points consult it here so
// engine.cpp and window.cpp stay untouched.
static bool reducedMotionActive(RowlEngineHandle handle) {
    auto checked = toEngineChecked(handle);
    const auto* cam = checked ? checked->getCamera() : nullptr;
    return cam && cam->reducedMotion();
}

void RowlEngine_TriggerScreenFlash(RowlEngineHandle handle, uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (reducedMotionActive(handle)) return;
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "trigger_screen_flash"); checked->triggerScreenFlash(r, g, b, durationSeconds, intensity); }
    });
}

void RowlEngine_TriggerScreenFlashHex(RowlEngineHandle handle, const char* colorHex, float durationSeconds, float intensity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (reducedMotionActive(handle)) return;
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "trigger_screen_flash_hex"); checked->triggerScreenFlashHex(colorHex ? colorHex : "#FFFFFF", durationSeconds, intensity); }
    });
}

int RowlEngine_IsScreenFlashActive(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->isScreenFlashActive()) ? 1 : 0;
    }, 0);
}

void RowlEngine_SetScreenTint(RowlEngineHandle handle, uint8_t r, uint8_t g, uint8_t b, float opacity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "set_screen_tint"); checked->setScreenTint(r, g, b, opacity); }
    });
}

void RowlEngine_SetScreenTintHex(RowlEngineHandle handle, const char* colorHex, float opacity) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "set_screen_tint_hex"); checked->setScreenTintHex(colorHex ? colorHex : "", opacity); }
    });
}

void RowlEngine_ClearScreenTint(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "clear_screen_tint"); checked->clearScreenTint(); }
    });
}

float RowlEngine_GetScreenTintOpacity(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getScreenTintOpacity() : 0.0f;
    }, 0.0f);
}

void RowlEngine_SetVignette(RowlEngineHandle handle, float intensity, float radius, const char* colorHex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto checked = toEngineChecked(handle)) { requireEngineInitialized(checked, "set_vignette"); checked->setVignette(intensity, radius, colorHex ? colorHex : "#000000"); }
    });
}

float RowlEngine_GetVignetteIntensity(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        auto checked = toEngineChecked(handle);
        return checked ? checked->getVignetteIntensity() : 0.0f;
    }, 0.0f);
}


} // extern "C"
