/**
 * test_extras_unlocks.cpp — D07-1: unlockable content (CG gallery, music room,
 * achievements): the extras.json catalog, the profile `unlocks` records, the
 * real-entry triggers and the C API.
 *
 * Kilit:
 *  1. Katalog ayristiricisi: gecerli dosya; yerellestirilmis baslik (tr/en, eksik
 *     dilde ilk mevcut); gecersiz oge / yinelenen id / oge siniri / dosya boyutu /
 *     format / JSON tanilari (dosya bir butun reddedilmez); yol normalizasyonu.
 *  2. Profil deposu: unlock idempotent (yeni = Ok), sira korunur, gecersiz id,
 *     salt-okunur / baglanmamis, tur siniri (Full), achievement `at`; bos
 *     unlocks `[]` kalir, ilk unlock nesne olur; eski/opak biciim bos sayilir +
 *     tani; BILINMEYEN anahtarlar (ust duzey ve unlocks icindeki) korunur.
 *  3. Otomatik CG (arka plan / karakter sprite'i) ve muzik (bgm_track) gercek
 *     dugum girisinde acilir (yol on eki / buyuk-kucuk harf farki dahil); proje
 *     baglanirken baslangic dugumu de.
 *  4. YAN ETKISIZLIK: load, rewind, backlog voice replay, kosulla atlanan dugum ve
 *     basarisiz (geri alinan) sahne guncellemesi hicbir sey acmaz; yeni gercek
 *     giris load'dan sonra yine acar.
 *  5. Acik `unlock` bileseni: achievement + `at`; katalogda olmayan / bozuk bilesen
 *     profile YAZILMAZ ve dugum basina tek uyari; devre disi bilesen yok sayilir;
 *     ayni dugume tekrar girmek tekrar acmaz (idempotans, ayni `at`, tek bildirim).
 *  6. Kalicilik: motoru kapat / ac (yeni handle, ayni XDG) -> unlock'lar duruyor.
 *  7. C API: yetenek biti; GetExtrasJson sekli + tampon sozlesmesi + dil; ProfileUnlock /
 *     ProfileIsUnlocked hata kodlari; Init oncesi STATE_ERROR; salt-okunur profil.
 *  8. Ornek proje (kayip_tablo): extras.json tanisiz ayrisir, her image / track VFS'te
 *     bulunur, hikayedeki her unlock bileseni katalogda, her galeri/muzik ogesi
 *     hikayede gosteriliyor.
 *
 * Bilerek-boz: applyEntryUnlocks'in replayEntryEffects kapisini kaldirmak 4'u
 * (load / rewind), cagriyi sahne guncellemesinin dongusune almak 4'un geri alinan
 * guncelleme kontrolunu, unlock'in markDirty'sini kaldirmak 6'yi, katalog kontrolunu
 * (unlockExtra) kaldirmak 5 ve 7'yi, serialize'daki unlocks nesnesini kaldirmak 2 ve
 * 6'yi kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/state/extras_catalog.hpp"
#include "rowl/state/player_profile.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::State::ExtrasCatalog;
using Rowl::State::PlayerProfileStatus;
using Rowl::State::PlayerProfileStore;
using Rowl::State::ProfileMutation;
using Rowl::State::UnlockKind;

constexpr const char* kSection = "extras-unlocks";

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
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// 16-bit mono PCM silence: a bgm / voice clip the audio side can decode.
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

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "30000000-0000-4000-8000-%012d", id);
    return text;
}

// ── Log capture (warnings are the observable error policy) ─────────────────
std::mutex g_logMutex;
std::vector<std::string> g_warnings;

void captureLog(int level, const char*, const char* message, const char*) {
    if (level < 3) return;  // Warn and above
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_warnings.emplace_back(message ? message : "");
}

size_t warningsContaining(const std::string& needle) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    size_t count = 0;
    for (const auto& line : g_warnings) {
        if (line.find(needle) != std::string::npos) ++count;
    }
    return count;
}

void clearWarnings() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_warnings.clear();
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

// ── Graph builder ──────────────────────────────────────────────────────────
struct Node {
    int id = 0;
    std::string text;
    std::vector<json> extra;   // components besides the dialogue
    std::vector<int> next;
};

json component(const std::string& type, const std::string& id, const json& data, bool enabled = true) {
    return {{"type", type}, {"id", id}, {"enabled", enabled}, {"data", data}};
}
json bg(const std::string& texture) { return component("background", "bg_" + texture, {{"texture", texture}}); }
json sprite(const std::string& file) { return component("character", "ch_" + file, {{"sprite", file}}); }
json bgm(const std::string& track, const std::string& voice = "") {
    json data = {{"bgm_track", track}};
    if (!voice.empty()) data["voice_track"] = voice;
    return component("audio", "au_" + track, data);
}
json unlockOf(const std::string& kind, const std::string& id, bool enabled = true) {
    return component("unlock", "ul_" + kind + "_" + id, {{"kind", kind}, {"id", id}}, enabled);
}
json variableSet(const std::string& key, const std::string& value) {
    return component("variable", "v_" + key, {{"key", key}, {"value", value}, {"operation", "set"}});
}
json condition(const std::string& expression, int failTarget) {
    return component("condition", "cond", {{"expression", expression}, {"fail_target_node_id", std::to_string(failTarget)}});
}

std::string buildGraph(const std::vector<Node>& nodes) {
    json out = json::array();
    for (const Node& n : nodes) {
        json components = json::array();
        components.push_back(
            {{"type", "dialogue"}, {"id", "d" + std::to_string(n.id)}, {"enabled", true},
             {"data", {{"speaker", "S"}, {"dialogue", n.text}, {"typewriter_enabled", false},
                       {"content_id", cid(n.id)}}}});
        for (const json& extra : n.extra) components.push_back(extra);
        json edges = json::array();
        for (int target : n.next) edges.push_back({{"id", target}});
        out.push_back({{"id", n.id}, {"title", "N" + std::to_string(n.id)},
                       {"objects", json::array({{{"id", "o" + std::to_string(n.id)}, {"name", "D"},
                                                 {"is_active", true}, {"components", components}}})},
                       {"next_nodes", edges}});
    }
    return json({{"format_version", 4}, {"start_node_id", nodes.front().id}, {"nodes", out}}).dump();
}

// The shared catalog of the engine-level tests.
const char* const kCatalog = R"({
  "format": 1,
  "gallery": [
    {"id": "cg_moon", "title": {"tr": "Ay", "en": "Moon"}, "image": "images/moon.jpg"},
    {"id": "cg_hero", "title": "Hero", "image": "hero.png"}
  ],
  "music": [
    {"id": "m_noct", "title": {"tr": "Nokturn", "en": "Nocturne"}, "track": "audio/noct.wav"}
  ],
  "achievements": [
    {"id": "a_end", "title": {"tr": "Son", "en": "The End"}, "description": {"tr": "Sona ulas.", "en": "Reach the end."}},
    {"id": "a_secret", "title": "Secret", "hidden": true}
  ]
})";

int g_rigCounter = 0;
fs::path g_root;

struct Reopen {};

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    fs::path game;

    // A fresh project directory.
    explicit Rig(const std::string& graph, const std::string& catalog = kCatalog,
                 const std::string& localeMembers = "")
        : Rig(makeProject(graph, catalog, localeMembers), Reopen{}) {}

    // Re-opens an existing project directory (same game id -> same profile).
    Rig(const fs::path& existingGame, Reopen) : game(existingGame) {
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        RowlEngine_SetPlayState(h, 1);
        RowlEngine_Step(h, 0.016f);
    }
    ~Rig() { if (h) RowlEngine_Destroy(h); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    static fs::path makeProject(const std::string& graph, const std::string& catalog,
                                const std::string& localeMembers) {
        ++g_rigCounter;
        const fs::path dir = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "5b7e2d14-3a9c-4e60-8f12-7c1d5a9e0b%02x", g_rigCounter);
        writeBytes(dir / "project.rowlproj",
                   std::string(R"({"project_uuid":")") + uuid + R"(","name":"Extras Test")" +
                       (localeMembers.empty() ? "" : "," + localeMembers) + "}");
        writeBytes(dir / "Assets" / "json" / "full_story_graph.json", graph);
        if (!catalog.empty()) writeBytes(dir / "Assets" / "json" / "extras.json", catalog);
        writeBytes(dir / "Assets" / "audio" / "noct.wav", makeWav(1));
        writeBytes(dir / "Assets" / "audio" / "v.wav", makeWav(1));
        if (!localeMembers.empty()) {
            for (const char* locale : {"en", "tr"}) {
                writeBytes(dir / "Assets" / "locales" / (std::string(locale) + ".json"),
                           std::string(R"({"schema_version":1,"locale":")") + locale + R"(","entries":{}})");
            }
        }
        return dir;
    }

    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    void advance() { RowlEngine_AdvanceNode(h, 0); }
    std::string dialogue() const { return capiText(h, RowlEngine_GetDialogueUtf8); }
    void expectNode(uint64_t id, const std::string& what) const {
        req(node() == id, what + " (node " + std::to_string(node()) + ", expected " + std::to_string(id) + ")");
    }
    bool unlocked(const char* kind, const std::string& id) const {
        int32_t value = -1;
        req(RowlEngine_ProfileIsUnlocked(h, kind, id.c_str(), &value) == ROWL_RESULT_OK,
            std::string("IsUnlocked failed for ") + kind + ":" + id);
        return value == 1;
    }
    json extras() const {
        const json parsed = json::parse(capiText(h, RowlEngine_GetExtrasJson), nullptr, false);
        req(parsed.is_object(), "extras JSON did not parse");
        return parsed;
    }
    json profile() const {
        const json parsed = json::parse(capiText(h, RowlEngine_ProfileGetJson), nullptr, false);
        req(parsed.is_object(), "profile JSON did not parse");
        return parsed;
    }
    std::vector<std::string> notices() const {
        std::vector<std::string> out;
        const json all = extras();
        for (const json& row : all["pending_notices"]) out.push_back(row.value("kind", "") + ":" + row.value("id", ""));
        return out;
    }
    // Every id the profile records, "kind:id", in the order they were unlocked.
    std::set<std::string> allUnlocks() const {
        std::set<std::string> out;
        const json document = profile();
        const json& unlocks = document["unlocks"];
        if (!unlocks.is_object()) return out;
        for (const json& id : unlocks["cg"]) out.insert("cg:" + id.get<std::string>());
        for (const json& id : unlocks["music"]) out.insert("music:" + id.get<std::string>());
        for (const json& row : unlocks["achievements"]) out.insert("achievement:" + row["id"].get<std::string>());
        return out;
    }
    void forget() { engine->playerProfile().clearUnlocksForTesting(); }
    void expectNothingUnlocked(const std::string& what) const {
        const auto all = allUnlocks();
        std::string joined;
        for (const auto& item : all) joined += item + " ";
        req(all.empty(), what + ": expected no unlocks, found " + joined);
    }
};

std::string describeSet(const std::set<std::string>& items) {
    std::string out;
    for (const auto& item : items) out += item + " ";
    return out;
}

// ── 1. catalog parser ──────────────────────────────────────────────────────
void catalogTests() {
    using namespace Rowl::State;
    {
        const ExtrasCatalog c = parseExtrasCatalog(kCatalog);
        req(c.fileFound && c.diagnostics.empty(), "valid catalog must parse clean");
        req(c.gallery.size() == 2 && c.music.size() == 1 && c.achievements.size() == 2, "item counts");
        req(c.gallery[0].id == "cg_moon" && c.gallery[0].image == "images/moon.jpg" && c.gallery[0].thumbnail.empty(),
            "gallery fields");
        req(c.gallery[0].title.resolve("tr") == "Ay" && c.gallery[0].title.resolve("en") == "Moon",
            "localized title resolves per language");
        req(c.gallery[0].title.resolve("de") == "Ay", "missing language falls back to the first entry");
        req(c.gallery[1].title.resolve("tr") == "Hero" && c.gallery[1].title.resolve("en") == "Hero",
            "plain title answers every language");
        req(c.achievements[1].hidden && !c.achievements[0].hidden, "hidden flag");
        req(c.achievements[1].description.empty() && c.achievements[1].description.resolve("tr").empty(),
            "missing description resolves to the empty string");
        req(c.findGallery("cg_hero") != nullptr && c.findGallery("nope") == nullptr &&
                c.findMusic("m_noct") != nullptr && c.findAchievement("a_end") != nullptr,
            "lookups");
    }
    {
        // Each bad item is skipped with a diagnostic; the good ones survive.
        const ExtrasCatalog c = parseExtrasCatalog(R"({
          "format": 1,
          "unknown_future_key": [1, 2],
          "gallery": [
            {"id": "ok", "image": "a.png"},
            {"id": "Bad Id", "image": "a.png"},
            {"id": "", "image": "a.png"},
            {"id": "no_image"},
            {"id": "abs", "image": "/etc/passwd"},
            {"id": "dots", "image": "../secret.png"},
            {"id": "drive", "image": "C:\\x.png"},
            {"id": "ok", "image": "dup.png"},
            {"id": "bad_title", "image": "b.png", "title": 5},
            {"id": "thumb_bad", "image": "b.png", "thumbnail": 7},
            "not an object",
            {"id": "ok2", "image": "c.png", "thumbnail": "t/c.png"}
          ],
          "music": {"not": "an array"},
          "achievements": [
            {"id": "a1", "hidden": "yes"},
            {"id": "a2", "description": [1]},
            {"id": "a3", "hidden": true}
          ]
        })");
        req(c.gallery.size() == 2 && c.gallery[0].id == "ok" && c.gallery[1].id == "ok2" &&
                c.gallery[1].thumbnail == "t/c.png",
            "only the valid gallery items survive (first duplicate wins): " + std::to_string(c.gallery.size()));
        req(c.gallery[0].image == "a.png", "the repeated id kept the FIRST item");
        req(c.music.empty(), "a non-array list is ignored");
        req(c.achievements.size() == 1 && c.achievements[0].id == "a3", "bad achievements skipped");
        // 10 bad gallery items + the non-array music list + 2 bad achievements.
        req(c.diagnostics.size() == 13, "one diagnostic per rejected item/list, got " + std::to_string(c.diagnostics.size()));
    }
    {
        // List limit: 256 per list, one diagnostic for the surplus.
        json big = {{"format", 1}, {"gallery", json::array()}};
        for (int i = 0; i < 300; ++i) big["gallery"].push_back({{"id", "g" + std::to_string(i)}, {"image", "i.png"}});
        const ExtrasCatalog c = parseExtrasCatalog(big.dump());
        req(c.gallery.size() == Rowl::State::kExtrasMaxItemsPerList && c.gallery.back().id == "g255",
            "the list is cut at 256 items");
        req(c.diagnostics.size() == 1, "one diagnostic for the whole surplus");
    }
    {
        // Whole-file rejections.
        const std::string huge = R"({"format":1,"pad":")" + std::string(Rowl::State::kExtrasMaxFileBytes, 'x') + R"("})";
        for (const std::string& text : {huge, std::string("not json"), std::string("[]"), std::string("{}"),
                                        std::string(R"({"format":2,"gallery":[{"id":"x","image":"a.png"}]})"),
                                        std::string(R"({"format":"1"})")}) {
            const ExtrasCatalog c = parseExtrasCatalog(text);
            req(c.fileFound && c.empty() && c.diagnostics.size() == 1,
                "unusable file: empty catalog + one diagnostic (" + text.substr(0, 24) + ")");
        }
        const ExtrasCatalog onlyFormat = parseExtrasCatalog(R"({"format":1})");
        req(onlyFormat.empty() && onlyFormat.diagnostics.empty(), "format-only file is a valid empty catalog");
    }
    {
        using Rowl::State::canonicalMediaPath;
        req(canonicalMediaPath("bogaz.jpg", "images/") == "bogaz.jpg", "bare name");
        req(canonicalMediaPath("images/bogaz.jpg", "images/") == "bogaz.jpg", "images/ prefix");
        req(canonicalMediaPath("Assets/images/Bogaz.JPG", "images/") == "bogaz.jpg", "Assets/images + case");
        req(canonicalMediaPath("assets\\images\\bogaz.jpg", "images/") == "bogaz.jpg", "backslashes");
        req(canonicalMediaPath("./images/sub/x.png", "images/") == "sub/x.png", "./ and subfolder kept");
        req(canonicalMediaPath("audio/n.ogg", "audio/") == "n.ogg" && canonicalMediaPath("Assets/audio/n.ogg", "audio/") == "n.ogg",
            "audio prefixes");
        req(canonicalMediaPath("images/x.png", "audio/") == "images/x.png", "a foreign prefix is not stripped");
        req(isValidExtrasId("a_b.c-9") && !isValidExtrasId("A") && !isValidExtrasId("") &&
                !isValidExtrasId(std::string(65, 'a')) && isValidExtrasId(std::string(64, 'a')),
            "id rule");
    }
    TEST_PASS("extras.json parser: localized titles, per-item diagnostics, limits, whole-file rejections, path keys");
}

// ── 2. profile store ───────────────────────────────────────────────────────
void storeTests() {
    const fs::path base = g_root / "store";
    const std::string kGame = "9c1f3a52-7d4e-4b86-a210-5e8f6c0d3b71";
    const auto readJsonFile = [](const fs::path& path) { return json::parse(readBytes(path)); };

    {
        // Fresh profile: nothing unlocked keeps the v1 `[]`; unlock/idempotence/order.
        const fs::path dir = base / "fresh";
        PlayerProfileStore store;
        req(store.unlock(UnlockKind::Cg, "a") == ProfileMutation::Unmounted, "unmounted store refuses");
        store.open(dir, kGame);
        req(store.status() == PlayerProfileStatus::Missing, "no file yet");
        req(store.flush() && !fs::exists(store.path()), "nothing pending, nothing written");
        req(json::parse(store.toJson())["unlocks"] == json::array(), "empty unlocks stays `[]`");
        req(store.unlock(UnlockKind::Cg, "zeta") == ProfileMutation::Ok &&
                store.unlock(UnlockKind::Cg, "alpha") == ProfileMutation::Ok &&
                store.unlock(UnlockKind::Cg, "zeta") == ProfileMutation::Unchanged,
            "new = Ok, repeat = Unchanged");
        req(store.unlockedIds(UnlockKind::Cg) == std::vector<std::string>({"zeta", "alpha"}) &&
                store.unlockCount(UnlockKind::Cg) == 2 && store.isUnlocked(UnlockKind::Cg, "alpha") &&
                !store.isUnlocked(UnlockKind::Music, "alpha"),
            "unlock order is kept; kinds are separate lists");
        for (const char* bad : {"", "UPPER", "has space", "a/b", "é"}) {
            req(store.unlock(UnlockKind::Music, bad) == ProfileMutation::Invalid, std::string("invalid id: ") + bad);
        }
        req(store.unlock(UnlockKind::Music, std::string(65, 'a')) == ProfileMutation::Invalid, "65-char id");
        req(store.unlock(UnlockKind::Achievement, "win", "2026-10-24T12:34:56Z") == ProfileMutation::Ok &&
                store.achievementTime("win") == "2026-10-24T12:34:56Z",
            "explicit achievement time kept");
        req(store.unlock(UnlockKind::Achievement, "win2", "garbage") == ProfileMutation::Ok &&
                std::regex_match(store.achievementTime("win2"),
                                 std::regex(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)")),
            "invalid time falls back to now (ISO UTC)");
        req(store.unlock(UnlockKind::Cg, "no_time") == ProfileMutation::Ok && store.achievementTime("no_time").empty(),
            "only achievements carry a time");
        req(store.dirty() && store.flush(), "flush");
        const json written = readJsonFile(store.path());
        req(written["unlocks"]["cg"] == json::parse(R"(["zeta","alpha","no_time"])") &&
                written["unlocks"]["music"] == json::array() && written["unlocks"]["achievements"].size() == 2 &&
                written["unlocks"]["achievements"][0] == json::parse(R"({"id":"win","at":"2026-10-24T12:34:56Z"})"),
            "written unlocks object: " + written["unlocks"].dump());
        PlayerProfileStore again;
        again.open(dir, kGame);
        req(again.diagnostic().empty() && again.unlockedIds(UnlockKind::Cg) == store.unlockedIds(UnlockKind::Cg) &&
                again.achievementTime("win") == "2026-10-24T12:34:56Z",
            "reload keeps everything: " + again.diagnostic());
        req(again.unlock(UnlockKind::Cg, "alpha") == ProfileMutation::Unchanged && !again.dirty(),
            "an unlock that is already recorded does not dirty the store");
    }
    {
        // Legacy / opaque forms: empty or dropped with a note; unknown keys survive.
        struct Case { const char* name; const char* unlocks; bool expectNote; const char* noteHas; };
        const Case cases[] = {
            {"empty array", "[]", false, ""},
            {"legacy array", R"(["gallery_1",{"id":2,"at":"t"}])", true, "Legacy"},
            {"string", R"("cg_1")", true, "Non-object"},
            {"number", "7", true, "Non-object"},
            {"null", "null", true, "Non-object"},
        };
        int index = 0;
        for (const Case& c : cases) {
            const fs::path dir = base / ("legacy" + std::to_string(index++));
            writeBytes(dir / "player-profile.json",
                       std::string(R"({"version":1,"zeta":{"deep":[1]},"unlocks":)") + c.unlocks + "}");
            PlayerProfileStore store;
            store.open(dir, "");
            req(store.status() == PlayerProfileStatus::Loaded, std::string(c.name) + ": loads");
            req(store.unlockCount(UnlockKind::Cg) == 0 && store.unlockCount(UnlockKind::Music) == 0 &&
                    store.unlockCount(UnlockKind::Achievement) == 0,
                std::string(c.name) + ": counts as empty");
            const bool noteOk = c.expectNote ? store.diagnostic().find(c.noteHas) != std::string::npos
                                             : store.diagnostic().empty();
            req(noteOk, std::string(c.name) + ": diagnostic '" + store.diagnostic() + "'");
            req(store.unlock(UnlockKind::Cg, "x") == ProfileMutation::Ok && store.flush(), std::string(c.name) + ": unlock after");
            const json out = readJsonFile(dir / "player-profile.json");
            req(out["zeta"] == json::parse(R"({"deep":[1]})") && out["unlocks"]["cg"] == json::parse(R"(["x"])"),
                std::string(c.name) + ": unknown key kept, unlocks is the object: " + out.dump());
        }
    }
    {
        // A damaged object: bad records are dropped, good ones and unknown inner keys kept.
        const fs::path dir = base / "damaged";
        writeBytes(dir / "player-profile.json", R"({"version":1,"unlocks":{
            "cg":["ok",5,"BAD ID","ok",null,"ok2"],
            "music":"nope",
            "achievements":[{"id":"a","at":"not a time"},"str",{"id":"b","at":"2026-01-02T03:04:05Z"},{"at":"x"},{"id":"a"}],
            "future":{"k":[1,2]}, "future2":3}})");
        PlayerProfileStore store;
        store.open(dir, "");
        req(store.unlockedIds(UnlockKind::Cg) == std::vector<std::string>({"ok", "ok2"}), "good cg ids kept");
        req(store.unlockCount(UnlockKind::Music) == 0, "non-array music ignored");
        req(store.unlockedIds(UnlockKind::Achievement) == std::vector<std::string>({"a", "b"}) &&
                store.achievementTime("a").empty() && store.achievementTime("b") == "2026-01-02T03:04:05Z",
            "good achievements kept (invalid time becomes unknown)");
        req(store.diagnostic().find("ignored") != std::string::npos, "dropped records are reported: " + store.diagnostic());
        req(store.unlock(UnlockKind::Music, "m") == ProfileMutation::Ok && store.flush(), "unlock + flush");
        const json out = readJsonFile(dir / "player-profile.json");
        req(out["unlocks"]["future"] == json::parse(R"({"k":[1,2]})") && out["unlocks"]["future2"] == 3 &&
                out["unlocks"]["cg"] == json::parse(R"(["ok","ok2"])") && out["unlocks"]["music"] == json::parse(R"(["m"])"),
            "unknown keys INSIDE unlocks survive a rewrite: " + out["unlocks"].dump());
    }
    {
        // Per-kind limit (seam), on load and on unlock.
        const fs::path dir = base / "limit";
        writeBytes(dir / "player-profile.json",
                   R"({"version":1,"unlocks":{"cg":["a","b","c","d","e"],"music":["m1"]}})");
        PlayerProfileStore store;
        store.setUnlockLimitForTesting(3);
        store.open(dir, "");
        req(store.unlockCount(UnlockKind::Cg) == 3 && store.diagnostic().find("limit") != std::string::npos,
            "load keeps the first 3 and reports the rest: " + store.diagnostic());
        req(store.unlock(UnlockKind::Cg, "f") == ProfileMutation::Full && !store.isUnlocked(UnlockKind::Cg, "f"),
            "a full kind refuses a new id");
        req(store.unlock(UnlockKind::Cg, "a") == ProfileMutation::Unchanged, "existing ids stay answerable when full");
        req(store.unlock(UnlockKind::Music, "m2") == ProfileMutation::Ok, "the limit is per kind");
        // The real limit.
        PlayerProfileStore real;
        real.open(base / "limit_real", "");
        for (size_t i = 0; i < Rowl::State::kPlayerProfileMaxUnlocksPerKind; ++i) {
            req(real.unlock(UnlockKind::Cg, "c" + std::to_string(i)) == ProfileMutation::Ok, "up to 1024 records");
        }
        req(real.unlock(UnlockKind::Cg, "overflow") == ProfileMutation::Full, "record 1025 is refused");
    }
    {
        // Read-only (newer schema) and wrong game never write.
        const fs::path dir = base / "readonly";
        const std::string future = R"({"version":99,"unlocks":{"cg":["a"]}})";
        writeBytes(dir / "player-profile.json", future);
        PlayerProfileStore store;
        store.open(dir, "");
        req(store.status() == PlayerProfileStatus::UnsupportedVersion &&
                store.unlock(UnlockKind::Cg, "x") == ProfileMutation::ReadOnly && store.flush(),
            "a newer-schema profile refuses unlocks");
        store.close();
        req(readBytes(dir / "player-profile.json") == future, "the read-only file is untouched");
        const fs::path other = base / "wrong_game";
        writeBytes(other / "player-profile.json", R"({"version":1,"game_id":"someone-else","unlocks":[]})");
        PlayerProfileStore wrong;
        wrong.open(other, kGame);
        req(wrong.status() == PlayerProfileStatus::WrongGame &&
                wrong.unlock(UnlockKind::Cg, "x") == ProfileMutation::ReadOnly,
            "a profile of another game refuses unlocks");
    }
    {
        req(Rowl::State::parseUnlockKind("cg") == UnlockKind::Cg && Rowl::State::parseUnlockKind("music") == UnlockKind::Music &&
                Rowl::State::parseUnlockKind("achievement") == UnlockKind::Achievement &&
                !Rowl::State::parseUnlockKind("CG") && !Rowl::State::parseUnlockKind("") &&
                !Rowl::State::parseUnlockKind("achievements"),
            "kind names are exact");
        req(std::string(Rowl::State::unlockKindName(UnlockKind::Achievement)) == "achievement", "kind name round trip");
    }
    TEST_PASS("Profile store: idempotent ordered unlocks, `[]` while empty, legacy forms dropped with a note, unknown keys kept, limits, read-only");
}

// ── 3-6. engine: triggers, guards, explicit component, persistence ─────────
std::vector<Node> triggerGraph() {
    // 1 start (no catalog image) -> 2 moon + music (+ voice) -> 3 hero sprite -> 4 ending
    // (+ bad components) -> 5 (bad kind) -> 4 (a loop to revisit node 4).
    Node n1{1, "Start", {bg("sky.jpg")}, {2}};
    Node n2{2, "Moon", {bg("Assets/images/MOON.JPG"), bgm("noct.wav", "v.wav")}, {3}};
    Node n3{3, "Hero", {bg("sky.jpg"), sprite("hero.png")}, {4}};
    Node n4{4, "Ending",
            {unlockOf("achievement", "a_end"), unlockOf("achievement", "ghost"), unlockOf("cg", "nope"),
             unlockOf("achievement", "a_secret", /*enabled=*/false)},
            {5}};
    Node n5{5, "Loop", {unlockOf("foo", "x"), component("unlock", "ul_noid", {{"kind", "cg"}})}, {4}};
    return {n1, n2, n3, n4, n5};
}

