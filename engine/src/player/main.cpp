#include "rowl/c_api.h"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <charconv>
#include <string_view>

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kMaxWindowDimension = 16'384;

bool parseWindowDimension(std::string_view text, uint32_t& output) {
    uint32_t parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed == 0 ||
        parsed > kMaxWindowDimension) {
        return false;
    }
    output = parsed;
    return true;
}

bool requireOptionValue(int& index, int argc, char* argv[], const std::string& option, std::string& output) {
    if (++index >= argc) {
        std::cerr << "Missing value for " << option << "\n";
        return false;
    }
    output = argv[index];
    return true;
}

} // namespace

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
              << "      --gpu-smoke-test     Render one standalone frame, then exit (CI)\n\n"
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
    bool gpuSmokeTest = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "Rowl Engine Standalone Player v1.0.0\n";
            return 0;
        } else if (arg == "-p" || arg == "--project") {
            if (!requireOptionValue(i, argc, argv, arg, projectDir)) return 1;
        } else if (arg == "-s" || arg == "--story") {
            if (!requireOptionValue(i, argc, argv, arg, storyGraphPath)) return 1;
        } else if (arg == "-w" || arg == "--width" || arg == "--height") {
            std::string dimension;
            if (!requireOptionValue(i, argc, argv, arg, dimension)) return 1;
            uint32_t& target = (arg == "--height") ? winHeight : winWidth;
            if (!parseWindowDimension(dimension, target)) {
                std::cerr << "Invalid " << arg << " value '" << dimension << "' (expected 1-"
                          << kMaxWindowDimension << ")\n";
                return 1;
            }
        } else if (arg == "-t" || arg == "--title") {
            if (!requireOptionValue(i, argc, argv, arg, appTitle)) return 1;
        } else if (arg == "--no-vsync") {
            vsync = false;
        } else if (arg == "--gpu-smoke-test") {
            gpuSmokeTest = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    const fs::path baseProj = fs::absolute(projectDir);
    if (!fs::exists(baseProj) || !fs::is_directory(baseProj)) {
        std::cerr << "Project directory does not exist or is not a directory: " << baseProj.string() << "\n";
        return 1;
    }
    if (!storyGraphPath.empty() && (!fs::exists(storyGraphPath) || !fs::is_regular_file(storyGraphPath))) {
        std::cerr << "Story graph file does not exist or is not a regular file: " << storyGraphPath << "\n";
        return 1;
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
    RowlEngine_SetProjectDirectory(engine, baseProj.string().c_str());

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

    if (gpuSmokeTest) {
        // This travels through the same standalone window, VFS-mounted atlas,
        // and render path as the interactive player without leaving CI in a
        // blocking event loop.
        RowlEngine_Step(engine, 1.0F / 60.0F);
        std::cout << "[Player] GPU MSDF smoke frame rendered.\n";
    } else {
        // Start standalone interactive game loop
        RowlEngine_Run(engine);
    }

    // Clean teardown
    RowlEngine_Destroy(engine);
    return 0;
}
