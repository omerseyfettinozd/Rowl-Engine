#pragma once

#include <string>

namespace Rowl::Render {
class Window;
}

namespace Rowl::Scene {

class GameObject;

/**
 * Base class for all modular components attachable to a GameObject.
 * Follows Unity's Component lifecycle pattern.
 */
class Component {
public:
    Component() = default;
    virtual ~Component() = default;

    // Components are owned by GameObject via unique_ptr; prevent slicing/copying.
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) noexcept = default;
    Component& operator=(Component&&) noexcept = default;

    // ── Lifecycle callbacks ──
    // `onInit` is retained for existing Rowl components. New components should
    // prefer the Unity-compatible callbacks below. A component is guaranteed to
    // receive onDestroy at most once.
    virtual void onAwake() {}
    virtual void onInit() {}
    virtual void onEnable() {}
    virtual void onStart() {}
    virtual void onUpdate(float deltaTime) { (void)deltaTime; }
    virtual void onLateUpdate(float deltaTime) { (void)deltaTime; }
    virtual void onRender(Rowl::Render::Window* window) { (void)window; }
    virtual void onDisable() {}
    virtual void onDestroy() {}

    /// Lower values execute first. Transform reserves -1000 so spatial state
    /// is always ready before ordinary behaviours run.
    virtual int executionOrder() const { return 0; }

    // ── Ownership & State ──
    GameObject* getOwner() const { return m_owner; }
    void setOwner(GameObject* owner) { m_owner = owner; }

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    bool hasStarted() const { return m_started; }

private:
    friend class GameObject;

    GameObject* m_owner = nullptr;
    bool m_enabled = true;
    bool m_started = false;
    bool m_destroyed = false;
};

} // namespace Rowl::Scene
