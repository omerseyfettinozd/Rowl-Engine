/**
 * test_rc_soak_and_data_safety.cpp — RC soak and data-safety regression.
 *
 * Job 24: long-run frame/transition soak with RSS stability, stressed
 * save/load/rewind integrity, repeated lifecycle + audio-focus
 * interruption cycles, and write-failure safety (read-only save dir keeps
 * the previous valid slot byte-identical via .tmp atomic isolation).
 */
#include "rowl_test_harness.hpp"

#include "rowl/state/session_persistence.hpp"

#include <algorithm>
#include <deque>
#include <sstream>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

#if defined(__linux__)
uint64_t currentRssBytes() {
    std::ifstream statm("/proc/self/statm");
    uint64_t size = 0, resident = 0;
    if (statm >> size >> resident) {
        const uint64_t page = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
        return resident * page;
    }
    return 0;
}
#else
uint64_t currentRssBytes() { return 0; }
#endif

class SoakHost final : public Rowl::Platform::PlatformHost {
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

    // Eight-node ring: every node advances to the next, last wraps to first.
    std::string assetPath = "json/full_story_graph.json";
    std::string assetJson = R"({
        "format_version": 4,
        "start_node_id": 1,
        "nodes": [
            {"id": 1, "speaker": "S", "dialogue": "N1", "next_nodes": [{"id": 2}]},
            {"id": 2, "speaker": "S", "dialogue": "N2", "next_nodes": [{"id": 3}]},
            {"id": 3, "speaker": "S", "dialogue": "N3", "next_nodes": [{"id": 4}]},
            {"id": 4, "speaker": "S", "dialogue": "N4", "next_nodes": [{"id": 5}]},
            {"id": 5, "speaker": "S", "dialogue": "N5", "next_nodes": [{"id": 6}]},
            {"id": 6, "speaker": "S", "dialogue": "N6", "next_nodes": [{"id": 7}]},
            {"id": 7, "speaker": "S", "dialogue": "N7", "next_nodes": [{"id": 8}]},
            {"id": 8, "speaker": "S", "dialogue": "N8", "next_nodes": [{"id": 1}]}
        ]
    })";
    std::filesystem::path savePath;
    Rowl::Platform::LifecycleState lifecycle = Rowl::Platform::LifecycleState::Active;
    Rowl::Platform::RenderSurface surface{
        Rowl::Platform::RenderSurfaceKind::Offscreen, nullptr, 320, 180};
    Rowl::Platform::AudioFocus focus = Rowl::Platform::AudioFocus::Granted;
    std::deque<Rowl::Platform::RuntimeInputEvent> input;
};

bool ringContains(uint64_t id) { return id >= 1 && id <= 8; }

std::string uniqueTempDir(const std::string& prefix) {
    return (std::filesystem::temp_directory_path() /
            (prefix + std::to_string(
                           std::chrono::steady_clock::now().time_since_epoch().count())))
        .string();
}

bool probeWriteBlocked(const std::filesystem::path& dir) {
    const auto probe = dir / ".rowl_write_probe";
    std::ofstream out(probe, std::ios::out | std::ios::trunc);
    const bool blocked = !out.is_open();
    if (out.is_open()) {
        out.close();
        std::error_code ec;
        std::filesystem::remove(probe, ec);
    }
    return blocked;
}

} // namespace

