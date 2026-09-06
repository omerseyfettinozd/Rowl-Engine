#include "rowl/scene/game_object.hpp"
#include "rowl/render/window.hpp"
#include <algorithm>

namespace Rowl::Scene {

GameObject::GameObject(const std::string& name, const std::string& id)
    : m_name(name), m_id(id) {
    // Automatically attach a TransformComponent (Unity-style default)
    m_transform = addComponent<TransformComponent>();
}

GameObject::~GameObject() {
    // Unity destroys in reverse attachment order. Disable is paired before
    // destruction for every component that is effectively active.
    for (auto it = m_components.rbegin(); it != m_components.rend(); ++it) {
        if (*it) {
            destroyComponent(**it);
        }
    }
    m_components.clear();
    for (auto it = m_pendingAdditions.rbegin(); it != m_pendingAdditions.rend(); ++it) {
        if (*it) destroyComponent(**it);
    }
    m_pendingAdditions.clear();
    m_transform = nullptr;
}

void GameObject::update(float deltaTime) {
    if (!m_active) return;
    m_isExecutingLifecycle = true;
    for (auto& comp : m_components) {
        if (comp && comp->isEnabled()) {
            if (!comp->m_started) {
                comp->m_started = true;
                comp->onStart();
            }
            comp->onUpdate(deltaTime);
        }
    }
    for (auto& comp : m_components) {
        if (comp && comp->isEnabled()) {
            comp->onLateUpdate(deltaTime);
        }
    }
    m_isExecutingLifecycle = false;
    flushPendingComponentChanges();
}

void GameObject::render(Rowl::Render::Window* window) {
    if (!m_active || !window) return;
    m_isExecutingLifecycle = true;
    for (auto& comp : m_components) {
        if (comp && comp->isEnabled()) {
            comp->onRender(window);
        }
    }
    m_isExecutingLifecycle = false;
    flushPendingComponentChanges();
}

void GameObject::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    for (auto& component : m_components) {
        if (!component || !component->isEnabled()) continue;
        if (m_active) activateComponent(*component);
        else deactivateComponent(*component);
    }
}

void GameObject::addComponentInternal(std::unique_ptr<Component> component) {
    if (!component) return;
    component->setOwner(this);
    component->onAwake();
    component->onInit();
    if (m_active && component->isEnabled()) activateComponent(*component);

    if (m_isExecutingLifecycle) {
        m_pendingAdditions.push_back(std::move(component));
    } else {
        m_components.push_back(std::move(component));
        sortComponents();
    }
}

bool GameObject::removeComponent(Component* component) {
    if (!component || component == m_transform || component->getOwner() != this) return false;
    if (m_isExecutingLifecycle) {
        if (std::find(m_pendingRemovals.begin(), m_pendingRemovals.end(), component) == m_pendingRemovals.end()) {
            m_pendingRemovals.push_back(component);
        }
        return true;
    }

    auto it = std::find_if(m_components.begin(), m_components.end(),
        [component](const std::unique_ptr<Component>& candidate) { return candidate.get() == component; });
    if (it == m_components.end()) return false;
    destroyComponent(**it);
    m_components.erase(it);
    return true;
}

void GameObject::onComponentEnabledChanged(Component& component, bool wasEnabled) {
    if (&component == m_transform && !component.isEnabled()) {
        // Transform is structurally required; keep it enabled as Unity does.
        component.m_enabled = true;
        return;
    }
    if (!m_active || component.m_destroyed || wasEnabled == component.isEnabled()) return;
    if (component.isEnabled()) activateComponent(component);
    else deactivateComponent(component);
}

void GameObject::activateComponent(Component& component) {
    if (!component.m_destroyed && component.isEnabled()) component.onEnable();
}

void GameObject::deactivateComponent(Component& component) {
    if (!component.m_destroyed) component.onDisable();
}

void GameObject::destroyComponent(Component& component) {
    if (component.m_destroyed) return;
    if (m_active && component.isEnabled()) component.onDisable();
    component.m_destroyed = true;
    component.onDestroy();
    component.setOwner(nullptr);
}

void GameObject::flushPendingComponentChanges() {
    if (!m_pendingRemovals.empty()) {
        const auto removals = std::move(m_pendingRemovals);
        m_pendingRemovals.clear();
        for (Component* component : removals) removeComponent(component);
    }
    if (!m_pendingAdditions.empty()) {
        for (auto& component : m_pendingAdditions) m_components.push_back(std::move(component));
        m_pendingAdditions.clear();
        sortComponents();
    }
}

void GameObject::sortComponents() {
    std::stable_sort(m_components.begin(), m_components.end(),
        [](const std::unique_ptr<Component>& left, const std::unique_ptr<Component>& right) {
            return left->executionOrder() < right->executionOrder();
        });
}

} // namespace Rowl::Scene
