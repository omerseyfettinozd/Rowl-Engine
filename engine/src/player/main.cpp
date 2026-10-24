#include "rowl/c_api.h"
#include "rowl/platform/crash_handler.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>
#include <filesystem>
#include <charconv>
#include <cstdio>
#include <cstdint>
#include <string_view>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#if defined(__ANDROID__)
// SDLActivity calls SDL_main from libmain.so; this header renames main().
#include <SDL3/SDL_main.h>
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

// D01d: caller-buffer text query (size query, then copy). Empty on failure.
template <typename Query>
std::string playerQueryText(RowlEngineHandle engine, Query query) {
    uint32_t required = 0;
    if (query(engine, nullptr, 0, &required) != ROWL_RESULT_OK || required == 0) return {};
    std::string buffer(required, '\0');
    if (query(engine, buffer.data(), required, &required) != ROWL_RESULT_OK || required == 0) return {};
    buffer.resize(required - 1);
    return buffer;
}

nlohmann::json playerParseObject(const std::string& text) {
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    return parsed.is_object() ? parsed : nlohmann::json::object();
}

const char* playerIdentitySourceName(int32_t source) {
    switch (source) {
        case ROWL_GAME_IDENTITY_PACKAGE: return "package";
        case ROWL_GAME_IDENTITY_MANIFEST: return "manifest";
        case ROWL_GAME_IDENTITY_DERIVED: return "derived";
        default: return "none";
    }
}

// D01d: everything a player (or a test) needs to know about where this game
// keeps its identity, saves and profile. Valid after RowlEngine_SetProjectDirectory.
nlohmann::json collectGameIdentity(RowlEngineHandle engine) {
    nlohmann::json out = nlohmann::json::object();
    out["game_id"] = playerQueryText(engine, RowlEngine_GetGameIdUtf8);
    out["identity_source"] = playerIdentitySourceName(RowlEngine_GetGameIdentitySource(engine));
    out["identity_diagnostic"] = playerQueryText(engine, RowlEngine_GetGameIdentityDiagnosticUtf8);
    out["data_dir"] = playerQueryText(engine, RowlEngine_GetGameDataDirectoryUtf8);
    out["save_dir"] = playerQueryText(engine, RowlEngine_GetSaveDirectoryUtf8);
    out["profile_dir"] = playerQueryText(engine, RowlEngine_GetProfileDirectoryUtf8);
    out["profile"] = playerParseObject(playerQueryText(engine, RowlEngine_ProfileGetStatusJson));
    const nlohmann::json migration =
        nlohmann::json::parse(playerQueryText(engine, RowlEngine_GetLastDataMigrationJson), nullptr, false);
    out["migration"] = migration.is_object() ? migration : nlohmann::json(nullptr);
    return out;
}

std::string playerFieldText(const nlohmann::json& object, const char* key, const std::string& fallback) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return fallback;
    return it->is_string() ? it->get<std::string>() : it->dump();
}

// Human-readable block on stdout (diagnosable without a debugger).
void printGameIdentityBlock(const nlohmann::json& info) {
    const nlohmann::json& profile = info["profile"];
    std::cout << "[Player] Game id: " << playerFieldText(info, "game_id", "") << " (source: "
              << playerFieldText(info, "identity_source", "none") << ")\n";
    const std::string identityNote = playerFieldText(info, "identity_diagnostic", "");
    if (!identityNote.empty()) std::cout << "[Player]   identity note: " << identityNote << "\n";
    std::cout << "[Player]   data dir: " << playerFieldText(info, "data_dir", "") << "\n"
              << "[Player]   save dir: " << playerFieldText(info, "save_dir", "") << "\n"
              << "[Player]   profile:  " << playerFieldText(profile, "path", "") << " (status: "
              << playerFieldText(profile, "status", "unmounted")
              << (profile.value("read_only", false) ? ", read-only" : "")
              << ", read ids: " << playerFieldText(profile, "read_count", "0") << ")\n";
    const std::string profileNote = playerFieldText(profile, "diagnostic", "");
    if (!profileNote.empty()) std::cout << "[Player]   profile note: " << profileNote << "\n";
    const nlohmann::json& migration = info["migration"];
    if (migration.is_object()) {
        const auto count = [&](const char* key) {
            const auto it = migration.find(key);
            return (it != migration.end() && it->is_array()) ? it->size() : std::size_t{0};
        };
        std::cout << "[Player]   legacy saves: copied " << count("copied") << ", skipped "
                  << count("skipped") << ", errors " << count("errors") << " from "
                  << playerFieldText(migration, "source", "?") << "\n";
    }
}

