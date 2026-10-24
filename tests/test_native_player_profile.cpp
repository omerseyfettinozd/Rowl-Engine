/**
 * test_native_player_profile.cpp — D01c-1: native player profile v1
 * (preferences + read set).
 *
 * Locks the store (state/player_profile) and its C API wiring:
 *  - id normalization, round-trip, clamping, atomic multi-key preference merge
 *  - corrupt file -> quarantine (3 kept, oldest dropped) + defaults + writable
 *  - future version / wrong game -> defaults, READ-ONLY, file byte-identical
 *  - unknown top-level fields and `unlocks` survive a rewrite
 *  - bounded read set (never drops existing ids), invalid ids ignored
 *  - debounced autosave, flush on destroy / shutdown / project change
 *  - two games -> disjoint profile files; derived id -> shared dir, no game_id
 *  - engine marks read at presentation (play state only), incl. the start
 *    node's line at the false->true play-state transition (D01d)
 *  - C API contract: null handle, size query, undersized buffer, wrong thread
 *  - golden fixture shared with the managed PlayerProfileStore parity test
 * XDG_DATA_HOME always points at a temp dir.
 */
#include "rowl_test_harness.hpp"
#include "rowl/platform/user_data_directories.hpp"
#include "rowl/state/player_profile.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace Rowl::State;
using nlohmann::json;

