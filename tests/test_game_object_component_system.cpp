/**
 * test_game_object_component_system.cpp — GameObject/component system (with local test components).
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

class VelocityComponent : public Rowl::Scene::Component {
public:
    VelocityComponent(float vx, float vy) : m_vx(vx), m_vy(vy) {}

    void onUpdate(float deltaTime) override {
        if (auto* tf = getOwner()->getTransform()) {
            tf->translate(m_vx * deltaTime, m_vy * deltaTime);
        }
    }

    float getVx() const { return m_vx; }
    float getVy() const { return m_vy; }

private:
    float m_vx = 0.0f;
    float m_vy = 0.0f;
};

class LifecycleProbeComponent : public Rowl::Scene::Component {
public:
    void onAwake() override { ++awake; }
    void onEnable() override { ++enabled; }
    void onStart() override { ++started; }
    void onUpdate(float) override { ++updated; }
    void onLateUpdate(float) override { ++lateUpdated; }
    void onDisable() override { ++disabled; }
    void onDestroy() override { ++destroyed; }

    int awake = 0;
    int enabled = 0;
    int started = 0;
    int updated = 0;
    int lateUpdated = 0;
    int disabled = 0;
    int destroyed = 0;
};

class SelfRemovingComponent : public Rowl::Scene::Component {
public:
    explicit SelfRemovingComponent(int* updateCount) : m_updateCount(updateCount) {}
    void onUpdate(float) override {
        if (m_updateCount) ++*m_updateCount;
        getOwner()->removeComponent(this);
    }
private:
    int* m_updateCount = nullptr;
};

void test_game_object_component_system() {
    TEST_SECTION("Entity-Component & GameObject Subsystem");

    // 1. Create empty scene and game object
    Rowl::Scene::Scene scene;
    auto* hero = scene.createGameObject("Hero");
    if (!hero || hero->getName() != "Hero" || !hero->isActive()) {
        std::cerr << "GameObject creation failed" << std::endl;
        exit(1);
    }
    if (scene.getObjectCount() != 1) {
        std::cerr << "Scene object count mismatch" << std::endl;
        exit(1);
    }

    // Default transform check
    auto* transform = hero->getTransform();
    if (!transform) {
        std::cerr << "GameObject missing default TransformComponent" << std::endl;
        exit(1);
    }
    transform->setPosition(100.0f, 200.0f);
    if (std::abs(transform->getX() - 100.0f) > 0.001f || std::abs(transform->getY() - 200.0f) > 0.001f) {
        std::cerr << "Transform position set failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Empty GameObject Creation with Default TransformComponent");

    // 2. Attach SpriteComponent
    auto* sprite = hero->addComponent<Rowl::Scene::SpriteComponent>("Margot.jpg", 360.0f, 540.0f, 0.95f);
    if (!sprite || !hero->hasComponent<Rowl::Scene::SpriteComponent>()) {
        std::cerr << "SpriteComponent attachment failed" << std::endl;
        exit(1);
    }
    if (hero->getComponent<Rowl::Scene::SpriteComponent>() != sprite) {
        std::cerr << "getComponent<SpriteComponent> mismatch" << std::endl;
        exit(1);
    }
    if (sprite->getOwner() != hero || sprite->getTexturePath() != "Margot.jpg") {
        std::cerr << "SpriteComponent owner or texture mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Attach SpriteComponent to Empty GameObject");

    // 3. Movement simulation via custom Component onUpdate
    auto* vel = hero->addComponent<VelocityComponent>(150.0f, 50.0f); // 150 px/s X, 50 px/s Y
    if (!vel || !hero->hasComponent<VelocityComponent>()) {
        std::cerr << "VelocityComponent attachment failed" << std::endl;
        exit(1);
    }

    // Simulate 2 seconds of updates (e.g. 2 x 1.0s)
    scene.update(1.0f);
    scene.update(1.0f);

    // Initial X: 100 + 2*150 = 400; Initial Y: 200 + 2*50 = 300
    if (std::abs(transform->getX() - 400.0f) > 0.01f || std::abs(transform->getY() - 300.0f) > 0.01f) {
        std::cerr << "Position after onUpdate translation mismatch: X=" << transform->getX() << ", Y=" << transform->getY() << std::endl;
        exit(1);
    }
    TEST_PASS("Component onUpdate Movement Simulation (Position Translation)");

    // 3b. Unity-style lifecycle order and safe mutation during callbacks.
    auto* lifecycleObject = scene.createGameObject("Lifecycle Probe");
    auto* lifecycle = lifecycleObject->addComponent<LifecycleProbeComponent>();
    if (lifecycle->awake != 1 || lifecycle->enabled != 1 || lifecycle->started != 0) {
        std::cerr << "Component Awake/OnEnable lifecycle mismatch" << std::endl;
        exit(1);
    }
    lifecycleObject->update(0.016f);
    if (lifecycle->started != 1 || lifecycle->updated != 1 || lifecycle->lateUpdated != 1) {
        std::cerr << "Component Start/Update/LateUpdate lifecycle mismatch" << std::endl;
        exit(1);
    }
    lifecycle->setEnabled(false);
    lifecycle->setEnabled(true);
    lifecycleObject->setActive(false);
    lifecycleObject->setActive(true);
    if (lifecycle->disabled != 2 || lifecycle->enabled != 3 || lifecycle->started != 1) {
        std::cerr << "Component enable/disable lifecycle mismatch" << std::endl;
        exit(1);
    }
    int selfRemovalUpdates = 0;
    lifecycleObject->addComponent<SelfRemovingComponent>(&selfRemovalUpdates);
    lifecycleObject->update(0.016f);
    if (selfRemovalUpdates != 1 || lifecycleObject->hasComponent<SelfRemovingComponent>()) {
        std::cerr << "Deferred component removal during update failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Unity-style Lifecycle and Deferred Component Mutation");

    // 4. Direct Transform Translation & Scaling
    transform->translate(100.0f, -50.0f);
    transform->setScale(2.0f);
    if (std::abs(transform->getX() - 500.0f) > 0.01f || std::abs(transform->getY() - 250.0f) > 0.01f ||
        std::abs(transform->getScaleX() - 2.0f) > 0.01f) {
        std::cerr << "Direct transform translation/scale failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Direct Transform Translation & Scaling");

    // 5. Engine Step Integration & Scene Rendering to Offscreen Framebuffer
    {
        Rowl::Core::Engine engine;
        Rowl::Core::EngineConfig cfg;
        cfg.appName = "Scene Test";
        cfg.virtualWidth = 1920;
        cfg.virtualHeight = 1080;
        if (!engine.initialize(cfg)) {
            std::cerr << "Engine initialize failed" << std::endl;
            exit(1);
        }

        auto* engineScene = engine.getScene();
        if (!engineScene) {
            std::cerr << "engine.getScene() returned null" << std::endl;
            exit(1);
        }

        auto* renderedObj = engineScene->createGameObject("RenderedSprite");
        renderedObj->getTransform()->setPosition(300.0f, 200.0f);
        renderedObj->addComponent<Rowl::Scene::SpriteComponent>("Margot.jpg", 360.0f, 540.0f);
        renderedObj->addComponent<VelocityComponent>(60.0f, 40.0f);

        // Step engine for 30 frames
        for (int i = 0; i < 30; ++i) {
            engine.step(0.0166f);
        }

        // Object moved during step
        float expectedX = 300.0f + 60.0f * (30 * 0.0166f);
        if (std::abs(renderedObj->getTransform()->getX() - expectedX) > 1.0f) {
            std::cerr << "Engine step scene update mismatch" << std::endl;
            exit(1);
        }

        // Pixel buffer check
        uint32_t pw = 0, ph = 0;
        const uint8_t* pixels = engine.getPixelBuffer(&pw, &ph);
        if (!pixels || pw != 1920 || ph != 1080) {
            std::cerr << "Pixel buffer mismatch in scene rendering" << std::endl;
            exit(1);
        }
        TEST_PASS("Engine Step Loop Integration with Scene Render & Pixel Buffer Output");

        engine.shutdown();
    }

    // 6. Safe Component Removal and Scene Cleanup
    bool removed = hero->removeComponent<VelocityComponent>();
    if (!removed || hero->hasComponent<VelocityComponent>()) {
        std::cerr << "removeComponent failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Dynamic Component Removal (removeComponent<T>)");

    bool destroyed = scene.destroyGameObject(hero);
    bool lifecycleDestroyed = scene.destroyGameObject(lifecycleObject);
    if (!destroyed || !lifecycleDestroyed || scene.getObjectCount() != 0) {
        std::cerr << "destroyGameObject failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Safe GameObject Destruction & Scene Teardown");
}
