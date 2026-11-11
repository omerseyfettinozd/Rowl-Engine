/**
 * test_game_data_layout.cpp — D01b: per-game data layout + legacy save migration.
 *
 * Two game ids never share a data dir; ids are validated as path components;
 * the legacy <project>/saves/ JSON copy is copy-only, idempotent, per-game
 * wins, partial failure is reported and never loses data; a read-only install
 * dir no longer refuses the mount; an unwritable user-data dir falls back to
 * the legacy dir with a diagnostic. XDG_DATA_HOME always points at a temp dir.
 */
#include "rowl_test_harness.hpp"
#include "rowl/state/game_data_layout.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace Rowl::State;

void dataRequire(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[game-data-layout] " << message << std::endl;
        exit(1);
    }
}

#ifdef _WIN32
std::string g_localAppData;
bool g_localAppDataSaved = false;
#endif

void setXdg(const fs::path& path) {
#ifdef _WIN32
    // Windows resolves user data from %LOCALAPPDATA% (XDG_DATA_HOME is Linux-only).
    if (!g_localAppDataSaved) {
        const char* previous = std::getenv("LOCALAPPDATA");
        g_localAppData = previous ? previous : "";
        g_localAppDataSaved = true;
    }
    _putenv_s("XDG_DATA_HOME", path.string().c_str());
    _putenv_s("LOCALAPPDATA", path.string().c_str());
#else
    ::setenv("XDG_DATA_HOME", path.string().c_str(), 1);
#endif
}

