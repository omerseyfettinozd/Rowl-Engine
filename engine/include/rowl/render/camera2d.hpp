#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>

namespace Rowl::Render {

enum class CameraEasing {
    Linear,
    EaseInQuad,
    EaseOutQuad,
    EaseInOutCubic,
    SmoothStep
};

enum class CameraShakePreset {
    Custom = 0,
    Subtle = 1,
    Earthquake = 2,
    Explosion = 3,
    Heartbeat = 4
};

class Camera2D {
public:
    explicit Camera2D(float virtualWidth = 1920.0f, float virtualHeight = 1080.0f);

    void setVirtualCanvasSize(float width, float height);
    float getVirtualWidth() const { return m_virtualWidth; }
    float getVirtualHeight() const { return m_virtualHeight; }

    // Position (Camera focus center in world virtual coordinates, default: 960, 540)
    void setPosition(float x, float y);
    float getPositionX() const { return m_posX; }
    float getPositionY() const { return m_posY; }

    // Smooth Pan to target coordinates over durationSeconds
    void panTo(float targetX, float targetY, float durationSeconds, CameraEasing easing = CameraEasing::EaseInOutCubic);
    bool isPanning() const { return m_panTimer < m_panDuration; }

    // Zoom (scale factor: clamped between 0.1f and 10.0f, default: 1.0f)
    void setZoom(float zoom);
    float getZoom() const { return m_zoom; }

    // Smooth Zoom to target scale factor over durationSeconds
    void zoomTo(float targetZoom, float durationSeconds, CameraEasing easing = CameraEasing::EaseInOutCubic);
    bool isZooming() const { return m_zoomTimer < m_zoomDuration; }

    // Rotation (degrees)
    void setRotation(float degrees);
    float getRotation() const { return m_rotation; }

    // Screen Shake: triggers decaying harmonic vibration
    void shake(float intensity, float durationSeconds, float frequency = 25.0f);
    void shakePreset(CameraShakePreset preset, float intensityMultiplier = 1.0f, float durationOverride = 0.0f);
    void shakePreset(const std::string& presetName, float intensityMultiplier = 1.0f, float durationOverride = 0.0f);
    void shakeWithProfile(CameraShakePreset preset, float intensity, float durationSeconds, float frequency, float damping = 1.0f, float dirX = 1.0f, float dirY = 1.0f);

    bool isShaking() const { return m_shakeTimer > 0.0f; }
    float getShakeOffsetX() const { return m_shakeOffsetX; }
    float getShakeOffsetY() const { return m_shakeOffsetY; }
    float getShakeTimer() const { return m_shakeTimer; }
    CameraShakePreset getShakePreset() const { return m_shakePreset; }
    float getShakeDamping() const { return m_shakeDamping; }
    float getShakeDirX() const { return m_shakeDirX; }
    float getShakeDirY() const { return m_shakeDirY; }

    // Movement query
    bool isMoving() const { return isPanning() || isZooming() || isShaking(); }

    // Update with delta time (decays shake and advances tweens)
    void update(float dt);

    // Transform a virtual rectangle (x, y, w, h) through camera projection
    void transformRect(float inX, float inY, float inW, float inH,
                       float& outX, float& outY, float& outW, float& outH) const;

    // Transform a rectangle with independent X/Y parallax scaling factors (1.0 = standard, 0.0 = static/sky)
    void transformRectParallax(float inX, float inY, float inW, float inH,
                               float parallaxX, float parallaxY,
                               float& outX, float& outY, float& outW, float& outH) const;

    // Transform a point
    void transformPoint(float inX, float inY, float& outX, float& outY) const;

    // Reset camera to default center (960, 540) and zoom 1.0f
    void reset();

private:
    float m_virtualWidth = 1920.0f;
    float m_virtualHeight = 1080.0f;

    float m_posX = 960.0f;
    float m_posY = 540.0f;
    float m_zoom = 1.0f;
    float m_rotation = 0.0f;

    // Pan tween
    float m_panStartX = 960.0f;
    float m_panStartY = 540.0f;
    float m_panTargetX = 960.0f;
    float m_panTargetY = 540.0f;
    float m_panDuration = 0.0f;
    float m_panTimer = 0.0f;
    CameraEasing m_panEasing = CameraEasing::EaseInOutCubic;

    // Zoom tween
    float m_zoomStart = 1.0f;
    float m_zoomTarget = 1.0f;
    float m_zoomDuration = 0.0f;
    float m_zoomTimer = 0.0f;
    CameraEasing m_zoomEasing = CameraEasing::EaseInOutCubic;

    // Shake state
    CameraShakePreset m_shakePreset = CameraShakePreset::Custom;
    float m_shakeIntensity = 0.0f;
    float m_shakeDuration = 0.0f;
    float m_shakeTimer = 0.0f;
    float m_shakeFrequency = 25.0f;
    float m_shakeDamping = 1.0f;
    float m_shakeDirX = 1.0f;
    float m_shakeDirY = 1.0f;
    float m_shakeOffsetX = 0.0f;
    float m_shakeOffsetY = 0.0f;
};

} // namespace Rowl::Render
