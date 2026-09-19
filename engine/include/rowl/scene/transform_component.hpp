#pragma once

#include "rowl/scene/component.hpp"

#include <cmath>

namespace Rowl::Scene {

/**
 * Fundamental component holding 2D spatial positioning, scaling, and rotation.
 * Every GameObject contains a TransformComponent by default.
 */
class TransformComponent : public Component {
public:
    TransformComponent() = default;
    TransformComponent(float x, float y, float scaleX = 1.0f, float scaleY = 1.0f, float rotation = 0.0f)
        : m_x(x), m_y(y), m_scaleX(scaleX), m_scaleY(scaleY), m_rotation(rotation) {}

    int executionOrder() const override { return -1000; }

    // ── Position ──
    // B6 (#3/#11): unguarded NaN/Inf flowed every frame into drawSprite
    // (std::clamp cannot stop NaN; the Uint8 alpha cast is UB). Non-finite
    // inputs keep the last-valid value (Camera2D::setPosition precedent).
    float getX() const { return m_x; }
    float getY() const { return m_y; }
    void setPosition(float x, float y) { if (std::isfinite(x) && std::isfinite(y)) { m_x = x; m_y = y; } }
    void setX(float x) { if (std::isfinite(x)) m_x = x; }
    void setY(float y) { if (std::isfinite(y)) m_y = y; }
    void translate(float dx, float dy) { if (std::isfinite(dx) && std::isfinite(dy)) { m_x += dx; m_y += dy; } }

    // ── Scale ──
    float getScaleX() const { return m_scaleX; }
    float getScaleY() const { return m_scaleY; }
    void setScale(float sx, float sy) { if (std::isfinite(sx) && std::isfinite(sy)) { m_scaleX = sx; m_scaleY = sy; } }
    void setScale(float uniformScale) { if (std::isfinite(uniformScale)) { m_scaleX = uniformScale; m_scaleY = uniformScale; } }

    // ── Rotation ──
    float getRotation() const { return m_rotation; }
    void setRotation(float degrees) { if (std::isfinite(degrees)) m_rotation = degrees; }
    void rotate(float deltaDegrees) { if (std::isfinite(deltaDegrees)) m_rotation += deltaDegrees; }

private:
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
    float m_rotation = 0.0f;
};

} // namespace Rowl::Scene
