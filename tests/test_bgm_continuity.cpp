/**
 * test_bgm_continuity.cpp — D-K3 (5): geri sarma / yukleme calan muzigi bastan baslatmaz.
 *
 * Gercek pencere bulgusu: restoreAudioStateFromGameState BGM'yi kosulsuz
 * playAudio ediyordu; ayni parca zaten calarken her Backspace'te muzik ~1,4 sn
 * susup bastan basliyordu (oyun gunlugu 04:09:43-50).
 *
 * Gozlem: AudioEngine::playAudio / stopBgm her cagrida son hatayi temizler; once
 * bir SFX-miss ile yapiskan hata ekilir — restore ayni parcayi yeniden calsaydi
 * (playAudio) hata silinirdi. Parca yolu / calma niyeti ayrica pinlenir.
 *
 * Kilit:
 *  1. Ayni parca calarken Rewind: parca ve niyet ayni, playAudio CAGRILMAZ (hata
 *     mandali durur); mikser (BGM sesi) yine durumdan geri yazilir.
 *  2. Farkli parcaya geri sarma: eski parca geri gelir (bugunku davranis).
 *  3. Calan parca durdurulmusken (stopBgm) geri sarma parcayi yeniden baslatir
 *     (durumda calmali yazar).
 *  4. Ayni sahnede kayit yukleme (ayni parca calarken) muzigi bastan baslatmaz;
 *     farkli parcadaysa yuklenen kaydin parcasi calinir.
 *
 * Bilerek-boz: restoreAudioStateFromGameState'te playAudio'nun onundeki
 * `!isBgmPlaying || yol != activeBgm` korumasini kaldirmak 1 ve 4'u kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
constexpr const char* kSection = "bgm-continuity";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

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

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// Node 1 starts track A; 2 and 3 leave the music alone; 4 switches to B; 5 follows.
constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "objects": [{"id": "o1", "name": "D", "is_active": true, "components": [
       {"type": "audio", "id": "a1", "enabled": true, "data": {"bgm_track": "a.wav", "volume": 0.5}},
       {"type": "dialogue", "id": "d1", "enabled": true, "data": {"speaker": "A", "dialogue": "One.", "typewriter_enabled": false}}]}],
     "next_nodes": [{"id": 2}]},
    {"id": 2, "objects": [{"id": "o2", "name": "D", "is_active": true, "components": [
       {"type": "dialogue", "id": "d2", "enabled": true, "data": {"speaker": "A", "dialogue": "Two.", "typewriter_enabled": false}}]}],
     "next_nodes": [{"id": 3}]},
    {"id": 3, "objects": [{"id": "o3", "name": "D", "is_active": true, "components": [
       {"type": "dialogue", "id": "d3", "enabled": true, "data": {"speaker": "A", "dialogue": "Three.", "typewriter_enabled": false}}]}],
     "next_nodes": [{"id": 4}]},
    {"id": 4, "objects": [{"id": "o4", "name": "D", "is_active": true, "components": [
       {"type": "audio", "id": "a4", "enabled": true, "data": {"bgm_track": "b.wav", "volume": 0.5}},
       {"type": "dialogue", "id": "d4", "enabled": true, "data": {"speaker": "A", "dialogue": "Four.", "typewriter_enabled": false}}]}],
     "next_nodes": [{"id": 5}]},
    {"id": 5, "objects": [{"id": "o5", "name": "D", "is_active": true, "components": [
       {"type": "dialogue", "id": "d5", "enabled": true, "data": {"speaker": "A", "dialogue": "Five.", "typewriter_enabled": false}}]}]}
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

}  // namespace

void test_bgm_continuity() {
    TEST_SECTION("BGM continuity (rewind / load never restart the playing track)");

    const fs::path root = fs::temp_directory_path() / "rowl_bgm_continuity";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "game";
    writeBytes(game / "project.rowlproj",
               R"({"project_uuid":"5d1c2a64-3b2c-4d7e-9a10-0c2b7d5e8f77","name":"BGM Continuity Test"})");
    writeBytes(game / "Assets" / "json" / "full_story_graph.json", kGraph);
    const std::string wav = makeWav(3);
    writeBytes(game / "Assets" / "audio" / "a.wav", wav);
    writeBytes(game / "Assets" / "audio" / "b.wav", wav);
    writeBytes(game / "Assets" / "audio" / "missing_sfx.wav", "");  // unreadable on purpose

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    RowlEngineHandle h = RowlEngine_Create();
    req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
    RowlEngine_SetProjectDirectory(h, game.string().c_str());
    fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
    RowlEngine_SetPlayState(h, 1);
    RowlEngine_Step(h, 0.016f);
    Rowl::Audio::AudioEngine* audio = Rowl::Core::testEngineFromHandle(h)->getAudio();
    req(audio != nullptr, "engine has no audio engine");

    auto settle = [&] { for (int i = 0; i < 4; ++i) RowlEngine_Step(h, 0.05f); };
    auto advance = [&](uint64_t expectedNode) {
        RowlEngine_AdvanceNode(h, 0);
        settle();
        req(RowlEngine_GetCurrentNodeId(h) == expectedNode,
            "advance reached node " + std::to_string(RowlEngine_GetCurrentNodeId(h)) + ", expected " +
                std::to_string(expectedNode));
    };
    // A sticky error that any playAudio / stopBgm call would clear.
    auto plantLatch = [&] {
        audio->playAudio("audio/definitely_missing_sfx.wav", Rowl::Audio::AudioChannelType::Sfx);
        req(!audio->getLastError().empty(), "could not plant the lastError latch");
    };
    auto latchIntact = [&] { return !audio->getLastError().empty(); };

    settle();
    req(RowlEngine_GetCurrentNodeId(h) == 1, "fixture start");
    req(audio->isBgmPlaying() && audio->getCurrentBgmPath() == "a.wav",
        "node 1 must start track A, got '" + audio->getCurrentBgmPath() + "'");
    advance(2);
    advance(3);
    req(audio->getCurrentBgmPath() == "a.wav", "track A keeps playing through nodes 2 and 3");

    // 1. Rewind inside the same track: no restart, volume restored from the state.
    plantLatch();
    audio->setBgmVolume(0.9f);
    req(RowlEngine_RewindNodes(h, 1) == 1, "rewind failed");
    settle();
    req(RowlEngine_GetCurrentNodeId(h) == 2, "rewound to node 2");
    req(latchIntact(), "rewind restarted the playing track (playAudio ran)");
    req(audio->isBgmPlaying() && audio->getCurrentBgmPath() == "a.wav", "track A must still be playing");
    req(std::abs(audio->getBgmVolume() - 0.5f) < 1e-6f, "the BGM volume is still restored from the state");
    plantLatch();
    req(RowlEngine_RewindNodes(h, 1) == 1, "second rewind failed");
    settle();
    req(latchIntact() && audio->getCurrentBgmPath() == "a.wav", "a second Backspace restarts nothing either");
    TEST_PASS("Rewind within the same track leaves the music alone, volume still restored");

    // 2. Rewind across a track change: the earlier track returns.
    advance(2);
    advance(3);
    advance(4);
    req(audio->getCurrentBgmPath() == "b.wav", "node 4 switches to track B");
    // F1 (A-1): a node that commits audio writes two state links (node + audio);
    // the player's Backspace is ONE node back, so a single call lands on node 3
    // with track A (this used to need "rewind until the earlier node shows").
    req(RowlEngine_RewindNodes(h, 1) == 1, "rewind failed");
    settle();
    req(RowlEngine_GetCurrentNodeId(h) == 3, "rewound to node 3, got " + std::to_string(RowlEngine_GetCurrentNodeId(h)));
    req(audio->isBgmPlaying() && audio->getCurrentBgmPath() == "a.wav",
        "rewinding before the switch brings track A back, got '" + audio->getCurrentBgmPath() + "'");
    TEST_PASS("Rewind across a track change restores the earlier track");

    // 3. A stopped BGM is started again when the state says it should be playing.
    audio->stopBgm();
    req(!audio->isBgmPlaying(), "stopBgm did not stop");
    RowlEngine_RewindNodes(h, 1);
    settle();
    req(audio->isBgmPlaying() && audio->getCurrentBgmPath() == "a.wav",
        "a stopped track must come back when the restored state is playing");
    TEST_PASS("A stopped track is restarted by a restore that says it plays");

    // 4. Load: same track keeps playing, a different one is switched to.
    req(RowlEngine_GetCurrentNodeId(h) == 2, "setup: node 2");
    req(RowlEngine_SaveGameSlot(h, 3) == 1, "save slot 3 failed");
    advance(3);
    plantLatch();
    req(RowlEngine_LoadGameSlot(h, 3) == 1, "load slot 3 failed");
    settle();
    req(RowlEngine_GetCurrentNodeId(h) == 2, "loaded node 2");
    req(latchIntact(), "loading a save in the same scene restarted the playing track");
    req(audio->getCurrentBgmPath() == "a.wav" && audio->isBgmPlaying(), "track A still playing after the load");
    advance(3);
    advance(4);
    req(audio->getCurrentBgmPath() == "b.wav", "setup: track B");
    req(RowlEngine_LoadGameSlot(h, 3) == 1, "load slot 3 (from B) failed");
    settle();
    req(audio->isBgmPlaying() && audio->getCurrentBgmPath() == "a.wav",
        "loading a save made under track A switches back to A, got '" + audio->getCurrentBgmPath() + "'");
    TEST_PASS("Load keeps the same track playing; a different track is switched to");

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
