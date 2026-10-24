/**
 * test_condition_component.cpp — D06-1: the node-level "condition" component
 * ({"type":"condition","data":{"expression":"gold >= 10","fail_target_node_id":"4"}})
 * written by the editor is honoured by the native engine.
 *
 * Kilit:
 *  1. Dogru kosul -> dugum oynar (diyalog + giris yan etkisi); bos ifade acik.
 *  2. Yanlis kosul + gecerli hedef (dize ve sayi bicimi) -> hedef sunulur; atlanan
 *     dugumun degisken / sfx / voice / gecis / gecmis satiri / okundu isareti /
 *     adim (stepId) etkisi YOK; yonlendirme tam bir adimdir.
 *  3. Kosul onceki dugumdeki degiskene baglidir (gold 5 -> yanlis, 10 -> dogru).
 *  4. Zincir A->B->C calisir; dongu 16 adimda durur, cokme yok, dugum oynar,
 *     tek uyari; disabled kosul ve secimle ulasilan dugum de yonlenir
 *     (SelectChoice basarili doner).
 *  5. Bos / var olmayan / cozumlenemeyen hedef -> dugum normal oynar, dugum basina
 *     tek uyari (ayni dugume tekrar girince uyari tekrarlanmaz).
 *  6. Lua hatasi (sozdizimi / calisma) = seceneklerdeki kosulla AYNI politika:
 *     "yanlis" sayilir (secenek kapali kalir / dugum hedefe yonlenir) + uyari.
 *  7. Kaydet -> yukle ve rewind kosulu tekrar DEGERLENDIRMEZ: yuklenen dugum
 *     ayni kalir, atlanan dugumun yan etkisi uygulanmaz, kayit hedef dugumu
 *     tasir, rewind atlanan dugum icin halka uretmez.
 *  8. Skip(All) ve Auto yonlendirilen dugumden gecer, atlanan dugume hic girmez.
 *  9. Grafik baglama (start dugumu) kosulu da degerlendirilir.
 * 10. Yeni Oyun (ResetToStartNode ve baslik menusu) ve load: Lua kosul degerlendiricisi
 *     onceki oturumun degiskenini degil taze / kayitli degiskenleri gorur.
 *
 * Bilerek-boz: (a) advanceStory'deki resolveNodeEntryConditions cagrisini
 * kaldirmak 2/3/4'u, (b) cagriyi sahne guncellemesinden SONRAYA almak 2'deki
 * "atlanan dugumun etkisi yok"u, (c) cagriyi createNextState'ten SONRAYA almak
 * 2'deki adim sayimini ve 7'deki rewind/kayit hedefini, (d) uyari tekrar
 * korumasini (m_conditionWarnedNodes) kaldirmak 5'i, resetToStartNode'daki
 * Lua degisken temizligini kaldirmak 10'u kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr const char* kSection = "condition-component";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// 16-bit mono PCM silence: a sfx / voice clip the audio counters can see.
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

// ── Graph builder ──────────────────────────────────────────────────────────
struct Node {
    int id = 0;
    std::string text;
    std::vector<json> extra;   // components besides the dialogue
    std::vector<int> next;
};

json variableSet(const std::string& key, const std::string& value) {
    return {{"type", "variable"}, {"id", "v_" + key}, {"enabled", true},
            {"data", {{"key", key}, {"value", value}, {"operation", "set"}}}};
}

json condition(const std::string& expression, const json& failTarget) {
    return {{"type", "condition"}, {"id", "cond"}, {"enabled", true},
            {"data", {{"expression", expression}, {"fail_target_node_id", failTarget}}}};
}

json choiceOf(const std::string& optionId, const std::string& optionCondition = "") {
    json option = json::object();
    option["option_id"] = optionId;
    option["text"] = optionId;
    if (!optionCondition.empty()) option["condition"] = optionCondition;
    option["x"] = 680;
    option["y"] = 520;
    option["width"] = 560;
    option["height"] = 64;
    json data = json::object();
    data["options"] = json::array({option});
    json component = json::object();
    component["type"] = "choice";
    component["id"] = "choice_" + optionId;
    component["enabled"] = true;
    component["data"] = data;
    return component;
}

// The skipped node carries every kind of entry effect the brief lists.
std::vector<json> loudEffects(const std::string& marker) {
    return {variableSet(marker, "yes"),
            {{"type", "audio"}, {"id", "snd_" + marker}, {"enabled", true},
             {"data", {{"sfx_track", "s.wav"}, {"voice_track", "v.wav"}}}},
            {{"type", "transition"}, {"id", "tr_" + marker}, {"enabled", true},
             {"data", {{"kind", "fade"}, {"duration", 1.0}}}}};
}

std::string buildGraph(const std::vector<Node>& nodes, int startId = 0) {
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
    return json({{"format_version", 4}, {"start_node_id", startId != 0 ? startId : nodes.front().id},
                 {"nodes", out}}).dump();
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

int g_rigCounter = 0;
fs::path g_root;

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    Rowl::Audio::AudioEngine* audio = nullptr;
    fs::path game;

    explicit Rig(const std::string& graph, bool play = true) {
        ++g_rigCounter;
        game = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "7d3e1c86-8e4f-4f90-9c32-2e4d9f7a0b%02x", g_rigCounter);
        writeBytes(game / "project.rowlproj",
                   std::string(R"({"project_uuid":")") + uuid + R"(","name":"Condition Test"})");
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", graph);
        writeBytes(game / "Assets" / "audio" / "s.wav", makeWav(2));
        writeBytes(game / "Assets" / "audio" / "v.wav", makeWav(2));
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        audio = engine->getAudio();
        req(audio != nullptr, "engine has no audio engine");
        if (play) {
            RowlEngine_SetPlayState(h, 1);
            RowlEngine_Step(h, 0.016f);
        }
    }
    ~Rig() { if (h) RowlEngine_Destroy(h); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    uint64_t step() const { return RowlEngine_GetCurrentStepId(h); }
    void advance() { RowlEngine_AdvanceNode(h, 0); }
    std::string dialogue() const { return capiText(h, RowlEngine_GetDialogueUtf8); }
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
    json history() const {
        const json parsed = json::parse(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
        req(parsed.is_array(), "history JSON did not parse");
        return parsed;
    }
    bool historyHasNode(uint64_t id) const {
        for (const json& row : history()) {
            if (row.value("node_id", uint64_t{0}) == id) return true;
        }
        return false;
    }
    bool isRead(int id) const {
        const json profile = json::parse(capiText(h, RowlEngine_ProfileGetJson), nullptr, false);
        req(profile.is_object() && profile["read_content_ids"].is_array(), "profile JSON did not parse");
        for (const json& entry : profile["read_content_ids"]) {
            if (entry == cid(id)) return true;
        }
        return false;
    }
    void expectNode(uint64_t id, const std::string& what) const {
        req(node() == id, what + " (node " + std::to_string(node()) + ", expected " + std::to_string(id) + ")");
    }
};

}  // namespace

void test_condition_component() {
    TEST_SECTION("Node condition component (D06-1)");

    g_root = fs::temp_directory_path() / "rowl_condition_component";
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

    // The shared shape: 1 sets gold, 2 is the gated node (condition + loud
    // effects), 3 is what 2 leads on to, 4 is the fail target, 5 follows 4.
    const auto gatedGraph = [](const std::string& goldValue, const json& failTarget,
                               const std::string& expression = "tonumber(gold) >= 10") {
        Node n1{1, "Start", {variableSet("gold", goldValue)}, {2}};
        Node n2{2, "Gated", {}, {3}};
        n2.extra.push_back(condition(expression, failTarget));
        for (const json& effect : loudEffects("m2")) n2.extra.push_back(effect);
        Node n3{3, "After gated", {}, {}};
        Node n4{4, "Fail target", {}, {5}};
        Node n5{5, "After fail", {}, {}};
        return buildGraph({n1, n2, n3, n4, n5});
    };

    // ── 1. true condition plays the node ───────────────────────────────────
    {
        Rig rig(gatedGraph("10", "4"));
        rig.expectNode(1, "start node");
        rig.advance();
        rig.expectNode(2, "true condition: the gated node plays");
        req(rig.dialogue() == "Gated", "true condition: dialogue is the gated node's, got " + rig.dialogue());
        req(rig.var("m2") == "yes", "true condition: entry variable effect applied");
        req(rig.audio->getVoiceClipCount() == 1, "true condition: voice plays");
        req(rig.historyHasNode(2), "true condition: history row recorded");
        TEST_PASS("True condition: node plays with its entry effects");
    }
    {
        // An empty expression and a condition with no expression field gate nothing.
        Node n1{1, "Start", {}, {2}};
        Node n2{2, "Open", {condition("  ", "4")}, {}};
        n2.extra.push_back({{"type", "condition"}, {"id", "noexpr"}, {"enabled", true},
                            {"data", {{"fail_target_node_id", "4"}}}});
        n2.extra.push_back({{"type", "condition"}, {"id", "off"}, {"enabled", false},
                            {"data", {{"expression", "false"}, {"fail_target_node_id", "4"}}}});
        Node n4{4, "Target", {}, {}};
        Rig rig(buildGraph({n1, n2, n4}));
        rig.advance();
        rig.expectNode(2, "empty / missing / disabled conditions gate nothing");
        TEST_PASS("Empty, expression-less and disabled conditions are open");
    }

    // ── 2. false + target: redirect, no skipped-node effects ───────────────
    for (const json& target : {json("4"), json(4)}) {
        Rig rig(gatedGraph("5", target));
        const uint64_t stepBefore = rig.step();
        const uint64_t sfxBefore = rig.audio->sfxStartedCount();
        rig.advance();
        rig.expectNode(4, "false condition: the fail target is presented");
        req(rig.dialogue() == "Fail target",
            "false condition: dialogue is the target's, got " + rig.dialogue());
        req(rig.var("m2").empty(), "skipped node's variable effect must not run");
        req(rig.audio->sfxStartedCount() == sfxBefore, "skipped node's sfx must not play");
        req(rig.audio->getVoiceClipCount() == 0, "skipped node's voice must not play");
        req(!RowlEngine_IsTransitionActive(rig.h), "skipped node's transition must not start");
        req(!rig.historyHasNode(2) && rig.historyHasNode(4),
            "history holds the target's line, not the skipped node's");
        req(!rig.isRead(2) && rig.isRead(4), "read mark belongs to the target only");
        req(rig.step() == stepBefore + 1, "a redirect is exactly one step (before " +
                                              std::to_string(stepBefore) + ", after " +
                                              std::to_string(rig.step()) + ")");
        // The story goes on from the target.
        rig.advance();
        rig.expectNode(5, "the story continues from the redirect target");
    }
    TEST_PASS("False condition + fail target (string and number forms): target plays, skipped node leaves no trace");

    // ── 3. variable-bound condition ────────────────────────────────────────
    {
        Rig low(gatedGraph("5", "4"));
        low.advance();
        low.expectNode(4, "gold 5 -> condition false");
        Rig high(gatedGraph("10", "4"));
        high.advance();
        high.expectNode(2, "gold 10 -> condition true");
        TEST_PASS("Condition follows the variable set on the previous node");
    }

    // ── 4. chain, cycle, choice entry ──────────────────────────────────────
    {
        // 1 -> 2 (false -> 3), 3 (false -> 4), 4 plays.
        Node n1{1, "Start", {}, {2}};
        Node n2{2, "B", {condition("1 == 2", "3")}, {}};
        for (const json& effect : loudEffects("m2")) n2.extra.push_back(effect);
        Node n3{3, "C", {condition("1 == 2", 4), variableSet("m3", "yes")}, {}};
        Node n4{4, "D", {}, {}};
        Rig rig(buildGraph({n1, n2, n3, n4}));
        const uint64_t stepBefore = rig.step();
        rig.advance();
        rig.expectNode(4, "chain 2 -> 3 -> 4 lands on the first node whose conditions hold");
        req(rig.var("m2").empty() && rig.var("m3").empty(), "no chain link runs its entry effects");
        req(rig.step() == stepBefore + 1, "the whole chain is one step");
        TEST_PASS("Chain of failing conditions resolves to the final node");
    }
    {
        // 1 -> 2; 2 (false -> 3) and 3 (false -> 2): a cycle.
        Node n1{1, "Start", {}, {2}};
        Node n2{2, "Loop B", {condition("1 == 2", "3"), variableSet("m2", "yes")}, {3}};
        Node n3{3, "Loop C", {condition("1 == 2", "2"), variableSet("m3", "yes")}, {2}};
        Rig rig(buildGraph({n1, n2, n3}));
        clearWarnings();
        rig.advance();
        // 16 hops from node 2 end on node 2 (even count); it plays normally.
        rig.expectNode(2, "cycle: redirects stop at the 16-hop limit and the node plays");
        req(rig.dialogue() == "Loop B", "cycle: the node's own dialogue is presented");
        req(rig.var("m2") == "yes", "cycle: the node plays normally (entry effect applied once)");
        req(warningsContaining("redirects exceeded 16") == 1, "cycle: exactly one warning");
        rig.advance();
        rig.expectNode(2 + 1, "cycle: play continues (3 -> 2 loop settles on node 3 this time)");
        TEST_PASS("Redirect cycle stops at 16 hops, no crash, one warning");
    }
    {
        // A node reached by choosing an option is gated like any other entry.
        Node n1{1, "Pick", {choiceOf("go")}, {}};
        Node n2{2, "Gated", {condition("1 == 2", "3")}, {}};
        Node n3{3, "Fallback", {}, {}};
        json graph = json::parse(buildGraph({n1, n2, n3}));
        graph["nodes"][0]["next_nodes"] = json::array({{{"id", 2}, {"label", "Go"}, {"option_id", "go"}}});
        Rig rig(graph.dump());
        req(RowlEngine_SelectChoice(rig.h, "go") == 1, "choice into a redirected node still reports taken");
        rig.expectNode(3, "choice target with a false condition redirects");
        TEST_PASS("Choice entry is gated; SelectChoice reports success");
    }

    // ── 5. invalid targets: node plays, one warning per node ───────────────
    {
        // 1 -> 2 -> 3 -> 4 -> 5 -> 2 ... each of 2..5 carries a false condition
        // with a different kind of unusable target.
        Node n1{1, "Start", {}, {2}};
        Node n2{2, "Empty", {condition("1 == 2", ""), variableSet("e2", "yes")}, {3}};
        Node n3{3, "Missing", {condition("1 == 2", "99"), variableSet("e3", "yes")}, {4}};
        Node n4{4, "Text", {condition("1 == 2", "abc"), variableSet("e4", "yes")}, {5}};
        Node n5{5, "Negative", {condition("1 == 2", -3), variableSet("e5", "yes")}, {1}};
        Rig rig(buildGraph({n1, n2, n3, n4, n5}));
        clearWarnings();
        for (int expected = 2; expected <= 5; ++expected) {
            rig.advance();
            rig.expectNode(static_cast<uint64_t>(expected), "unusable fail target: the node plays");
            req(rig.var("e" + std::to_string(expected)) == "yes",
                "unusable fail target: entry effect of node " + std::to_string(expected) + " applied");
        }
        for (int id = 2; id <= 5; ++id) {
            req(warningsContaining("Node #" + std::to_string(id) + " condition") == 1,
                "one warning for node " + std::to_string(id) + ", got " +
                    std::to_string(warningsContaining("Node #" + std::to_string(id) + " condition")));
        }
        // Around the loop again: no new warnings.
        for (int i = 0; i < 5; ++i) rig.advance();
        for (int id = 2; id <= 5; ++id) {
            req(warningsContaining("Node #" + std::to_string(id) + " condition") == 1,
                "re-entering node " + std::to_string(id) + " must not repeat its warning");
        }
        TEST_PASS("Empty / missing / non-numeric / negative target: node plays, warned once per node");
    }

    // ── 6. Lua errors follow the choice-option policy ──────────────────────
    {
        for (const std::string badExpression : {"gold >>= 10", "nosuch.field > 1"}) {
            // Parity: a choice option gated by the same expression is closed.
            Node n1{1, "Pick", {choiceOf("gated", badExpression)}, {}};
            Node n2{2, "Gated", {}, {}};
            json choiceGraph = json::parse(buildGraph({n1, n2}));
            choiceGraph["nodes"][0]["next_nodes"] =
                json::array({{{"id", 2}, {"label", "Gated"}, {"option_id", "gated"}}});
            Rig choiceRig(choiceGraph.dump());
            req(RowlEngine_SelectChoice(choiceRig.h, "gated") == 0 && choiceRig.node() == 1,
                "choice policy: an erroring condition closes the option (" + badExpression + ")");

            // The node condition treats the same error as false -> fail target.
            Node m1{1, "Start", {}, {2}};
            Node m2{2, "Gated", {condition(badExpression, "4"), variableSet("m2", "yes")}, {}};
            Node m4{4, "Target", {}, {}};
            Rig rig(buildGraph({m1, m2, m4}));
            clearWarnings();
            rig.advance();
            rig.expectNode(4, "node policy: an erroring condition counts as false (" + badExpression + ")");
            req(rig.var("m2").empty(), "erroring condition: skipped node left no effect");
            req(warningsContaining("failed to evaluate") == 1,
                "erroring condition is reported once (" + badExpression + ")");
        }
        TEST_PASS("Lua syntax/runtime errors = false, same as a choice option");
    }

    // ── 7. save / load / rewind never re-evaluate ──────────────────────────
    {
        Rig rig(gatedGraph("5", "4"));
        const uint64_t step0 = rig.step();
        rig.advance();
        rig.expectNode(4, "redirected to the target");
        const uint64_t stepAtTarget = rig.step();
        req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save at the redirect target");
        rig.advance();
        rig.expectNode(5, "play on past the target");
        // Flip the variable so a re-evaluation would now open node 2.
        RowlEngine_SetVariable(rig.h, "gold", "50");
        const uint64_t sfxBefore = rig.audio->sfxStartedCount();
        req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load the redirect-target save");
        rig.expectNode(4, "load: the saved node (the target) is restored, not re-evaluated");
        req(rig.var("gold") == "5", "load: variables come from the save");
        req(rig.var("m2").empty(), "load: the skipped node's effect does not appear");
        req(rig.audio->sfxStartedCount() == sfxBefore, "load: no sfx replayed");
        req(rig.step() == stepAtTarget, "load: step id restored");
        req(!rig.historyHasNode(2), "load: skipped node never in the history");

        // Rewind: forward again, flip gold, then step back across the redirect.
        rig.advance();
        rig.expectNode(5, "forward again after load");
        RowlEngine_SetVariable(rig.h, "gold", "50");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind one step (undoes the gold change)");
        rig.expectNode(5, "rewind keeps node 5 (only the variable step is undone)");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind back to the redirect target");
        rig.expectNode(4, "rewind: lands on the target");
        req(rig.var("gold") == "5" && rig.var("m2").empty(), "rewind: target state, no skipped effects");
        req(rig.step() == stepAtTarget, "rewind: step id of the target");
        RowlEngine_SetVariable(rig.h, "gold", "50");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind the gold change");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind across the redirect");
        rig.expectNode(1, "rewind across a redirect lands on the node BEFORE the skipped one");
        req(rig.step() == step0, "one rewind step undoes the whole redirect: no link exists for the "
                                 "skipped node (step " + std::to_string(rig.step()) + ", expected " +
                                     std::to_string(step0) + ")");
        req(rig.var("m2").empty(), "rewind: the skipped node's effect never appeared");
        TEST_PASS("Load and rewind do not re-evaluate; the chain has no link for the skipped node");
    }

    // ── 8. Skip(All) and Auto pass through the redirect ────────────────────
    {
        // 1 -> 2 (false -> 4) -> 3 ; 4 -> 5.
        Rig rig(gatedGraph("5", "4"));
        req(RowlEngine_SetSkipPolicy(rig.h, 2) == ROWL_RESULT_OK, "skip policy all");
        req(RowlEngine_SetSkipActive(rig.h, 1) == ROWL_RESULT_OK, "skip on");
        std::set<uint64_t> visited{rig.node()};
        for (int i = 0; i < 60; ++i) {
            RowlEngine_Step(rig.h, 0.06f);
            visited.insert(rig.node());
        }
        req(visited.count(2) == 0 && visited.count(3) == 0, "Skip(All) never enters the gated node");
        req(visited.count(4) == 1 && visited.count(5) == 1, "Skip(All) runs through the fail target chain");
        req(!rig.isRead(2) && rig.var("m2").empty(), "Skip: skipped node has no read mark or effect");
    }
    {
        Rig rig(gatedGraph("5", "4"));
        req(RowlEngine_SetAutoMode(rig.h, 1) == ROWL_RESULT_OK, "auto on");
        std::set<uint64_t> visited{rig.node()};
        for (int i = 0; i < 200; ++i) {
            RowlEngine_Step(rig.h, 0.1f);
            visited.insert(rig.node());
        }
        req(visited.count(2) == 0 && visited.count(3) == 0, "Auto never enters the gated node");
        req(visited.count(4) == 1 && visited.count(5) == 1, "Auto runs through the fail target chain");
        TEST_PASS("Skip(All) and Auto pass through the redirect without entering the gated node");
    }

    // ── 9. graph mount evaluates the start node's condition ────────────────
    {
        Node n1{1, "Gated start", {condition("1 == 2", "3"), variableSet("m1", "yes")}, {2}};
        Node n2{2, "Two", {}, {}};
        Node n3{3, "Redirected start", {}, {}};
        Rig rig(buildGraph({n1, n2, n3}), false);
        rig.expectNode(3, "mount: a start node with a false condition hands over to its target");
        req(rig.var("m1").empty(), "mount: the skipped start node left no effect");
        TEST_PASS("Graph mount evaluates the start node's condition");
    }

    // ── 10. New Game / load start from the fresh variable state ────────────
    // The Lua condition evaluator reads the Lua globals, not GameState: a
    // previous session's value must not survive a new game (it would flip the
    // start node's condition and the choice condition), and load must hand the
    // evaluator exactly the save's variables.
    {
        const auto vipGraph = [&] {
            Node n1{1, "Start", {condition("vip ~= 'yes'", "3"), choiceOf("go", "vip ~= 'yes'")}, {}};
            Node n2{2, "Chosen", {}, {}};
            Node n3{3, "Vip lounge", {}, {}};
            json graph = json::parse(buildGraph({n1, n2, n3}));
            graph["nodes"][0]["next_nodes"] = json::array({{{"id", 2}, {"label", "Go"}, {"option_id", "go"}}});
            return graph.dump();
        };
        const auto freshGameChecks = [&](Rig& rig, const std::string& how) {
            rig.expectNode(1, how + ": the start node's condition sees the fresh game, not the old session");
            req(rig.var("vip").empty(), how + ": old session variable must be gone");
            req(RowlEngine_SelectChoice(rig.h, "go") == 1 && rig.node() == 2,
                how + ": the choice condition sees the fresh game");
        };
        {
            Rig rig(vipGraph());
            rig.expectNode(1, "fresh start");
            RowlEngine_SetVariable(rig.h, "vip", "yes");
            RowlEngine_ResetToStartNode(rig.h);
            freshGameChecks(rig, "ResetToStartNode");
        }
        {
            Rig rig(vipGraph());
            RowlEngine_SetVariable(rig.h, "vip", "yes");
            req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "open title");
            for (int i = 0; i < 16; ++i) {
                const json menu = json::parse(capiText(rig.h, RowlEngine_GetPauseMenuJsonUtf8), nullptr, false);
                if (menu.is_object() && menu["selected"].get<int>() == 0) break;
                RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
            }
            RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_CONFIRM);
            freshGameChecks(rig, "title New Game");
        }
        {
            // Load: the evaluator holds exactly the save's variables.
            Rig rig(vipGraph());
            req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save the fresh game");
            RowlEngine_SetVariable(rig.h, "vip", "yes");
            RowlEngine_SetVariable(rig.h, "temp", "x");
            req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load the fresh save");
            rig.expectNode(1, "load: node");
            req(rig.var("vip").empty() && rig.var("temp").empty(), "load: later variables are gone");
            req(RowlEngine_SelectChoice(rig.h, "go") == 1 && rig.node() == 2,
                "load: the choice condition sees the save's variables");
        }
        TEST_PASS("New Game (reset + title) and load give the Lua condition evaluator the fresh / saved variables");
    }

    RowlEngine_SetLogCallback(nullptr);
    fs::remove_all(g_root, ec);
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
}
