/**
 * test_f1_audit_fixes.cpp — F1: denetim A/B/D duzeltmeleri (geri sarma, bellek,
 * dugum-giris sesleri, gecmis).
 *
 * Kilit:
 *  1. A-1 geri sarma = bir onceki DUGUM: bilesenli/betikli dugumde (degisken +
 *     betik + muzik girisi birden cok halka yazar) Backspace TEK basista onceki
 *     dugume iner; degisken, muzik ve gecmis o dugumun tum giris commit'leri
 *     sonrasi halidir; kendine dongu (A->A) ziyaretleri ayri sayilir; ilk
 *     dugumde etkisiz; Auto kapanir; ham RowlEngine_Rewind(steps) halka adimi
 *     kalir, RowlEngine_RewindNodes eklemelidir.
 *  2. A-2 sesli dugume baska dugumlerden sonra donunce voice/sfx calar.
 *  3. A-3 gecmis kalici liste: halkalar arasi paylasim, derin zincir yikimi
 *     (ozyinelemesiz), eski kayit yuklenir, node_entry/choice anahtarlari
 *     eklemeli ve round-trip.
 *  4. A-4 gecmis etkin dilde (contentId ile) cozulur; dil donunce orijinale doner.
 *  5. A-5 uzun gecmis satiri: secili girdi tam gorunur + girdi ici kaydirma.
 *  6. A-6 Skip etkinken voice/sfx calmaz (BGM surer); Skip durunca dugumun sesi
 *     bir kez calar. A-7 SKIP rozeti yalniz gercekten atlarken. A-9 odak
 *     kaybinda Ctrl tutma biter.
 *  7. Urun karari 1: secilen cevap gecmiste ayri "> secenek" satiri (sonraki
 *     dugumun satiri; geri sarinca/kayitla tutarli; eski kayitlar yuklenir).
 *     Karar 6: gecmisten tekrar calinan klip pencere kapaninca durur.
 *
 * Bilerek-boz: (1) Type::Rewind'i rewind(1)'e geri almak 1'i; (2) engine.cpp'de
 * marker'i yalniz sesli dugumde guncellemek 2'yi; (3) withDialogueHistory'de
 * listeyi her halkada yeniden kurmak 3'u ve rowl_rewind_history_budget_probe'u;
 * (4) getDialogueHistory'de cozumu kaldirmak 4'u; (5) backlogCell'de secili
 * satirin tam acilimini kaldirmak 5'i; (6) deferEntrySounds'u false yapmak 6'yi;
 * (7) FocusLost dalini kaldirmak 6'daki odak testini kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"
#include "rowl/state/game_state.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;

constexpr const char* kSection = "f1-audit-fixes";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
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

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "20000000-0000-4000-8000-%012d", id);
    return text;
}

// ── Graph builder (raw component lists) ─────────────────────────────────────
struct Node {
    int id = 0;
    std::string text;
    std::vector<json> extra;                           // variable / script / audio components
    std::vector<int> next;                             // plain edges
    std::vector<std::pair<std::string, int>> choices;  // option_id -> target
};

std::string buildGraph(const std::vector<Node>& nodes) {
    json out = json::array();
    for (const Node& n : nodes) {
        json components = json::array();
        int serial = 0;
        for (const json& extra : n.extra) {
            json comp = extra;
            comp["id"] = "x" + std::to_string(n.id) + "_" + std::to_string(serial++);
            comp["enabled"] = true;
            components.push_back(comp);
        }
        json dialogue = {{"speaker", "S"}, {"dialogue", n.text}, {"typewriter_enabled", false},
                         {"content_id", cid(n.id)}};
        components.push_back({{"type", "dialogue"}, {"id", "d" + std::to_string(n.id)}, {"enabled", true},
                              {"data", dialogue}});
        json edges = json::array();
        if (!n.choices.empty()) {
            json options = json::array();
            float y = 520.0f;
            for (const auto& [optionId, target] : n.choices) {
                options.push_back({{"option_id", optionId}, {"text", optionId}, {"x", 680}, {"y", y},
                                   {"width", 560}, {"height", 64}});
                edges.push_back({{"id", target}, {"label", optionId}, {"option_id", optionId}});
                y += 80.0f;
            }
            components.push_back({{"type", "choice"}, {"id", "c" + std::to_string(n.id)}, {"enabled", true},
                                  {"data", {{"options", options}}}});
        }
        for (int target : n.next) edges.push_back({{"id", target}});
        out.push_back({{"id", n.id}, {"title", "N" + std::to_string(n.id)},
                       {"objects", json::array({{{"id", "o" + std::to_string(n.id)}, {"name", "D"},
                                                 {"is_active", true}, {"components", components}}})},
                       {"next_nodes", edges}});
    }
    return json({{"format_version", 4}, {"start_node_id", nodes.front().id}, {"nodes", out}}).dump();
}

json varSet(const std::string& key, const std::string& value, const std::string& op = "set") {
    return {{"type", "variable"}, {"data", {{"key", key}, {"value", value}, {"operation", op}}}};
}
json audioComp(const json& data) { return {{"type", "audio"}, {"data", data}}; }
json scriptComp(const std::string& code) { return {{"type", "script"}, {"data", {{"code", code}}}}; }

class InputHost final : public Rowl::Platform::PlatformHost {
public:
    explicit InputHost(std::shared_ptr<Rowl::Platform::PlatformHost> inner) : m_inner(std::move(inner)) {}
    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override {
        return m_inner ? m_inner->openAssetStream(path) : nullptr;
    }
    std::filesystem::path writableSavePath() const override {
        return m_inner ? m_inner->writableSavePath() : std::filesystem::path();
    }
    std::filesystem::path writableProfilePath() const override {
        return m_inner ? m_inner->writableProfilePath() : std::filesystem::path("profiles");
    }
    Rowl::Platform::LifecycleState lifecycleState() const override {
        return m_inner ? m_inner->lifecycleState() : Rowl::Platform::LifecycleState::Active;
    }
    std::vector<RuntimeInputEvent> takeInputEvents() override {
        std::vector<RuntimeInputEvent> result(input.begin(), input.end());
        input.clear();
        return result;
    }
    Rowl::Platform::RenderSurface renderSurface() const override {
        return m_inner ? m_inner->renderSurface() : Rowl::Platform::RenderSurface{};
    }
    Rowl::Platform::AudioFocus audioFocus() const override {
        return m_inner ? m_inner->audioFocus() : Rowl::Platform::AudioFocus::Granted;
    }
    std::deque<RuntimeInputEvent> input;

private:
    std::shared_ptr<Rowl::Platform::PlatformHost> m_inner;
};

std::string capiText(RowlEngineHandle h,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    req(query(h, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    req(query(h, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

int g_rigCounter = 0;
fs::path g_root;

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    std::shared_ptr<InputHost> host;
    fs::path game;
    Rowl::Audio::AudioEngine* audio = nullptr;

    // `manifestExtra` is merged into project.rowlproj; `files` are Assets/<path>.
    explicit Rig(const std::vector<Node>& nodes, const json& manifestExtra = json::object(),
                 const std::map<std::string, std::string>& files = {}) {
        ++g_rigCounter;
        game = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "7d3f2c86-8e4f-4f90-9c32-2e4d9f7a0b%02x", g_rigCounter);
        json manifest = {{"project_uuid", uuid}, {"name", "F1 Test"}};
        for (auto& item : manifestExtra.items()) manifest[item.key()] = item.value();
        writeBytes(game / "project.rowlproj", manifest.dump());
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph(nodes));
        const std::string wav = makeWav(20);
        for (const char* name : {"a.wav", "b.wav", "v.wav", "s.wav"}) {
            writeBytes(game / "Assets" / "audio" / name, wav);
        }
        for (const auto& [path, bytes] : files) writeBytes(game / "Assets" / path, bytes);
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        audio = engine->getAudio();
        req(audio != nullptr, "engine has no audio engine");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
    }
    ~Rig() { if (h) RowlEngine_Destroy(h); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void play() {
        RowlEngine_SetPlayState(h, 1);
        RowlEngine_Step(h, 0.016f);
    }
    void step(float dt, int times = 1) {
        for (int i = 0; i < times; ++i) RowlEngine_Step(h, dt);
    }
    void press(InType type) {
        RuntimeInputEvent event{};
        event.type = type;
        host->input.push_back(event);
        step(0.001f);
    }
    void key(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        step(0.001f);
    }
    void advance() {
        RowlEngine_AdvanceNode(h, 0);
        step(0.016f);
    }
    void choose(const std::string& optionId) {
        req(RowlEngine_SelectChoice(h, optionId.c_str()) == 1, "choice " + optionId + " refused");
        step(0.016f);
    }
    void markRead(int id) {
        req(RowlEngine_ProfileMarkRead(h, cid(id).c_str()) == ROWL_RESULT_OK, "markRead");
    }
    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    uint64_t stepId() const { return RowlEngine_GetCurrentStepId(h); }
    std::string var(const char* key) const {
        const char* value = RowlEngine_GetVariable(h, key);
        return value ? value : "";
    }
    json history() const {
        json parsed = json::parse(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
        req(parsed.is_array(), "history JSON did not parse");
        return parsed;
    }
    json driver() const {
        json parsed = json::parse(capiText(h, RowlEngine_GetPlayerDriverStateJsonUtf8), nullptr, false);
        req(parsed.is_object(), "driver state JSON did not parse");
        return parsed;
    }
    json menu() const {
        json parsed = json::parse(RowlEngine_GetPauseMenuJson(h), nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    void expectNode(uint64_t id, const std::string& what) const {
        req(node() == id, what + " (node " + std::to_string(node()) + ", expected " + std::to_string(id) + ")");
    }
};

std::vector<Node> chain(int count) {
    std::vector<Node> nodes;
    for (int id = 1; id <= count; ++id) {
        Node n;
        n.id = id;
        n.text = "A" + std::to_string(id);
        if (id < count) n.next = {id + 1};
        nodes.push_back(n);
    }
    return nodes;
}

void setPolicyAll(Rig& rig) {
    req(RowlEngine_SetSkipPolicy(rig.h, 2) == ROWL_RESULT_OK && rig.driver()["skip_policy"] == "all",
        "could not select skip policy all");
}

}  // namespace

void test_f1_audit_fixes() {
    TEST_SECTION("F1 audit fixes (rewind by node, backlog memory, entry sounds, backlog)");

    g_root = fs::temp_directory_path() / "rowl_f1_audit_fixes";
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

    // ── 1. A-1: one Backspace = one NODE back ──────────────────────────────
    {
        std::vector<Node> nodes(6);
        for (int i = 0; i < 6; ++i) {
            nodes[i].id = i + 1;
            nodes[i].text = "L" + std::to_string(i + 1);
        }
        nodes[0].next = {2};
        // n2: variable + bgm (extra links beyond the entry link)
        nodes[1].extra = {varSet("k", "1"), audioComp({{"bgm_track", "a.wav"}})};
        nodes[1].next = {3};
        // n3: variable add + script on_enter + bgm + sfx
        nodes[2].extra = {varSet("score", "5", "add"),
                          scriptComp("function on_enter() rowl.var_set('s','1') end"),
                          audioComp({{"bgm_track", "b.wav"}, {"sfx_track", "s.wav"}})};
        nodes[2].next = {4};
        nodes[3].next = {5};
        nodes[4].choices = {{"again", 5}, {"go", 6}};
        Rig rig(nodes);
        rig.play();
        rig.expectNode(1, "start");

        std::vector<uint64_t> lastStep(7, 0);
        lastStep[1] = rig.stepId();
        for (int id = 2; id <= 5; ++id) {
            rig.advance();
            rig.expectNode(id, "advance");
            lastStep[id] = rig.stepId();
        }
        req(lastStep[3] > lastStep[2] + 1, "node 3 entry must write several links (it wrote " +
                                               std::to_string(lastStep[3] - lastStep[2]) + ")");
        // The start node is presented before play (no entry), so the history holds nodes 2..5.
        req(rig.history().size() == 4, "history before rewinding: " + rig.history().dump());

        // 1a. Choice answer row: pick "go" -> the answer is the first row of node 6.
        rig.choose("go");
        rig.expectNode(6, "go");
        {
            const json rows = rig.history();
            req(rows.size() == 6, "history after the answer: " + std::to_string(rows.size()));
            req(rows[4]["choice"] == true && rows[4]["dialogue"] == "go" && rows[4]["speaker"] == "" &&
                    rows[4]["voice"] == "",
                "answer row: " + rows[4].dump());
            req(rows[5]["choice"] == false && rows[5]["dialogue"] == "L6", "node 6 line: " + rows[5].dump());
        }
        // One Backspace: back to the choice node, the question UNANSWERED.
        rig.press(InType::Rewind);
        rig.expectNode(5, "rewind from 6");
        req(rig.stepId() == lastStep[5], "landed on the last link of node 5");
        {
            const json rows = rig.history();
            req(rows.size() == 4 && rows.back()["dialogue"] == "L5" && rows.back()["choice"] == false,
                "rewinding to the choice drops the answer: " + std::to_string(rows.size()));
        }
        TEST_PASS("Choice answer is a history row of the next node and rewinds with it");

        // 1b. Self-loop visits count separately (A -> A -> A).
        rig.choose("again");
        rig.expectNode(5, "again #1");
        const uint64_t loop1 = rig.stepId();
        rig.choose("again");
        rig.expectNode(5, "again #2");
        const uint64_t loop2 = rig.stepId();
        req(loop1 > lastStep[5] && loop2 > loop1, "self-loop visits advance the chain");
        rig.press(InType::Rewind);
        rig.expectNode(5, "rewind to the previous visit of the same node");
        req(rig.stepId() == loop1, "previous self-loop visit, step " + std::to_string(rig.stepId()));
        rig.press(InType::Rewind);
        rig.expectNode(5, "rewind to the first visit");
        req(rig.stepId() == lastStep[5], "first visit, step " + std::to_string(rig.stepId()));
        rig.press(InType::Rewind);
        rig.expectNode(4, "rewind out of the loop");
        TEST_PASS("A self-loop visit is its own node visit");

        // 1c. Node 4 -> back to 3: ONE press, every entry commit intact.
        rig.press(InType::Rewind);
        rig.expectNode(3, "rewind to the multi-link node");
        req(rig.stepId() == lastStep[3], "landed on node 3's LAST link, step " + std::to_string(rig.stepId()));
        req(rig.var("k") == "1" && rig.var("score") == "5.000000" && rig.var("s") == "1",
            "node 3 variables intact: k=" + rig.var("k") + " score=" + rig.var("score") + " s=" + rig.var("s"));
        req(rig.audio->getCurrentBgmPath() == "b.wav", "node 3 music: " + rig.audio->getCurrentBgmPath());
        req(rig.history().size() == 2 && rig.history().back()["dialogue"] == "L3",
            "node 3 history: " + rig.history().dump());
        TEST_PASS("One Backspace lands on the previous node with variables, music and history intact");

        rig.press(InType::Rewind);
        rig.expectNode(2, "rewind to node 2");
        req(rig.stepId() == lastStep[2], "node 2 last link");
        req(rig.var("k") == "1" && rig.var("score").empty() && rig.var("s").empty(),
            "node 2 state: score=" + rig.var("score") + " s=" + rig.var("s"));
        req(rig.audio->getCurrentBgmPath() == "a.wav", "node 2 music: " + rig.audio->getCurrentBgmPath());
        req(rig.history().size() == 1, "node 2 history size");
        rig.press(InType::Rewind);
        rig.expectNode(1, "rewind to node 1");
        req(rig.var("k").empty() && !rig.audio->isBgmPlaying(), "node 1 state: no variable, no music");
        req(rig.history().empty(), "node 1 history size");
        // The first node cannot be rewound out of.
        const uint64_t firstStep = rig.stepId();
        rig.press(InType::Rewind);
        rig.expectNode(1, "rewind at the first node");
        req(rig.stepId() == firstStep && rig.history().empty(), "first node: nothing changes");
        TEST_PASS("Rewind walks node by node down to the first node and stops there");

        // Walking forward again re-applies the entry effects exactly once.
        rig.advance();
        rig.expectNode(2, "forward again");
        req(rig.var("k") == "1" && rig.audio->getCurrentBgmPath() == "a.wav" && rig.history().size() == 1,
            "forward again: state and history rebuilt");

        // 1d. The raw C API keeps its state-link meaning; the node API is additive.
        rig.advance();
        rig.expectNode(3, "to node 3 again");
        const uint64_t top = rig.stepId();
        req(RowlEngine_Rewind(rig.h, 1) == 1, "raw rewind");
        rig.expectNode(3, "raw Rewind(1) moves ONE state link");
        req(rig.stepId() == top - 1, "raw rewind step");
        req(RowlEngine_RewindNodes(rig.h, 1) == 1, "node rewind via the C API");
        rig.expectNode(2, "RewindNodes(1) from inside a node");
        req(RowlEngine_RewindNodes(nullptr, 1) == 0, "null handle");
        req(RowlEngine_RewindNodes(rig.h, 0) == 0, "zero nodes");
        TEST_PASS("RowlEngine_Rewind stays a raw link step; RowlEngine_RewindNodes steps whole nodes");
    }

    // ── 1e. Rewind ends Auto ───────────────────────────────────────────────
    {
        Rig rig(chain(4));
        rig.play();
        rig.advance();
        rig.advance();
        rig.expectNode(3, "to 3");
        req(RowlEngine_SetAutoMode(rig.h, 1) == ROWL_RESULT_OK && rig.driver()["auto"] == true, "auto on");
        rig.press(InType::Rewind);
        rig.expectNode(2, "rewind");
        req(rig.driver()["auto"] == false, "rewind must end Auto");
        // The swipe-back gesture takes the same path.
        RowlEngine_SetAutoMode(rig.h, 1);
        rig.press(InType::SwipeBack);
        rig.expectNode(1, "swipe back");
        req(rig.driver()["auto"] == false, "swipe back must end Auto");
    }
    TEST_PASS("Going back ends Auto (and its badge) for the session");

    // ── 2. A-2: a voiced node entered again after other nodes plays again ──
    {
        std::vector<Node> nodes(5);
        for (int i = 0; i < 5; ++i) {
            nodes[i].id = i + 1;
            nodes[i].text = "V" + std::to_string(i + 1);
        }
        nodes[0].next = {2};
        nodes[1].extra = {audioComp({{"voice_track", "v.wav"}, {"sfx_track", "s.wav"}})};
        nodes[1].next = {3};
        nodes[2].next = {4};
        nodes[3].choices = {{"back", 2}, {"end", 5}};
        Rig rig(nodes);
        rig.play();
        rig.advance();
        rig.expectNode(2, "voiced node");
        req(rig.audio->getVoiceClipCount() == 1, "first visit plays the clip");
        const uint64_t sfxFirst = rig.audio->sfxStartedCount();
        rig.advance();
        rig.advance();
        rig.expectNode(4, "choice node");
        rig.choose("back");
        rig.expectNode(2, "back to the voiced node");
        req(rig.audio->getVoiceClipCount() == 2 && rig.audio->getCurrentVoicePath() == "v.wav",
            "second visit must play the voice clip again, clips=" +
                std::to_string(rig.audio->getVoiceClipCount()));
        req(rig.audio->sfxStartedCount() == sfxFirst + 1, "second visit must play the sfx again");
        rig.step(0.05f, 5);
        req(rig.audio->getVoiceClipCount() == 2, "stepping the node does not retrigger it");
    }
    TEST_PASS("Voice and sfx play again on a later visit of the same node");

    // ── 3. A-3: persistent backlog (GameState level) ───────────────────────
    {
        using Rowl::State::DialogueHistory;
        using Rowl::State::DialogueHistoryEntry;
        using Rowl::State::GameState;
        auto entryFor = [](size_t i) {
            DialogueHistoryEntry entry;
            entry.nodeId = 1 + i % 7;
            entry.speaker = "S";
            entry.dialogue = "line " + std::to_string(i);
            return entry;
        };
        // Append shares: the old list is untouched, the new one adds a node.
        DialogueHistory a;
        DialogueHistory b = a.appended(entryFor(0));
        DialogueHistory c = b.appended(entryFor(1));
        DialogueHistory d = b.appended(entryFor(2));
        req(a.empty() && b.size() == 1 && c.size() == 2 && d.size() == 2, "persistent append sizes");
        req(c.at(1).dialogue == "line 1" && d.at(1).dialogue == "line 2" && c.at(0).dialogue == "line 0" &&
                d.at(0).dialogue == "line 0",
            "two branches share the first node and differ after it");
        req(c.identity() != d.identity(), "branches have different tails");
        // Bound: only the newest 500 are visible, chronologically.
        DialogueHistory big;
        for (size_t i = 0; i < 1300; ++i) big = big.appended(entryFor(i));
        req(big.size() == 500 && big.at(0).dialogue == "line 800" && big.back().dialogue == "line 1299",
            "bounded view keeps the newest 500");
        const auto flat = big.toVector();
        req(flat.size() == 500 && flat.front().dialogue == "line 800" && flat.back().dialogue == "line 1299",
            "toVector oldest->newest");
        bool threw = false;
        try { (void)big.at(500); } catch (const std::out_of_range&) { threw = true; }
        req(threw, "at() out of range throws");
        // A very deep list is released without recursion (300k nodes would
        // overflow a recursive teardown).
        {
            DialogueHistory deep;
            DialogueHistoryEntry tiny;
            tiny.nodeId = 1;
            for (size_t i = 0; i < 300000; ++i) deep = deep.appended(tiny);
            req(deep.size() == 500 && deep.totalNodes() == 300000, "deep list sizes");
        }
        TEST_PASS("Backlog is a persistent list: branches share, view is bounded, deep teardown is iterative");

        // Rewind-chain sharing: distinct nodes across the chain == appended lines.
        auto state = GameState::createInitialState(1);
        size_t appended = 0;
        for (size_t i = 0; i < 1500; ++i) {
            state = GameState::createNextState(state, 1 + i % 5, "", "", true);
            state = GameState::withDialogueHistory(state, {entryFor(i)});
            ++appended;
        }
        std::unordered_set<const void*> distinct;
        std::vector<const void*> scratch;
        for (auto link = state; link; link = link->previousState) {
            scratch.clear();
            link->dialogueHistory.collectNodeAddresses(scratch);
            distinct.insert(scratch.begin(), scratch.end());
        }
        req(distinct.size() <= appended,
            "rewind links must share history nodes: " + std::to_string(distinct.size()) + " distinct for " +
                std::to_string(appended) + " lines");
        // Rewind depth to the root is unchanged.
        auto root = GameState::rewind(state, 100000);
        req(root && root->stepId == 1 && !root->previousState, "rewind still reaches the root");
        TEST_PASS("1500 rewind links hold " + std::to_string(distinct.size()) + " distinct history nodes");

        // Save/load round trip keeps the bound, the choice row and the node_entry flag;
        // pre-F1 saves (no such keys) still load.
        auto withChoice = GameState::createNextState(state, 9, "", "", true);
        DialogueHistoryEntry answer = entryFor(7);
        answer.choice = true;
        answer.speaker.clear();
        withChoice = GameState::withDialogueHistory(withChoice, {answer, entryFor(8)});
        const auto restored = GameState::deserializeJson(withChoice->serializeJson());
        req(restored && restored->nodeEntry && restored->dialogueHistory.size() == 500 &&
                restored->dialogueHistory.at(498).choice && !restored->dialogueHistory.back().choice,
            "save round trip keeps node_entry and the choice row");
        const auto legacy = GameState::deserializeJson(
            R"({"version":4,"step_id":3,"active_node_id":5,"variables":{},"dialogue_history":[
                {"node_id":5,"speaker":"A","dialogue":"Old","read":true}]})");
        req(legacy && !legacy->nodeEntry && legacy->dialogueHistory.size() == 1 &&
                !legacy->dialogueHistory.at(0).choice,
            "a pre-F1 save loads without the new keys");
        const std::string plainJson = state->serializeJson();
        req(plainJson.find("\"choice\"") == std::string::npos,
            "ordinary lines keep the pre-F1 byte shape (no choice key)");
        TEST_PASS("Save format: node_entry / choice keys are additive and round-trip");

        // GameState::rewindNodes unit semantics.
        auto s1 = GameState::createInitialState(1);
        auto s2 = GameState::createNextState(s1, 2, "", "", true);
        auto s2b = GameState::createNextState(s2, 2, "k", "v");       // commit in node 2
        auto s2c = GameState::createNextState(s2b, 2, "k2", "v2");    // commit in node 2
        auto s3 = GameState::createNextState(s2c, 3, "", "", true);
        req(GameState::rewindNodes(s3, 1) == s2c, "node rewind lands on the previous node's last link");
        req(GameState::rewindNodes(s2c, 1) == s1, "from the last link of node 2 -> node 1");
        req(GameState::rewindNodes(s2, 1) == s1, "from the entry link of node 2 -> node 1");
        req(GameState::rewindNodes(s1, 1) == nullptr, "first node: nothing earlier");
        req(GameState::rewindNodes(s3, 2) == s1, "two nodes back");
        req(GameState::rewindNodes(s3, 9) == s1, "more nodes than exist clamps to the oldest visit");
        req(GameState::rewindNodes(s3, 0) == nullptr, "zero nodes");
    }

    // ── 4. A-4: the backlog follows the active language ────────────────────
    {
        std::vector<Node> nodes = chain(4);
        std::string en = R"({"schema_version":1,"locale":"en","entries":{)";
        const char* words[] = {"zero", "one", "two", "three", "four"};
        for (int id = 1; id <= 4; ++id) {
            en += "\"" + cid(id) + "\":{\"speaker\":\"Narrator\",\"text\":\"English " +
                  std::string(words[id]) + "\",\"alt_text\":\"\"}";
            if (id < 4) en += ",";
        }
        en += "}}";
        Rig rig(nodes, {{"default_locale", "tr"}, {"supported_locales", json::array({"tr", "en"})}},
                {{"locales/en.json", en},
                 {"locales/tr.json", R"({"schema_version":1,"locale":"tr","entries":{}})"}});
        rig.play();
        rig.advance();
        rig.advance();
        rig.expectNode(3, "two history lines in");
        req(rig.history().size() == 2 && rig.history()[0]["dialogue"] == "A2",
            "source language first: " + rig.history().dump());
        req(RowlEngine_SetLocale(rig.h, "en") == ROWL_RESULT_OK, "SetLocale(en)");
        rig.step(0.016f);
        {
            const json rows = rig.history();
            req(rows[0]["dialogue"] == "English two" && rows[1]["dialogue"] == "English three" &&
                    rows[0]["speaker"] == "Narrator",
                "history re-translated: " + rows.dump());
        }
        rig.advance();
        rig.expectNode(4, "fourth line in English");
        {
            const json rows = rig.history();
            req(rows.size() == 3 && rows[2]["dialogue"] == "English four" && rows[0]["dialogue"] == "English two",
                "no mixed languages: " + rows.dump());
        }
        req(RowlEngine_SetLocale(rig.h, "tr") == ROWL_RESULT_OK, "SetLocale(tr)");
        rig.step(0.016f);
        {
            const json rows = rig.history();
            req(rows[0]["dialogue"] == "A2" && rows[2]["dialogue"] == "A4" && rows[0]["speaker"] == "S",
                "switching back restores the source text: " + rows.dump());
        }
    }
    TEST_PASS("Backlog rows with a content id follow the active language both ways");

    // ── 5. A-5: the selected backlog row shows its whole text ──────────────
    {
        std::vector<Node> nodes = chain(3);
        std::string longText;
        for (int i = 0; i < 60; ++i) {
            char line[32];
            std::snprintf(line, sizeof(line), "Para %03d", i);
            longText += line;
            if (i < 59) longText += "\n";
        }
        nodes[2].text = longText;
        nodes[1].text = longText;  // a long UNSELECTED row stays truncated
        Rig rig(nodes);
        rig.play();
        rig.advance();
        rig.advance();
        req(RowlEngine_OpenBacklog(rig.h) == ROWL_RESULT_OK, "open backlog");
        auto selectedLines = [&]() {
            const json menu = rig.menu();
            for (const auto& entry : menu["backlog"]["entries"]) {
                if (entry["selected"] == true) return entry["lines"];
            }
            return json::array();
        };
        auto joined = [](const json& lines) {
            std::string out;
            for (const auto& line : lines) out += line.get<std::string>() + "|";
            return out;
        };
        json lines = selectedLines();
        req(lines.size() > 8, "selected row is not cut at 8 lines: " + std::to_string(lines.size()));
        req(joined(lines).find("Para 015") != std::string::npos,
            "selected row shows more than the first 8 lines: " + joined(lines));
        req(joined(lines).find("Para 059") == std::string::npos, "the tail needs scrolling first");
        // Scrolling inside the row reaches the tail without moving the selection.
        const int selectedBefore = rig.menu()["backlog"]["selected"].get<int>();
        for (int i = 0; i < 80; ++i) RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
        lines = selectedLines();
        req(joined(lines).find("Para 059") != std::string::npos,
            "scrolling reveals the end of the line: " + joined(lines));
        req(lines.front().get<std::string>().rfind("... ", 0) == 0, "an earlier-text marker leads the window");
        req(rig.menu()["backlog"]["selected"].get<int>() == selectedBefore,
            "selection stays on the long row while it scrolls");
        for (int i = 0; i < 80; ++i) RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_UP);
        req(joined(selectedLines()).find("Para 000") != std::string::npos, "scrolled back to the start");
        // Up once more leaves the row; the older long row is selected and opens in full,
        // the row it left is cut at 8 lines again.
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_UP);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_UP);
        const json menu = rig.menu();
        req(menu["backlog"]["selected"].get<int>() < selectedBefore, "selection moved to an older row");
        for (const auto& entry : menu["backlog"]["entries"]) {
            if (entry["index"].get<int>() == selectedBefore) {
                req(entry["lines"].size() == 8 &&
                        entry["lines"].back().get<std::string>().find(" ...") != std::string::npos,
                    "an unselected long row stays at 8 lines + ' ...': " + joined(entry["lines"]));
            }
        }
    }
    TEST_PASS("Selected long backlog row opens in full and scrolls inside itself");

    // ── 6. A-6 / A-7 / A-9: Skip sounds, badge, focus ──────────────────────
    {
        std::vector<Node> nodes = chain(5);
        for (int id : {2, 3, 4, 5}) {
            nodes[id - 1].extra = {audioComp({{"voice_track", "v.wav"}, {"sfx_track", "s.wav"}})};
        }
        nodes[2].extra = {audioComp({{"voice_track", "v.wav"}, {"sfx_track", "s.wav"}, {"bgm_track", "b.wav"}})};
        Rig rig(nodes);
        for (int id : {1, 2, 3, 4}) rig.markRead(id);  // node 5 stays unread
        rig.play();
        const uint32_t clipsBefore = rig.audio->getVoiceClipCount();
        const uint64_t sfxBefore = rig.audio->sfxStartedCount();
        req(RowlEngine_SetSkipActive(rig.h, 1) == ROWL_RESULT_OK, "skip on");
        for (int i = 0; i < 40 && rig.node() != 5; ++i) rig.step(0.06f);
        rig.expectNode(5, "skip stops at the first unread line");
        rig.step(0.06f, 3);
        // Nodes 2..4 were skipped silently; node 5 (where Skip stopped) plays once.
        req(rig.audio->getVoiceClipCount() == clipsBefore + 1,
            "skipped nodes play no voice, the stop node plays its own once: clips " +
                std::to_string(rig.audio->getVoiceClipCount() - clipsBefore));
        req(rig.audio->sfxStartedCount() == sfxBefore + 1,
            "skipped nodes play no sfx, the stop node plays its own once: sfx " +
                std::to_string(rig.audio->sfxStartedCount() - sfxBefore));
        req(rig.audio->getCurrentBgmPath() == "b.wav", "BGM keeps following the story while skipping");
        req(rig.driver()["skip_active"] == false, "skip ended at the unread line");
        rig.step(0.06f, 5);
        req(rig.audio->getVoiceClipCount() == clipsBefore + 1, "the deferred clip plays only once");
    }
    TEST_PASS("Skip mutes voice and sfx (BGM follows); the node Skip stops on plays its sound once");

    {
        // A-7: the badge / "skipping" is true only while Skip really skips.
        Rig rig(chain(6));
        rig.play();
        setPolicyAll(rig);
        rig.key(SDLK_LCTRL);
        req(rig.driver()["skipping"] == true, "Ctrl held mid-chain really skips");
        for (int i = 0; i < 40 && rig.node() != 6; ++i) rig.step(0.06f);
        rig.expectNode(6, "chain end reached");
        req(rig.driver()["skipping"] == false, "Ctrl still held at the chain end: nothing to skip, no badge");
    }
    {
        Rig rig(chain(4));
        rig.play();  // read_only policy, nothing read
        rig.key(SDLK_LCTRL);
        rig.step(0.06f, 4);
        rig.expectNode(1, "unread line: Ctrl cannot skip");
        req(rig.driver()["skipping"] == false, "Ctrl held on an unread line shows no SKIP badge");
    }
    TEST_PASS("SKIP badge state follows real skipping, not the held key");

    {
        // A-9: losing window focus ends the Ctrl hold.
        Rig rig(chain(12));
        rig.play();
        setPolicyAll(rig);
        rig.key(SDLK_LCTRL);
        rig.step(0.06f, 2);
        req(rig.node() > 1, "Ctrl hold skips");
        rig.press(InType::FocusLost);
        const uint64_t atLoss = rig.node();
        rig.step(0.06f, 20);
        req(rig.node() == atLoss, "no more skipping after focus loss (node " + std::to_string(rig.node()) +
                                      " vs " + std::to_string(atLoss) + ")");
    }
    TEST_PASS("Focus loss ends the Ctrl Skip-hold");

    // ── 7. Karar 6: a clip replayed from the backlog stops with the page ───
    {
        std::vector<Node> nodes = chain(3);
        nodes[1].extra = {audioComp({{"voice_track", "v.wav"}})};
        Rig rig(nodes);
        rig.play();
        rig.advance();
        rig.expectNode(2, "voiced node");
        const uint32_t clips = rig.audio->getVoiceClipCount();
        req(RowlEngine_OpenBacklog(rig.h) == ROWL_RESULT_OK, "open backlog");
        req(RowlEngine_ReplayBacklogVoice(rig.h, 0) == ROWL_RESULT_OK, "replay");
        req(rig.audio->getVoiceClipCount() == clips + 1 && rig.audio->isVoicePlaying(), "replay clip plays");
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_BACK);
        req(RowlEngine_IsPaused(rig.h) == 0, "backlog closed");
        req(!rig.audio->isVoicePlaying() && rig.audio->getCurrentVoicePath().empty(),
            "the replayed clip stops when the page closes");
        req(rig.audio->getVoiceClipCount() == clips + 1, "the node's own voice is not restarted");
    }
    TEST_PASS("A backlog replay clip stops when the backlog closes");

    fs::remove_all(g_root, ec);
#ifndef _WIN32
    if (savedXdg.empty()) ::unsetenv("XDG_DATA_HOME");
    else ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
#endif
}
