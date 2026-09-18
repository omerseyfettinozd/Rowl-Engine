#include "rowl/c_api.h"
#include "rowl/platform/crash_handler.hpp"
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#include <filesystem>
#include <charconv>
#include <string_view>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

// A2b: player-local UTF-8 <-> path conversion. Rowl::Platform::pathFromUtf8/
// pathToUtf8 live inside RowlEngineCore without an export attribute (hidden
// visibility — only the ROWL_API C surface crosses the .so boundary), so the
// player cannot link them. Same contract, implemented at the entry point:
// UTF-8 in/out, wide path on Windows, native bytes on POSIX.
#if defined(_WIN32)
fs::path playerPathFromUtf8(std::string_view utf8) {
    if (utf8.empty()) return {};
    const int wideLen = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
        static_cast<int>(utf8.size()), nullptr, 0);
    if (wideLen <= 0) return {};
    std::wstring wide(static_cast<size_t>(wideLen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                          static_cast<int>(utf8.size()), wide.data(), wideLen);
    return fs::path(wide);
}

std::string playerPathToUtf8(const fs::path& value) {
    const std::wstring wide = value.wstring();
    if (wide.empty()) return {};
    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return {};
    std::string utf8(static_cast<size_t>(needed), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(),
                          needed, nullptr, nullptr);
    utf8.pop_back();
    return utf8;
}
#else
inline fs::path playerPathFromUtf8(std::string_view utf8) {
    return fs::path(std::string(utf8));
}

inline std::string playerPathToUtf8(const fs::path& value) {
    return value.string();
}
#endif

// Crash snapshot refreshed only at safe points in normal control flow; the
// crash path itself never calls into the engine (see crash_handler.hpp).
void refreshCrashSnapshot(RowlEngineHandle engine) {
    if (engine == nullptr || !Rowl::Platform::RowlCrash_IsInstalled()) {
        return;
    }
    Rowl::Platform::RowlCrash_RefreshSnapshot(RowlEngine_GetLastResultCode(engine),
                                              RowlEngine_GetLastResultOperation(engine),
                                              RowlEngine_GetLastResultTarget(engine));
}

constexpr uint32_t kMaxWindowDimension = 16'384;

// Faz 6 Dilim 10: oyuncu sürümünün tek kaynağı. help başlığı ile --version
// aynı sabiti kullanır; CMake-üretimli header'a çevrilmedi — player
// CMakeLists project() VERSION alanını okumuyor ve ayrı bir üretilmiş
// başlık bu dilimin kapsamını büyütür; literal tek-kaynak yeterlidir.
constexpr std::string_view kPlayerVersion = "1.0.0";

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

bool parseQuickSlot(std::string_view text, int32_t& output) {
    int parsed = -1;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed < 0 || parsed > 9) {
        return false;
    }
    output = static_cast<int32_t>(parsed);
    return true;
}

} // namespace

static void printHelp(const char* progName) {
    std::cout << "=======================================================\n"
              << "🎮 ROWL ENGINE — Standalone Desktop Player v" << kPlayerVersion << "\n"
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
              << "      --slot <N>           Active quick-save slot 0-9 for F5/F9 (default: 0)\n"
              << "      --no-vsync           Disable vertical sync\n\n"
              << "      --gpu-smoke-test     Render one standalone frame, then exit (CI)\n\n"
              << "      --package-smoke-test Load the packaged VFS graph, render one frame, then exit\n\n"
              << "Controls:\n"
              << "  Space / Enter / Click    Advance to next dialogue line / select choice\n"
              << "  F5                       Quick Save (active slot)\n"
              << "  F9                       Quick Load (active slot)\n"
              << "  0-9                      Select the active quick-save slot\n"
              << "  Backspace / Z            Rewind 1 step back in history\n"
              << "  Escape / P               Pause menu (save/load slots, volumes,\n"
              << "                           text speed, exit confirmation)\n"
              << "Pause menu:\n"
              << "  Up / Down / Click        Select item    Left / Right  Adjust value\n"
              << "  Enter / Space / Click    Confirm        Escape        Back / resume\n"
              << "  Exit needs a second confirmation so a stray Escape never quits.\n"
              << "=========================================================================\n";
}

