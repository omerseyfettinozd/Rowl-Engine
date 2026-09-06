#include "rowl/scene/scene.hpp"
#include <algorithm>

namespace Rowl::Scene {

Scene::Scene() = default;

Scene::~Scene() {
    clear();
}

GameObject* Scene::createGameObject(const std::string& name, const std::string& id) {
    std::string finalId = id.empty() ? ("go_" + std::to_string(m_nextGeneratedId++)) : id;
    auto obj = std::make_unique<GameObject>(name, finalId);
    GameObject* rawPtr = obj.get();
    m_gameObjects.push_back(std::move(obj));
    return rawPtr;
}

bool Scene::destroyGameObject(GameObject* obj) {
    if (!obj) return false;
    auto it = std::find_if(m_gameObjects.begin(), m_gameObjects.end(),
        [obj](const std::unique_ptr<GameObject>& ptr) { return ptr.get() == obj; });
    if (it != m_gameObjects.end()) {
        m_gameObjects.erase(it);
        return true;
    }
    return false;
}

bool Scene::destroyGameObject(const std::string& id) {
    if (id.empty()) return false;
    auto it = std::find_if(m_gameObjects.begin(), m_gameObjects.end(),
        [&id](const std::unique_ptr<GameObject>& ptr) { return ptr->getId() == id; });
    if (it != m_gameObjects.end()) {
        m_gameObjects.erase(it);
        return true;
    }
    return false;
}

void Scene::clear() {
    m_gameObjects.clear();
}

GameObject* Scene::findGameObject(const std::string& name) {
    for (auto& obj : m_gameObjects) {
        if (obj && obj->getName() == name) {
            return obj.get();
        }
    }
    return nullptr;
}

GameObject* Scene::findGameObjectById(const std::string& id) {
    for (auto& obj : m_gameObjects) {
        if (obj && obj->getId() == id) {
            return obj.get();
        }
    }
    return nullptr;
}

void Scene::update(float deltaTime) {
    for (size_t i = 0; i < m_gameObjects.size(); ++i) {
        if (m_gameObjects[i]) {
            m_gameObjects[i]->update(deltaTime);
        }
    }
}

void Scene::render(Rowl::Render::Window* window) {
    if (!window) return;
    for (auto& obj : m_gameObjects) {
        if (obj && obj->isActive()) {
            obj->render(window);
        }
    }
}

} // namespace Rowl::Scene
