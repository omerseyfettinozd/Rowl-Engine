#include "rowl/render/camera2d.hpp"
#include <cctype>

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
    shakeWithProfile(CameraShakePreset::Custom, intensity, durationSeconds, frequency, 2.0f, 1.0f, 1.0f);
}

void Camera2D::shakeWithProfile(CameraShakePreset preset, float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY) {
    if (!std::isfinite(intensity) || !std::isfinite(durationSeconds)) return;
    if (intensity <= 0.0f || durationSeconds <= 0.0f) {
        m_shakePreset = preset;
        m_shakeIntensity = 0.0f;
        m_shakeDuration = 0.0f;
        m_shakeTimer = 0.0f;
        m_shakeOffsetX = 0.0f;
        m_shakeOffsetY = 0.0f;
        return;
    }
    m_shakePreset = preset;
    m_shakeIntensity = std::clamp(intensity, 0.0f, kMaxShakeIntensity);
    m_shakeDuration = std::clamp(durationSeconds, 0.0f, kMaxShakeDuration);
    m_shakeTimer = m_shakeDuration;
    m_shakeFrequency = (frequency > 0.1f && std::isfinite(frequency)) ? frequency : 25.0f;
    m_shakeDamping = (damping > 0.01f && std::isfinite(damping)) ? damping : 1.0f;
    m_shakeDirX = std::clamp(dirX, 0.0f, 1.0f);
    m_shakeDirY = std::clamp(dirY, 0.0f, 1.0f);
}

void Camera2D::shakePreset(CameraShakePreset preset, float intensityMultiplier, float durationOverride) {
    float mult = (intensityMultiplier > 0.0f && std::isfinite(intensityMultiplier)) ? intensityMultiplier : 1.0f;
    switch (preset) {
        case CameraShakePreset::Subtle: {
            float dur = (durationOverride > 0.0f && std::isfinite(durationOverride)) ? durationOverride : 0.4f;
            shakeWithProfile(CameraShakePreset::Subtle, 5.0f * mult, dur, 16.0f, 1.0f, 0.7f, 0.7f);
            break;
        }
        case CameraShakePreset::Earthquake: {
            float dur = (durationOverride > 0.0f && std::isfinite(durationOverride)) ? durationOverride : 1.2f;
            shakeWithProfile(CameraShakePreset::Earthquake, 16.0f * mult, dur, 11.0f, 0.7f, 1.0f, 0.2f);
            break;
        }
        case CameraShakePreset::Explosion: {
            float dur = (durationOverride > 0.0f && std::isfinite(durationOverride)) ? durationOverride : 0.7f;
            shakeWithProfile(CameraShakePreset::Explosion, 32.0f * mult, dur, 30.0f, 2.2f, 1.0f, 1.0f);
            break;
        }
        case CameraShakePreset::Heartbeat: {
            float dur = (durationOverride > 0.0f && std::isfinite(durationOverride)) ? durationOverride : 1.5f;
            shakeWithProfile(CameraShakePreset::Heartbeat, 12.0f * mult, dur, 2.0f, 0.5f, 0.15f, 1.0f);
            break;
        }
        case CameraShakePreset::Custom:
        default: {
            float dur = (durationOverride > 0.0f && std::isfinite(durationOverride)) ? durationOverride : 0.5f;
            shakeWithProfile(CameraShakePreset::Custom, 10.0f * mult, dur, 25.0f, 2.0f, 1.0f, 1.0f);
            break;
        }
    }
}

void Camera2D::shakePreset(const std::string& presetName, float intensityMultiplier, float durationOverride) {
    std::string lower = presetName;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "subtle") {
        shakePreset(CameraShakePreset::Subtle, intensityMultiplier, durationOverride);
    } else if (lower == "earthquake") {
        shakePreset(CameraShakePreset::Earthquake, intensityMultiplier, durationOverride);
    } else if (lower == "explosion") {
        shakePreset(CameraShakePreset::Explosion, intensityMultiplier, durationOverride);
    } else if (lower == "heartbeat" || lower == "pulse") {
        shakePreset(CameraShakePreset::Heartbeat, intensityMultiplier, durationOverride);
    } else {
        shakePreset(CameraShakePreset::Custom, intensityMultiplier, durationOverride);
    }
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
            float progress = std::clamp(m_shakeTimer / m_shakeDuration, 0.0f, 1.0f);
            float elapsed = m_shakeDuration - m_shakeTimer;

            if (m_shakePreset == CameraShakePreset::Heartbeat) {
                // Rhythmic physiological lub-dub pulse
                float beatPhase = std::fmod(elapsed * m_shakeFrequency, 1.0f);
                float pulse = 0.0f;
                if (beatPhase < 0.22f) {
                    pulse = std::sin(beatPhase / 0.22f * 3.14159265f);
                } else if (beatPhase >= 0.28f && beatPhase < 0.48f) {
                    pulse = 0.65f * std::sin((beatPhase - 0.28f) / 0.20f * 3.14159265f);
                }
                float currentIntensity = m_shakeIntensity * std::pow(progress, m_shakeDamping) * pulse;
                m_shakeOffsetX = currentIntensity * m_shakeDirX * 0.4f * std::sin(elapsed * 15.0f);
                m_shakeOffsetY = currentIntensity * m_shakeDirY * (pulse > 0.01f ? 1.0f : 0.0f);
            } else {
                float decay = std::pow(progress, m_shakeDamping);
                float currentIntensity = m_shakeIntensity * decay;
                float phase = elapsed * m_shakeFrequency;

                m_shakeOffsetX = currentIntensity * m_shakeDirX * std::sin(phase * 6.283185307f);
                m_shakeOffsetY = currentIntensity * m_shakeDirY * std::cos(phase * 4.712388980f);
            }
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
    m_shakePreset = CameraShakePreset::Custom;
    m_shakeIntensity = 0.0f;
    m_shakeDuration = 0.0f;
    m_shakeTimer = 0.0f;
    m_shakeFrequency = 25.0f;
    m_shakeDamping = 1.0f;
    m_shakeDirX = 1.0f;
    m_shakeDirY = 1.0f;
    m_shakeOffsetX = 0.0f;
    m_shakeOffsetY = 0.0f;
}

} // namespace Rowl::Render