void profReq(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[player-profile] " << message << std::endl;
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
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

json readJson(const fs::path& path) {
    return json::parse(readText(path), nullptr, false);
}

std::string capiText(RowlEngineHandle handle,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    profReq(query(handle, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    profReq(query(handle, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

json capiJson(RowlEngineHandle handle,
              RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    json parsed = json::parse(capiText(handle, query), nullptr, false);
    profReq(parsed.is_object(), "C API did not return a JSON object");
    return parsed;
}

fs::path makeGame(const fs::path& root, const std::string& id, bool withGraph = false) {
    writeText(root / "project.rowlproj", "{\"project_uuid\":\"" + id + "\"}");
    if (withGraph) {
        writeText(root / "Assets" / "json" / "full_story_graph.json",
                  R"({"format_version":4,"start_node_id":1,"nodes":[{"id":1,"dialogue":"Hello"}]})");
    }
    return root;
}

RowlEngineHandle newEngine() {
    RowlEngineHandle h = RowlEngine_Create();
    profReq(h != nullptr && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
    return h;
}

const std::string kC1 = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
const std::string kC2 = "11111111-2222-3333-4444-555555555555";
const std::string kC3 = "0f0e0d0c-0b0a-0908-0706-050403020100";

std::vector<std::string> quarantineFiles(const fs::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("player-profile.corrupt-", 0) == 0) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> directoryNames(const fs::path& dir) {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

void test_native_player_profile() {
    TEST_SECTION("Native Player Profile v1 (D01c-1)");

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string previousXdgValue = previousXdg ? previousXdg : "";
    const fs::path base = fs::temp_directory_path() / "rowl_player_profile_probe";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);
    const fs::path xdg = base / "xdg";
    fs::create_directories(xdg);
    setXdg(xdg);
    const fs::path userRoot = Rowl::Platform::resolveUserDataRoot();
    const std::string idA = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90";
    const std::string idB = "11111111-2222-4333-8444-555555555555";

    // 1) Content id normalization (managed ContentIdService.Normalize port).
    {
        const std::string canonical = "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90";
        for (const std::string& form :
             {canonical, std::string("0B6F1C2E-7D3A-4E58-9C41-2A8F5D6E7B90"), "{" + canonical + "}",
              "(" + canonical + ")", "  " + canonical + "\n", std::string("0b6f1c2e7d3a4e589c412a8f5d6e7b90")}) {
            const auto id = normalizeContentId(form);
            profReq(id && *id == canonical, "valid id form not normalized: " + form);
        }
        for (const char* bad :
             {"", "   ", "not-a-uuid", "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b9", "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b900",
              "0b6f1c2e_7d3a_4e58_9c41_2a8f5d6e7b90", "0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b9g",
              "{0b6f1c2e-7d3a-4e58-9c41-2a8f5d6e7b90)", "../../etc/passwd"}) {
            profReq(!normalizeContentId(bad), std::string("invalid id accepted: ") + bad);
        }
    }
    TEST_PASS("Content ids normalize to canonical lowercase UUID; malformed ids are rejected");

    // 2) Round-trip: missing -> defaults -> edit -> flush -> reopen.
    {
        const fs::path dir = base / "roundtrip";
        PlayerProfileStore store;
        profReq(store.status() == PlayerProfileStatus::Unmounted && store.markRead(kC1) == ProfileMutation::Unmounted,
                "unmounted store must reject mutations");
        store.open(dir, idA);
        profReq(store.status() == PlayerProfileStatus::Missing && !store.readOnly() && store.readCount() == 0,
                "no file must give Missing defaults, writable");
        const auto& defaults = store.preferences();
        profReq(defaults.language.empty() && defaults.masterVolume == 1.0f && defaults.textSpeedMultiplier == 1.0f &&
                    defaults.autoAdvanceDelay == 2.0f && defaults.textScale == 1.0f && defaults.skipMode == "read_only" &&
                    !defaults.autoEnabled && defaults.mixerFadeCurve == "Linear" && defaults.sfxPoolDepth == 8 &&
                    !defaults.highContrast && !defaults.reducedMotion,
                "defaults differ from the managed PlayerProfile defaults");
        std::string error;
        profReq(store.setPreferencesJson(R"({"master_volume":0.1,"bgm_volume":0.3,"language":"tr-TR",
                                            "skip_mode":"all","auto_enabled":true,"high_contrast":true,
                                            "text_scale":1.5,"sfx_pool_depth":12,"mixer_fade_curve":"equalpower",
                                            "auto_advance_delay":4.5})",
                                  &error) == ProfileMutation::Ok,
                "preference merge failed: " + error);
        profReq(store.markRead(kC2) == ProfileMutation::Ok && store.markRead(kC1) == ProfileMutation::Ok,
                "mark read failed");
        profReq(store.dirty() && !fs::exists(store.path()), "changes must stay in memory until flushed");
        profReq(store.flush(&error), "flush failed: " + error);
        profReq(!store.dirty() && fs::exists(dir / "player-profile.json"), "flush did not write the profile");
        profReq(directoryNames(dir) == std::vector<std::string>{"player-profile.json"}, "temp residue after flush");

        const json written = readJson(dir / "player-profile.json");
        profReq(written.value("version", 0) == 1 && written.value("game_id", "") == idA &&
                    written.value("language", "") == "tr-tr" && written.value("skip_mode", "") == "all" &&
                    written.value("mixer_fade_curve", "") == "EqualPower" && written.value("sfx_pool_depth", 0) == 12 &&
                    written.value("text_scale", 0.0) == 1.5 && written.value("auto_enabled", false) &&
                    written["unlocks"].is_array() && written["unlocks"].empty(),
                "written document wrong: " + written.dump());
        profReq(written["read_content_ids"].size() == 2 && written["read_content_ids"][0] == kC2 &&
                    written["read_content_ids"][1] == kC1,
                "read ids must be written sorted: " + written["read_content_ids"].dump());
        // 0.1f must be written as 0.1, not as the widened float.
        const std::string raw = readText(dir / "player-profile.json");
        profReq(raw.find("\"master_volume\": 0.1,") != std::string::npos &&
                    raw.find("\"bgm_volume\": 0.3,") != std::string::npos,
                "float values are not written in shortest form: " + raw);

        PlayerProfileStore reopened;
        reopened.open(dir, idA);
        profReq(reopened.status() == PlayerProfileStatus::Loaded && !reopened.readOnly() && reopened.diagnostic().empty(),
                "reopen must load cleanly: " + reopened.diagnostic());
        const auto& p = reopened.preferences();
        profReq(p.masterVolume == 0.1f && p.bgmVolume == 0.3f && p.language == "tr-tr" && p.skipMode == "all" &&
                    p.autoEnabled && p.highContrast && p.textScale == 1.5f && p.sfxPoolDepth == 12 &&
                    p.mixerFadeCurve == "EqualPower" && p.autoAdvanceDelay == 4.5f,
                "preferences did not round-trip");
        profReq(reopened.isRead(kC1) && reopened.isRead(kC2) && !reopened.isRead(kC3) && reopened.readCount() == 2,
                "read set did not round-trip");
        profReq(reopened.toJson() == store.toJson(), "reopened document differs from the written one");
    }
    TEST_PASS("Round-trip: defaults, merge, sorted read ids, shortest floats, no temp residue");

    // 3) Clamping, sanitizing and atomic merge.
    {
        const fs::path dir = base / "clamp";
        writeText(dir / "player-profile.json",
                  R"({"version":1,"language":"fr","master_volume":7,"bgm_volume":-3,"voice_volume":"loud",
                      "ambience_volume":0.5,"ui_volume":2,"text_speed_multiplier":100,"auto_advance_delay":-1,
                      "text_scale":1.3,"skip_mode":"CLOSED","auto_enabled":"yes","mixer_fade_curve":"Equal_Power",
                      "sfx_pool_depth":99,"high_contrast":1,"reduced_motion":true})");
        PlayerProfileStore store;
        store.open(dir, "");
        const auto& p = store.preferences();
        profReq(store.status() == PlayerProfileStatus::Loaded, "clamp file must load");
        profReq(p.language == "fr" && p.masterVolume == 1.0f && p.bgmVolume == 0.0f && p.voiceVolume == 1.0f &&
                    p.ambienceVolume == 0.5f && p.uiVolume == 1.0f && p.textSpeedMultiplier == 4.0f &&
                    p.autoAdvanceDelay == 0.0f && p.textScale == 1.25f && p.skipMode == "off" && !p.autoEnabled &&
                    p.mixerFadeCurve == "EqualPower" && p.sfxPoolDepth == 16 && !p.highContrast && p.reducedMotion,
                "load did not clamp/sanitize like the managed store");

        // Merge is all-or-nothing and rejects unknown keys / wrong types / reserved fields.
        const std::string before = store.toJson();
        std::string error;
        for (const char* bad :
             {"{\"master_volume\":0.5,\"nope\":1}", "{\"master_volume\":\"0.5\"}", "{\"high_contrast\":1}",
              "{\"language\":5}", "{\"skip_mode\":[]}", "{\"read_content_ids\":[]}", "{\"version\":2}",
              "{\"game_id\":\"x\"}", "{\"unlocks\":[]}", "[1]", "not json", "", "{\"sfx_pool_depth\":null}"}) {
            profReq(store.setPreferencesJson(bad, &error) == ProfileMutation::Invalid && !error.empty(),
                    std::string("bad preferences accepted: ") + bad);
            profReq(store.toJson() == before && !store.dirty(), std::string("rejected merge changed state: ") + bad);
        }
        profReq(store.setPreferencesJson("{}") == ProfileMutation::Unchanged && !store.dirty(), "empty merge must be a no-op");
        profReq(store.setPreferencesJson(R"({"master_volume":9,"text_speed_multiplier":0,"auto_advance_delay":1e9,
                                            "text_scale":2,"sfx_pool_depth":0.9,"ui_volume":-1})") == ProfileMutation::Ok,
                "clamped merge failed");
        const auto& q = store.preferences();
        profReq(q.masterVolume == 1.0f && q.textSpeedMultiplier == 0.25f && q.autoAdvanceDelay == 60.0f &&
                    q.textScale == 1.5f && q.sfxPoolDepth == 1 && q.uiVolume == 0.0f,
                "merge did not clamp");
        profReq(store.setPreferencesJson(R"({"skip_mode":"READ-ONLY"})") == ProfileMutation::Ok &&
                    store.preferences().skipMode == "read_only",
                "skip_mode spelling not normalized");
    }
    TEST_PASS("Clamping/sanitizing matches the managed store; merge is all-or-nothing");

    // 4) Corrupt files: quarantine, defaults, still writable, 3 kept (oldest dropped).
    {
        const fs::path dir = base / "corrupt";
        const std::vector<std::pair<std::string, std::string>> cases = {
            {"garbage-1", "garbage-1"},
            {"array-2", "[1,2,3]"},
            {"version-3", R"({"version":"one"})"},
            {"truncated-4", R"({"version":1,"read_content_ids":["aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")"},
            {"empty-5", ""},
        };
        for (size_t i = 0; i < cases.size(); ++i) {
            writeText(dir / "player-profile.json", cases[i].second);
            PlayerProfileStore store;
            store.open(dir, idA);
            profReq(store.status() == PlayerProfileStatus::Corrupt && !store.readOnly() && store.readCount() == 0 &&
                        store.preferences().masterVolume == 1.0f && !store.diagnostic().empty(),
                    "corrupt file must yield Corrupt defaults, writable: " + cases[i].first);
            profReq(!fs::exists(dir / "player-profile.json"), "corrupt original must be moved away: " + cases[i].first);
            const auto quarantined = quarantineFiles(dir);
            profReq(!quarantined.empty() && quarantined.size() <= kPlayerProfileMaxQuarantine,
                    "quarantine count out of bounds after " + cases[i].first);
            profReq(store.diagnostic().find("player-profile.corrupt-") != std::string::npos,
                    "diagnostic must name the quarantine file: " + store.diagnostic());
            // The store is writable: a mutation persists to a fresh player-profile.json.
            profReq(store.markRead(kC1) == ProfileMutation::Ok && store.flush(), "writable after corruption");
            profReq(fs::exists(dir / "player-profile.json"), "fresh profile not written after corruption");
            fs::remove(dir / "player-profile.json");
        }
        // Only the three newest quarantines remain; the oldest were deleted.
        const auto kept = quarantineFiles(dir);
        profReq(kept.size() == 3, "expected exactly 3 quarantine files, got " + std::to_string(kept.size()));
        std::vector<std::string> contents;
        for (const auto& name : kept) contents.push_back(readText(dir / name));
        profReq(contents == std::vector<std::string>{R"({"version":"one"})",
                                                      R"({"version":1,"read_content_ids":["aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee")",
                                                      ""},
                "the OLDEST quarantine files must be the ones deleted");
        // Quarantine names carry a UTC stamp: player-profile.corrupt-YYYYMMDDTHHMMSSZ[_N].json
        const std::string& sample = kept.front();
        profReq(sample.size() >= std::string("player-profile.corrupt-20260101T000000Z.json").size() &&
                    sample.find('T') != std::string::npos && sample.find("Z") != std::string::npos &&
                    sample.compare(sample.size() - 5, 5, ".json") == 0,
                "quarantine name is not UTC-stamped: " + sample);
    }
    TEST_PASS("Corrupt profile: quarantined (3 kept, oldest only deleted), defaults, store stays writable");

    // 5) Future version: defaults, read-only, bytes untouched, nothing else written.
    {
        const fs::path dir = base / "future";
        const std::string original =
            "{\"version\": 2,\n \"language\":\"tr\",  \"read_content_ids\":[\"" + kC3 + "\"],\n \"new_thing\":{\"a\":[1,2]}}";
        writeText(dir / "player-profile.json", original);
        {
            PlayerProfileStore store;
            store.open(dir, idA);
            profReq(store.status() == PlayerProfileStatus::UnsupportedVersion && store.readOnly(),
                    "future version must be UnsupportedVersion + read-only");
            profReq(store.preferences().language.empty() && store.readCount() == 0 && !store.isRead(kC3) &&
                        store.diagnostic().find("v2") != std::string::npos,
                    "future version must load defaults with a diagnostic");
            profReq(store.markRead(kC1) == ProfileMutation::ReadOnly &&
                        store.setPreferencesJson(R"({"master_volume":0.2})") == ProfileMutation::ReadOnly,
                    "read-only store must reject mutations");
            profReq(store.flush() && !store.dirty(), "flush on a read-only store is a successful no-op");
            store.tick(10.0f);
        }
        profReq(readText(dir / "player-profile.json") == original, "future-version file was modified");
        profReq(directoryNames(dir) == std::vector<std::string>{"player-profile.json"},
                "read-only store left extra files (tmp/quarantine/backup)");
    }
    TEST_PASS("Future-version profile: read-only, defaults in memory, file byte-identical");

    // 6) Wrong game; missing game_id; shared (non-enforcing) directory.
    {
        const fs::path dir = base / "wronggame";
        const std::string original = "{\"version\":1,\"game_id\":\"" + idB + "\",\"master_volume\":0.5}";
        writeText(dir / "player-profile.json", original);
        {
            PlayerProfileStore store;
            store.open(dir, idA);
            profReq(store.status() == PlayerProfileStatus::WrongGame && store.readOnly() &&
                        store.preferences().masterVolume == 1.0f && store.diagnostic().find(idB) != std::string::npos,
                    "foreign game_id must be WrongGame + read-only defaults");
            profReq(store.markRead(kC1) == ProfileMutation::ReadOnly && store.flush(), "wrong-game store must not write");
        }
        profReq(readText(dir / "player-profile.json") == original, "wrong-game file was modified");

        // Missing game_id in a per-game dir is accepted and stamped on the next write.
        const fs::path stampDir = base / "stamp";
        writeText(stampDir / "player-profile.json", R"({"version":1,"master_volume":0.5})");
        PlayerProfileStore stamped;
        stamped.open(stampDir, idA);
        profReq(stamped.status() == PlayerProfileStatus::Loaded && stamped.preferences().masterVolume == 0.5f,
                "profile without game_id must load");
        profReq(stamped.markRead(kC1) == ProfileMutation::Ok && stamped.flush(), "write failed");
        profReq(readJson(stampDir / "player-profile.json").value("game_id", "") == idA, "game_id not stamped");

        // Shared directory (empty game id): another game's id is not enforced, and is preserved.
        PlayerProfileStore shared;
        shared.open(dir, "");
        profReq(shared.status() == PlayerProfileStatus::Loaded && !shared.readOnly() &&
                    shared.preferences().masterVolume == 0.5f,
                "non-enforcing open must load a foreign game_id");
        profReq(shared.markRead(kC1) == ProfileMutation::Ok && shared.flush(), "shared write failed");
        profReq(readJson(dir / "player-profile.json").value("game_id", "") == idB, "foreign game_id not preserved");
        // Managed-written files (no game_id, no unlocks) never gain a game_id in a shared dir.
        const fs::path legacyDir = base / "legacy_shared";
        writeText(legacyDir / "player-profile.json", R"({"version":1,"language":"tr"})");
        PlayerProfileStore legacy;
        legacy.open(legacyDir, "");
        profReq(legacy.markRead(kC1) == ProfileMutation::Ok && legacy.flush(), "legacy write failed");
        profReq(!readJson(legacyDir / "player-profile.json").contains("game_id"), "shared dir must not stamp game_id");
    }
    TEST_PASS("Wrong game_id: read-only + untouched; missing id stamped; shared dir not enforced");

    // 7) Unknown top-level fields and `unlocks` (D07-1 object form, unknown inner
    //    keys included) survive a rewrite.
    {
        const fs::path dir = base / "extras";
        writeText(dir / "player-profile.json",
                  R"({"version":1,"zeta":{"deep":[1,2,{"x":null}]},
                      "unlocks":{"cg":["gallery_1"],"music":[],"achievements":[{"id":"a2","at":"2026-10-24T12:00:00Z"}],
                                 "future_kind":[{"id":2,"at":"t"}]},
                      "alpha":"a","read_content_ids":["aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"]})");
        PlayerProfileStore store;
        store.open(dir, idA);
        profReq(store.status() == PlayerProfileStatus::Loaded && store.diagnostic().empty(), "extras file must load");
        profReq(store.setPreferencesJson(R"({"master_volume":0.4})") == ProfileMutation::Ok &&
                    store.markRead(kC3) == ProfileMutation::Ok && store.flush(),
                "mutation failed");
        const json out = readJson(dir / "player-profile.json");
        profReq(out.contains("zeta") && out.contains("alpha") && out.contains("unlocks") &&
                    out.contains("master_volume") && out.contains("read_content_ids"),
                "unknown fields / unlocks missing after rewrite: " + out.dump());
        profReq(out["zeta"] == json::parse(R"({"deep":[1,2,{"x":null}]})") && out["alpha"] == "a" &&
                    out["unlocks"] == json::parse(R"({"cg":["gallery_1"],"music":[],
                        "achievements":[{"id":"a2","at":"2026-10-24T12:00:00Z"}],"future_kind":[{"id":2,"at":"t"}]})") &&
                    out["master_volume"] == 0.4 && out["read_content_ids"].size() == 2,
                "unknown fields / unlocks not preserved: " + out.dump());
        // A second generation keeps them too.
        PlayerProfileStore again;
        again.open(dir, idA);
        profReq(again.markRead(kC2) == ProfileMutation::Ok && again.flush() && readJson(dir / "player-profile.json")["zeta"] == out["zeta"],
                "second rewrite lost unknown fields");
    }
    TEST_PASS("Unknown top-level fields and reserved unlocks are preserved on rewrite");

    // 8) Bounded read set + invalid ids + dedupe.
    {
        const fs::path dir = base / "bounded";
        auto uuid = [](int n) {
            char buffer[40];
            std::snprintf(buffer, sizeof(buffer), "00000000-0000-4000-8000-%012x", n);
            return std::string(buffer);
        };
        PlayerProfileStore store;
        store.open(dir, idA);
        store.setReadLimitForTesting(5);
        for (int i = 0; i < 5; ++i) profReq(store.markRead(uuid(i)) == ProfileMutation::Ok, "fill failed");
        std::string upper = uuid(1);
        for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        profReq(store.markRead(uuid(0)) == ProfileMutation::Unchanged &&
                    store.markRead(" " + uuid(0) + " ") == ProfileMutation::Unchanged &&
                    store.markRead("{" + upper + "}") == ProfileMutation::Unchanged,
                "duplicates (any form) must be Unchanged, not Full");
        profReq(store.readCount() == 5 && store.markRead(uuid(5)) == ProfileMutation::Full && !store.isRead(uuid(5)) &&
                    store.readCount() == 5 && store.diagnostic().find("full") != std::string::npos,
                "limit must refuse NEW ids with a diagnostic");
        for (int i = 0; i < 5; ++i) profReq(store.isRead(uuid(i)), "existing id dropped at the limit");
        profReq(store.flush(), "flush failed");

        // A file already beyond the limit loads completely (never drops existing ids).
        json ids = json::array();
        for (int i = 0; i < 8; ++i) ids.push_back(uuid(i));
        writeText(dir / "player-profile.json", json{{"version", 1}, {"read_content_ids", ids}}.dump());
        PlayerProfileStore big;
        big.setReadLimitForTesting(5);
        big.open(dir, idA);
        profReq(big.readCount() == 8 && big.isRead(uuid(7)) && big.markRead(uuid(9)) == ProfileMutation::Full,
                "over-limit file must keep all ids and refuse new ones");
        profReq(big.setPreferencesJson(R"({"master_volume":0.5})") == ProfileMutation::Ok && big.flush() &&
                    readJson(dir / "player-profile.json")["read_content_ids"].size() == 8,
                "rewrite of an over-limit set dropped ids");

        // Invalid ids are ignored, on load and on mark.
        writeText(dir / "player-profile.json",
                  json{{"version", 1},
                       {"read_content_ids", json::array({kC1, kC1, "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", "junk", 7, nullptr, "", kC2})}}
                      .dump());
        PlayerProfileStore messy;
        messy.open(dir, idA);
        profReq(messy.status() == PlayerProfileStatus::Loaded && messy.readCount() == 2 && messy.isRead(kC1) &&
                    messy.isRead(kC2) && messy.diagnostic().find("malformed") != std::string::npos,
                "load must dedupe, normalize and drop malformed ids with a diagnostic: " + messy.diagnostic());
        for (const char* bad : {"", " ", "junk", "00000000-0000-0000-0000-00000000000", "zzzzzzzz-zzzz-zzzz-zzzz-zzzzzzzzzzzz"}) {
            profReq(messy.markRead(bad) == ProfileMutation::Invalid && !messy.isRead(bad), std::string("invalid id marked: ") + bad);
        }
        profReq(messy.readCount() == 2 && !messy.dirty(), "invalid ids changed the read set");
    }
    TEST_PASS("Read set: bounded (existing ids never dropped), deduped, invalid ids ignored");

    // 9) Debounced autosave + flush on destruction.
    {
        const fs::path dir = base / "debounce";
        {
            PlayerProfileStore store;
            store.open(dir, idA);
            profReq(store.markRead(kC1) == ProfileMutation::Ok, "mark failed");
            store.tick(1.0f);
            profReq(store.dirty() && !fs::exists(dir / "player-profile.json"), "autosave fired before the debounce");
            store.tick(1.1f);
            profReq(!store.dirty() && fs::exists(dir / "player-profile.json"), "autosave did not fire after the debounce");
            profReq(store.markRead(kC2) == ProfileMutation::Ok, "second mark failed");
            store.tick(0.5f);  // stays pending; destructor must still persist it
        }
        PlayerProfileStore reopened;
        reopened.open(dir, idA);
        profReq(reopened.isRead(kC1) && reopened.isRead(kC2), "pending change lost on destruction");

        // A failing write keeps the change pending and backs off the next attempt.
        const fs::path blocked = base / "blocked";
        writeText(blocked, "a file where the profile directory should be");
        PlayerProfileStore failing;
        failing.open(blocked / "sub", idA);
        profReq(failing.markRead(kC1) == ProfileMutation::Ok, "mark on blocked dir failed");
        std::string error;
        profReq(!failing.flush(&error) && !error.empty() && failing.dirty(), "failed write must stay pending with an error");
        failing.tick(5.0f);  // inside the 30 s retry back-off
        profReq(failing.dirty(), "failed write must stay pending");
    }
    TEST_PASS("Autosave is debounced (2 s), destruction flushes, failed writes stay pending");

    // 10) Engine wiring: disjoint per-game files, persistence across engine lifetimes,
    //     flush on shutdown and on project change, derived id shares the legacy dir.
    {
        const fs::path gameA = makeGame(base / "gameA", idA);
        const fs::path gameB = makeGame(base / "gameB", idB);
        const fs::path profileA = userRoot / "games" / idA / "profile" / "player-profile.json";
        const fs::path profileB = userRoot / "games" / idB / "profile" / "player-profile.json";

        RowlEngineHandle h = newEngine();
        auto status = capiJson(h, RowlEngine_ProfileGetStatusJson);
        profReq(status["status"] == "unmounted" && status["read_only"] == true, "pre-mount profile must be unmounted");
        profReq(RowlEngine_ProfileMarkRead(h, kC1.c_str()) == ROWL_RESULT_STATE_ERROR &&
                    RowlEngine_ProfileSetPreferencesJson(h, "{}") == ROWL_RESULT_STATE_ERROR,
                "unmounted profile must reject mutations");
        RowlEngine_SetProjectDirectory(h, gameA.string().c_str());
        status = capiJson(h, RowlEngine_ProfileGetStatusJson);
        profReq(status["status"] == "missing" && status["read_only"] == false && status["read_count"] == 0 &&
                    fs::path(status["path"].get<std::string>()) == profileA && status["game_id"] == idA,
                "mount must load the per-game profile: " + status.dump());
        profReq(RowlEngine_ProfileSetPreferencesJson(h, R"({"master_volume":0.25,"skip_mode":"off"})") == ROWL_RESULT_OK &&
                    RowlEngine_ProfileMarkRead(h, kC1.c_str()) == ROWL_RESULT_OK,
                "mutations failed");
        int32_t isRead = -1;
        profReq(RowlEngine_ProfileIsRead(h, kC1.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 1 &&
                    RowlEngine_ProfileIsRead(h, kC2.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 0,
                "IsRead wrong");
        profReq(!fs::exists(profileA), "nothing may be written before a flush/debounce/shutdown");

        // Project change flushes the previous game's profile; B starts clean and isolated.
        RowlEngine_SetProjectDirectory(h, gameB.string().c_str());
        profReq(fs::exists(profileA) && readJson(profileA).value("game_id", "") == idA &&
                    readJson(profileA)["read_content_ids"].size() == 1,
                "project change must flush the previous profile");
        status = capiJson(h, RowlEngine_ProfileGetStatusJson);
        profReq(status["status"] == "missing" && fs::path(status["path"].get<std::string>()) == profileB &&
                    capiJson(h, RowlEngine_ProfileGetJson)["master_volume"] == 1.0 &&
                    RowlEngine_ProfileIsRead(h, kC1.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 0,
                "game B must not see game A's profile");
        profReq(RowlEngine_ProfileMarkRead(h, kC2.c_str()) == ROWL_RESULT_OK, "B mark failed");
        // Shutdown (no explicit flush) persists; Destroy afterwards is a no-op for the profile.
        RowlEngine_Shutdown(h);
        profReq(fs::exists(profileB) && readJson(profileB)["read_content_ids"] == json::array({kC2}),
                "shutdown must flush pending profile changes (before Destroy)");
        RowlEngine_Destroy(h);
        profReq(readJson(profileA)["read_content_ids"] == json::array({kC1}) &&
                    readJson(profileA)["master_volume"] == 0.25 && readJson(profileA).value("skip_mode", "") == "off",
                "game A profile was disturbed by game B");

        // Reopen game A in a brand-new engine: persisted values come back.
        RowlEngineHandle again = newEngine();
        RowlEngine_SetProjectDirectory(again, gameA.string().c_str());
        status = capiJson(again, RowlEngine_ProfileGetStatusJson);
        const json restored = capiJson(again, RowlEngine_ProfileGetJson);
        profReq(status["status"] == "loaded" && restored["master_volume"] == 0.25 && restored["skip_mode"] == "off" &&
                    status["read_count"] == 1 && RowlEngine_ProfileIsRead(again, kC1.c_str(), &isRead) == ROWL_RESULT_OK &&
                    isRead == 1,
                "profile did not persist across engine lifetimes: " + status.dump());
        // Destroy without Shutdown/Flush also persists (destructor path).
        profReq(RowlEngine_ProfileMarkRead(again, kC3.c_str()) == ROWL_RESULT_OK, "mark failed");
        RowlEngine_Destroy(again);
        profReq(readJson(profileA)["read_content_ids"].size() == 2, "Destroy dropped a pending profile change");

        // Autosave from the engine frame loop (RowlEngine_Step), no explicit flush.
        RowlEngineHandle stepper = newEngine();
        RowlEngine_SetProjectDirectory(stepper, gameB.string().c_str());
        profReq(RowlEngine_ProfileSetPreferencesJson(stepper, R"({"bgm_volume":0.6})") == ROWL_RESULT_OK, "set failed");
        for (int i = 0; i < 4; ++i) RowlEngine_Step(stepper, 0.25f);
        profReq(readJson(profileB).value("bgm_volume", 1.0) == 1.0, "autosave fired before the debounce");
        for (int i = 0; i < 6; ++i) RowlEngine_Step(stepper, 0.25f);
        profReq(readJson(profileB).value("bgm_volume", 1.0) == 0.6, "engine frames did not autosave the profile");
        RowlEngine_Destroy(stepper);

        // Derived id (no authored id): profile beside the project-local saves, game_id never stamped.
        const fs::path bare = base / "bare";
        fs::create_directories(bare);
        RowlEngineHandle derived = newEngine();
        RowlEngine_SetProjectDirectory(derived, bare.string().c_str());
        profReq(RowlEngine_GetGameIdentitySource(derived) == ROWL_GAME_IDENTITY_DERIVED, "bare project must be derived");
        const fs::path bareProfile = bare / "profile";
        status = capiJson(derived, RowlEngine_ProfileGetStatusJson);
        profReq(status["status"] == "missing" && fs::path(status["path"].get<std::string>()) == bareProfile / "player-profile.json" &&
                    status["game_id"] == "",
                "derived id must use <project>/profile without a game id: " + status.dump());
        profReq(RowlEngine_ProfileMarkRead(derived, kC1.c_str()) == ROWL_RESULT_OK && RowlEngine_ProfileFlush(derived) == ROWL_RESULT_OK,
                "derived write failed");
        profReq(!readJson(bareProfile / "player-profile.json").contains("game_id"), "derived profile stamped a game_id");
        profReq(!fs::exists(userRoot / "profiles" / "player-profile.json"), "derived profile leaked into the shared profiles dir");
        RowlEngine_Destroy(derived);
        // Another derived project never sees the first one's read set.
        const fs::path other = base / "bare_other";
        fs::create_directories(other);
        RowlEngineHandle otherEngine = newEngine();
        RowlEngine_SetProjectDirectory(otherEngine, other.string().c_str());
        int32_t otherRead = 1;
        profReq(capiJson(otherEngine, RowlEngine_ProfileGetStatusJson)["status"] == "missing" &&
                    RowlEngine_ProfileIsRead(otherEngine, kC1.c_str(), &otherRead) == ROWL_RESULT_OK && otherRead == 0,
                "two derived projects share a profile");
        RowlEngine_Destroy(otherEngine);
        // A moved derived project (new path => new derived id) keeps its profile, NOT a WrongGame.
        const fs::path bare2 = base / "bare_moved";
        fs::copy(bare, bare2, fs::copy_options::recursive);
        RowlEngineHandle moved = newEngine();
        RowlEngine_SetProjectDirectory(moved, bare2.string().c_str());
        int32_t movedRead = 0;
        profReq(capiJson(moved, RowlEngine_ProfileGetStatusJson)["status"] == "loaded" &&
                    RowlEngine_ProfileIsRead(moved, kC1.c_str(), &movedRead) == ROWL_RESULT_OK && movedRead == 1,
                "moved derived project must keep its profile");
        RowlEngine_Destroy(moved);
    }
    TEST_PASS("Engine: per-game profiles are disjoint, persist across lifetimes, flush on project change/shutdown/destroy/frames");

    // 11) Engine-level future version / corrupt / wrong game through the C API.
    {
        const fs::path gameC = makeGame(base / "gameC", "22222222-3333-4444-8555-666666666666");
        const fs::path dirC = userRoot / "games" / "22222222-3333-4444-8555-666666666666" / "profile";
        const std::string future = "{\"version\":3,\"master_volume\":0.1,\"x\":true}";
        writeText(dirC / "player-profile.json", future);
        RowlEngineHandle h = newEngine();
        RowlEngine_SetProjectDirectory(h, gameC.string().c_str());
        auto status = capiJson(h, RowlEngine_ProfileGetStatusJson);
        profReq(status["status"] == "unsupported_version" && status["read_only"] == true &&
                    !status["diagnostic"].get<std::string>().empty(),
                "future version through the engine: " + status.dump());
        profReq(RowlEngine_ProfileMarkRead(h, kC1.c_str()) == ROWL_RESULT_STATE_ERROR &&
                    RowlEngine_ProfileSetPreferencesJson(h, R"({"master_volume":0.2})") == ROWL_RESULT_STATE_ERROR &&
                    RowlEngine_ProfileFlush(h) == ROWL_RESULT_OK,
                "read-only profile C API contract");
        for (int i = 0; i < 12; ++i) RowlEngine_Step(h, 0.25f);
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        profReq(readText(dirC / "player-profile.json") == future, "future-version file changed through the engine");
        profReq(directoryNames(dirC) == std::vector<std::string>{"player-profile.json"}, "extra files beside a future-version profile");

        // Wrong game through the engine.
        writeText(dirC / "player-profile.json", "{\"version\":1,\"game_id\":\"" + idA + "\"}");
        RowlEngineHandle w = newEngine();
        RowlEngine_SetProjectDirectory(w, gameC.string().c_str());
        profReq(capiJson(w, RowlEngine_ProfileGetStatusJson)["status"] == "wrong_game" &&
                    RowlEngine_ProfileMarkRead(w, kC1.c_str()) == ROWL_RESULT_STATE_ERROR,
                "wrong game through the engine");
        RowlEngine_Destroy(w);

        // Corrupt through the engine: quarantine + writable.
        writeText(dirC / "player-profile.json", "{ definitely not json");
        RowlEngineHandle c = newEngine();
        RowlEngine_SetProjectDirectory(c, gameC.string().c_str());
        status = capiJson(c, RowlEngine_ProfileGetStatusJson);
        profReq(status["status"] == "corrupt" && status["read_only"] == false &&
                    RowlEngine_ProfileMarkRead(c, kC1.c_str()) == ROWL_RESULT_OK && RowlEngine_ProfileFlush(c) == ROWL_RESULT_OK,
                "corrupt through the engine: " + status.dump());
        profReq(!quarantineFiles(dirC).empty() && fs::exists(dirC / "player-profile.json"), "corrupt quarantine through the engine");
        RowlEngine_Destroy(c);
    }
    TEST_PASS("Engine: future-version stays byte-identical, wrong game is read-only, corrupt is quarantined");

    // 12) The engine marks dialogues read when it presents them in play state only.
    {
        const fs::path game = makeGame(base / "gameHook", "33333333-4444-4555-8666-777777777777", true);
        RowlEngineHandle h = newEngine();
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        profReq(RowlEngine_GetCurrentNodeId(h) == 1, "story graph must load for the hook test");
        const std::string scene = R"([
            {"type": "dialogue", "enabled": true, "data": {
                "speaker": "Evelyn", "dialogue": "Hi.", "content_id": "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"}},
            {"type": "dialogue", "enabled": true, "data": {"speaker": "Mina", "dialogue": "Legacy line."}}
        ])";
        const std::string previewScene = R"([
            {"type": "dialogue", "enabled": true, "data": {
                "speaker": "Evelyn", "dialogue": "Preview.", "content_id": "11111111-2222-3333-4444-555555555555"}}
        ])";
        int32_t isRead = -1;
        RowlEngine_SetPlayState(h, 0);  // editor preview / not playing: nothing is read
        RowlEngine_UpdateSceneFromJson(h, previewScene.c_str());
        profReq(RowlEngine_ProfileIsRead(h, kC2.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 0,
                "preview presentation must not mark read");
        // D01d: the preview line is still on screen when play state begins, and
        // from that moment it IS presented to the player -> read at the
        // false->true transition (see the start-node section below).
        RowlEngine_SetPlayState(h, 1);
        profReq(RowlEngine_ProfileIsRead(h, kC2.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 1,
                "the line on screen when play state began was not marked read");
        RowlEngine_UpdateSceneFromJson(h, scene.c_str());
        profReq(RowlEngine_ProfileIsRead(h, kC1.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 1,
                "presented dialogue was not marked read");
        profReq(capiJson(h, RowlEngine_ProfileGetStatusJson)["read_count"] == 2,
                "legacy (id-less) dialogue must never be marked read");
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Engine marks dialogue read at presentation (play state), never legacy lines or previews");

    // 12b) D01d: the start node's dialogue is on screen BEFORE play state; it is
    // marked read at the false->true transition (RowlEngine_Run and the
    // editor's SetPlayState(1) share it) - read state only, no second backlog entry.
    {
        const fs::path game = makeGame(base / "gameFirstLine", "55555555-6666-4777-8888-999999999999");
        writeText(game / "Assets" / "json" / "full_story_graph.json", R"({
            "format_version": 4, "start_node_id": 1, "nodes": [
              {"id": 1, "components": [
                {"type": "dialogue", "id": "d1", "enabled": true, "data": {
                  "speaker": "Evelyn", "dialogue": "First line.", "typewriter_enabled": false,
                  "content_id": "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"}},
                {"type": "dialogue", "id": "d2", "enabled": true, "data": {
                  "speaker": "Mina", "dialogue": "Legacy first line.", "typewriter_enabled": false}}
              ], "next_nodes": []}]})");
        RowlEngineHandle h = newEngine();
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        profReq(RowlEngine_GetCurrentNodeId(h) == 1, "story graph must load for the first-line test");
        int32_t isRead = -1;
        profReq(RowlEngine_ProfileIsRead(h, kC1.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 0,
                "start-node dialogue must not be read before play state");
        RowlEngine_SetPlayState(h, 1);
        profReq(RowlEngine_ProfileIsRead(h, kC1.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 1,
                "start-node dialogue was not marked read when play state began");
        profReq(capiJson(h, RowlEngine_ProfileGetStatusJson)["read_count"] == 1,
                "the id-less start-node line must never be marked read");
        // Re-entering (stop -> play) and a redundant SetPlayState(1) are harmless.
        const std::string historyBefore = RowlEngine_GetDialogueHistoryJson(h);
        RowlEngine_SetPlayState(h, 1);
        RowlEngine_SetPlayState(h, 0);
        RowlEngine_SetPlayState(h, 1);
        profReq(capiJson(h, RowlEngine_ProfileGetStatusJson)["read_count"] == 1, "read count drifted");
        profReq(historyBefore == RowlEngine_GetDialogueHistoryJson(h),
                "the play-state transition must not record backlog history");
        RowlEngine_Destroy(h);
    }
    TEST_PASS("Start-node dialogue is marked read when play state begins (no duplicate backlog)");

    // 13) C API contract.
    {
        uint64_t capabilities = 0;
        RowlEngine_GetCapabilities(&capabilities);
        profReq((capabilities & ROWL_ENGINE_CAPABILITY_PLAYER_PROFILE) != 0 &&
                    ROWL_ENGINE_CAPABILITY_PLAYER_PROFILE == UINT64_C(16777216),
                "capability bit missing");
        uint32_t required = 7;
        int32_t isRead = 5;
        char tiny[2] = {'x', 'x'};
        profReq(RowlEngine_ProfileGetJson(nullptr, nullptr, 0, &required) == ROWL_RESULT_INVALID_HANDLE &&
                    RowlEngine_ProfileGetStatusJson(nullptr, nullptr, 0, &required) == ROWL_RESULT_INVALID_HANDLE &&
                    RowlEngine_ProfileSetPreferencesJson(nullptr, "{}") == ROWL_RESULT_INVALID_HANDLE &&
                    RowlEngine_ProfileMarkRead(nullptr, kC1.c_str()) == ROWL_RESULT_INVALID_HANDLE &&
                    RowlEngine_ProfileIsRead(nullptr, kC1.c_str(), &isRead) == ROWL_RESULT_INVALID_HANDLE && isRead == 0 &&
                    RowlEngine_ProfileFlush(nullptr) == ROWL_RESULT_INVALID_HANDLE,
                "null handle must be INVALID_HANDLE");

        const fs::path game = makeGame(base / "gameApi", "44444444-5555-4666-8777-888888888888");
        RowlEngineHandle h = newEngine();
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        for (auto query : {RowlEngine_ProfileGetJson, RowlEngine_ProfileGetStatusJson}) {
            profReq(query(h, nullptr, 0, &required) == ROWL_RESULT_OK && required > 2, "size query failed");
            tiny[0] = 'x';
            profReq(query(h, tiny, 2, &required) == ROWL_RESULT_BUFFER_TOO_SMALL && tiny[0] == '\0' && required > 2,
                    "undersized buffer must be cleared + BUFFER_TOO_SMALL");
            profReq(query(h, nullptr, 0, nullptr) == ROWL_RESULT_INVALID_ARGUMENT, "null size pointer accepted");
            std::string exact(required, '\x7f');
            uint32_t again = 0;
            profReq(query(h, exact.data(), required, &again) == ROWL_RESULT_OK && again == required &&
                        exact.back() == '\0' && json::parse(exact.c_str(), nullptr, false).is_object(),
                    "exact buffer copy failed");
        }
        profReq(RowlEngine_ProfileSetPreferencesJson(h, nullptr) == ROWL_RESULT_INVALID_ARGUMENT &&
                    RowlEngine_ProfileSetPreferencesJson(h, "{\"bogus\":1}") == ROWL_RESULT_INVALID_ARGUMENT &&
                    RowlEngine_ProfileSetPreferencesJson(h, "{\"master_volume\":\"x\"}") == ROWL_RESULT_INVALID_ARGUMENT &&
                    RowlEngine_ProfileMarkRead(h, nullptr) == ROWL_RESULT_INVALID_ARGUMENT &&
                    RowlEngine_ProfileMarkRead(h, "junk") == ROWL_RESULT_INVALID_ARGUMENT &&
                    RowlEngine_ProfileMarkRead(h, "") == ROWL_RESULT_INVALID_ARGUMENT,
                "invalid arguments");
        profReq(capiJson(h, RowlEngine_ProfileGetJson)["master_volume"] == 1.0, "invalid arguments changed the profile");
        isRead = 9;
        profReq(RowlEngine_ProfileIsRead(h, nullptr, &isRead) == ROWL_RESULT_INVALID_ARGUMENT && isRead == 0 &&
                    RowlEngine_ProfileIsRead(h, kC1.c_str(), nullptr) == ROWL_RESULT_INVALID_ARGUMENT,
                "IsRead argument contract");
        profReq(RowlEngine_ProfileIsRead(h, "junk", &isRead) == ROWL_RESULT_OK && isRead == 0, "malformed id reads as unread");
        profReq(RowlEngine_ProfileMarkRead(h, kC1.c_str()) == ROWL_RESULT_OK &&
                    RowlEngine_ProfileMarkRead(h, kC1.c_str()) == ROWL_RESULT_OK,
                "marking twice must stay OK");

        // Foreign thread: WRONG_THREAD, state untouched (the main thread owns the handle).
        std::atomic<int> foreignResults{0};
        std::thread foreign([&] {
            uint32_t req = 0;
            int32_t out = 5;
            if (RowlEngine_ProfileMarkRead(h, kC3.c_str()) == ROWL_RESULT_WRONG_THREAD) ++foreignResults;
            if (RowlEngine_ProfileSetPreferencesJson(h, R"({"master_volume":0})") == ROWL_RESULT_WRONG_THREAD) ++foreignResults;
            if (RowlEngine_ProfileFlush(h) == ROWL_RESULT_WRONG_THREAD) ++foreignResults;
            if (RowlEngine_ProfileGetJson(h, nullptr, 0, &req) == ROWL_RESULT_WRONG_THREAD) ++foreignResults;
            if (RowlEngine_ProfileIsRead(h, kC1.c_str(), &out) == ROWL_RESULT_WRONG_THREAD) ++foreignResults;
        });
        foreign.join();
        profReq(foreignResults == 5, "foreign-thread calls must return WRONG_THREAD");
        profReq(RowlEngine_ProfileIsRead(h, kC3.c_str(), &isRead) == ROWL_RESULT_OK && isRead == 0 &&
                    capiJson(h, RowlEngine_ProfileGetJson)["master_volume"] == 1.0,
                "foreign-thread calls changed the profile");
        RowlEngine_Destroy(h);
        profReq(RowlEngine_ProfileFlush(h) == ROWL_RESULT_INVALID_HANDLE, "dead handle must be INVALID_HANDLE");
    }
    TEST_PASS("C API contract: null/dead handle, size query, undersized buffer, bad arguments, foreign thread");

    // 14) Golden fixture (shared with the managed PlayerProfileStore parity test).
    {
        const fs::path fixture = fs::path(ROWL_TEST_SOURCE_DIR) / "tests" / "fixtures" / "profile" / "player-profile.v1.json";
        profReq(fs::exists(fixture), "golden fixture missing: " + fixture.string());
        const fs::path dir = base / "golden";
        fs::create_directories(dir);
        fs::copy_file(fixture, dir / "player-profile.json");
        PlayerProfileStore store;
        store.open(dir, idA);
        const auto& p = store.preferences();
        profReq(store.status() == PlayerProfileStatus::Loaded && store.diagnostic().empty() && !store.readOnly(),
                "golden fixture must load clean: " + store.diagnostic());
        profReq(p.language == "tr" && p.masterVolume == 0.5f && p.bgmVolume == 0.75f && p.voiceVolume == 1.0f &&
                    p.sfxVolume == 0.25f && p.ambienceVolume == 1.0f && p.uiVolume == 0.5f &&
                    p.textSpeedMultiplier == 1.5f && p.autoAdvanceDelay == 3.0f && p.textScale == 1.25f && p.highContrast &&
                    !p.reducedMotion && p.skipMode == "all" && p.autoEnabled && p.mixerFadeCurve == "EqualPower" &&
                    p.sfxPoolDepth == 12,
                "golden fixture preferences wrong");
        profReq(store.readCount() == 2 && store.isRead(kC1) && store.isRead(kC3), "golden fixture read set wrong");
        // Native output keeps the managed key set (plus game_id/unlocks) and the same values.
        const json out = json::parse(store.toJson());
        const json golden = json::parse(readText(fixture));
        for (auto it = golden.begin(); it != golden.end(); ++it) {
            profReq(out.contains(it.key()) && out[it.key()] == it.value(), "native output differs from golden at " + it.key());
        }
        for (auto it = out.begin(); it != out.end(); ++it) {
            profReq(golden.contains(it.key()) || it.key() == "game_id" || it.key() == "unlocks",
                    "native output has an extra key " + it.key());
        }
    }
    TEST_PASS("Golden fixture (managed shape) loads Loaded and re-serializes to the same values");

    setXdg(previousXdgValue.empty() ? xdg : fs::path(previousXdgValue));
    if (previousXdgValue.empty()) {
#ifdef _WIN32
        _putenv_s("XDG_DATA_HOME", "");
#else
        ::unsetenv("XDG_DATA_HOME");
#endif
    }
    restoreLocalAppData();
    fs::remove_all(base, ec);
}
