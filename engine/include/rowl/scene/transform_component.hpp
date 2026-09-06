#pragma once

#include "rowl/scene/component.hpp"

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
    float getX() const { return m_x; }
    float getY() const { return m_y; }
    void setPosition(float x, float y) { m_x = x; m_y = y; }
    void setX(float x) { m_x = x; }
    void setY(float y) { m_y = y; }
    void translate(float dx, float dy) { m_x += dx; m_y += dy; }

    // ── Scale ──
    float getScaleX() const { return m_scaleX; }
    float getScaleY() const { return m_scaleY; }
    void setScale(float sx, float sy) { m_scaleX = sx; m_scaleY = sy; }
    void setScale(float uniformScale) { m_scaleX = uniformScale; m_scaleY = uniformScale; }

    // ── Rotation ──
    float getRotation() const { return m_rotation; }
    void setRotation(float degrees) { m_rotation = degrees; }
    void rotate(float deltaDegrees) { m_rotation += deltaDegrees; }

private:
    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_scaleX = 1.0f;
    float m_scaleY = 1.0f;
    float m_rotation = 0.0f;
};

} // namespace Rowl::Scene
