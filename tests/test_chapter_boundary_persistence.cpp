/**
 * test_chapter_boundary_persistence.cpp — D06-3: chapters + prefetch in the
 * packaged-player shape, and the chapter boundary through save / load / rewind.
 *
 * Kilit:
 *  1. Iki bolumlu v5 grafikte bolum sinirini gec: chapterId sinirda degisir; kayit
 *     metadatasi chapter_id / chapter_title tasir; kayit -> yukle dogru dugumu, dogru
 *     bolumu, degiskenleri (giris etkisi TEKRAR uygulanmaz) ve calan muzigi geri getirir;
 *     sinirin otesinden geri sarma onceki bolume doner (chapterId geri gelir), yan etki
 *     (degisken / sfx) tekrar uygulanmaz, muzik o adimdaki parcaya doner. Muzik sinirda
 *     bellek parcasindan (WAV) akis parcasina (uzun OGG) gecer ve geri sarmada geri doner.
 *  2. Oyuncunun prefetch surucusu (rowl/player/chapter_prefetch_driver.hpp) beslenmis ve
 *     beslenmemis (bolunmemis proje) bicimde AYNI oyun akisini verir: uc kosunun
 *     (surucusuz / surucu + bolunmus proje / surucu + bolunmemis proje) gozlem dokumu bire
 *     bir ayni. Surucu bolum degisiminde pencere baslatir ve kuyrugu bosaltir.
 *  3. Bolunmus bolum dosyalari grafikle sapmissa (bayat bolme) yukleyici BIRAKILIR, net bir
 *     tani doner; bolunmus proje yoksa FILE_NOT_FOUND (normal durum).
 *  4. RowlEngine_RunWithFrameCallback gercek engelleyici dongude (gorunur pencere) kareden
 *     kareye cagrilir, surucu dugum degisimini gorur; bos callback RowlEngine_Run gibi davranir.
 *
 * Bilerek-boz: (a) surucuden RowlEngine_FeedChaptersFromVfs cagrisini kaldirmak 2'yi,
 * (b) bolum degisiminde pencere baslatmayi kaldirmak 2'yi, (c) FeedChaptersFromVfs'teki
 * sapma denetimini kaldirmak 3'u, (d) Engine::run'daki afterStep cagrisini kaldirmak 4'u,
 * (e) kayit yuklemede restoreAudioStateFromGameState'i kaldirmak 1'i kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/player/chapter_prefetch_driver.hpp"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr const char* kSection = "chapter-boundary-persistence";

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
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// 16-bit mono PCM silence: a clip the audio counters can see.
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

// OGG page CRC (poly 0x04C11DB7, init 0): the muxer's algorithm.
uint32_t oggPageCrc(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) r = (r & 0x80000000u) ? ((r << 1) ^ 0x04C11DB7u) : (r << 1);
            table[i] = r;
        }
        ready = true;
    }
    uint32_t crc = 0;
    for (size_t i = 0; i < size; ++i) crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xFFu];
    return crc;
}

// A real OGG whose last page claims a huge sample count (CRC repaired): the
// engine's streaming gate (source duration over the 64 MiB decoded budget)
// routes it to the streamed BGM path while the decoder still plays real
// packets. Same trick as test_audio_streaming.cpp, applied to a repo sample.
std::string makeStreamingOgg(const std::string& source) {
    std::vector<uint8_t> ogg(source.begin(), source.end());
    size_t lastPage = std::string::npos;
    for (size_t i = 0; i + 27 <= ogg.size(); ++i) {
        if (std::memcmp(ogg.data() + i, "OggS", 4) == 0) lastPage = i;
    }
    req(lastPage != std::string::npos, "sample OGG has no page");
    const uint64_t granule = 1000000000ull;
    for (int b = 0; b < 8; ++b) ogg[lastPage + 6 + b] = static_cast<uint8_t>((granule >> (8 * b)) & 0xFFu);
    const size_t nseg = ogg[lastPage + 26];
    size_t body = 0;
    for (size_t i = 0; i < nseg; ++i) body += ogg[lastPage + 27 + i];
    const size_t pageEnd = lastPage + 27 + nseg + body;
    req(pageEnd <= ogg.size(), "sample OGG last page is truncated");
    for (int i = 22; i < 26; ++i) ogg[lastPage + i] = 0;
    const uint32_t crc = oggPageCrc(ogg.data() + lastPage, pageEnd - lastPage);
    for (int i = 0; i < 4; ++i) ogg[lastPage + 22 + i] = static_cast<uint8_t>((crc >> (8 * i)) & 0xFFu);
    return std::string(ogg.begin(), ogg.end());
}

std::string capiText(RowlEngineHandle h,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    req(query(h, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    req(query(h, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

constexpr const char* kStreamTrack = "audio/theme_stream.ogg";
constexpr const char* kMemoryTrack = "audio/theme_memory.wav";

// Two chapters. Node 1 starts the memory-resident BGM; node 4 opens chapter 2
// and switches to the streamed OGG. Every node adds 1 to "n" (node 1 sets it),
// so a repeated entry effect shows up as a wrong count.
json buildGraph() {
    const auto node = [](int id, const std::string& chapter, const std::string& text,
                         std::vector<json> extra, std::vector<int> next) {
        json components = json::array();
        components.push_back({{"type", "dialogue"}, {"id", "d" + std::to_string(id)}, {"enabled", true},
                              {"data", {{"speaker", "S"}, {"dialogue", text}, {"typewriter_enabled", false},
                                        {"content_id", "30000000-0000-4000-8000-0000000000" + std::string(id < 10 ? "0" : "") + std::to_string(id)}}}});
        components.push_back({{"type", "variable"}, {"id", "v" + std::to_string(id)}, {"enabled", true},
                              {"data", {{"key", "n"}, {"value", "1"}, {"operation", id == 1 ? "set" : "add"}}}});
        for (json& component : extra) components.push_back(std::move(component));
        json edges = json::array();
        for (int target : next) edges.push_back({{"id", target}});
        return json({{"id", id}, {"chapter_id", chapter}, {"title", "N" + std::to_string(id)},
                     {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"},
                                               {"is_active", true}, {"components", components}}})},
                     {"next_nodes", edges}});
    };
    const auto audio = [](const char* bgm) {
        return json({{"type", "audio"}, {"id", std::string("a_") + bgm}, {"enabled", true},
                     {"data", {{"bgm_track", bgm}, {"bgm_transition", "instant"},
                               {"sfx_track", "audio/blip.wav"}}}});
    };
    json nodes = json::array();
    nodes.push_back(node(1, "ch1", "One", {audio(kMemoryTrack)}, {2}));
    nodes.push_back(node(2, "ch1", "Two", {}, {3}));
    nodes.push_back(node(3, "ch1", "Three", {}, {4}));
    nodes.push_back(node(4, "ch2", "Four", {audio(kStreamTrack)}, {5}));
    nodes.push_back(node(5, "ch2", "Five", {}, {6}));
    nodes.push_back(node(6, "ch2", "Six", {}, {}));
    return json({{"format_version", 5},
                 {"start_node_id", 1},
                 {"chapters", json::array({{{"id", "ch1"}, {"title", "Prologue"}, {"order", 1}, {"start_node_id", 1}},
                                           {{"id", "ch2"}, {"title", "The Crossing"}, {"order", 2}, {"start_node_id", 4}}})},
                 {"nodes", nodes}});
}

// The editor's Split: one file per chapter + chapter_index.json.
void writeSplitChapters(const fs::path& jsonDir, const json& graph, bool dropNodeFromCh2) {
    const fs::path dir = jsonDir / "chapters";
    json index = json::object();
    index["format_version"] = 5;
    index["start_node_id"] = graph["start_node_id"];
    index["chapters"] = graph["chapters"];
    json order = json::array();
    for (const json& node : graph["nodes"]) order.push_back(node["id"]);
    index["node_order"] = order;
    writeBytes(dir / "chapter_index.json", index.dump(2));
    for (const json& chapter : graph["chapters"]) {
        json file = json::object();
        file["format_version"] = 5;
        file["chapter_id"] = chapter["id"];
        json nodes = json::array();
        for (const json& node : graph["nodes"]) {
            if (node["chapter_id"] != chapter["id"]) continue;
            if (dropNodeFromCh2 && chapter["id"] == "ch2" && node["id"] == 6) continue;
            nodes.push_back(node);
        }
        file["nodes"] = nodes;
        writeBytes(dir / (chapter["id"].get<std::string>() + ".json"), file.dump(2));
    }
}

int g_rigCounter = 0;
fs::path g_root;
std::string g_oggSource;

// One game folder + one engine. `split` writes the editor's chapter files;
// `withDriver` attaches the player's prefetch driver (frame() feeds it).
struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    Rowl::Audio::AudioEngine* audio = nullptr;
    fs::path game;
    std::unique_ptr<Rowl::Player::ChapterPrefetchDriver> driver;
    RowlEngine_ResultCode attachCode = ROWL_RESULT_OK;

    Rig(bool split, bool withDriver, bool dropNodeFromCh2 = false) {
        ++g_rigCounter;
        game = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "7d3e1c86-8e4f-4f90-9c32-2e4d9f7a0c%02x", g_rigCounter);
        writeBytes(game / "project.rowlproj",
                   std::string(R"({"project_uuid":")") + uuid + R"(","name":"Chapter Boundary Test"})");
        const json graph = buildGraph();
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", graph.dump());
        if (split) writeSplitChapters(game / "Assets" / "json", graph, dropNodeFromCh2);
        writeBytes(game / "Assets" / "audio" / "theme_memory.wav", makeWav(2));
        writeBytes(game / "Assets" / "audio" / "blip.wav", makeWav(1));
        writeBytes(game / "Assets" / "audio" / "theme_stream.ogg", makeStreamingOgg(g_oggSource));
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        audio = engine->getAudio();
        req(audio != nullptr, "engine has no audio engine");
        RowlEngine_SetPlayState(h, 1);
        if (withDriver) {
            driver = std::make_unique<Rowl::Player::ChapterPrefetchDriver>(2.0F);
            attachCode = driver->attach(h);
        }
        frame();
    }
    ~Rig() { if (h) RowlEngine_Destroy(h); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // One host frame: step the engine, then the player's frame hook.
    void frame(int count = 1) {
        for (int i = 0; i < count; ++i) {
            RowlEngine_Step(h, 0.016f);
            if (driver) driver->onFrame(h);
        }
    }
    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    std::string chapter() const { return capiText(h, RowlEngine_GetCurrentChapterIdUtf8); }
    void advance() { RowlEngine_AdvanceNode(h, 0); frame(); }
    std::string var(const std::string& key) const {
        uint32_t required = 0;
        req(RowlEngine_GetVariableUtf8(h, key.c_str(), nullptr, 0, &required) == ROWL_RESULT_OK,
            "variable size query failed");
        std::string buffer(required, '\0');
        req(RowlEngine_GetVariableUtf8(h, key.c_str(), buffer.data(), required, &required) == ROWL_RESULT_OK,
            "variable copy failed");
        buffer.resize(required > 0 ? required - 1 : 0);
        return buffer;
    }
    int n() const {
        const std::string text = var("n");
        return text.empty() ? -1 : static_cast<int>(std::stod(text));
    }
    size_t historyRows() const {
        const json parsed = json::parse(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
        req(parsed.is_array(), "history JSON did not parse");
        return parsed.size();
    }
    // The whole observable state of one moment, as one comparable line.
    std::string row(const std::string& label) const {
        return label + ": node=" + std::to_string(node()) + " ch=" + chapter() +
               " n=" + std::to_string(n()) + " bgm=" + audio->getCurrentBgmPath() +
               " playing=" + (audio->isBgmPlaying() ? "1" : "0") +
               " stream=" + (audio->isStreaming() ? "1" : "0") +
               " sfx=" + std::to_string(audio->sfxStartedCount()) +
               " hist=" + std::to_string(historyRows());
    }
    void expectAt(uint64_t id, const char* chapterId, int expectedN, const char* bgm, bool streaming,
                  const std::string& what) const {
        req(node() == id, what + ": node " + std::to_string(node()) + ", expected " + std::to_string(id));
        req(chapter() == chapterId, what + ": chapter '" + chapter() + "', expected '" + chapterId + "'");
        req(n() == expectedN, what + ": n=" + std::to_string(n()) + ", expected " + std::to_string(expectedN) +
                                  " (an entry effect was repeated or lost)");
        req(audio->getCurrentBgmPath() == bgm && audio->isBgmPlaying(),
            what + ": bgm '" + audio->getCurrentBgmPath() + "' playing=" + std::to_string(audio->isBgmPlaying()) +
                ", expected '" + bgm + "' playing");
        req(audio->isStreaming() == streaming,
            what + ": bgm streaming=" + std::to_string(audio->isStreaming()) + ", expected " + std::to_string(streaming));
    }
};

// Backspace/Z rewind ONE state link (Engine::rewind(1)), and a node with entry
// effects (variable, music) owns several links - the first presses can land on
// the same node with the effect undone. This presses until the story is back
// on `target`, like a player would, and records every landing.
void rewindUntilNode(Rig& rig, uint64_t target, std::vector<std::string>& record, const std::string& what) {
    constexpr int kMaxPresses = 12;
    int presses = 0;
    const uint64_t sfxAtStart = rig.audio->sfxStartedCount();
    while (rig.node() != target && presses < kMaxPresses) {
        req(RowlEngine_Rewind(rig.h, 1) == 1, what + ": a rewind press was refused at node " + std::to_string(rig.node()));
        rig.frame(2);
        ++presses;
        record.push_back(rig.row(what + " press " + std::to_string(presses)));
        req(rig.audio->sfxStartedCount() == sfxAtStart, what + ": rewind must never replay a node's sfx");
    }
    req(rig.node() == target, what + ": did not reach node " + std::to_string(target) + " in " +
                                  std::to_string(kMaxPresses) + " presses (at " + std::to_string(rig.node()) + ")");
}

// The boundary through save / load / rewind. Returns the observation record.
std::vector<std::string> runScenario(Rig& rig) {
    std::vector<std::string> record;
    const auto note = [&](const std::string& label) { record.push_back(rig.row(label)); };

    // 1) Walk to the end of chapter 1; the memory track plays.
    rig.expectAt(1, "ch1", 1, kMemoryTrack, false, "start");
    note("start");
    rig.advance();
    rig.advance();
    rig.expectAt(3, "ch1", 3, kMemoryTrack, false, "last node of chapter 1");
    req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save before the boundary");
    note("saved slot 1 before the boundary");
    const uint64_t sfxBeforeBoundary = rig.audio->sfxStartedCount();

    // 2) Cross the boundary: chapter id flips, the stream takes over, the node
    //    entry effects ran exactly once.
    rig.advance();
    rig.expectAt(4, "ch2", 4, kStreamTrack, true, "first node of chapter 2");
    req(rig.audio->sfxStartedCount() == sfxBeforeBoundary + 1, "crossing plays the node's sfx once");
    note("crossed");
    req(RowlEngine_SaveGameSlot(rig.h, 2) == 1, "save at the boundary node");

    // 3) Save metadata carries the chapter.
    {
        const json meta = json::parse(capiText(rig.h, [](RowlEngineHandle handle, char* buffer, uint32_t size, uint32_t* required) {
            return RowlEngine_GetSaveSlotMetadataJson(handle, 2, buffer, size, required);
        }), nullptr, false);
        req(meta.is_object() && meta.value("chapter_id", std::string()) == "ch2" &&
                meta.value("chapter_title", std::string()) == "The Crossing",
            "slot 2 metadata must carry chapter_id ch2 / 'The Crossing', got " + meta.dump());
        const json before = json::parse(capiText(rig.h, [](RowlEngineHandle handle, char* buffer, uint32_t size, uint32_t* required) {
            return RowlEngine_GetSaveSlotMetadataJson(handle, 1, buffer, size, required);
        }), nullptr, false);
        req(before.is_object() && before.value("chapter_id", std::string()) == "ch1" &&
                before.value("chapter_title", std::string()) == "Prologue",
            "slot 1 metadata must carry chapter_id ch1 / 'Prologue', got " + before.dump());
    }

    // 4) Play on, then load the boundary save: the node, chapter, variables and
    //    the streamed music come back; the entry effects do not run again.
    rig.advance();
    rig.advance();
    rig.expectAt(6, "ch2", 6, kStreamTrack, true, "deep in chapter 2");
    const uint64_t sfxBeforeLoad = rig.audio->sfxStartedCount();
    req(RowlEngine_LoadGameSlot(rig.h, 2) == 1, "load the boundary save");
    rig.frame(2);
    rig.expectAt(4, "ch2", 4, kStreamTrack, true, "loaded boundary save");
    req(rig.audio->sfxStartedCount() == sfxBeforeLoad, "load must not replay the node's sfx");
    note("loaded slot 2");

    // 5) Load the pre-boundary save from inside chapter 2: back in chapter 1
    //    with the memory track, no entry effect applied twice.
    rig.advance();
    req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load the pre-boundary save from chapter 2");
    rig.frame(2);
    rig.expectAt(3, "ch1", 3, kMemoryTrack, false, "loaded pre-boundary save");
    req(rig.audio->sfxStartedCount() == sfxBeforeLoad, "load must not replay sfx (pre-boundary)");
    note("loaded slot 1");

    // 6) Re-cross, then rewind back over the boundary.
    rig.advance();
    rig.expectAt(4, "ch2", 4, kStreamTrack, true, "re-crossed");
    const uint64_t sfxAfterRecross = rig.audio->sfxStartedCount();
    req(sfxAfterRecross == sfxBeforeLoad + 1, "a real second entry plays the sfx again");
    rig.advance();
    rig.expectAt(5, "ch2", 5, kStreamTrack, true, "second node of chapter 2");
    rewindUntilNode(rig, 3, record, "rewind over the boundary");
    rig.expectAt(3, "ch1", 3, kMemoryTrack, false, "rewound past the boundary");
    req(rig.audio->sfxStartedCount() == sfxAfterRecross, "rewind must not replay sfx");
    note("rewound past the boundary");

    // 7) Forward again, and a one-step rewind over the boundary.
    rig.advance();
    rig.expectAt(4, "ch2", 4, kStreamTrack, true, "re-crossed after rewind");
    rewindUntilNode(rig, 3, record, "rewind from the first node of chapter 2");
    rig.expectAt(3, "ch1", 3, kMemoryTrack, false, "one step back over the boundary");
    note("rewound one step over the boundary");
    return record;
}

json progressOf(RowlEngineHandle h) {
    return Rowl::Player::ChapterPrefetchDriver::progress(h);
}

// ── RowlEngine_RunWithFrameCallback on a real (dummy-driver) window ─────────
struct RunProbe {
    RowlEngineHandle h = nullptr;
    Rowl::Player::ChapterPrefetchDriver* driver = nullptr;
    int frames = 0;
    int advancedAtFrame = 0;
    bool pushedQuit = false;
};

void runProbeCallback(RowlEngineHandle h, float, void* userData) {
    auto* probe = static_cast<RunProbe*>(userData);
    ++probe->frames;
    if (probe->driver) probe->driver->onFrame(h);
    if (probe->frames >= 3 && probe->frames <= 5) {
        // Walk 1 -> 4 (into chapter 2) from inside the loop, one node a frame.
        RowlEngine_AdvanceNode(h, 0);
        probe->advancedAtFrame = probe->frames;
    }
    if (probe->frames == 8 && !probe->pushedQuit) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        probe->pushedQuit = true;
    }
    // Safety net: never hang the suite. Throwing aborts the run exactly like a
    // throwing step() (#123) - the loop's own guard turns it into a stop.
    if (probe->frames > 400) throw std::runtime_error("run probe: quit event never arrived");
}

}  // namespace

void test_chapter_boundary_persistence() {
    TEST_SECTION("Chapter boundary: save / load / rewind, music, prefetch in the player shape (D06-3)");

    g_root = fs::temp_directory_path() / "rowl_chapter_boundary";
    std::error_code ec;
    fs::remove_all(g_root, ec);
    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (g_root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (g_root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (g_root / "xdg").string().c_str(), 1);
#endif

    // A real OGG from the repository's own sample, patched into a stream route.
    const fs::path sample = fs::path(ROWL_TEST_SOURCE_DIR) / "samples" / "kayip_tablo" / "Assets" /
                            "audio" / "yagmur_gok.ogg";
    req(fs::is_regular_file(sample), "sample OGG missing: " + sample.string());
    g_oggSource = readBytes(sample);

    // ── 1. the boundary scenario without any prefetch ───────────────────────
    std::vector<std::string> plain;
    {
        Rig rig(/*split=*/false, /*withDriver=*/false);
        plain = runScenario(rig);
    }
    TEST_PASS("Chapter boundary through save / load / rewind: node, chapter, metadata, variables, sfx and streamed music stay consistent");

    // ── 2. the same with the player's prefetch driver ───────────────────────
    //    (a) project without split chapters: prefetch follows the graph's own chapters
    {
        Rig rig(/*split=*/false, /*withDriver=*/true);
        req(rig.attachCode == ROWL_RESULT_FILE_NOT_FOUND,
            "no split chapters must report FILE_NOT_FOUND (a normal condition), got " +
                std::to_string(static_cast<int>(rig.attachCode)));
        req(!rig.driver->stats().chaptersFed, "no loader feed without chapter files");
        const std::vector<std::string> withDriver = runScenario(rig);
        req(withDriver == plain, "prefetch (graph chapters) changed the play flow");
        const auto stats = rig.driver->stats();
        req(stats.triggers >= 4 && stats.refusedTriggers == 0,
            "windows start at the start and at each chapter change (>= 4), none refused; triggers=" +
                std::to_string(stats.triggers) + " refused=" + std::to_string(stats.refusedTriggers));
        rig.frame(60);
        const json progress = progressOf(rig.h);
        req(progress.value("total_assets", 0) > 0 && progress.value("ready_assets", 0) > 0 &&
                progress.value("missing_assets", -1) == 0,
            "the window's assets are warmed from the VFS: " + progress.dump());
        req(!rig.driver->pumping() && progress.value("complete", false),
            "the queue drains and the driver stops pumping: " + progress.dump());
    }
    //    (b) project with the editor's split chapter files, fed from the VFS
    {
        Rig rig(/*split=*/true, /*withDriver=*/true);
        req(rig.attachCode == ROWL_RESULT_OK && rig.driver->stats().chaptersFed &&
                rig.driver->stats().fedFiles == 3,
            "index + 2 chapter files must feed (code " + std::to_string(static_cast<int>(rig.attachCode)) +
                ", files " + std::to_string(rig.driver->stats().fedFiles) + ")");
        const json loaded = json::parse(capiText(rig.h, RowlEngine_GetLoadedChaptersJson), nullptr, false);
        req(loaded.is_object() && loaded.value("total_nodes", 0) == 6 && !loaded.value("legacy_single_graph", true),
            "the loader holds the two chapters (6 nodes): " + loaded.dump());
        const std::vector<std::string> withDriver = runScenario(rig);
        req(withDriver == plain, "prefetch (fed chapters) changed the play flow");
        const auto stats = rig.driver->stats();
        req(stats.triggers >= 4 && stats.refusedTriggers == 0,
            "fed chapters: windows at start and each chapter change, none refused; triggers=" +
                std::to_string(stats.triggers) + " refused=" + std::to_string(stats.refusedTriggers));
        rig.frame(60);
        const json progress = progressOf(rig.h);
        req(progress.value("total_assets", 0) > 0 && progress.value("ready_assets", 0) > 0 &&
                progress.value("missing_assets", -1) == 0 && progress.value("complete", false),
            "fed chapters: assets warmed and the queue drained: " + progress.dump());
    }
    TEST_PASS("The prefetch driver (graph chapters and fed chapter files) leaves the play flow byte-identical and warms each chapter window");

    // ── 3. stale split: chapter files that disagree with the graph ──────────
    {
        Rig rig(/*split=*/true, /*withDriver=*/false, /*dropNodeFromCh2=*/true);
        int32_t fed = -1;
        const RowlEngine_ResultCode code = RowlEngine_FeedChaptersFromVfs(rig.h, nullptr, &fed);
        req(code == ROWL_RESULT_VALIDATION_ERROR && fed == 0,
            "a stale split must be rejected with VALIDATION_ERROR, got " + std::to_string(static_cast<int>(code)));
        const json loaded = json::parse(capiText(rig.h, RowlEngine_GetLoadedChaptersJson), nullptr, false);
        req(loaded.is_object() && loaded.value("total_nodes", -1) == 0,
            "the loader is cleared after a stale split: " + loaded.dump());
        // The player keeps prefetching from the graph itself.
        req(RowlEngine_PrefetchChapterAssets(rig.h, "ch1", 0) == ROWL_RESULT_OK,
            "prefetch falls back to the graph's own chapters");
        // And through the driver the game plays on, flow unchanged.
        Rig driven(/*split=*/true, /*withDriver=*/true, /*dropNodeFromCh2=*/true);
        req(driven.attachCode == ROWL_RESULT_VALIDATION_ERROR && !driven.driver->stats().chaptersFed,
            "driver reports the stale split and does not claim a feed");
        req(runScenario(driven) == plain, "a stale split must not change the play flow");
    }
    {
        // No project folder / bad arguments.
        Rig rig(/*split=*/false, /*withDriver=*/false);
        int32_t fed = 7;
        req(RowlEngine_FeedChaptersFromVfs(rig.h, nullptr, &fed) == ROWL_RESULT_FILE_NOT_FOUND && fed == 0,
            "a project without chapter_index.json reports FILE_NOT_FOUND and feeds nothing");
        req(RowlEngine_FeedChaptersFromVfs(rig.h, "no/such/dir", nullptr) == ROWL_RESULT_FILE_NOT_FOUND,
            "an unknown folder reports FILE_NOT_FOUND (null out-count allowed)");
        req(RowlEngine_FeedChaptersFromVfs(nullptr, nullptr, &fed) == ROWL_RESULT_INVALID_HANDLE,
            "a dead handle reports INVALID_HANDLE");
    }
    TEST_PASS("Stale chapter files are rejected with a clear result and the loader is dropped; projects without a split report FILE_NOT_FOUND");

    // ── 4. the blocking loop's frame callback (real standalone window) ───────
    {
        ++g_rigCounter;
        const fs::path game = g_root / ("game" + std::to_string(g_rigCounter));
        writeBytes(game / "project.rowlproj",
                   R"({"project_uuid":"7d3e1c86-8e4f-4f90-9c32-2e4d9f7a0cff","name":"Run Probe"})");
        const json graph = buildGraph();
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", graph.dump());
        writeSplitChapters(game / "Assets" / "json", graph, false);
        writeBytes(game / "Assets" / "audio" / "theme_memory.wav", makeWav(2));
        writeBytes(game / "Assets" / "audio" / "blip.wav", makeWav(1));
        writeBytes(game / "Assets" / "audio" / "theme_stream.ogg", makeStreamingOgg(g_oggSource));

        RowlEngineHandle h = RowlEngine_Create();
        req(h != nullptr, "create");
        if (RowlEngine_InitStandalone(h, "D06-3 run probe", 320, 180, 0) != 1) {
            std::cout << "  (run section SKIP: no visible window on this driver)" << std::endl;
            RowlEngine_Destroy(h);
        } else {
            RowlEngine_SetProjectDirectory(h, game.string().c_str());
            fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
            Rowl::Player::ChapterPrefetchDriver driver(2.0F);
            req(driver.attach(h) == ROWL_RESULT_OK, "run probe: chapters feed");
            RowlEngine_SetPlayState(h, 1);
            RunProbe probe;
            probe.h = h;
            probe.driver = &driver;
            // Watchdog: if the hook is never called (nothing pushes the quit
            // event), the blocking loop would hang the suite. SDL_PushEvent is
            // thread-safe; after 15 s it asks the loop to quit so the
            // assertions below fail with a message instead.
            std::atomic<bool> loopDone{false};
            std::thread watchdog([&] {
                for (int tick = 0; tick < 150 && !loopDone.load(); ++tick) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                if (!loopDone.load()) {
                    SDL_Event quit{};
                    quit.type = SDL_EVENT_QUIT;
                    SDL_PushEvent(&quit);
                }
            });
            RowlEngine_RunWithFrameCallback(h, &runProbeCallback, &probe);
            loopDone = true;
            watchdog.join();
            req(probe.pushedQuit && probe.frames == 8,
                "the loop calls the hook once per frame until quit (frames " + std::to_string(probe.frames) + ")");
            req(driver.stats().triggers == 2 && driver.stats().refusedTriggers == 0,
                "the driver started the chapter-1 window, then the chapter-2 window from inside the loop; triggers=" +
                    std::to_string(driver.stats().triggers));
            req(driver.stats().pumpCalls >= 2, "the loop's frames pumped the queue after each window");
            RowlEngine_Destroy(h);
        }
        // A null callback is plain Run: an offscreen handle is refused, never hangs.
        RowlEngineHandle offscreen = RowlEngine_Create();
        req(offscreen && RowlEngine_Init(offscreen, 320, 180, 0) == 1, "offscreen init");
        RowlEngine_RunWithFrameCallback(offscreen, nullptr, nullptr);
        req(RowlEngine_GetLastResultCode(offscreen) == ROWL_RESULT_STATE_ERROR,
            "an offscreen handle is refused by the callback variant too (StateError), like RowlEngine_Run");
        int calls = 0;
        RowlEngine_RunWithFrameCallback(offscreen, [](RowlEngineHandle, float, void* user) { ++*static_cast<int*>(user); }, &calls);
        req(calls == 0, "the refused loop never calls the hook");
        RowlEngine_Destroy(offscreen);
    }
    TEST_PASS("RowlEngine_RunWithFrameCallback calls the hook per frame in the real loop, keeps Run's refusals, and the prefetch driver reacts inside it");

    fs::remove_all(g_root, ec);
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
}
