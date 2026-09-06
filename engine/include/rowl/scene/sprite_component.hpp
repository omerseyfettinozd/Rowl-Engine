#pragma once

#include "rowl/scene/component.hpp"
#include <string>

namespace Rowl::Scene {

/**
 * Visual component that renders a 2D texture/sprite at the owner GameObject's Transform position.
 */
class SpriteComponent : public Component {
public:
    SpriteComponent() = default;
    explicit SpriteComponent(const std::string& texturePath, float width = 0.0f, float height = 0.0f, float opacity = 1.0f)
        : m_texturePath(texturePath), m_width(width), m_height(height), m_opacity(opacity) {}

    void onRender(Rowl::Render::Window* window) override;

    // ── Getters & Setters ──
    const std::string& getTexturePath() const { return m_texturePath; }
    void setTexturePath(const std::string& texturePath) { m_texturePath = texturePath; }

    float getWidth() const { return m_width; }
    void setWidth(float width) { m_width = width; }

    float getHeight() const { return m_height; }
    void setHeight(float height) { m_height = height; }

    void setSize(float width, float height) { m_width = width; m_height = height; }

    float getOpacity() const { return m_opacity; }
    void setOpacity(float opacity) { m_opacity = opacity; }

    int getSortOrder() const { return m_sortOrder; }
    void setSortOrder(int sortOrder) { m_sortOrder = sortOrder; }

private:
    std::string m_texturePath;
    float m_width = 0.0f;
    float m_height = 0.0f;
    float m_opacity = 1.0f;
    int m_sortOrder = 0;
};

} // namespace Rowl::Scene