void engineTests() {
    // ── 3. triggers on a real entry ────────────────────────────────────────
    {
        Rig rig(buildGraph(triggerGraph()));
        rig.expectNothingUnlocked("start node (not a catalog image)");
        req(rig.extras()["loaded"] == true && rig.extras()["diagnostics"].empty(), "catalog loaded clean");
        rig.advance();
        rig.expectNode(2, "to the moon node");
        req(rig.unlocked("cg", "cg_moon") && rig.unlocked("music", "m_noct") && !rig.unlocked("cg", "cg_hero"),
            "background (prefix + case variant) and bgm_track unlock; the hero is still locked: " +
                describeSet(rig.allUnlocks()));
        rig.advance();
        rig.expectNode(3, "to the hero node");
        req(rig.unlocked("cg", "cg_hero"), "a character sprite unlocks its CG");
        req(rig.notices().empty(), "CG / music produce no notice (achievements only)");
        clearWarnings();
        rig.advance();
        rig.expectNode(4, "to the ending node");
        req(rig.unlocked("achievement", "a_end"), "explicit achievement component");
        req(!rig.unlocked("achievement", "a_secret"), "a disabled unlock component does nothing");
        req(rig.notices() == std::vector<std::string>({"achievement:a_end"}),
            "the new achievement is queued once: " + rig.extras()["pending_notices"].dump());
        const json extras = rig.extras();
        const std::string at = extras["achievements"][0]["at"].get<std::string>();
        req(extras["achievements"][0]["unlocked"] == true &&
                std::regex_match(at, std::regex(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ)")),
            "achievement `at` is an ISO-8601 UTC stamp: " + extras["achievements"][0].dump());
        req(extras["achievements"][1]["at"].is_null() && extras["achievements"][1]["unlocked"] == false, "locked achievement: at is null");

        // ── 5. bad components: nothing written, one warning per node ───────
        const auto afterEnd = rig.allUnlocks();
        req(afterEnd == std::set<std::string>({"cg:cg_moon", "cg:cg_hero", "music:m_noct", "achievement:a_end"}),
            "unknown ids are not written: " + describeSet(afterEnd));
        req(warningsContaining("unlock component") == 1, "node 4: ONE warning for its two bad components, got " +
                                                         std::to_string(warningsContaining("unlock component")));
        rig.advance();
        rig.expectNode(5, "to the loop node");
        req(warningsContaining("unlock component") == 2 && warningsContaining("Node #5 unlock component") == 1,
            "node 5 (bad kind + missing id): one more warning");
        // Back to 4: idempotent, no second notice, same `at`, no repeated warning.
        rig.advance();
        rig.expectNode(4, "back to the ending node");
        req(rig.allUnlocks() == afterEnd, "revisiting changes nothing");
        req(rig.notices() == std::vector<std::string>({"achievement:a_end"}), "no second notice");
        req(rig.extras()["achievements"][0]["at"] == at, "the unlock time is not rewritten");
        req(warningsContaining("unlock component") == 2, "re-entering a node does not repeat its warning");
        rig.engine->clearUnlockNotices();
        req(rig.notices().empty(), "notices can be cleared");
        TEST_PASS("Real entry unlocks CG (background / sprite), music (bgm_track) and explicit achievements; bad components warn once per node");
    }
    {
        // The start node of a freshly mounted project is a real entry too.
        Node n1{1, "Start", {bg("images/moon.jpg")}, {}};
        Rig rig(buildGraph({n1}));
        req(rig.unlocked("cg", "cg_moon"), "graph mount unlocks the start node's CG");
        TEST_PASS("Graph mount (start node) unlocks");
    }

    // ── 4. guards: no unlock without a real entry ──────────────────────────
    {
        // load
        Rig rig(buildGraph(triggerGraph()));
        rig.advance();
        rig.expectNode(2, "node 2");
        req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save at node 2");
        rig.advance();
        rig.expectNode(3, "node 3");
        rig.forget();
        req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load the node-2 save");
        rig.expectNode(2, "loaded node");
        rig.expectNothingUnlocked("load restores node 2's scene but must not unlock");
        rig.advance();
        rig.expectNode(3, "advance after load");
        req(rig.unlocked("cg", "cg_hero"), "a real entry after a load unlocks again");
        // rewind
        rig.forget();
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind one step");
        rig.expectNode(2, "rewound to node 2");
        rig.expectNothingUnlocked("rewind");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind again");
        rig.expectNothingUnlocked("rewind to the start");
        TEST_PASS("Load and rewind never unlock (and a real entry afterwards still does)");
    }
    {
        // backlog voice replay: node 2 carries the voice clip, the replay re-presents nothing
        Rig rig(buildGraph(triggerGraph()));
        rig.advance();
        rig.advance();
        rig.expectNode(3, "node 3");
        const json history = json::parse(capiText(rig.h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
        int voicedRow = -1;
        for (size_t i = 0; i < history.size(); ++i) {
            if (history[i].value("node_id", uint64_t{0}) == 2 && !history[i].value("voice", std::string()).empty()) {
                voicedRow = static_cast<int>(i);
            }
        }
        req(voicedRow >= 0, "the voiced history row of node 2 exists: " + history.dump());
        rig.forget();
        req(RowlEngine_ReplayBacklogVoice(rig.h, static_cast<uint32_t>(voicedRow)) == ROWL_RESULT_OK, "replay the voiced row");
        rig.expectNothingUnlocked("backlog voice replay");
        TEST_PASS("Backlog voice replay unlocks nothing");
    }
    {
        // a node skipped by a false condition unlocks nothing; its redirect target does
        Node n1{1, "Start", {variableSet("gold", "0")}, {2}};
        Node n2{2, "Gated",
                {bg("images/moon.jpg"), bgm("noct.wav"), unlockOf("achievement", "a_secret"),
                 condition("tonumber(gold) >= 10", 4)},
                {3}};
        Node n3{3, "After", {}, {}};
        Node n4{4, "Fallback", {sprite("hero.png"), unlockOf("achievement", "a_end")}, {}};
        Rig rig(buildGraph({n1, n2, n3, n4}));
        rig.advance();
        rig.expectNode(4, "the false condition redirects");
        req(rig.allUnlocks() == std::set<std::string>({"cg:cg_hero", "achievement:a_end"}),
            "only the redirect target unlocks: " + describeSet(rig.allUnlocks()));
        // and a true condition plays (and unlocks) the node
        Node m1{1, "Start", {variableSet("gold", "50")}, {2}};
        Rig open(buildGraph({m1, n2, n3, n4}));
        open.advance();
        open.expectNode(2, "the true condition plays the node");
        req(open.allUnlocks() == std::set<std::string>({"cg:cg_moon", "music:m_noct", "achievement:a_secret"}),
            "the played node unlocks: " + describeSet(open.allUnlocks()));
        TEST_PASS("A node skipped by a condition unlocks nothing");
    }
    {
        // a scene update that throws is rolled back - and its unlocks never happen
        json badAdd = component("variable", "v_bad", {{"key", "k"}, {"value", "abc"}, {"operation", "add"}});
        Node n1{1, "Start", {}, {2}};
        Node n2{2, "Broken", {bg("images/moon.jpg"), unlockOf("achievement", "a_end"), badAdd}, {}};
        Rig rig(buildGraph({n1, n2}));
        rig.advance();
        req(rig.dialogue() == "Start", "the failed update was rolled back (still shows node 1), got: " + rig.dialogue());
        rig.expectNothingUnlocked("a rolled-back scene update");
        TEST_PASS("A failed (rolled back) scene update unlocks nothing");
    }

    // ── 6. persistence across engine restarts ──────────────────────────────
    {
        fs::path game;
        std::string at;
        std::string profilePath;
        {
            Rig rig(buildGraph(triggerGraph()));
            game = rig.game;
            rig.advance();
            rig.advance();
            rig.advance();
            req(RowlEngine_ProfileFlush(rig.h) == ROWL_RESULT_OK, "flush");
            at = rig.extras()["achievements"][0]["at"].get<std::string>();
            profilePath = capiText(rig.h, RowlEngine_GetProfileDirectoryUtf8) + "/player-profile.json";
            const json onDisk = json::parse(readBytes(profilePath));
            req(onDisk["unlocks"]["cg"].size() == 2 && onDisk["unlocks"]["music"] == json::parse(R"(["m_noct"])") &&
                    onDisk["unlocks"]["achievements"][0]["id"] == "a_end" && onDisk["unlocks"]["achievements"][0]["at"] == at,
                "the profile file holds the unlocks: " + onDisk["unlocks"].dump());
        }
        {
            Rig again(game, Reopen{});
            again.expectNode(1, "a restarted engine begins at the start node");
            req(again.allUnlocks() == std::set<std::string>({"cg:cg_moon", "cg:cg_hero", "music:m_noct", "achievement:a_end"}),
                "unlocks survived the restart: " + describeSet(again.allUnlocks()));
            const json extras = again.extras();
            req(extras["counts"]["gallery"] == json::parse(R"({"total":2,"unlocked":2})") &&
                    extras["achievements"][0]["unlocked"] == true && extras["achievements"][0]["at"] == at,
                "extras JSON agrees after the restart (same `at`): " + extras["counts"].dump());
            req(again.notices().empty(), "the notice queue is session state (empty after a restart)");
        }
        TEST_PASS("Unlocks persist across an engine restart (same profile directory)");
    }
}

// ── 7. C API ───────────────────────────────────────────────────────────────
void capiTests() {
    {
        uint64_t caps = 0;
        req(RowlEngine_GetCapabilities(&caps) == ROWL_RESULT_OK &&
                (caps & ROWL_ENGINE_CAPABILITY_EXTRAS_UNLOCKS) == ROWL_ENGINE_CAPABILITY_EXTRAS_UNLOCKS &&
                ROWL_ENGINE_CAPABILITY_EXTRAS_UNLOCKS == 268435456u,
            "capability bit 268435456 is advertised");
    }
    {
        // null / not-initialized handles
        uint32_t size = 0;
        int32_t flag = 5;
        req(RowlEngine_GetExtrasJson(nullptr, nullptr, 0, &size) == ROWL_RESULT_INVALID_HANDLE &&
                RowlEngine_ProfileUnlock(nullptr, "cg", "x") == ROWL_RESULT_INVALID_HANDLE &&
                RowlEngine_ProfileIsUnlocked(nullptr, "cg", "x", &flag) == ROWL_RESULT_INVALID_HANDLE && flag == 0,
            "null handle");
        RowlEngineHandle early = RowlEngine_Create();
        req(early != nullptr, "create");
        req(RowlEngine_GetExtrasJson(early, nullptr, 0, &size) == ROWL_RESULT_STATE_ERROR &&
                RowlEngine_ProfileUnlock(early, "cg", "x") == ROWL_RESULT_STATE_ERROR &&
                RowlEngine_ProfileIsUnlocked(early, "cg", "x", &flag) == ROWL_RESULT_STATE_ERROR,
            "before Init everything is STATE_ERROR");
        RowlEngine_Destroy(early);
    }

    Rig rig(buildGraph(triggerGraph()));
    {
        // JSON shape + caller-buffer contract
        const json extras = rig.extras();
        req(extras["format"] == 1 && extras["loaded"] == true && extras["language"] == "tr" &&
                extras["profile_status"] == "missing" && extras["diagnostics"].is_array() &&
                extras["pending_notices"].is_array(),
            "top-level keys: " + extras.dump());
        req(extras["gallery"].size() == 2 && extras["music"].size() == 1 && extras["achievements"].size() == 2,
            "lists");
        const json& cg = extras["gallery"][0];
        req(cg["id"] == "cg_moon" && cg["title"] == "Ay" && cg["image"] == "images/moon.jpg" &&
                cg["thumbnail"] == "images/moon.jpg" && cg["unlocked"] == false,
            "gallery row (thumbnail defaults to the image, Turkish title for an undeclared-locale project): " + cg.dump());
        const json& ach = extras["achievements"][1];
        req(ach["id"] == "a_secret" && ach["hidden"] == true && ach["description"] == "" && ach["at"].is_null(),
            "achievement row: " + ach.dump());
        req(extras["counts"]["achievements"] == json::parse(R"({"total":2,"unlocked":0})"), "counts");

        uint32_t need = 0;
        req(RowlEngine_GetExtrasJson(rig.h, nullptr, 0, &need) == ROWL_RESULT_OK && need > 100, "size query");
        char tiny[4] = {'x', 'x', 'x', 'x'};
        req(RowlEngine_GetExtrasJson(rig.h, tiny, sizeof(tiny), &need) == ROWL_RESULT_BUFFER_TOO_SMALL && tiny[0] == '\0',
            "undersized buffer is cleared");
        req(RowlEngine_GetExtrasJson(rig.h, nullptr, 0, nullptr) == ROWL_RESULT_INVALID_ARGUMENT, "null size pointer");
    }
    {
        // ProfileUnlock / ProfileIsUnlocked
        int32_t flag = 9;
        req(RowlEngine_ProfileUnlock(rig.h, nullptr, "cg_moon") == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileUnlock(rig.h, "cg", nullptr) == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileUnlock(rig.h, "bogus", "cg_moon") == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileUnlock(rig.h, "CG", "cg_moon") == ROWL_RESULT_INVALID_ARGUMENT,
            "null / unknown kind");
        req(RowlEngine_ProfileUnlock(rig.h, "cg", "not_in_catalog") == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileUnlock(rig.h, "cg", "Bad Id!") == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileUnlock(rig.h, "music", "cg_moon") == ROWL_RESULT_INVALID_ARGUMENT,
            "ids outside the catalog of that kind");
        rig.expectNothingUnlocked("rejected ProfileUnlock calls");
        req(RowlEngine_ProfileUnlock(rig.h, "cg", "cg_moon") == ROWL_RESULT_OK &&
                RowlEngine_ProfileUnlock(rig.h, "cg", "cg_moon") == ROWL_RESULT_OK &&
                rig.unlocked("cg", "cg_moon"),
            "unlock + repeat");
        req(RowlEngine_ProfileUnlock(rig.h, "achievement", "a_secret") == ROWL_RESULT_OK &&
                rig.notices() == std::vector<std::string>({"achievement:a_secret"}),
            "an achievement unlocked through the API is queued as a notice");
        req(rig.profile()["unlocks"]["achievements"][0]["id"] == "a_secret" && rig.extras()["counts"]["gallery"]["unlocked"] == 1,
            "profile + extras reflect the API unlock");
        req(RowlEngine_ProfileIsUnlocked(rig.h, "cg", "not_in_catalog", &flag) == ROWL_RESULT_OK && flag == 0 &&
                RowlEngine_ProfileIsUnlocked(rig.h, "cg", "cg_hero", &flag) == ROWL_RESULT_OK && flag == 0 &&
                RowlEngine_ProfileIsUnlocked(rig.h, "cg", "cg_moon", &flag) == ROWL_RESULT_OK && flag == 1,
            "IsUnlocked reports the profile");
        req(RowlEngine_ProfileIsUnlocked(rig.h, "bogus", "x", &flag) == ROWL_RESULT_INVALID_ARGUMENT && flag == 0 &&
                RowlEngine_ProfileIsUnlocked(rig.h, nullptr, "x", &flag) == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileIsUnlocked(rig.h, "cg", nullptr, &flag) == ROWL_RESULT_INVALID_ARGUMENT &&
                RowlEngine_ProfileIsUnlocked(rig.h, "cg", "x", nullptr) == ROWL_RESULT_INVALID_ARGUMENT,
            "IsUnlocked argument errors");
        // a full kind
        rig.engine->playerProfile().setUnlockLimitForTesting(1);
        req(RowlEngine_ProfileUnlock(rig.h, "cg", "cg_hero") == ROWL_RESULT_VALIDATION_ERROR && !rig.unlocked("cg", "cg_hero"),
            "a full kind is VALIDATION_ERROR");
    }
    {
        // No extras.json: a normal empty state.
        Rig none(buildGraph(triggerGraph()), /*catalog=*/"");
        const json extras = none.extras();
        req(extras["loaded"] == false && extras["gallery"].empty() && extras["music"].empty() &&
                extras["achievements"].empty() && extras["diagnostics"].empty(),
            "a project without extras.json: " + extras.dump());
        none.advance();
        none.expectNothingUnlocked("no catalog, no unlock");
        req(RowlEngine_ProfileUnlock(none.h, "cg", "cg_moon") == ROWL_RESULT_INVALID_ARGUMENT, "nothing to unlock without a catalog");
    }
    {
        // An unusable catalog: empty + diagnostics (logged too), still working.
        clearWarnings();
        Rig bad(buildGraph(triggerGraph()), R"({"format": 9})");
        const json extras = bad.extras();
        req(extras["loaded"] == true && extras["gallery"].empty() && extras["diagnostics"].size() == 1,
            "unusable file: " + extras.dump());
        req(warningsContaining("[Extras]") == 1, "the diagnostic is also logged");
    }
    {
        // English project -> English titles.
        Rig en(buildGraph(triggerGraph()), kCatalog, R"("default_locale":"en","supported_locales":["en","tr"])");
        const json extras = en.extras();
        req(extras["language"] == "en" && extras["gallery"][0]["title"] == "Moon" &&
                extras["achievements"][0]["title"] == "The End" && extras["achievements"][0]["description"] == "Reach the end.",
            "English UI language: " + extras["achievements"][0].dump());
    }
    {
        // Read-only profile (newer schema): STATE_ERROR, file untouched.
        fs::path game;
        std::string profileFile;
        {
            Rig first(buildGraph(triggerGraph()));
            game = first.game;
            profileFile = capiText(first.h, RowlEngine_GetProfileDirectoryUtf8) + "/player-profile.json";
        }
        const std::string future = R"({"version":99,"unlocks":{"cg":["cg_moon"]}})";
        writeBytes(profileFile, future);
        Rig ro(game, Reopen{});
        req(ro.extras()["profile_status"] == "unsupported_version", "profile is read-only");
        req(RowlEngine_ProfileUnlock(ro.h, "cg", "cg_hero") == ROWL_RESULT_STATE_ERROR, "ProfileUnlock on a read-only profile");
        ro.advance();
        ro.advance();
        ro.advance();
        req(RowlEngine_ProfileFlush(ro.h) == ROWL_RESULT_OK && readBytes(profileFile) == future,
            "node entries write nothing to a read-only profile");
    }
    TEST_PASS("C API: capability bit, GetExtrasJson shape + buffers + language, ProfileUnlock / ProfileIsUnlocked errors, read-only profile");
}

// ── 8. the shipped sample ──────────────────────────────────────────────────
void sampleTests() {
    const fs::path sample = fs::path(ROWL_TEST_SOURCE_DIR) / "samples" / "kayip_tablo";
    const fs::path extrasFile = sample / "Assets" / "json" / "extras.json";
    req(fs::is_regular_file(extrasFile), "the sample ships extras.json");
    const ExtrasCatalog catalog = Rowl::State::parseExtrasCatalog(readBytes(extrasFile));
    req(catalog.diagnostics.empty(), "the sample's extras.json parses without a single diagnostic: " +
                                         (catalog.diagnostics.empty() ? std::string() : catalog.diagnostics[0]));
    req(catalog.gallery.size() >= 6 && catalog.gallery.size() <= 10 && catalog.music.size() >= 4 &&
            catalog.music.size() <= 5 && catalog.achievements.size() >= 3 && catalog.achievements.size() <= 4,
        "sample sizes: " + std::to_string(catalog.gallery.size()) + " CG, " + std::to_string(catalog.music.size()) +
            " music, " + std::to_string(catalog.achievements.size()) + " achievements");
    for (const auto& item : catalog.gallery) {
        req(!item.title.resolve("tr").empty() && !item.title.resolve("en").empty() && item.title.entries.size() == 2,
            "gallery item has Turkish + English titles: " + item.id);
    }

    // The story: every texture / bgm / unlock component, from the shipped graph.
    const json graph = json::parse(readBytes(sample / "Assets" / "json" / "full_story_graph.json"));
    req(graph["nodes"].size() == 184, "the sample keeps its 184 nodes");
    std::set<std::string> shownImages;
    std::set<std::string> playedTracks;
    std::set<std::string> unlockedAchievements;
    for (const json& node : graph["nodes"]) {
        for (const json& object : node["objects"]) {
            for (const json& comp : object["components"]) {
                const std::string type = comp["type"];
                const json& data = comp["data"];
                if (type == "background") shownImages.insert(Rowl::State::canonicalMediaPath(data.value("texture", ""), "images/"));
                if (type == "character") shownImages.insert(Rowl::State::canonicalMediaPath(data.value("sprite", ""), "images/"));
                if (type == "audio" && !data.value("bgm_track", std::string()).empty()) {
                    playedTracks.insert(Rowl::State::canonicalMediaPath(data["bgm_track"].get<std::string>(), "audio/"));
                }
                if (type == "unlock") {
                    req(data.value("kind", "") == "achievement" && catalog.findAchievement(data.value("id", "")) != nullptr,
                        "the story's unlock component names a catalog achievement: " + data.dump());
                    unlockedAchievements.insert(data["id"]);
                }
            }
        }
    }
    for (const auto& item : catalog.gallery) {
        req(shownImages.count(Rowl::State::canonicalMediaPath(item.image, "images/")) == 1,
            "gallery image is shown by the story (so it can be unlocked): " + item.id);
    }
    for (const auto& item : catalog.music) {
        req(playedTracks.count(Rowl::State::canonicalMediaPath(item.track, "audio/")) == 1,
            "music track is played by the story: " + item.id);
    }
    req(unlockedAchievements.size() == catalog.achievements.size(), "every sample achievement has an unlock component");

    // Mounted for real: the VFS finds every file, the catalog loads clean, the
    // start node unlocks its CG.
    RowlEngineHandle h = RowlEngine_Create();
    req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
    RowlEngine_SetProjectDirectory(h, sample.string().c_str());
    auto* engine = Rowl::Core::testEngineFromHandle(h);
    req(engine != nullptr && engine->getVfs() != nullptr, "engine + vfs");
    const auto vfsHas = [&](const std::vector<std::string>& candidates) {
        for (const auto& candidate : candidates) {
            if (engine->getVfs()->exists(candidate)) return true;
        }
        return false;
    };
    for (const auto& item : catalog.gallery) {
        req(vfsHas({item.image, "images/" + item.image, "Assets/images/" + item.image}), "VFS finds image " + item.image);
    }
    for (const auto& item : catalog.music) {
        req(vfsHas({item.track, "Assets/" + item.track, "Assets/audio/" + item.track, "audio/" + item.track}),
            "VFS finds track " + item.track);
    }
    const json extras = json::parse(capiText(h, RowlEngine_GetExtrasJson));
    req(extras["loaded"] == true && extras["diagnostics"].empty() && extras["gallery"].size() == catalog.gallery.size() &&
            extras["music"].size() == catalog.music.size() && extras["achievements"].size() == catalog.achievements.size(),
        "the mounted sample exposes its whole catalog: " + extras["counts"].dump());
    int32_t flag = 0;
    req(RowlEngine_ProfileIsUnlocked(h, "cg", "bogaz_gunduz", &flag) == ROWL_RESULT_OK && flag == 1,
        "the prologue's CG unlocks at mount");
    RowlEngine_Destroy(h);
    TEST_PASS("kayip_tablo: extras.json is clean, every image / track is in the VFS, the story references the whole catalog");
}

}  // namespace

void test_extras_unlocks() {
    TEST_SECTION("Unlockable content: extras.json, profile unlocks, triggers, C API (D07-1)");

    g_root = fs::temp_directory_path() / "rowl_extras_unlocks";
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
    RowlEngine_SetLogCallback(&captureLog);

    catalogTests();
    storeTests();
    engineTests();
    capiTests();
    sampleTests();

    RowlEngine_SetLogCallback(nullptr);
    fs::remove_all(g_root, ec);
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
}