// D01d: applies the stored player preferences with the existing public setters
// and returns "key=value" pairs for the ones applied.
//
// Not applied here (no native setter yet - D02/D05): auto_advance_delay (the
// native knob is an additive per-line offset, not an absolute delay),
// skip_mode, auto_enabled. language is applied only when it is not the profile
// default "en": the profile cannot tell "never chosen" from "chose en", and a
// project whose own default locale is not "en" must keep it until the player
// explicitly picks another language (explicit marker: D02).
std::vector<std::string> applyStoredPreferences(RowlEngineHandle engine) {
    std::vector<std::string> applied;
    const nlohmann::json status = playerParseObject(playerQueryText(engine, RowlEngine_ProfileGetStatusJson));
    if (status.value("status", std::string{}) != "loaded") return applied;  // defaults == engine defaults
    const nlohmann::json prefs = playerParseObject(playerQueryText(engine, RowlEngine_ProfileGetJson));

    const auto number = [&](const char* key, float& out) {
        const auto it = prefs.find(key);
        if (it == prefs.end() || !it->is_number()) return false;
        out = it->get<float>();
        return true;
    };
    const auto note = [&](const char* key, const std::string& value) {
        applied.push_back(std::string(key) + "=" + value);
    };
    const auto floatText = [](float value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
        return std::string(text);
    };
    // The value reported is read back from the engine, so the line proves the
    // setter took effect (not just that the profile held a number).
    const auto volume = [&](const char* key, void (*setter)(RowlEngineHandle, float),
                            float (*getter)(RowlEngineHandle)) {
        float value = 0.0f;
        if (number(key, value)) {
            setter(engine, value);
            note(key, floatText(getter(engine)));
        }
    };
    volume("master_volume", RowlEngine_SetMasterVolume, RowlEngine_GetMasterVolume);
    volume("bgm_volume", RowlEngine_SetBgmVolume, RowlEngine_GetBgmVolume);
    volume("voice_volume", RowlEngine_SetVoiceVolume, RowlEngine_GetVoiceVolume);
    volume("sfx_volume", RowlEngine_SetSfxVolume, RowlEngine_GetSfxVolume);
    volume("ambience_volume", RowlEngine_SetAmbienceVolume, RowlEngine_GetAmbienceVolume);
    volume("ui_volume", RowlEngine_SetUiVolume, RowlEngine_GetUiVolume);

    float value = 0.0f;
    if (number("text_speed_multiplier", value)) {  // no native getter
        RowlEngine_SetTextSpeedMultiplier(engine, value);
        note("text_speed_multiplier", floatText(value));
    }
    if (number("text_scale", value)) {
        RowlEngine_SetTextScale(engine, value);
        note("text_scale", floatText(RowlEngine_GetTextScale(engine)));
    }
    if (prefs.value("high_contrast", false)) {
        RowlEngine_SetHighContrast(engine, 1);
        note("high_contrast", RowlEngine_IsHighContrast(engine) != 0 ? "true" : "false");
    }
    if (prefs.value("reduced_motion", false)) {
        RowlEngine_SetReducedMotion(engine, 1);
        note("reduced_motion", RowlEngine_IsReducedMotion(engine) != 0 ? "true" : "false");
    }
    if (prefs.value("mixer_fade_curve", std::string{}) == "EqualPower") {
        RowlEngine_SetFadeCurve(engine, 1);
        note("mixer_fade_curve", RowlEngine_GetFadeCurve(engine) == 1 ? "EqualPower" : "Linear");
    }
    if (const auto it = prefs.find("sfx_pool_depth"); it != prefs.end() && it->is_number_integer()) {
        RowlEngine_SetSfxPoolDepth(engine, it->get<int>());
        note("sfx_pool_depth", std::to_string(RowlEngine_GetSfxPoolDepth(engine)));
    }
    const std::string language = prefs.value("language", std::string{"en"});
    if (language != "en") {
        const std::string current = playerQueryText(engine, RowlEngine_GetLocale);
        if (language != current) {
            if (RowlEngine_SetLocale(engine, language.c_str()) == ROWL_RESULT_OK) {
                note("language", playerQueryText(engine, RowlEngine_GetLocale));
            } else {
                std::cout << "[Player] Stored language '" << language
                          << "' is not available in this game; keeping '" << current << "'.\n";
            }
        }
    }
    return applied;
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
              << "      --title-state <name> Start on the \"title\" screen (New Game / Continue / Load /\n"
              << "                           Preferences / Exit; default) or jump straight into the\n"
              << "                           \"story\" (development)\n"
              << "      --slot <N>           Active quick-save slot 0-9 for F5/F9 (default: 0)\n"
              << "      --no-vsync           Disable vertical sync\n\n"
              << "      --gpu-smoke-test     Render one standalone frame, then exit (CI)\n\n"
              << "      --package-smoke-test Load the packaged VFS graph, render one frame, exercise the\n"
              << "                           player profile (mark the first line read, set a preference,\n"
              << "                           flush), then exit. Writes to the game's profile.\n"
              << "      --print-game-identity Mount the project, print one JSON line (game id, identity\n"
              << "                           source, data/save/profile paths, profile status), exit\n\n"
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
    // D02: "title" (default) opens the native title screen before the
    // interactive loop; "story" skips it (development).
    std::string titleState = "title";
    uint32_t winWidth = 1920;
    uint32_t winHeight = 1080;
    bool vsync = true;
    bool gpuSmokeTest = false;
    bool packageSmokeTest = false;
    bool printGameIdentity = false;
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
        } else if (arg == "--title-state") {
            // Dilim-4 P1-A: salt-okunur seçim; validasyon ayrıştırma
            // sonrasında yapılır, motor durumuna yazılmaz.
            if (!requireOptionValue(i, argc, argv, arg, titleState)) return 1;
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
        } else if (arg == "--print-game-identity") {
            printGameIdentity = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printHelp(argv[0]);
            return 1;
        }
    }

    if (titleState != "title" && titleState != "story") {
        std::cerr << "Invalid --title-state value '" << titleState << "' (expected one of: title, story)\n";
        return 1;
    }
    if (!printGameIdentity) {
        std::cout << "[Player] Title state selected: '" << titleState << "'"
                  << (titleState == "title" ? " (title screen opens before the story).\n"
                                            : " (title screen skipped).\n");
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
    if (printGameIdentity && (packageSmokeTest || gpuSmokeTest)) {
        std::cerr << "--print-game-identity cannot be combined with a smoke test.\n";
        return 1;
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
    // modes retain the standalone path. --print-game-identity needs no window.
    const int initialized = (packageSmokeTest || printGameIdentity)
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

    // D01d: say where this game keeps its identity, saves and profile.
    const nlohmann::json gameInfo = collectGameIdentity(engine);
    if (printGameIdentity) {
        std::cout << gameInfo.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) << std::endl;
        RowlEngine_Destroy(engine);
        return 0;
    }
    printGameIdentityBlock(gameInfo);

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
    } else if (!packageSmokeTest && RowlEngine_GetCurrentNodeId(engine) != 0) {
        // Gerçek ekran denemesi (2026-10-20): sürüm paketinde grafik zaten
        // SetProjectDirectory ile VFS'ten (Assets/packages/game.rowlpkg) yüklenir;
        // fiziksel aday yok diye "boş sahneyle başlıyor" demek yanıltıcıydı.
        std::cout << "[Player] Story graph loaded from the project's packaged VFS.\n";
    } else if (!packageSmokeTest) {
        std::cerr << "[Player] ⚠️  Warning: No story graph file found!\n"
                  << "  Searched in: " << baseProjUtf8 << "\n"
                  << "  Expected: full_story_graph.json (in project root or Assets/)\n"
                  << "  Use --story <path> to specify manually, or --help for usage.\n"
                  << "  Starting with empty default scene...\n";
    }
    refreshCrashSnapshot(engine);

    // D01d: the player's stored preferences (volumes, text speed, ...) take
    // effect before the first frame. Only the existing public setters are used.
    {
        const std::vector<std::string> applied = applyStoredPreferences(engine);
        if (!applied.empty()) {
            std::cout << "[Player] Applied stored preferences:";
            for (const std::string& entry : applied) std::cout << ' ' << entry;
            std::cout << "\n";
        }
    }

    if (packageSmokeTest) {
        // D01d: exercise the profile through the same paths the game uses:
        // entering play state marks the first line read, a preference write
        // goes through the store, and the flush must land on disk (or be
        // refused for a read-only profile - future version / wrong game -
        // which is reported, not fatal). The preference is changed and then
        // restored, so a smoke run never alters the player's own settings.
        RowlEngine_SetPlayState(engine, 1);
        RowlEngine_Step(engine, 1.0F / 60.0F);
        const nlohmann::json before = playerParseObject(playerQueryText(engine, RowlEngine_ProfileGetJson));
        const double originalSpeed = before.value("text_speed_multiplier", 1.0);
        const double probeSpeed = originalSpeed == 1.5 ? 1.25 : 1.5;
        auto setSpeed = [&](double speed) {
            const std::string json = nlohmann::json{{"text_speed_multiplier", speed}}.dump();
            return RowlEngine_ProfileSetPreferencesJson(engine, json.c_str()) == ROWL_RESULT_OK;
        };
        const bool preferenceSet = setSpeed(probeSpeed) && setSpeed(originalSpeed);
        const bool flushed = RowlEngine_ProfileFlush(engine) == ROWL_RESULT_OK;
        const nlohmann::json after = playerParseObject(playerQueryText(engine, RowlEngine_ProfileGetStatusJson));
        const bool readOnly = after.value("read_only", false);
        std::cout << "[Player] Package smoke profile: status=" << playerFieldText(after, "status", "unmounted")
                  << " read_only=" << (readOnly ? "true" : "false")
                  << " read_count=" << playerFieldText(after, "read_count", "0")
                  << " preference_set=" << (preferenceSet ? "true" : "false")
                  << " flushed=" << (flushed ? "true" : "false") << "\n";
        if (readOnly) {
            std::cout << "[Player] Package smoke profile is read-only ("
                      << playerFieldText(after, "status", "unmounted")
                      << "): nothing was written. " << playerFieldText(after, "diagnostic", "") << "\n";
        } else if (!preferenceSet || !flushed) {
            std::cerr << "[Player] Package smoke failed: the player profile could not be written.\n";
            RowlEngine_Destroy(engine);
            return 1;
        }
        // D02: the title screen opens, New Game closes it again.
        {
            bool titleOk = RowlEngine_OpenTitleScreen(engine) == 1;
            nlohmann::json menu = playerParseObject(RowlEngine_GetPauseMenuJson(engine));
            titleOk = titleOk && menu.value("mode", -1) == 3 && menu.value("title_active", false);
            const int continueSlot = menu.value("continue_slot", -1);
            if (titleOk) {
                // Continue is preselected when the game already has a save.
                for (int i = 0; i < 5 && menu.value("selected", 0) != 0; ++i) {
                    RowlEngine_PauseMenuCommand(engine, ROWL_PAUSE_MENU_UP);
                    menu = playerParseObject(RowlEngine_GetPauseMenuJson(engine));
                }
                RowlEngine_PauseMenuCommand(engine, ROWL_PAUSE_MENU_CONFIRM);
                titleOk = RowlEngine_IsTitleScreenActive(engine) == 0 && RowlEngine_IsPaused(engine) == 0;
            }
            if (!titleOk) {
                std::cerr << "[Player] Package smoke failed: the title screen did not open/close.\n";
                RowlEngine_Destroy(engine);
                return 1;
            }
            std::cout << "[Player] Package smoke title: ok continue_slot=" << continueSlot << "\n";
        }
        std::cout << "[Player] Package smoke frame rendered from VFS story graph.\n";
    } else if (gpuSmokeTest) {
        // This travels through the same standalone window, VFS-mounted atlas,
        // and render path as the interactive player without leaving CI in a
        // blocking event loop.
        RowlEngine_Step(engine, 1.0F / 60.0F);
        std::cout << "[Player] GPU MSDF smoke frame rendered.\n";
    } else {
        // D02: the packaged player opens on the title screen.
        if (titleState == "title" && RowlEngine_OpenTitleScreen(engine) != 1) {
            std::cerr << "[Player] Title screen could not be opened; starting the story.\n";
        }
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
