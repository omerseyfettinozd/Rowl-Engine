#pragma once

#include "rowl/scene/game_object.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace Rowl::Render {
class Window;
}

namespace Rowl::Scene {

/**
 * Manages the collection and lifecycle of GameObjects in a scene.
 */
class Scene {
public:
    Scene();
    ~Scene();

    // Scenes own their GameObjects
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    // ── Creation & Destruction ──
    GameObject* createGameObject(const std::string& name = "GameObject", const std::string& id = "");
    bool destroyGameObject(GameObject* obj);
    bool destroyGameObject(const std::string& id);
    void clear();

    // ── Queries ──
    GameObject* findGameObject(const std::string& name);
    GameObject* findGameObjectById(const std::string& id);
    const std::vector<std::unique_ptr<GameObject>>& getGameObjects() const { return m_gameObjects; }
    size_t getObjectCount() const { return m_gameObjects.size(); }

    // ── Lifecycle Execution ──
    void update(float deltaTime);
    void render(Rowl::Render::Window* window);

private:
    std::vector<std::unique_ptr<GameObject>> m_gameObjects;
    uint64_t m_nextGeneratedId = 1;
};

} // namespace Rowl::Scene
