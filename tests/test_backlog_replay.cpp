/**
 * test_backlog_replay.cpp — D03-1: node voice_track plays on entry, backlog
 * rows carry their voice path, and RowlEngine_ReplayBacklogVoice replays ONLY
 * that clip.
 *
 * Kilit:
 *  1. Dugume girince voice_track Voice kanalinda calar (yol + klip sayaci);
 *     sonraki dugume gecis onceki klibi keser; voice_track'li dugumde
 *     typewriter blip'i yoktur, voice'suz dugumde vardir.
 *  2. Gecmis satiri voice yolunu tasir; JSON'da "voice"; save->load gidis-
 *     donusunde korunur; voice'suz (eski) kayit, bozuk tipli ve asiri uzun
 *     degerli kayit yuklenir ve satir sessiz kalir.
 *  3. load / rewind voice_track'i tekrar CALMAZ.
 *  4. Replay: sadece o satirin klibi (oncekini keserek); degiskenler, stepId,
 *     dugum, gecmis, okundu profili, yuva dosyalari bayt-bayt ve SFX sayaci
 *     degismez; gecersiz index / sessiz satir / Init oncesi hata kodu.
 *
 * Bilerek-boz: ReplayBacklogVoice'i mevcut dugumu yeniden girme yoluyla
 * (updateSceneFromComponents replayEntryEffects=true) yazmak 4'u kirmizi yapar;
 * voice_track'in replayEntryEffects kapisini kaldirmak 3'u, blip bastirmayi
 * kaldirmak 1'i kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr const char* kSection = "backlog-replay";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// 16-bit mono PCM WAV of silence: long enough (3 s) that a clip is still
// queued when the test inspects the Voice stream.
std::string makeWav(uint32_t seconds) {
    constexpr uint32_t kRate = 22050;
    const uint32_t dataBytes = kRate * 2u * seconds;
    std::string out;
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    const auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    out += "RIFF"; u32(36u + dataBytes); out += "WAVE";
    out += "fmt "; u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2u); u16(2); u16(16);
    out += "data"; u32(dataBytes);
    out.append(dataBytes, '\0');
    return out;
}

constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "title": "Prologue",
     "objects": [{"id": "o1", "name": "D", "is_active": true, "components": [
       {"type": "dialogue", "id": "d1", "enabled": true,
        "data": {"speaker": "A", "dialogue": "Prologue.", "typewriter_enabled": false,
                 "content_id": "00000000-0000-4000-8000-000000000001"}}]}],
     "next_nodes": [{"id": 2}]},
    {"id": 2, "title": "Voiced with effects",
     "objects": [{"id": "o2", "name": "D", "is_active": true, "components": [
       {"type": "variable", "id": "v1", "enabled": true,
        "data": {"key": "mood", "value": "calm", "operation": "set"}},
       {"type": "variable", "id": "v2", "enabled": true,
        "data": {"key": "score", "value": "5", "operation": "add"}},
       {"type": "audio", "id": "a2", "enabled": true,
        "data": {"sfx_track": "s.wav", "voice_track": "v2.wav"}},
       {"type": "dialogue", "id": "d2", "enabled": true,
        "data": {"speaker": "A", "dialogue": "Voiced line two.", "typewriter_enabled": false,
                 "content_id": "00000000-0000-4000-8000-000000000002"}}]}],
     "next_nodes": [{"id": 3}]},
    {"id": 3, "title": "Voiced typewriter",
     "objects": [{"id": "o3", "name": "D", "is_active": true, "components": [
       {"type": "variable", "id": "v3", "enabled": true,
        "data": {"key": "score", "value": "7", "operation": "add"}},
       {"type": "audio", "id": "a3", "enabled": true,
        "data": {"sfx_track": "s.wav", "voice_track": "v3.wav"}},
       {"type": "dialogue", "id": "d3", "enabled": true,
        "data": {"speaker": "A", "dialogue": "Voiced line three with a long typed text.",
                 "typewriter_enabled": true, "text_speed": 10,
                 "content_id": "00000000-0000-4000-8000-000000000003"}}]}],
     "next_nodes": [{"id": 4}]},
    {"id": 4, "title": "Silent typewriter",
     "objects": [{"id": "o4", "name": "D", "is_active": true, "components": [
       {"type": "dialogue", "id": "d4", "enabled": true,
        "data": {"speaker": "A", "dialogue": "Silent line four with a long typed text.",
                 "typewriter_enabled": true, "text_speed": 10,
                 "content_id": "00000000-0000-4000-8000-000000000004"}}]}],
     "next_nodes": [{"id": 5}]},
    {"id": 5, "title": "Silent end",
     "objects": [{"id": "o5", "name": "D", "is_active": true, "components": [
       {"type": "variable", "id": "v5", "enabled": true,
        "data": {"key": "tail", "value": "1", "operation": "add"}},
       {"type": "audio", "id": "a5", "enabled": true, "data": {"sfx_track": "s.wav"}},
       {"type": "dialogue", "id": "d5", "enabled": true,
        "data": {"speaker": "A", "dialogue": "Silent line five.", "typewriter_enabled": false,
                 "content_id": "00000000-0000-4000-8000-000000000005"}}]}]}
  ]
})json";

std::string capiText(RowlEngineHandle h,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    req(query(h, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    req(query(h, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

json history(RowlEngineHandle h) {
    json parsed = json::parse(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
    req(parsed.is_array(), "history JSON did not parse");
    return parsed;
}

std::string variable(RowlEngineHandle h, const char* key) {
    const char* value = RowlEngine_GetVariable(h, key);
    return value ? value : "";
}

void stepFor(RowlEngineHandle h, int frames, float dt) {
    for (int i = 0; i < frames; ++i) RowlEngine_Step(h, dt);
}

// Visits every object that looks like a history row (has node_id) in a save.
template <typename Fn>
void forEachHistoryRow(json& node, Fn&& fn) {
    if (node.is_object()) {
        if (node.contains("node_id") && node.contains("dialogue")) fn(node);
        for (auto& item : node.items()) forEachHistoryRow(item.value(), fn);
    } else if (node.is_array()) {
        for (auto& item : node) forEachHistoryRow(item, fn);
    }
}

std::map<std::string, std::string> snapshotDir(const fs::path& dir) {
    std::map<std::string, std::string> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) files[fs::relative(entry.path(), dir).string()] = readBytes(entry.path());
    }
    return files;
}

}  // namespace

void test_backlog_replay() {
    TEST_SECTION("Backlog voice replay (D03-1)");

    const fs::path root = fs::temp_directory_path() / "rowl_backlog_replay";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "game";
    writeBytes(game / "project.rowlproj",
               R"({"project_uuid":"7a1e0a64-5b2c-4d7e-9a10-0c2b7d5e8f22","name":"Backlog Replay Test"})");
    writeBytes(game / "Assets" / "json" / "full_story_graph.json", kGraph);
    const std::string wav = makeWav(3);
    writeBytes(game / "Assets" / "audio" / "v2.wav", wav);
    writeBytes(game / "Assets" / "audio" / "v3.wav", wav);
    writeBytes(game / "Assets" / "audio" / "s.wav", wav);

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    // Before Init the call is a state error, a null handle is invalid.
    {
        RowlEngineHandle early = RowlEngine_Create();
        req(early != nullptr, "create failed");
        req(RowlEngine_ReplayBacklogVoice(early, 0) == ROWL_RESULT_STATE_ERROR,
            "ReplayBacklogVoice before Init must be STATE_ERROR");
        RowlEngine_Destroy(early);
        req(RowlEngine_ReplayBacklogVoice(nullptr, 0) == ROWL_RESULT_INVALID_HANDLE,
            "ReplayBacklogVoice(null) must be INVALID_HANDLE");
    }

    RowlEngineHandle h = RowlEngine_Create();
    req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
    RowlEngine_SetProjectDirectory(h, game.string().c_str());
    const fs::path saveDir = capiText(h, RowlEngine_GetSaveDirectoryUtf8);
    fs::create_directories(saveDir);
    RowlEngine_SetPlayState(h, 1);
    RowlEngine_Step(h, 0.016f);
    Rowl::Audio::AudioEngine* audio = Rowl::Core::testEngineFromHandle(h)->getAudio();
    req(audio != nullptr, "engine has no audio engine");

    uint64_t capabilities = 0;
    req(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK &&
            (capabilities & ROWL_ENGINE_CAPABILITY_BACKLOG_REPLAY) != 0 &&
            ROWL_ENGINE_CAPABILITY_BACKLOG_REPLAY == UINT64_C(67108864),
        "BACKLOG_REPLAY capability bit missing");
    req(RowlEngine_GetCurrentNodeId(h) == 1 && audio->getVoiceClipCount() == 0,
        "start node must not play a voice clip");

    // 1. Node entry plays the voice_track on the Voice channel.
    const uint64_t sfxBeforeEntry = audio->sfxStartedCount();
    RowlEngine_AdvanceNode(h, 0);
    req(RowlEngine_GetCurrentNodeId(h) == 2, "advance to node 2");
    req(audio->getVoiceClipCount() == 1 && audio->getCurrentVoicePath() == "v2.wav",
        "node 2 voice_track must play once on the Voice channel, path=" + audio->getCurrentVoicePath());
    req(audio->sfxStartedCount() == sfxBeforeEntry + 1, "node 2 sfx plays once");
    req(audio->isVoicePlaying() && audio->testQueuedBytes(Rowl::Audio::AudioChannelType::Voice) > 0,
        "voice clip must be queued on the Voice stream");
    stepFor(h, 3, 0.016f);
    req(audio->getVoiceClipCount() == 1, "stepping a node must not retrigger its voice");
    TEST_PASS("Node entry plays voice_track once on the Voice channel");

    // Node with a voice_track + typewriter: the previous clip is cut and no
    // blip is played (a blip would clear the Voice stream under the clip).
    audio->resetVoiceBlipCount();
    RowlEngine_AdvanceNode(h, 0);
    req(RowlEngine_GetCurrentNodeId(h) == 3, "advance to node 3");
    req(audio->getVoiceClipCount() == 2 && audio->getCurrentVoicePath() == "v3.wav",
        "node 3 voice replaces node 2 clip, path=" + audio->getCurrentVoicePath());
    stepFor(h, 40, 0.05f);
    req(audio->getVoiceBlipCount() == 0, "voiced node must play no typewriter blips, got " +
                                             std::to_string(audio->getVoiceBlipCount()));
    req(audio->testQueuedBytes(Rowl::Audio::AudioChannelType::Voice) > 0,
        "voice clip still queued after typewriter reveal");
    TEST_PASS("New node cuts the previous clip; voiced nodes play no blips");

    // Voiceless node: previous node's clip is cut, blips are back.
    audio->resetVoiceBlipCount();
    RowlEngine_AdvanceNode(h, 0);
    req(RowlEngine_GetCurrentNodeId(h) == 4, "advance to node 4");
    req(audio->getCurrentVoicePath().empty() && audio->getVoiceClipCount() == 2,
        "entering a voiceless node cuts the previous clip without starting another");
    stepFor(h, 40, 0.05f);
    req(audio->getVoiceBlipCount() > 0, "voiceless typewriter node keeps its blips");
    TEST_PASS("Voiceless node: clip cut, typewriter blips unchanged");

    RowlEngine_AdvanceNode(h, 0);
    req(RowlEngine_GetCurrentNodeId(h) == 5, "advance to node 5");
    stepFor(h, 3, 0.016f);

    // 2. History rows carry the voice.
    {
        const json rows = history(h);
        req(rows.size() == 4, "history must hold nodes 2..5, got " + std::to_string(rows.size()));
        req(rows[0]["node_id"] == 2 && rows[0]["voice"] == "v2.wav", "row 0 voice: " + rows[0].dump());
        req(rows[1]["node_id"] == 3 && rows[1]["voice"] == "v3.wav", "row 1 voice: " + rows[1].dump());
        req(rows[2].contains("voice") && rows[2]["voice"] == "", "row 2 has an empty voice field");
        req(rows[3].contains("voice") && rows[3]["voice"] == "", "row 3 has an empty voice field");
    }
    TEST_PASS("History JSON rows carry the voice path (empty when voiceless)");

    // Save slot 1 here (node 5); slot file carries voices for rows 0/1 only.
    req(RowlEngine_SaveGameSlot(h, 1) == 1, "save slot 1");
    const fs::path slot1 = saveDir / "save_slot_1.json";
    const json savedBefore = history(h);
    {
        json file = json::parse(readBytes(slot1), nullptr, false);
        req(file.is_object(), "slot file must be JSON");
        int withVoice = 0, rowCount = 0;
        forEachHistoryRow(file, [&](json& row) {
            ++rowCount;
            if (row.contains("voice")) {
                ++withVoice;
                req(!row["voice"].get<std::string>().empty(), "empty voice must not be written");
            }
        });
        req(rowCount >= 4 && withVoice == 2, "slot file: voiced rows keep voice, others omit the key");
    }

    // 4. Replay is a pure audio action.
    const std::string moodBefore = variable(h, "mood");
    const std::string scoreBefore = variable(h, "score");
    const std::string tailBefore = variable(h, "tail");
    req(moodBefore == "calm" && scoreBefore == "12.000000" && tailBefore == "1.000000",
        "variable precondition: " + moodBefore + "/" + scoreBefore + "/" + tailBefore);
    const uint64_t stepBefore = RowlEngine_GetCurrentStepId(h);
    const uint64_t nodeBefore = RowlEngine_GetCurrentNodeId(h);
    const std::string historyBefore = capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8);
    const std::string profileBefore = capiText(h, RowlEngine_ProfileGetJson);
    const auto dirBefore = snapshotDir(saveDir);
    const uint64_t sfxBefore = audio->sfxStartedCount();
    const uint32_t clipsBefore = audio->getVoiceClipCount();

    const auto expectUntouched = [&](const std::string& what) {
        req(variable(h, "mood") == moodBefore && variable(h, "score") == scoreBefore &&
                variable(h, "tail") == tailBefore,
            what + ": variables changed");
        req(RowlEngine_GetCurrentStepId(h) == stepBefore, what + ": stepId changed");
        req(RowlEngine_GetCurrentNodeId(h) == nodeBefore, what + ": current node changed");
        req(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8) == historyBefore, what + ": history changed");
        req(capiText(h, RowlEngine_ProfileGetJson) == profileBefore, what + ": read profile changed");
        req(snapshotDir(saveDir) == dirBefore, what + ": slot files changed");
        req(audio->sfxStartedCount() == sfxBefore, what + ": sfx was triggered");
    };

    req(RowlEngine_ReplayBacklogVoice(h, 0) == ROWL_RESULT_OK, "replay row 0");
    expectUntouched("replay row 0");
    req(audio->getCurrentVoicePath() == "v2.wav" && audio->getVoiceClipCount() == clipsBefore + 1,
        "replay row 0 plays that row's clip");
    req(RowlEngine_ReplayBacklogVoice(h, 1) == ROWL_RESULT_OK, "replay row 1");
    expectUntouched("replay row 1");
    req(audio->getCurrentVoicePath() == "v3.wav" && audio->getVoiceClipCount() == clipsBefore + 2,
        "replay row 1 cuts row 0's clip and plays its own");
    req(RowlEngine_ReplayBacklogVoice(h, 0) == ROWL_RESULT_OK && audio->getCurrentVoicePath() == "v2.wav",
        "replaying an older row again");
    expectUntouched("replay row 0 again");

    // Errors: silent rows and out-of-range indices change nothing.
    const uint32_t clipsAfter = audio->getVoiceClipCount();
    req(RowlEngine_ReplayBacklogVoice(h, 2) == ROWL_RESULT_FILE_NOT_FOUND, "silent row must fail");
    req(RowlEngine_ReplayBacklogVoice(h, 3) == ROWL_RESULT_FILE_NOT_FOUND, "silent last row must fail");
    req(RowlEngine_ReplayBacklogVoice(h, 4) == ROWL_RESULT_INVALID_ARGUMENT, "index past the end");
    req(RowlEngine_ReplayBacklogVoice(h, 0xFFFFFFFFu) == ROWL_RESULT_INVALID_ARGUMENT, "huge index");
    req(audio->getVoiceClipCount() == clipsAfter && audio->getCurrentVoicePath() == "v2.wav",
        "failed replays must not touch the Voice channel");
    expectUntouched("failed replays");
    TEST_PASS("Replay plays only the row's clip; variables, step, node, history, read profile, slots and sfx unchanged");

    // 3. Load / rewind never replay a voice_track.
    RowlEngine_Step(h, 0.016f);
    const uint32_t clipsBeforeRestore = audio->getVoiceClipCount();
    uint64_t sfxBeforeRestore = audio->sfxStartedCount();
    // Entry effects bump stepId, so one node can span several rewind steps.
    const auto rewindToNode = [&](uint64_t node) {
        for (int i = 0; i < 12 && RowlEngine_GetCurrentNodeId(h) != node; ++i)
            req(RowlEngine_Rewind(h, 1) != 0, "rewind step");
        req(RowlEngine_GetCurrentNodeId(h) == node, "rewind lands on node " + std::to_string(node));
    };
    rewindToNode(4);
    req(audio->getCurrentVoicePath().empty(), "rewind away from a playing clip must cut it");
    rewindToNode(3);
    req(audio->getVoiceClipCount() == clipsBeforeRestore && audio->sfxStartedCount() == sfxBeforeRestore,
        "rewind must not replay voice or sfx");
    audio->resetVoiceBlipCount();
    stepFor(h, 40, 0.05f);
    req(audio->getVoiceBlipCount() == 0, "a voiced node stays blip-free after rewind");
    // D-K1: a rewind restores state, it is not a node entry - but the node it came FROM is
    // entered again as soon as the player walks forward. The one-shot entry markers (sfx,
    // voice clip, backlog row) used to keep the old node: node 5 below played no sfx and
    // got no backlog row the second time (node 4, rewound to, has no audio component).
    RowlEngine_AdvanceNode(h, 0);
    stepFor(h, 40, 0.05f);  // node 4 types its text out first; a click would only finish it
    RowlEngine_AdvanceNode(h, 0);
    req(RowlEngine_GetCurrentNodeId(h) == 5 && history(h).size() == 4,
        "walk forward to node 5 again: node " + std::to_string(RowlEngine_GetCurrentNodeId(h)) + ", " +
            std::to_string(history(h).size()) + " rows");
    const uint64_t sfxBeforeReentry = audio->sfxStartedCount();
    rewindToNode(4);
    req(history(h).size() == 3 && audio->sfxStartedCount() == sfxBeforeReentry,
        "rewinding from node 5 to node 4 drops node 5's row and plays nothing");
    stepFor(h, 40, 0.05f);
    RowlEngine_AdvanceNode(h, 0);
    req(RowlEngine_GetCurrentNodeId(h) == 5, "advance back into node 5");
    req(audio->sfxStartedCount() == sfxBeforeReentry + 1,
        "entering node 5 again after a rewind plays its sfx again");
    {
        const json rows = history(h);
        req(rows.size() == 4 && rows[3]["node_id"] == 5,
            "entering node 5 again after a rewind records its backlog row again: " + json(rows).dump());
    }
    sfxBeforeRestore = audio->sfxStartedCount();
    req(RowlEngine_ReplayBacklogVoice(h, 0) == ROWL_RESULT_OK, "replay before load");
    const uint32_t clipsBeforeLoad = audio->getVoiceClipCount();
    req(RowlEngine_LoadGameSlot(h, 1) == 1 && RowlEngine_GetCurrentNodeId(h) == 5, "load slot 1");
    req(audio->getVoiceClipCount() == clipsBeforeLoad && audio->sfxStartedCount() == sfxBeforeRestore,
        "load must not replay voice or sfx");
    req(audio->getCurrentVoicePath().empty(), "load must cut the clip playing before it");
    TEST_PASS("Load and rewind never replay voice_track");

    // 2b. Save -> load round trip keeps the voice paths.
    req(history(h) == savedBefore, "voice paths survive the save->load round trip");

    // Older save (no "voice" keys), wrong-typed and oversized values.
    {
        json file = json::parse(readBytes(slot1), nullptr, false);
        forEachHistoryRow(file, [](json& row) { row.erase("voice"); });
        writeBytes(saveDir / "save_slot_2.json", file.dump());
        req(RowlEngine_LoadGameSlot(h, 2) == 1, "voiceless (older) save must load");
        const json rows = history(h);
        req(rows.size() == 4 && rows[0]["voice"] == "" && rows[1]["voice"] == "",
            "older save rows decode to an empty voice");

        json hostile = json::parse(readBytes(slot1), nullptr, false);
        int index = 0;
        forEachHistoryRow(hostile, [&](json& row) {
            if (index++ % 2 == 0) row["voice"] = 12345;
            else row["voice"] = std::string(5000, 'x');
        });
        writeBytes(saveDir / "save_slot_3.json", hostile.dump());
        req(RowlEngine_LoadGameSlot(h, 3) == 1, "save with wrong-typed / oversized voice must load");
        const json hostileRows = history(h);
        req(hostileRows.size() == 4, "hostile save keeps all rows");
        for (const auto& row : hostileRows) req(row["voice"] == "", "hostile voice degrades to empty");
        req(RowlEngine_ReplayBacklogVoice(h, 0) == ROWL_RESULT_FILE_NOT_FOUND,
            "a degraded row cannot be replayed");
    }
    TEST_PASS("History voice: JSON, save round trip, older and hostile saves");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(root, ec);
}
