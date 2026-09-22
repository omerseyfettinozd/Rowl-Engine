/**
 * test_d08_rewind_after_load_probe.cpp — D08 save->load->rewind RED/YEŞİL probu.
 *
 * Onarım yönü (a): sınırlı geçmişi serileştirip decode zinciri kur.
 * Kusur satırı: engine/src/state/game_state.cpp:534
 * (`state->previousState = nullptr`) + serializeJson previousState'i
 * dosyaya yazmaz (~319-375).
 *
 * RED (mevcut kod): r==false, step kımıldamaz → exit 1.
 * YEŞİL (onarım sonrası): r==true, step1==step0-1, node bir önceki
 * halka → exit 0.
 *
 * Çalışma: SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ile koşar.
 * rowl_tests gövdesine gömülmez; manuel bağlanır (rowl_engine_objects).
 */
#include "rowl_test_harness.hpp"

#include <deque>
#include <iostream>
#include <sstream>

namespace {

class ProbeHost final : public Rowl::Platform::PlatformHost {
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

    // Sekizli halka: her düğüm sonrakine ilerler, sonuncu başa sarar.
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

}  // namespace

int main() {
    TEST_SECTION("D08 probe (save->load->rewind(1))");

    auto vfs = std::make_shared<Rowl::VFS::VFSManager>();
    auto host = std::make_shared<ProbeHost>();
    host->savePath = std::filesystem::temp_directory_path() /
        ("rowl_d08_probe_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto context = std::make_shared<Rowl::Core::RuntimeContext>(vfs, host);
    Rowl::Core::Engine engine(context);

    Rowl::Core::EngineConfig config;
    config.virtualWidth = 320;
    config.virtualHeight = 180;
    if (!engine.initialize(config)) {
        std::cerr << "D08 probe: engine could not initialize offscreen" << std::endl;
        return 1;
    }
    engine.setPlayState(true);
    engine.resetToStartNode();
    engine.advanceToNextNode();
    engine.advanceToNextNode();

    if (!engine.saveGameSlot(2)) {
        std::cerr << "D08 probe: saveGameSlot(2) failed" << std::endl;
        return 1;
    }
    if (!engine.loadGameSlot(2)) {
        std::cerr << "D08 probe: loadGameSlot(2) failed" << std::endl;
        return 1;
    }

    const uint64_t loadedNode = engine.getCurrentNodeId();
    if (loadedNode < 1 || loadedNode > 8) {
        std::cerr << "D08 probe: loaded node out of ring: " << loadedNode << std::endl;
        return 1;
    }
    // 8'li halkada öncül: ((loaded-1+7)%8)+1.
    const uint64_t wantNode = ((loadedNode - 1 + 7) % 8) + 1;

    const uint64_t step0 = engine.getCurrentStepId();
    const bool r = engine.rewind(1);
    const uint64_t step1 = engine.getCurrentStepId();
    const uint64_t node1 = engine.getCurrentNodeId();

    std::cout << "  [d08-probe] rewind=" << (r ? "true" : "false")
              << " step0=" << step0 << " step1=" << step1
              << " loadedNode=" << loadedNode << " node1=" << node1 << std::endl;

    engine.shutdown();

    if (!r) {
        std::cerr << "D08 probe RED: rewind(1) after load moved nothing "
                     "(chain-less loaded state)"
                  << std::endl;
        return 1;
    }
    if (step1 + 1 != step0) {
        std::cerr << "D08 probe FAIL: step1=" << step1 << " step0=" << step0
                  << " (expected step1==step0-1)" << std::endl;
        return 1;
    }
    if (node1 != wantNode) {
        std::cerr << "D08 probe FAIL: node1=" << node1 << " want=" << wantNode << std::endl;
        return 1;
    }
    TEST_PASS("D08 save->load->rewind(1) restores the predecessor ring");
    return 0;
}