void test_rc_soak_and_data_safety() {
    TEST_SECTION("RC Soak & Data Safety");

    auto vfs = std::make_shared<Rowl::VFS::VFSManager>();
    auto host = std::make_shared<SoakHost>();
    const std::string saveRoot = uniqueTempDir("rowl_rc_soak_");
    host->savePath = saveRoot;
    auto context = std::make_shared<Rowl::Core::RuntimeContext>(vfs, host);
    Rowl::Core::Engine engine(context);

    Rowl::Core::EngineConfig config;
    config.virtualWidth = 320;
    config.virtualHeight = 180;
    if (!engine.initialize(config)) {
        std::cerr << "Soak engine could not initialize offscreen" << std::endl;
        exit(1);
    }
    engine.setPlayState(true);
    engine.resetToStartNode();

    // 1. Frame + transition soak with RSS stability.
    {
        constexpr int kWarmupSteps = 300;
        constexpr int kSoakSteps = 3000;
        constexpr int kAdvanceEvery = 10; // 300 node transitions total
        for (int i = 0; i < kWarmupSteps; ++i) engine.step(1.0f / 60.0f);

        const uint64_t firstRss = currentRssBytes();
        uint64_t minRss = firstRss, maxRss = firstRss;
        uint64_t transitions = 0;
        for (int i = 1; i <= kSoakSteps; ++i) {
            engine.step(1.0f / 60.0f);
            if (i % kAdvanceEvery == 0) {
                engine.advanceToNextNode();
                ++transitions;
            }
            if (i % 300 == 0) {
                const uint64_t rss = currentRssBytes();
                if (rss > 0) {
                    minRss = std::min(minRss == 0 ? rss : minRss, rss);
                    maxRss = std::max(maxRss, rss);
                }
            }
            if (!ringContains(engine.getCurrentNodeId()) || !engine.isRunning()) {
                std::cerr << "Soak diverged at step " << i << std::endl;
                exit(1);
            }
        }
        if (transitions != 300) {
            std::cerr << "Soak did not perform the expected transitions" << std::endl;
            exit(1);
        }
        if (firstRss > 0) {
            constexpr uint64_t kTolerance = 8ULL * 1024ULL * 1024ULL;
            if (maxRss < minRss || maxRss - minRss > kTolerance) {
                std::cerr << "Soak RSS drifted: min=" << minRss << " max=" << maxRss << std::endl;
                exit(1);
            }
        }
        TEST_PASS("3000-frame / 300-transition soak keeps RSS stable");
    }

    // 2. Stressed save / load / rewind integrity.
    {
        for (int i = 0; i < 200; ++i) {
            engine.advanceToNextNode();
            const int32_t slot = static_cast<int32_t>(i % 4);
            if (!engine.saveGameSlot(slot)) {
                std::cerr << "Stressed save failed at iteration " << i << std::endl;
                exit(1);
            }
            if (i % 5 == 0) {
                if (!engine.loadGameSlot(slot) || !ringContains(engine.getCurrentNodeId())) {
                    std::cerr << "Stressed load lost integrity at iteration " << i << std::endl;
                    exit(1);
                }
            }
            if (i % 7 == 0) {
                engine.rewind(1);
                if (!ringContains(engine.getCurrentNodeId())) {
                    std::cerr << "Stressed rewind left the ring at iteration " << i << std::endl;
                    exit(1);
                }
            }
        }
        for (int32_t slot = 0; slot < 4; ++slot) {
            if (!engine.loadGameSlot(slot) || !ringContains(engine.getCurrentNodeId())) {
                std::cerr << "Post-stress slot reload failed for slot " << slot << std::endl;
                exit(1);
            }
        }
        TEST_PASS("200-iteration save/load/rewind stress preserves state");
    }

    // 3. Repeated lifecycle + audio-focus interruption cycles.
    {
        engine.resetToStartNode();
        uint64_t completedCycles = 0;
        for (int cycle = 0; cycle < 20; ++cycle) {
            const uint64_t frozen = engine.getCurrentNodeId();
            host->lifecycle = Rowl::Platform::LifecycleState::Suspended;
            host->focus = Rowl::Platform::AudioFocus::Lost;
            for (int i = 0; i < 5; ++i) engine.step(1.0f / 60.0f);
            if (engine.getCurrentNodeId() != frozen || !engine.getAudio()->isOutputSuspended()) {
                std::cerr << "Interruption cycle " << cycle << " did not freeze work" << std::endl;
                exit(1);
            }
            host->lifecycle = Rowl::Platform::LifecycleState::Active;
            host->focus = Rowl::Platform::AudioFocus::Granted;
            host->input.push_back({Rowl::Platform::RuntimeInputEvent::Type::Advance});
            for (int i = 0; i < 5; ++i) engine.step(1.0f / 60.0f);
            if (!ringContains(engine.getCurrentNodeId()) ||
                engine.getAudio()->isOutputSuspended()) {
                std::cerr << "Interruption cycle " << cycle << " did not resume" << std::endl;
                exit(1);
            }
            ++completedCycles;
        }
        if (completedCycles != 20) {
            std::cerr << "Interruption soak did not complete its cycles" << std::endl;
            exit(1);
        }
        TEST_PASS("20 suspend/lost-focus cycles freeze and resume without hanging");
    }

    engine.shutdown();

    // 4. Write-failure safety: read-only save dir, old slot preserved.
    {
        const std::string dir = uniqueTempDir("rowl_rc_readonly_");
        Rowl::State::SessionPersistence store(dir);
        auto initial = Rowl::State::GameState::createInitialState(4);
        if (!store.saveSlot(initial, 3)) {
            std::cerr << "Could not seed the write-failure fixture" << std::endl;
            exit(1);
        }
        const auto slotPath = std::filesystem::path(dir) / "save_slot_3.json";
        std::ifstream seeded(slotPath, std::ios::binary);
        const std::string before((std::istreambuf_iterator<char>(seeded)),
                                 std::istreambuf_iterator<char>());
        if (before.empty()) {
            std::cerr << "Seeded save slot is empty" << std::endl;
            exit(1);
        }

        std::error_code permError;
        std::filesystem::permissions(dir, std::filesystem::perms::owner_read |
                                              std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::replace, permError);
        if (!permError && probeWriteBlocked(dir)) {
            auto newer = Rowl::State::GameState::createNextState(initial, 5);
            if (store.saveSlot(newer, 3) || store.saveSlot(newer, 4)) {
                std::cerr << "Save into a read-only directory reported success" << std::endl;
                exit(1);
            }
            std::ifstream after(slotPath, std::ios::binary);
            const std::string preserved((std::istreambuf_iterator<char>(after)),
                                        std::istreambuf_iterator<char>());
            if (preserved != before) {
                std::cerr << "Failed save corrupted the previous valid slot" << std::endl;
                exit(1);
            }
            if (std::filesystem::exists(std::filesystem::path(dir) / "save_slot_3.json.tmp") ||
                std::filesystem::exists(std::filesystem::path(dir) / "save_slot_4.json") ||
                std::filesystem::exists(std::filesystem::path(dir) / "save_slot_4.json.tmp")) {
                std::cerr << "Failed save left stray slot or temp files" << std::endl;
                exit(1);
            }
            auto reloaded = store.loadSlotDetailed(3);
            if (!reloaded.succeeded() || reloaded.state->activeNodeId != 4) {
                std::cerr << "Preserved slot did not reload after write failure" << std::endl;
                exit(1);
            }
            TEST_PASS("read-only save dir fails safe and preserves the valid slot");
        } else {
            std::cout << "  (privileged filesystem: read-only gate skipped, "
                         "crash-safety probed via blocked path instead)" << std::endl;
            // A regular file where the directory must be: create_directories
            // throws inside saveSlot and is contained as `false`, everywhere.
            const std::string blocker = uniqueTempDir("rowl_rc_blocker_") + ".file";
            {
                std::ofstream touch(blocker);
                touch << "block";
            }
            Rowl::State::SessionPersistence blocked(blocker);
            if (blocked.saveSlot(initial, 0)) {
                std::cerr << "Save through a file-blocked directory reported success" << std::endl;
                exit(1);
            }
            std::error_code ec;
            std::filesystem::remove(blocker, ec);
            TEST_PASS("blocked save path fails safe without crashing");
        }

        std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace, permError);
        std::error_code cleanupError;
        std::filesystem::remove_all(dir, cleanupError);
    }

    std::error_code cleanupError;
    std::filesystem::remove_all(saveRoot, cleanupError);
}
