#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace Rowl::Render {

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

    // Zoom (scale factor: clamped between 0.1f and 10.0f, default: 1.0f)
    void setZoom(float zoom);
    float getZoom() const { return m_zoom; }

    // Rotation (degrees)
    void setRotation(float degrees);
    float getRotation() const { return m_rotation; }

    // Screen Shake: triggers decaying harmonic vibration
    void shake(float intensity, float durationSeconds, float frequency = 25.0f);
    bool isShaking() const { return m_shakeTimer > 0.0f; }
    float getShakeOffsetX() const { return m_shakeOffsetX; }
    float getShakeOffsetY() const { return m_shakeOffsetY; }
    float getShakeTimer() const { return m_shakeTimer; }

    // Update with delta time (decays shake)
    void update(float dt);

    // Transform a virtual rectangle (x, y, w, h) through camera projection
    void transformRect(float inX, float inY, float inW, float inH,
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

    // Shake state
    float m_shakeIntensity = 0.0f;
    float m_shakeDuration = 0.0f;
    float m_shakeTimer = 0.0f;
    float m_shakeFrequency = 25.0f;
    float m_shakeOffsetX = 0.0f;
    float m_shakeOffsetY = 0.0f;
};

} // namespace Rowl::Render