void restoreLocalAppData() {
#ifdef _WIN32
    if (g_localAppDataSaved) {
        _putenv_s("LOCALAPPDATA", g_localAppData.c_str());
        g_localAppDataSaved = false;
    }
#endif
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string capiText(RowlEngineHandle handle,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    dataRequire(query(handle, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    dataRequire(query(handle, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

std::string saveDirOf(RowlEngineHandle handle) {
    return capiText(handle, RowlEngine_GetSaveDirectoryUtf8);
}

fs::path makeGame(const fs::path& root, const std::string& id) {
    writeText(root / "project.rowlproj", "{\"project_uuid\":\"" + id + "\"}");
    return root;
}

void setReadOnly(const fs::path& dir, bool readOnly) {
    std::error_code ec;
    const auto writeBits = fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;
    fs::permissions(dir, writeBits, readOnly ? fs::perm_options::remove : fs::perm_options::add, ec);
}

bool directoryIsReadOnly(const fs::path& dir) {
    std::error_code ec;
    const auto probe = dir / ".ro_probe";
    const bool created = fs::create_directory(probe, ec);
    if (created) fs::remove(probe, ec);
    return !created;
}

} // namespace

void test_game_data_layout() {
    TEST_SECTION("Per-Game Data Layout (D01b)");

    const fs::path base = fs::temp_directory_path() / "rowl_game_data_layout_probe";
    std::error_code ec;
    setReadOnly(base / "ro_install", false);
    setReadOnly(base / "ro_xdg", false);
    fs::remove_all(base, ec);
    fs::create_directories(base);
    const std::string idA = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90";
    const std::string idB = "11111111-2222-4333-8444-555555555555";
    const fs::path xdg = base / "xdg";
    fs::create_directories(xdg);
    setXdg(xdg);
    const fs::path userRoot = Rowl::Platform::resolveUserDataRoot();
    dataRequire(userRoot == (xdg / "rowl-engine").lexically_normal(), "user data root ignores XDG_DATA_HOME");

    // 1) Id validation + disjoint dirs.
    for (const char* bad : {"", "..", "../x", "a/b", "0B6F1C2E-7D3A-4E58-9C41-2A8F5D6E7B90",
                            "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b9", "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b9g",
                            "..\\..\\0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e"}) {
        GameDataDirectories dirs;
        dataRequire(!isValidGameDataId(bad) && !makeGameDataDirectories(userRoot, bad, dirs),
                    std::string("bad game id accepted: ") + bad);
    }
    GameDataDirectories a, b;
    dataRequire(makeGameDataDirectories(userRoot, idA, a) && makeGameDataDirectories(userRoot, idB, b),
                "valid ids rejected");
    dataRequire(a.saves == userRoot / "games" / idA / "saves" && a.profile == userRoot / "games" / idA / "profile",
                "layout is not <root>/games/<id>/{saves,profile}");
    dataRequire(a.gameDir != b.gameDir && a.saves != b.saves && a.profile != b.profile &&
                    a.gameDir.parent_path() == b.gameDir.parent_path(),
                "two game ids must get disjoint dirs");
    TEST_PASS("Ids validated as path components; two ids -> disjoint dirs");

    // 2) Migration: copy-only, per-game wins, ignores non-json, marker, idempotent.
    const fs::path legacy = base / "legacy" / "saves";
    writeText(legacy / "save_slot_1.json", "legacy-1");
    writeText(legacy / "save_slot_2.json", "legacy-2");
    writeText(legacy / "notes.txt", "not a save");
    writeText(a.saves / "save_slot_2.json", "per-game-2");
    const auto first = migrateLegacySaves(legacy, a);
    dataRequire(first.attempted && first.complete() && first.copied.size() == 1 &&
                    first.copied[0] == "save_slot_1.json" && first.skipped.size() == 1 &&
                    first.skipped[0] == "save_slot_2.json",
                "migration did not copy 1 / skip 1");
    dataRequire(readText(a.saves / "save_slot_1.json") == "legacy-1", "copied content differs");
    dataRequire(readText(a.saves / "save_slot_2.json") == "per-game-2", "per-game file was overwritten");
    dataRequire(!fs::exists(a.saves / "notes.txt"), "non-json file was migrated");
    dataRequire(readText(legacy / "save_slot_1.json") == "legacy-1" &&
                    readText(legacy / "save_slot_2.json") == "legacy-2",
                "migration touched the originals");
    const auto marker = nlohmann::json::parse(readText(a.gameDir / "migrated.json"));
    dataRequire(marker["source"] == legacy.string() && marker["copied"].size() == 1 &&
                    marker["skipped"].size() == 1 && marker["errors"].empty() && marker.contains("time"),
                "marker content wrong");
    writeText(legacy / "save_slot_3.json", "late");
    const auto again = migrateLegacySaves(legacy, a);
    dataRequire(again.alreadyDone && !again.attempted && !fs::exists(a.saves / "save_slot_3.json"),
                "second migration must be a no-op (once)");
    fs::remove(legacy / "save_slot_3.json");
    // No leftovers from the atomic copy.
    for (const auto& entry : fs::directory_iterator(a.saves)) {
        dataRequire(entry.path().filename().string().find(".tmp.") == std::string::npos,
                    "temp file left behind");
    }
    TEST_PASS("Migration copies (never moves), per-game wins, marker written, idempotent");

    // 3) Partial failure: unreadable source -> error reported, nothing lost, retried later.
#ifndef _WIN32
    {
        const fs::path legacy2 = base / "legacy2" / "saves";
        writeText(legacy2 / "save_slot_1.json", "ok-1");
        writeText(legacy2 / "save_slot_2.json", "locked-2");
        fs::permissions(legacy2 / "save_slot_2.json", fs::perms::none);
        std::ifstream probeRead(legacy2 / "save_slot_2.json");
        const bool locked = !probeRead.is_open();
        probeRead.close();
        if (locked) {
            GameDataDirectories c;
            dataRequire(makeGameDataDirectories(userRoot, idB, c), "dirs");
            const auto partial = migrateLegacySaves(legacy2, c);
            dataRequire(partial.attempted && !partial.complete() && partial.errors.size() == 1 &&
                            partial.copied.size() == 1 && fs::exists(c.saves / "save_slot_1.json") &&
                            !fs::exists(c.saves / "save_slot_2.json"),
                        "partial failure must copy the rest and report the error");
            dataRequire(fs::exists(legacy2 / "save_slot_2.json"), "original lost on failure");
            dataRequire(!nlohmann::json::parse(readText(c.gameDir / "migrated.json"))["errors"].empty(),
                        "marker must record the error");
            fs::permissions(legacy2 / "save_slot_2.json", fs::perms::owner_read | fs::perms::owner_write);
            const auto retry = migrateLegacySaves(legacy2, c);
            dataRequire(retry.attempted && retry.complete() && retry.copied.size() == 1 &&
                            retry.skipped.size() == 1 && readText(c.saves / "save_slot_2.json") == "locked-2",
                        "incomplete marker must be retried");
            fs::remove_all(c.gameDir, ec);
        }
    }
    TEST_PASS("Partial failure reports a diagnostic, loses nothing, retries");
#endif

    // 4) C API: SetProjectDirectory wires the per-game dir and migrates once.
    {
        const fs::path gameA = makeGame(base / "gameA", idA);
        const fs::path gameB = makeGame(base / "gameB", idB);
        writeText(gameB / "saves" / "save_slot_1.json", "B-legacy");
        fs::remove_all(a.gameDir, ec);

        RowlEngineHandle h = RowlEngine_Create();
        dataRequire(h != nullptr && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, gameA.string().c_str());
        dataRequire(fs::path(saveDirOf(h)) == a.saves, "save dir is not the per-game dir for A: " + saveDirOf(h));
        dataRequire(fs::path(capiText(h, RowlEngine_GetGameDataDirectoryUtf8)) == a.gameDir,
                    "GetGameDataDirectory mismatch");
        dataRequire(capiText(h, RowlEngine_GetLastDataMigrationJson).empty(), "A had nothing to migrate");
        dataRequire(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_OK, "clean mount must not stamp an error");
        const auto profileA = capiText(h, RowlEngine_GetProfileDirectoryUtf8);
        dataRequire(fs::path(profileA) == a.profile, "profile dir is not per-game");

        RowlEngine_SetProjectDirectory(h, gameB.string().c_str());
        dataRequire(fs::path(saveDirOf(h)) == b.saves, "save dir did not switch to game B");
        dataRequire(fs::path(capiText(h, RowlEngine_GetProfileDirectoryUtf8)) == b.profile, "B profile dir wrong");
        dataRequire(readText(b.saves / "save_slot_1.json") == "B-legacy" &&
                        readText(gameB / "saves" / "save_slot_1.json") == "B-legacy",
                    "game B save was not copied (original kept)");
        const auto report = nlohmann::json::parse(capiText(h, RowlEngine_GetLastDataMigrationJson));
        dataRequire(report["copied"].size() == 1 && report["errors"].empty(), "migration JSON wrong");
        dataRequire(!fs::exists(a.saves / "save_slot_1.json"), "game A sees game B's saves");

        dataRequire(RowlEngine_SaveGameSlot(h, 4) != 0 && fs::exists(b.saves / "save_slot_4.json"),
                    "runtime save did not land in the per-game dir");
        dataRequire(!fs::exists(gameB / "saves" / "save_slot_4.json"), "runtime save went to the install dir");
        uint64_t caps = 0;
        RowlEngine_GetCapabilities(&caps);
        dataRequire((caps & ROWL_ENGINE_CAPABILITY_GAME_DATA) != 0, "capability bit missing");
        RowlEngine_Destroy(h);
        uint32_t required = 7;
        dataRequire(RowlEngine_GetGameDataDirectoryUtf8(h, nullptr, 0, &required) == ROWL_RESULT_INVALID_HANDLE,
                    "dead handle must be rejected");
    }
    TEST_PASS("C API: per-game save/profile dirs, migration report, disjoint games");

    // 5) Moving the game folder keeps the saves (same id, new path).
    {
        const fs::path before = makeGame(base / "move_before", idB);
        fs::rename(before, base / "move_after");
        RowlEngineHandle h = RowlEngine_Create();
        dataRequire(RowlEngine_Init(h, 320, 180, 0) == 1, "init");
        RowlEngine_SetProjectDirectory(h, (base / "move_after").string().c_str());
        dataRequire(fs::path(saveDirOf(h)) == b.saves && fs::exists(b.saves / "save_slot_4.json"),
                    "moving the game folder lost the saves");
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Moved game folder keeps its saves");

    // 6) Derived id (no authored id): project-local saves, nothing shared silently.
    {
        const fs::path bare = base / "bare";
        fs::create_directories(bare);
        RowlEngineHandle h = RowlEngine_Create();
        dataRequire(RowlEngine_Init(h, 320, 180, 0) == 1, "init");
        RowlEngine_SetProjectDirectory(h, bare.string().c_str());
        dataRequire(RowlEngine_GetGameIdentitySource(h) == ROWL_GAME_IDENTITY_DERIVED &&
                        fs::path(saveDirOf(h)) == bare / "saves",
                    "derived id must keep project-local saves");
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Derived id keeps project-local saves");

#ifndef _WIN32
    // 7) Read-only install dir no longer refuses the mount.
    {
        const fs::path install = makeGame(base / "ro_install", idA);
        setReadOnly(install, true);
        if (directoryIsReadOnly(install)) {
            RowlEngineHandle h = RowlEngine_Create();
            dataRequire(RowlEngine_Init(h, 320, 180, 0) == 1, "init");
            RowlEngine_SetProjectDirectory(h, install.string().c_str());
            dataRequire(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_OK &&
                            RowlEngine_GetGameIdentitySource(h) == ROWL_GAME_IDENTITY_MANIFEST &&
                            fs::path(saveDirOf(h)) == a.saves,
                        "read-only install dir must mount with per-game saves");
            RowlEngine_Destroy(h);
        }
        setReadOnly(install, false);
    }
    TEST_PASS("Read-only install dir mounts (saves live in user data)");

    // 8) Unwritable user-data dir -> legacy fallback + diagnostic, no data copied.
    {
        const fs::path roXdg = base / "ro_xdg";
        fs::create_directories(roXdg);
        setReadOnly(roXdg, true);
        if (directoryIsReadOnly(roXdg)) {
            setXdg(roXdg);
            const fs::path game = makeGame(base / "gameC", "22222222-3333-4444-8555-666666666666");
            writeText(game / "saves" / "save_slot_1.json", "C-legacy");
            RowlEngineHandle h = RowlEngine_Create();
            dataRequire(RowlEngine_Init(h, 320, 180, 0) == 1, "init");
            RowlEngine_SetProjectDirectory(h, game.string().c_str());
            dataRequire(fs::path(saveDirOf(h)) == game / "saves", "no legacy fallback on unwritable user data");
            dataRequire(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_IO_ERROR,
                        "fallback must be reported via the last-result error");
            dataRequire(fs::path(capiText(h, RowlEngine_GetGameDataDirectoryUtf8)) == game,
                        "fallback data dir must be the project dir");
            dataRequire(readText(game / "saves" / "save_slot_1.json") == "C-legacy", "legacy save touched");
            RowlEngine_Destroy(h);
            setXdg(xdg);
        }
        setReadOnly(roXdg, false);
    }
    TEST_PASS("Unwritable user-data dir falls back to legacy saves with a diagnostic");
#endif

    restoreLocalAppData();
    fs::remove_all(base, ec);
}