int rowlPlayerMain(int argc, char* argv[]) {
    // Faz 6 Dilim 1: crash-log kurulumu argüman ayrıştırmadan ÖNCE yapılır, o
    // yüzden sonraki her crash yakalanır. Best-effort: dizin açılamazsa
    // sessizce vazgeçilir, normal akış asla kırılmaz.
    // A2b CWD kararı: bu ilk kurulum CWD-göreli "crash-logs"a yazar — proje
    // kökü belli olur olmaz proje-göreli "<project>/crash-logs"a taşınır
    // (aşağıda); taşınamazsa bu kurulum fallback olarak kalır.
    {
        std::error_code dirEc;
        std::filesystem::create_directories("crash-logs", dirEc);
        if (!dirEc) {
            Rowl::Platform::RowlCrash_Install("crash-logs", argc, argv);
        }
    }

    std::string projectDir = ".";
    std::string storyGraphPath = "";
    std::string appTitle = "Rowl Game";
    uint32_t winWidth = 1920;
    uint32_t winHeight = 1080;
    bool vsync = true;
    bool gpuSmokeTest = false;
    bool packageSmokeTest = false;
    int32_t quickSlot = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp(argv[0]);
            return 0;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "Rowl Engine Standalone Player v" << kPlayerVersion << "\n";
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
        } else if (arg == "--slot") {
            std::string slotText;
            if (!requireOptionValue(i, argc, argv, arg, slotText)) return 1;
            if (!parseQuickSlot(slotText, quickSlot)) {
                std::cerr << "Invalid " << arg << " value '" << slotText << "' (expected 0-9)\n";
                return 1;
            }
        } else if (arg == "--gpu-smoke-test") {
            gpuSmokeTest = true;
        } else if (arg == "--package-smoke-test") {
            packageSmokeTest = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    // A2b: argv UTF-8 sözleşmesi. POSIX'te argv zaten UTF-8'dir; Windows'ta
    // main'in char* argv'si ANSI-codepage mojibake üretir (non-ASCII proje
    // yolu sessizce yanlış yere çözülür). wmain UTF-16 alır, burada bir kez
    // UTF-8'e çevrilir — aşağısı her platformda UTF-8 görür.
    const fs::path baseProjIn = playerPathFromUtf8(projectDir);
    std::error_code baseEc;
    const fs::path baseProj = fs::absolute(baseProjIn, baseEc);
    if (baseEc || !fs::exists(baseProj, baseEc) || baseEc ||
        !fs::is_directory(baseProj, baseEc) || baseEc) {
        std::cerr << "Project directory does not exist or is not a directory: " << projectDir << "\n";
        return 1;
    }
    fs::path storyPath;
    if (!storyGraphPath.empty()) {
        storyPath = playerPathFromUtf8(storyGraphPath);
        std::error_code storyEc;
        if (!fs::exists(storyPath, storyEc) || storyEc ||
            !fs::is_regular_file(storyPath, storyEc) || storyEc) {
            std::cerr << "Story graph file does not exist or is not a regular file: " << storyGraphPath << "\n";
            return 1;
        }
    }
    if (packageSmokeTest && !storyGraphPath.empty()) {
        std::cerr << "--package-smoke-test requires the graph to come from the packaged VFS; omit --story.\n";
        return 1;
    }

    // A2b crash-log CWD kararı (uygulama): loglar oyuna aittir, fırlatılan
    // CWD'ye değil — "<project>/crash-logs"a taşınır. Taşınamazsa (salt-okunur
    // proje dizini vb.) baştaki CWD kurulumu fallback olarak yaşar; akış kırılmaz.
    {
        std::error_code crashEc;
        const fs::path crashDir = baseProj / "crash-logs";
        std::filesystem::create_directories(crashDir, crashEc);
        if (!crashEc) {
            Rowl::Platform::RowlCrash_Uninstall();
            const std::string crashDirUtf8 = playerPathToUtf8(crashDir);
            Rowl::Platform::RowlCrash_Install(crashDirUtf8.c_str(), argc, argv);
        }
    }

    // Allocate engine instance via C API
    RowlEngineHandle engine = RowlEngine_Create();
    if (!engine) {
        std::cerr << "Failed to allocate Rowl Engine handle!" << std::endl;
        return 1;
    }

    // Package smoke uses the normal offscreen engine so it runs on desktop CI
    // without opening a persistent native window. Interactive and GPU smoke
    // modes retain the standalone path.
    const int initialized = packageSmokeTest
        ? RowlEngine_Init(engine, winWidth, winHeight, vsync ? 1 : 0)
        : RowlEngine_InitStandalone(engine, appTitle.c_str(), winWidth, winHeight, vsync ? 1 : 0);
    if (!initialized) {
        std::cerr << "Failed to initialize Rowl Engine!" << std::endl;
        RowlEngine_Destroy(engine);
        return 1;
    }

    // MS-6: active quick-save slot for F5/F9 (default 0, or --slot N).
    if (RowlEngine_SetQuickSaveSlot(engine, quickSlot) != 1) {
        std::cerr << "Failed to select quick-save slot " << quickSlot << "\n";
        RowlEngine_Destroy(engine);
        return 1;
    }

    // Set project root directory (isolates and mounts project VFS).
    // A2b: pathToUtf8 — path::string() Windows'ta ANSI-codepage'ten geçer ve
    // non-ASCII kökte FIRLATIR (fix-5 bug sınıfı); C API sözleşmesi UTF-8'dir.
    const std::string baseProjUtf8 = playerPathToUtf8(baseProj);
    RowlEngine_SetProjectDirectory(engine, baseProjUtf8.c_str());
    refreshCrashSnapshot(engine);

    // The release contract loads its graph through game.rowlpkg.  Physical
    // --story and legacy loose-file discovery remain available for development.
    if (packageSmokeTest) {
        if (!RowlEngine_LoadStoryGraphFromVfs(engine, "json/full_story_graph.json")) {
            std::cerr << "[Player] Package smoke failed: missing json/full_story_graph.json in VFS.\n";
            RowlEngine_Destroy(engine);
            return 1;
        }
    } else if (storyGraphPath.empty()) {
        // Explicit project root only; never resolve from the launch CWD.
        const std::vector<fs::path> candidates = {
            baseProj / "full_story_graph.json",
            baseProj / "Assets" / "full_story_graph.json",
            baseProj / "Assets" / "json" / "full_story_graph.json",
            baseProj / "story_graph.json"
        };
        for (const auto& candidate : candidates) {
            // A2b: error_code probe — main'de yakalanmamış throwing exists,
            // hostile kökte terminate'e kadar gider (unhandled exception).
            std::error_code probeEc;
            const bool usable = fs::exists(candidate, probeEc) && !probeEc &&
                fs::is_regular_file(candidate, probeEc) && !probeEc;
            if (usable) {
                storyPath = candidate;
                // A2b: candidate.string() ANSI-ceviriminden gecer;
                // downstream UTF-8 bekler (C API + log + tekrar-probe).
                storyGraphPath = playerPathToUtf8(candidate);
                break;
            }
        }
    }

    if (!packageSmokeTest && !storyGraphPath.empty() && !storyPath.empty()) {
        RowlEngine_LoadStoryGraph(engine, storyGraphPath.c_str());
    } else if (!packageSmokeTest) {
        std::cerr << "[Player] ⚠️  Warning: No story graph file found!\n"
                  << "  Searched in: " << baseProjUtf8 << "\n"
                  << "  Expected: full_story_graph.json (in project root or Assets/)\n"
                  << "  Use --story <path> to specify manually, or --help for usage.\n"
                  << "  Starting with empty default scene...\n";
    }
    refreshCrashSnapshot(engine);

    if (packageSmokeTest) {
        RowlEngine_Step(engine, 1.0F / 60.0F);
        std::cout << "[Player] Package smoke frame rendered from VFS story graph.\n";
    } else if (gpuSmokeTest) {
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

#if defined(_WIN32)
// A2b: Windows argv UTF-8 kapısı. main'in char* argv'si ANSI-codepage'ten
// gecer — non-ASCII proje/yolu mojibake olur. wmain UTF-16 alır; burada bir
// kez UTF-8'e cevrilir, rowlPlayerMain her platformda UTF-8 gorur.
int wmain(int argc, wchar_t* argv[]) {
    std::vector<std::string> utf8Args;
    utf8Args.reserve(static_cast<size_t>(argc));
    std::vector<char*> narrowArgv;
    narrowArgv.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        // needed counts the terminating NUL; the string keeps bytes only.
        const int needed = ::WideCharToMultiByte(
            CP_UTF8, 0, argv[i], -1, nullptr, 0, nullptr, nullptr);
        std::string converted;
        if (needed > 1) {
            converted.resize(static_cast<size_t>(needed));
            ::WideCharToMultiByte(CP_UTF8, 0, argv[i], -1, converted.data(),
                                  needed, nullptr, nullptr);
            converted.pop_back();
        }
        utf8Args.push_back(std::move(converted));
        narrowArgv.push_back(utf8Args.back().data());
    }
    return rowlPlayerMain(argc, narrowArgv.data());
}
#else
int main(int argc, char* argv[]) {
    return rowlPlayerMain(argc, argv);
}
#endif
