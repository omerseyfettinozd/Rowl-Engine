#include "rowl/c_api.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

static void printHelp(const char* progName) {
    std::cout << "=======================================================\n"
              << "🎮 ROWL ENGINE — Standalone Desktop Player v1.0.0\n"
              << "=======================================================\n\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Options:\n"
              << "  -h, --help               Display this help message and exit\n"
              << "  -v, --version            Display version information and exit\n"
              << "  -p, --project <dir>      Root directory of the project (default: current directory)\n"
              << "  -s, --story <file>       Path to story graph JSON (e.g. full_story_graph.json)\n"
              << "  -w, --width <pixels>     Window width in pixels (default: 1920)\n"
              << "      --height <pixels>    Window height in pixels (default: 1080)\n"
              << "  -t, --title <name>       Window title (default: \"Rowl Game\")\n"
              << "      --no-vsync           Disable vertical sync\n\n"
              << "Controls:\n"
              << "  Space / Enter / Click    Advance to next dialogue line / select choice\n"
              << "  F5                       Quick Save (Slot 0)\n"
              << "  F9                       Quick Load (Slot 0)\n"
              << "  Backspace / Z            Rewind 1 step back in history\n"
              << "  Escape                   Exit the game\n"
              << "=========================================================================\n";
}

int main(int argc, char* argv[]) {
    std::string projectDir = ".";
    std::string storyGraphPath = "";
    std::string appTitle = "Rowl Game";
    uint32_t winWidth = 1920;
    uint32_t winHeight = 1080;
    bool vsync = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "Rowl Engine Standalone Player v1.0.0\n";
            return 0;
        } else if ((arg == "-p" || arg == "--project") && i + 1 < argc) {
            projectDir = argv[++i];
        } else if ((arg == "-s" || arg == "--story") && i + 1 < argc) {
            storyGraphPath = argv[++i];
        } else if ((arg == "-w" || arg == "--width") && i + 1 < argc) {
            winWidth = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--height" && i + 1 < argc) {
            winHeight = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if ((arg == "-t" || arg == "--title") && i + 1 < argc) {
            appTitle = argv[++i];
        } else if (arg == "--no-vsync") {
            vsync = false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    // Allocate engine instance via C API
    RowlEngineHandle engine = RowlEngine_Create();
    if (!engine) {
        std::cerr << "Failed to allocate Rowl Engine handle!" << std::endl;
        return 1;
    }

    // Initialize in standalone window mode
    if (!RowlEngine_InitStandalone(engine, appTitle.c_str(), winWidth, winHeight, vsync ? 1 : 0)) {
        std::cerr << "Failed to initialize Rowl Engine standalone window!" << std::endl;
        RowlEngine_Destroy(engine);
        return 1;
    }

    // Set project root directory (isolates and mounts project VFS)
    fs::path baseProj = fs::absolute(projectDir);
    if (fs::exists(baseProj)) {
        RowlEngine_SetProjectDirectory(engine, baseProj.string().c_str());
    }

    // Determine story graph path if not provided
    if (storyGraphPath.empty()) {
        std::vector<fs::path> candidates = {
            baseProj / "full_story_graph.json",
            baseProj / "Assets" / "full_story_graph.json",
            baseProj / "Assets" / "json" / "full_story_graph.json",
            baseProj / "story_graph.json",
            fs::current_path() / "full_story_graph.json",
            fs::current_path() / "Assets" / "full_story_graph.json"
        };
        for (const auto& candidate : candidates) {
            if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
                storyGraphPath = candidate.string();
                break;
            }
        }
    }

    if (!storyGraphPath.empty() && fs::exists(storyGraphPath)) {
        RowlEngine_LoadStoryGraph(engine, storyGraphPath.c_str());
    } else {
        std::cerr << "[Player] ⚠️  Warning: No story graph file found!\n"
                  << "  Searched in: " << baseProj.string() << "\n"
                  << "  Expected: full_story_graph.json (in project root or Assets/)\n"
                  << "  Use --story <path> to specify manually, or --help for usage.\n"
                  << "  Starting with empty default scene...\n";
    }

    // Start standalone interactive game loop
    RowlEngine_Run(engine);

    // Clean teardown
    RowlEngine_Destroy(engine);
    return 0;
}
