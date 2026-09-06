#include "rowl/scene/component.hpp"
#include "rowl/scene/game_object.hpp"

namespace Rowl::Scene {

void Component::setEnabled(bool enabled) {
    if (m_enabled == enabled || m_destroyed) return;

    const bool wasEnabled = m_enabled;
    m_enabled = enabled;
    if (m_owner) {
        m_owner->onComponentEnabledChanged(*this, wasEnabled);
    }
}

} // namespace Rowl::Scene
