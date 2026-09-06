#include "rowl/scene/sprite_component.hpp"
#include "rowl/scene/game_object.hpp"
#include "rowl/render/window.hpp"

namespace Rowl::Scene {

void SpriteComponent::onRender(Rowl::Render::Window* window) {
    if (!window || m_texturePath.empty()) return;

    auto* owner = getOwner();
    if (!owner) return;

    auto* transform = owner->getTransform();
    if (!transform) return;

    float posX = transform->getX();
    float posY = transform->getY();
    float scaleX = transform->getScaleX();
    float scaleY = transform->getScaleY();

    float finalWidth = m_width * scaleX;
    float finalHeight = m_height * scaleY;

    window->drawSprite(m_texturePath, posX, posY, finalWidth, finalHeight, m_opacity);
}

} // namespace Rowl::Scene
