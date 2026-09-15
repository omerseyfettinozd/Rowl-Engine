/**
 * test_platform_host.cpp — minimum native-host boundary integration.
 */
#include "rowl_test_harness.hpp"

#include <deque>
#include <sstream>

namespace {

class FakePlatformHost final : public Rowl::Platform::PlatformHost {
public:
    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override {
        if (path != assetPath) return nullptr;
        return std::make_unique<std::istringstream>(assetJson);
    }

    std::filesystem::path writableSavePath() const override { return savePath; }
    Rowl::Platform::LifecycleState lifecycleState() const override { return lifecycle; }

    std::vector<Rowl::Platform::RuntimeInputEvent> takeInputEvents() override {
        std::vector<Rowl::Platform::RuntimeInputEvent> result;
        while (!input.empty()) {
            result.push_back(input.front());
            input.pop_front();
        }
        return result;
    }

    Rowl::Platform::RenderSurface renderSurface() const override { return surface; }
    Rowl::Platform::AudioFocus audioFocus() const override { return focus; }

    std::string assetPath = "json/full_story_graph.json";
    std::string assetJson = R"({
        "format_version": 4,
        "start_node_id": 701,
        "nodes": [
            {"id": 701, "speaker": "Host", "dialogue": "One", "next_nodes": [{"id": 702}]},
            {"id": 702, "speaker": "Host", "dialogue": "Two", "next_nodes": []}
        ]
    })";
    std::filesystem::path savePath;
    Rowl::Platform::LifecycleState lifecycle = Rowl::Platform::LifecycleState::Active;
    Rowl::Platform::RenderSurface surface{
        Rowl::Platform::RenderSurfaceKind::Offscreen, nullptr, 320, 180};
    Rowl::Platform::AudioFocus focus = Rowl::Platform::AudioFocus::Granted;
    std::deque<Rowl::Platform::RuntimeInputEvent> input;
};

} // namespace

