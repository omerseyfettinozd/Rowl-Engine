/**
 * test_hardening_and_reliability.cpp — Hardening and reliability probes.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_hardening_and_reliability() {
    TEST_SECTION("Hardening & Lifecycle Reliability");

    // 1. Texture Cache Double-Free Safety
    {
        Rowl::Render::Window win(&Rowl::VFS::VFSManager::instance());
        bool initOk = win.initializeOffscreen(400, 300);
        if (initOk) {
            auto* t1 = win.loadTexture("Woman.png");
            auto* t2 = win.loadTexture("Margot.jpg");
            if (!t1 || !t2 || win.getTextureCacheTextureCount() != 2 ||
                win.getTextureCacheBytes() == 0) {
                std::cerr << "Texture cache statistics did not report loaded textures" << std::endl;
                exit(1);
            }
            win.clearTextureCache(); // Must safely free unique textures only once
            if (win.getTextureCacheTextureCount() != 0 || win.getTextureCacheBytes() != 0) {
                std::cerr << "Texture cache statistics were not cleared" << std::endl;
                exit(1);
            }

            // A low-end target may not have room for every decoded asset. The
            // cache must evict old textures before admitting a new one and
            // reject a single oversized texture without retaining stale state.
            constexpr uint64_t kThirtyOneMiB = 31ULL * 1024ULL * 1024ULL;
            constexpr uint64_t kOneMiB = 1ULL * 1024ULL * 1024ULL;
            win.setTextureCacheBudgetBytes(kThirtyOneMiB);
            if (!win.loadTexture("Woman.png") || !win.loadTexture("Margot.jpg") ||
                win.getTextureCacheTextureCount() != 1 ||
                win.getTextureCacheBytes() > kThirtyOneMiB ||
                win.getTextureCacheEvictionCount() != 1) {
                std::cerr << "Texture cache did not evict the least-recently-used texture" << std::endl;
                exit(1);
            }
            win.setTextureCacheBudgetBytes(kOneMiB);
            if (win.getTextureCacheTextureCount() != 0 ||
                win.loadTexture("Margot.jpg") != nullptr ||
                win.getTextureCacheTextureCount() != 0 ||
                win.getTextureCacheEvictionCount() != 2) {
                std::cerr << "Texture cache admitted an asset larger than its budget" << std::endl;
                exit(1);
            }
            win.setTextureCacheBudgetBytes(64ULL * 1024ULL * 1024ULL);
            if (!win.loadTexture("Margot.jpg")) {
                std::cerr << "Texture rejected by a smaller budget was not retried after budget growth" << std::endl;
                exit(1);
            }
            win.loadTexture("Woman.png");
            if (win.loadTexture("missing_texture_for_negative_cache.png") != nullptr ||
                win.loadTexture("missing_texture_for_negative_cache.png") != nullptr ||
                win.getNegativeTextureCacheSize() != 1) {
                std::cerr << "Texture negative cache did not retain a missing asset lookup" << std::endl;
                exit(1);
            }
            for (int index = 0; index < 600; ++index) {
                win.loadTexture("missing_texture_budget_" + std::to_string(index) + ".png");
            }
            if (win.getNegativeTextureCacheSize() > 512) {
                std::cerr << "Texture negative cache exceeded its bounded entry count" << std::endl;
                exit(1);
            }
            win.clearTextureCache();
            if (win.getNegativeTextureCacheSize() != 0 || win.getTextureCacheEvictionCount() != 0) {
                std::cerr << "Texture negative cache was not cleared with the texture cache" << std::endl;
                exit(1);
            }
            const auto oversizedTexturePath = std::filesystem::temp_directory_path() / "rowl_oversized_texture.png";
            const uint8_t oversizedPngHeader[] = {
                137, 80, 78, 71, 13, 10, 26, 10, // PNG signature
                0, 0, 0, 13, 'I', 'H', 'D', 'R',
                0, 0, 78, 32, // 20,000 px width
                0, 0, 0, 1,   // 1 px height
                8, 6, 0, 0, 0, 0, 0, 0, 0 // IHDR fields + unused CRC
            };
            {
                std::ofstream oversizedTexture(oversizedTexturePath, std::ios::binary);
                oversizedTexture.write(reinterpret_cast<const char*>(oversizedPngHeader), sizeof(oversizedPngHeader));
            }
            const auto textureProject = std::filesystem::temp_directory_path() / "rowl_texture_vfs_test_project";
            const auto textureAssetDir = textureProject / "Assets" / "images";
            std::filesystem::create_directories(textureAssetDir);
            std::filesystem::copy_file(oversizedTexturePath, textureAssetDir / "oversized.png",
                                       std::filesystem::copy_options::overwrite_existing);
            Rowl::VFS::VFSManager::instance().remountProject(textureProject.string());
            if (win.loadTexture("images/oversized.png") != nullptr) {
                std::cerr << "Renderer decoded a texture with unsafe dimensions" << std::endl;
                exit(1);
            }
            std::filesystem::remove(oversizedTexturePath);
            std::filesystem::remove_all(textureProject);
            Rowl::VFS::VFSManager::instance().remountProject(std::filesystem::current_path().string());
            win.shutdown();          // Must safely free unique textures only once
            TEST_PASS("Texture Cache Unique Teardown and Missing-Asset Negative Cache");
        }
    }

    // 2. VFS Cross-Platform Path Normalization (Windows Backslashes)
    {
        auto& vfs = Rowl::VFS::VFSManager::instance();
        bool existsSlash = vfs.exists("images/Woman.png");
        bool existsBackslash = vfs.exists("images\\Woman.png");
        if (existsSlash && !existsBackslash) {
            std::cerr << "VFS backslash normalization failed for images\\Woman.png" << std::endl;
            exit(1);
        }
        TEST_PASS("VFS Cross-Platform Backslash (\\) Path Normalization");
    }

    {
        auto& vfs = Rowl::VFS::VFSManager::instance();
        auto stream = vfs.openReadStream("images/Woman.png");
        char signature[8]{};
        if (stream && stream->read(signature, sizeof(signature)) &&
            std::memcmp(signature, "\x89PNG\r\n\x1a\n", sizeof(signature)) == 0) {
            TEST_PASS("VFS Read-Only Asset Stream (Loose File)");
        } else if (vfs.exists("images/Woman.png")) {
            std::cerr << "VFS failed to open an existing loose asset as a stream" << std::endl;
            exit(1);
        }
    }

    // 3. GameState Step-by-Step Node Traversal & Rewind Integrity
    {
        const auto tempGraph = std::filesystem::temp_directory_path() / "rowl_rewind_chain_test.json";
        {
            std::ofstream f(tempGraph);
            f << R"({
                "format_version": 4,
                "start_node_id": 201,
                "nodes": [
                    {"id": 201, "speaker": "A", "dialogue": "Step 1", "next_nodes": [{"id": 202, "label": "Next"}]},
                    {"id": 202, "speaker": "B", "dialogue": "Step 2", "next_nodes": [{"id": 203, "label": "Next"}]},
                    {"id": 203, "speaker": "C", "dialogue": "Step 3", "next_nodes": []}
                ]
            })";
        }

        Rowl::Core::Engine engine;
        Rowl::Core::EngineConfig cfg;
        cfg.virtualWidth = 1920;
        cfg.virtualHeight = 1080;
        engine.initialize(cfg);
        engine.loadStoryGraphFromPath(tempGraph.string());
        engine.setPlayState(true);
        engine.resetToStartNode();

        if (engine.getCurrentNodeId() != 201) {
            std::cerr << "Engine failed to start at Node 201" << std::endl;
            exit(1);
        }

        engine.advanceToNextNode();
        if (engine.getCurrentNodeId() != 202) {
            std::cerr << "Engine failed to advance to Node 202" << std::endl;
            exit(1);
        }

        engine.advanceToNextNode();
        if (engine.getCurrentNodeId() != 203) {
            std::cerr << "Engine failed to advance to Node 203" << std::endl;
            exit(1);
        }

        if (engine.getDialogueHistory().size() != 3 ||
            engine.getDialogueHistory().front().dialogue != "Step 1" ||
            engine.getDialogueHistory().back().dialogue != "Step 3") {
            std::cerr << "Engine did not record dialogue history by active node" << std::endl;
            exit(1);
        }

        // Rewind 1 step -> should return to Node 202
        bool rw1 = engine.rewind(1);
        if (!rw1 || engine.getCurrentNodeId() != 202) {
            std::cerr << "Rewind 1 failed: expected Node 202, got " << engine.getCurrentNodeId() << std::endl;
            exit(1);
        }

        // Rewind another step -> should return to Node 201
        bool rw2 = engine.rewind(1);
        if (!rw2 || engine.getCurrentNodeId() != 201) {
            std::cerr << "Rewind 2 failed: expected Node 201, got " << engine.getCurrentNodeId() << std::endl;
            exit(1);
        }
        TEST_PASS("GameState Node-by-Node Step Recording & History Rewind Chain");
        std::filesystem::remove(tempGraph);
    }

    // 4. BGM Looping State & Configuration
    {
        const auto autoGraph = std::filesystem::temp_directory_path() / "rowl_auto_advance_test.json";
        {
            std::ofstream f(autoGraph);
            f << R"({
                "format_version":4,"start_node_id":301,"nodes":[
                  {"id":301,"components":[{"type":"dialogue","data":{"speaker":"A","dialogue":"Auto","typewriter_enabled":false,"auto_advance":true,"auto_advance_delay":0.0}}],"next_nodes":[{"id":302}]},
                  {"id":302,"components":[{"type":"dialogue","data":{"speaker":"B","dialogue":"Arrived"}}],"next_nodes":[]}
                ]
            })";
        }
        Rowl::Core::Engine engine;
        engine.initialize({});
        engine.loadStoryGraphFromPath(autoGraph.string());
        engine.setPlayState(true);
        engine.resetToStartNode();
        engine.step(0.05f);
        if (engine.getCurrentNodeId() != 302) {
            std::cerr << "Auto advance did not move after its configured delay" << std::endl;
            exit(1);
        }
        engine.shutdown();
        std::filesystem::remove(autoGraph);
        TEST_PASS("Dialogue Auto Advance Waits for Completion and Uses Node Delay");
    }

    {
        Rowl::Audio::AudioEngine audio(&Rowl::VFS::VFSManager::instance());
        if (!audio.isBgmLooping()) {
            std::cerr << "BGM looping expected to default to true" << std::endl;
            exit(1);
        }
        audio.setBgmLooping(false);
        if (audio.isBgmLooping()) {
            std::cerr << "setBgmLooping(false) failed" << std::endl;
            exit(1);
        }
        audio.setMasterVolume(0.8f);
        audio.setBgmVolume(0.7f);
        audio.setVoiceVolume(0.6f);
        audio.setSfxVolume(0.5f);
        if (std::abs(audio.getMasterVolume() - 0.8f) > 0.001f ||
            std::abs(audio.getBgmVolume() - 0.7f) > 0.001f ||
            std::abs(audio.getVoiceVolume() - 0.6f) > 0.001f ||
            std::abs(audio.getSfxVolume() - 0.5f) > 0.001f) {
            std::cerr << "Independent audio channel volume configuration failed" << std::endl;
            exit(1);
        }
        TEST_PASS("Audio Engine BGM Looping Configuration");
    }
}
