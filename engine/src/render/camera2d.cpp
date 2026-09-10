#include "rowl/render/camera2d.hpp"

namespace Rowl::Render {

constexpr float kMinZoom = 0.1f;
constexpr float kMaxZoom = 10.0f;
constexpr float kMaxShakeIntensity = 1000.0f;
constexpr float kMaxShakeDuration = 60.0f;

Camera2D::Camera2D(float virtualWidth, float virtualHeight)
    : m_virtualWidth(virtualWidth > 10.0f ? virtualWidth : 1920.0f),
      m_virtualHeight(virtualHeight > 10.0f ? virtualHeight : 1080.0f),
      m_posX(m_virtualWidth * 0.5f),
      m_posY(m_virtualHeight * 0.5f) {
}

void Camera2D::setVirtualCanvasSize(float width, float height) {
    if (width > 10.0f && height > 10.0f) {
        m_virtualWidth = width;
        m_virtualHeight = height;
    }
}

void Camera2D::setPosition(float x, float y) {
    if (std::isfinite(x) && std::isfinite(y)) {
        m_posX = x;
        m_posY = y;
    }
}

void Camera2D::setZoom(float zoom) {
    if (std::isfinite(zoom)) {
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    }
}

void Camera2D::setRotation(float degrees) {
    if (std::isfinite(degrees)) {
        m_rotation = degrees;
    }
}

void Camera2D::shake(float intensity, float durationSeconds, float frequency) {
    if (!std::isfinite(intensity) || !std::isfinite(durationSeconds)) return;
    if (intensity <= 0.0f || durationSeconds <= 0.0f) {
        m_shakeIntensity = 0.0f;
        m_shakeDuration = 0.0f;
        m_shakeTimer = 0.0f;
        m_shakeOffsetX = 0.0f;
        m_shakeOffsetY = 0.0f;
        return;
    }
    m_shakeIntensity = std::clamp(intensity, 0.0f, kMaxShakeIntensity);
    m_shakeDuration = std::clamp(durationSeconds, 0.0f, kMaxShakeDuration);
    m_shakeTimer = m_shakeDuration;
    m_shakeFrequency = (frequency > 0.1f && std::isfinite(frequency)) ? frequency : 25.0f;
}

void Camera2D::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;

    if (m_shakeTimer > 0.0f) {
        m_shakeTimer -= dt;
        if (m_shakeTimer <= 0.0f) {
            m_shakeTimer = 0.0f;
            m_shakeOffsetX = 0.0f;
            m_shakeOffsetY = 0.0f;
        } else {
            // Quadratic decay for natural shock absorption
            float progress = std::clamp(m_shakeTimer / m_shakeDuration, 0.0f, 1.0f);
            float currentIntensity = m_shakeIntensity * (progress * progress);
            float phase = (m_shakeDuration - m_shakeTimer) * m_shakeFrequency;

            m_shakeOffsetX = currentIntensity * std::sin(phase * 6.283185307f);
            m_shakeOffsetY = currentIntensity * std::cos(phase * 4.712388980f);
        }
    } else {
        m_shakeOffsetX = 0.0f;
        m_shakeOffsetY = 0.0f;
    }
}

void Camera2D::transformRect(float inX, float inY, float inW, float inH,
                            float& outX, float& outY, float& outW, float& outH) const {
    const float cx = m_virtualWidth * 0.5f;
    const float cy = m_virtualHeight * 0.5f;

    outX = cx + (inX - m_posX) * m_zoom + m_shakeOffsetX;
    outY = cy + (inY - m_posY) * m_zoom + m_shakeOffsetY;
    outW = inW * m_zoom;
    outH = inH * m_zoom;
}

void Camera2D::transformPoint(float inX, float inY, float& outX, float& outY) const {
    const float cx = m_virtualWidth * 0.5f;
    const float cy = m_virtualHeight * 0.5f;

    outX = cx + (inX - m_posX) * m_zoom + m_shakeOffsetX;
    outY = cy + (inY - m_posY) * m_zoom + m_shakeOffsetY;
}

void Camera2D::reset() {
    m_posX = m_virtualWidth * 0.5f;
    m_posY = m_virtualHeight * 0.5f;
    m_zoom = 1.0f;
    m_rotation = 0.0f;
    m_shakeIntensity = 0.0f;
    m_shakeDuration = 0.0f;
    m_shakeTimer = 0.0f;
    m_shakeOffsetX = 0.0f;
    m_shakeOffsetY = 0.0f;
}

} // namespace Rowl::Render
