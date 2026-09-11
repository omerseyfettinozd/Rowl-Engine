#include "rowl/render/camera2d.hpp"

namespace Rowl::Render {

constexpr float kMinZoom = 0.1f;
constexpr float kMaxZoom = 10.0f;
constexpr float kMaxShakeIntensity = 1000.0f;
constexpr float kMaxShakeDuration = 60.0f;

static float evaluateEasing(CameraEasing easing, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (easing) {
        case CameraEasing::Linear:
            return t;
        case CameraEasing::EaseInQuad:
            return t * t;
        case CameraEasing::EaseOutQuad:
            return t * (2.0f - t);
        case CameraEasing::EaseInOutCubic:
            return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
        case CameraEasing::SmoothStep:
            return t * t * (3.0f - 2.0f * t);
        default:
            return t;
    }
}

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
        m_panDuration = 0.0f;
        m_panTimer = 0.0f;
    }
}

void Camera2D::panTo(float targetX, float targetY, float durationSeconds, CameraEasing easing) {
    if (!std::isfinite(targetX) || !std::isfinite(targetY)) return;
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f) {
        setPosition(targetX, targetY);
        return;
    }
    m_panStartX = m_posX;
    m_panStartY = m_posY;
    m_panTargetX = targetX;
    m_panTargetY = targetY;
    m_panDuration = std::clamp(durationSeconds, 0.01f, 60.0f);
    m_panTimer = 0.0f;
    m_panEasing = easing;
}

void Camera2D::setZoom(float zoom) {
    if (std::isfinite(zoom)) {
        m_zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        m_zoomDuration = 0.0f;
        m_zoomTimer = 0.0f;
    }
}

void Camera2D::zoomTo(float targetZoom, float durationSeconds, CameraEasing easing) {
    if (!std::isfinite(targetZoom)) return;
    float clampedTarget = std::clamp(targetZoom, kMinZoom, kMaxZoom);
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f) {
        setZoom(clampedTarget);
        return;
    }
    m_zoomStart = m_zoom;
    m_zoomTarget = clampedTarget;
    m_zoomDuration = std::clamp(durationSeconds, 0.01f, 60.0f);
    m_zoomTimer = 0.0f;
    m_zoomEasing = easing;
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

    // Advance pan tween
    if (m_panTimer < m_panDuration) {
        m_panTimer += dt;
        float progress = std::clamp(m_panTimer / m_panDuration, 0.0f, 1.0f);
        float factor = evaluateEasing(m_panEasing, progress);
        m_posX = m_panStartX + (m_panTargetX - m_panStartX) * factor;
        m_posY = m_panStartY + (m_panTargetY - m_panStartY) * factor;
        if (progress >= 1.0f) {
            m_posX = m_panTargetX;
            m_posY = m_panTargetY;
            m_panTimer = m_panDuration;
        }
    }

    // Advance zoom tween
    if (m_zoomTimer < m_zoomDuration) {
        m_zoomTimer += dt;
        float progress = std::clamp(m_zoomTimer / m_zoomDuration, 0.0f, 1.0f);
        float factor = evaluateEasing(m_zoomEasing, progress);
        m_zoom = m_zoomStart + (m_zoomTarget - m_zoomStart) * factor;
        if (progress >= 1.0f) {
            m_zoom = m_zoomTarget;
            m_zoomTimer = m_zoomDuration;
        }
    }

    // Advance shake
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

void Camera2D::transformRectParallax(float inX, float inY, float inW, float inH,
                                    float parallaxX, float parallaxY,
                                    float& outX, float& outY, float& outW, float& outH) const {
    const float cx = m_virtualWidth * 0.5f;
    const float cy = m_virtualHeight * 0.5f;

    const float effectivePosX = cx + (m_posX - cx) * parallaxX;
    const float effectivePosY = cy + (m_posY - cy) * parallaxY;

    outX = cx + (inX - effectivePosX) * m_zoom + m_shakeOffsetX * parallaxX;
    outY = cy + (inY - effectivePosY) * m_zoom + m_shakeOffsetY * parallaxY;
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
    m_panDuration = 0.0f;
    m_panTimer = 0.0f;
    m_zoomDuration = 0.0f;
    m_zoomTimer = 0.0f;
    m_shakeIntensity = 0.0f;
    m_shakeDuration = 0.0f;
    m_shakeTimer = 0.0f;
    m_shakeOffsetX = 0.0f;
    m_shakeOffsetY = 0.0f;
}

} // namespace Rowl::Render