void test_platform_host() {
    TEST_SECTION("Minimum PlatformHost Boundary");

    // Faz 4.5 Dilim 4: the base carries safe desktop defaults (no pure
    // virtuals), so an unknown/future host degrades instead of failing to link.
    {
        Rowl::Platform::PlatformHost bare;
        if (bare.openAssetStream("anything") != nullptr ||
            !bare.writableSavePath().empty() ||
            bare.lifecycleState() != Rowl::Platform::LifecycleState::Active ||
            !bare.takeInputEvents().empty() ||
            bare.renderSurface().kind != Rowl::Platform::RenderSurfaceKind::Automatic ||
            bare.audioFocus() != Rowl::Platform::AudioFocus::Granted) {
            std::cerr << "Bare PlatformHost did not degrade into safe defaults" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Bare PlatformHost Degrades Into Safe Desktop Defaults");

    const std::string unicodeRootUtf8 = "/tmp/Rowl-Çağrı-玩家";
    const auto unicodePath = Rowl::Platform::pathFromUtf8(unicodeRootUtf8);
    const auto unicodeLayout = Rowl::Platform::makeUserDataDirectories(unicodePath);
    if (unicodeLayout.saves.filename() != "saves" ||
        unicodeLayout.profiles.filename() != "profiles" ||
        unicodeLayout.saves.parent_path() != unicodeLayout.profiles.parent_path() ||
        Rowl::Platform::pathToUtf8(unicodePath) != unicodeRootUtf8 ||
        !Rowl::Platform::pathFromUtf8("").empty()) {
        std::cerr << "Unicode user-data layout was not preserved" << std::endl;
        exit(1);
    }
    auto defaultVfs = std::make_shared<Rowl::VFS::VFSManager>();
    Rowl::Platform::DefaultPlatformHost defaultHost(defaultVfs);
    if (defaultHost.writableSavePath().empty() ||
        defaultHost.writableProfilePath().empty() ||
        !defaultHost.writableSavePath().is_absolute() ||
        !defaultHost.writableProfilePath().is_absolute() ||
        defaultHost.writableSavePath() == defaultHost.writableProfilePath()) {
        std::cerr << "Default host did not resolve distinct absolute user directories" << std::endl;
        exit(1);
    }

    const auto saveRoot = std::filesystem::temp_directory_path() /
        ("rowl_platform_host_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    auto vfs = std::make_shared<Rowl::VFS::VFSManager>();
    auto host = std::make_shared<FakePlatformHost>();
    host->savePath = saveRoot;
    auto context = std::make_shared<Rowl::Core::RuntimeContext>(vfs, host);
    Rowl::Core::Engine engine(context);

    Rowl::Core::EngineConfig config;
    config.virtualWidth = 640;
    config.virtualHeight = 360;
    if (!engine.initialize(config)) {
        std::cerr << "Engine could not initialize through injected PlatformHost" << std::endl;
        exit(1);
    }
    if (engine.getPlatformHost().get() != host.get() ||
        !engine.getWindow() || !engine.getWindow()->isOffscreen() ||
        engine.getWindow()->getWidth() != 320 || engine.getWindow()->getHeight() != 180) {
        std::cerr << "PlatformHost render surface was not honored" << std::endl;
        exit(1);
    }
    if (engine.getCurrentNodeId() != 701 || engine.getActiveDialogue() != "One") {
        std::cerr << "PlatformHost asset stream did not load the initial story graph" << std::endl;
        exit(1);
    }
    if (engine.getSaveDirectory() != saveRoot.string() || !engine.saveGameSlot(0) ||
        !std::filesystem::is_regular_file(saveRoot / "save_slot_0.json")) {
        std::cerr << "PlatformHost writable save path was not used" << std::endl;
        exit(1);
    }

    host->input.push_back({Rowl::Platform::RuntimeInputEvent::Type::Advance});
    engine.step(1.0f / 60.0f);
    if (engine.getCurrentNodeId() != 702) {
        std::cerr << "PlatformHost input was not dispatched to the runtime" << std::endl;
        exit(1);
    }

    engine.resetToStartNode();
    host->lifecycle = Rowl::Platform::LifecycleState::Suspended;
    host->input.push_back({Rowl::Platform::RuntimeInputEvent::Type::Advance});
    engine.step(1.0f / 60.0f);
    if (engine.getCurrentNodeId() != 701 || !engine.getAudio()->isOutputSuspended()) {
        std::cerr << "Suspended lifecycle did not pause runtime work and audio" << std::endl;
        exit(1);
    }
    host->lifecycle = Rowl::Platform::LifecycleState::Active;
    engine.step(1.0f / 60.0f);
    if (engine.getCurrentNodeId() != 702 || engine.getAudio()->isOutputSuspended()) {
        std::cerr << "Active lifecycle did not resume queued host input and audio" << std::endl;
        exit(1);
    }

    host->focus = Rowl::Platform::AudioFocus::Lost;
    engine.step(1.0f / 60.0f);
    if (!engine.getAudio()->isOutputSuspended()) {
        std::cerr << "Lost PlatformHost audio focus did not suspend output" << std::endl;
        exit(1);
    }
    host->focus = Rowl::Platform::AudioFocus::Granted;
    engine.step(1.0f / 60.0f);
    if (engine.getAudio()->isOutputSuspended()) {
        std::cerr << "Restored PlatformHost audio focus did not resume output" << std::endl;
        exit(1);
    }

    host->lifecycle = Rowl::Platform::LifecycleState::Stopping;
    engine.step(1.0f / 60.0f);
    if (engine.isRunning()) {
        std::cerr << "Stopping PlatformHost lifecycle did not stop the runtime" << std::endl;
        exit(1);
    }

    engine.shutdown();
    std::error_code cleanupError;
    std::filesystem::remove_all(saveRoot, cleanupError);
    TEST_PASS("asset, save, lifecycle, input, render surface, and audio focus are host-owned");
}
