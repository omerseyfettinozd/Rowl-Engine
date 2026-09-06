#pragma once

#include "rowl/scene/component.hpp"
#include "rowl/scene/transform_component.hpp"
#include <string>
#include <vector>
#include <memory>
#include <type_traits>
#include <algorithm>
#include <utility>

namespace Rowl::Scene {

/**
 * Represents an Entity / GameObject in the scene.
 * Acts as a container for modular components (Unity-style architecture).
 * Every GameObject has a TransformComponent attached by default.
 */
class GameObject {
public:
    explicit GameObject(const std::string& name = "GameObject", const std::string& id = "");
    ~GameObject();

    // GameObjects are uniquely owned by a Scene
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;
    GameObject(GameObject&&) noexcept = default;
    GameObject& operator=(GameObject&&) noexcept = default;

    // ── Lifecycle ──
    void update(float deltaTime);
    void render(Rowl::Render::Window* window);

    // ── Transform Accessor ──
    TransformComponent* getTransform() const { return m_transform; }

    // ── Identification & State ──
    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

    const std::string& getId() const { return m_id; }
    void setId(const std::string& id) { m_id = id; }

    bool isActive() const { return m_active; }
    void setActive(bool active);

    // ── Component Management (Unity-style API) ──

    template <typename T, typename... Args>
    T* addComponent(Args&&... args) {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Rowl::Scene::Component");
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = comp.get();
        addComponentInternal(std::move(comp));
        return ptr;
    }

    template <typename T>
    T* getComponent() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Rowl::Scene::Component");
        for (auto& comp : m_components) {
            if (auto* casted = dynamic_cast<T*>(comp.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template <typename T>
    const T* getComponent() const {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Rowl::Scene::Component");
        for (const auto& comp : m_components) {
            if (const auto* casted = dynamic_cast<const T*>(comp.get())) {
                return casted;
            }
        }
        return nullptr;
    }

    template <typename T>
    std::vector<T*> getComponents() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Rowl::Scene::Component");
        std::vector<T*> result;
        for (auto& comp : m_components) {
            if (auto* casted = dynamic_cast<T*>(comp.get())) {
                result.push_back(casted);
            }
        }
        return result;
    }

    template <typename T>
    bool hasComponent() const {
        return getComponent<T>() != nullptr;
    }

    template <typename T>
    bool removeComponent() {
        static_assert(std::is_base_of_v<Component, T>, "T must derive from Rowl::Scene::Component");
        for (auto it = m_components.begin(); it != m_components.end(); ++it) {
            if (dynamic_cast<T*>(it->get())) {
                // Safeguard: do not allow removing the core transform component
                if (it->get() == m_transform) {
                    return false;
                }
                return removeComponent(it->get());
            }
        }
        return false;
    }

    const std::vector<std::unique_ptr<Component>>& getAllComponents() const {
        return m_components;
    }

    /// Removes this exact component instance. During a lifecycle callback the
    /// removal is deferred until the current pass completes, preventing iterator
    /// invalidation and use-after-free bugs.
    bool removeComponent(Component* component);

    /// Called by Component::setEnabled; public only to keep the component API
    /// compact. Applications should change state through Component::setEnabled.
    void onComponentEnabledChanged(Component& component, bool wasEnabled);

private:
    void addComponentInternal(std::unique_ptr<Component> component);
    void activateComponent(Component& component);
    void deactivateComponent(Component& component);
    void destroyComponent(Component& component);
    void flushPendingComponentChanges();
    void sortComponents();

    std::string m_name;
    std::string m_id;
    bool m_active = true;
    TransformComponent* m_transform = nullptr; // Non-owning cached pointer to component in m_components
    std::vector<std::unique_ptr<Component>> m_components;
    std::vector<std::unique_ptr<Component>> m_pendingAdditions;
    std::vector<Component*> m_pendingRemovals;
    bool m_isExecutingLifecycle = false;
};

} // namespace Rowl::Scene
