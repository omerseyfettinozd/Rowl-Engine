/**
 * test_auto_skip.cpp — D04-1: the player's native Auto driver and read-aware
 * Skip driver (engine core + C API + input mapping).
 *
 * Kilit:
 *  1. C API: Init oncesi STATE_ERROR, null INVALID_HANDLE, gecersiz politika /
 *     NaN gecikme INVALID_ARGUMENT, [0,10] kiskaci, varsayilanlar, caller-buffer
 *     sozlesmesi; PLAYER_AUTO_SKIP yetenegi.
 *  2. Auto: satir tamamlanmadan ilerlemez; bekleme = gecikme + karakter*0.04/
 *     metinHizi, [0.8,30] kiskaci, UTF-8 KOD NOKTASI sayimi; secimde bekler,
 *     secimden sonra surer; gecis krediyi dondurur; elle ilerleme / load /
 *     rewind / dil degisimi krediyi sifirlar; sesli dugumde ses bitene kadar
 *     (en cok 30 sn) bekler; duraklatmada ilerlemez; A tusu profile yazar.
 *  3. Skip: ReadOnly okunmus zincirde ilerler, ilk okunmamis satirda, secimde,
 *     zincir sonunda, bos/eksik contentId'de durur; All okunmamisi da gecer ama
 *     secimde/zincir sonunda durur; Off: S ve Ctrl etkisiz; menu / geri
 *     gecmis / elle Advance / load / rewind durdurur; adim basina en cok bir
 *     dugum, adimlar arasi >= 0.05 sn; Ctrl basili tut/birak.
 *  4. Okundu anlik goruntusu: ilk kez sunulan satirda false, ikinci oyunda
 *     (ayni profil) true; New Game'de cift hesap yok; load/rewind sonrasi dogru.
 *  5. Yazar auto_advance davranisi degismez; basliktaki/duraklatmadaki A/S/Ctrl
 *     etkisiz.
 *
 * Bilerek-boz: (a) anlik goruntuyu markRead'DEN SONRA almak 3'teki "ilk
 * okunmamis satirda durur"u, (b) advanceStory'deki resetAutoWait'i kaldirmak
 * 2'deki elle-ilerleme kredisi sifirlamayi, (c) bekleme formulunde bayt
 * sayimi 2'deki UTF-8 sayimini, (d) adimlar-arasi 0.05 sn kapisini kaldirmak
 * 3'teki aralik testini, (e) markPresentedDialoguesRead'deki
 * m_lastRecordedDialogueNodeId korumasini kaldirmak 4'teki New Game testini
 * kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;

constexpr const char* kSection = "auto-skip";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

// 16-bit mono PCM silence: a clip that is still queued when the test looks.
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
    std::snprintf(text, sizeof(text), "10000000-0000-4000-8000-%012d", id);
    return text;
}

// ── Graph builder ──────────────────────────────────────────────────────────
struct Node {
    int id = 0;
    std::string text;
    bool hasCid = true;
    bool typing = false;
    float speed = 30.0f;
    bool authorAuto = false;
    float authorDelay = 0.0f;
    std::vector<int> next;                              // plain edges
    std::vector<std::pair<std::string, int>> choices;   // option_id -> target
    std::string voice;
};

std::string buildGraph(const std::vector<Node>& nodes) {
    json out = json::array();
    for (const Node& n : nodes) {
        json components = json::array();
        if (!n.voice.empty()) {
            components.push_back({{"type", "audio"}, {"id", "a" + std::to_string(n.id)}, {"enabled", true},
                                  {"data", {{"voice_track", n.voice}}}});
        }
        json dialogue = {{"speaker", "S"}, {"dialogue", n.text}, {"typewriter_enabled", n.typing},
                         {"text_speed", n.speed}};
        if (n.hasCid) dialogue["content_id"] = cid(n.id);
        if (n.authorAuto) {
            dialogue["auto_advance"] = true;
            dialogue["auto_advance_delay"] = n.authorDelay;
        }
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
        json node = {{"id", n.id}, {"title", "N" + std::to_string(n.id)},
                     {"objects", json::array({{{"id", "o" + std::to_string(n.id)}, {"name", "D"},
                                               {"is_active", true}, {"components", components}}})},
                     {"next_nodes", edges}};
        out.push_back(node);
    }
    return json({{"format_version", 4}, {"start_node_id", nodes.front().id}, {"nodes", out}}).dump();
}

// 1..count chain, equal-length texts ("A1".."A9": 2 chars).
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

// Forwards to the host the engine runs with and adds an injectable input queue
// (the channel a native host uses), so keys travel through handleRuntimeInput.
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

    explicit Rig(const std::vector<Node>& nodes, bool voiceAssets = false) {
        ++g_rigCounter;
        game = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "6c2f1b75-7d3e-4e8f-8b21-1d3c8e6f9a%02x", g_rigCounter);
        writeBytes(game / "project.rowlproj",
                   std::string(R"({"project_uuid":")") + uuid + R"(","name":"Auto Skip Test"})");
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph(nodes));
        if (voiceAssets) writeBytes(game / "Assets" / "audio" / "v.wav", makeWav(20));
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
    }
    ~Rig() { if (h) RowlEngine_Destroy(h); }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void preRead(std::initializer_list<int> ids) {
        for (int id : ids) {
            req(RowlEngine_ProfileMarkRead(h, cid(id).c_str()) == ROWL_RESULT_OK,
                "could not pre-mark node " + std::to_string(id) + " read");
        }
    }
    void preReadAll(int count) {
        for (int id = 1; id <= count; ++id) preRead({id});
    }
    void play() {
        RowlEngine_SetPlayState(h, 1);
        RowlEngine_Step(h, 0.016f);
    }
    void step(float dt, int times = 1) {
        for (int i = 0; i < times; ++i) RowlEngine_Step(h, dt);
    }
    void queue(InType type) {
        RuntimeInputEvent event{};
        event.type = type;
        host->input.push_back(event);
    }
    void queueKey(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
    }
    void queueKeyUp(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::KeyUp;
        event.key = sdlKey;
        host->input.push_back(event);
    }
    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    json state() const {
        const json parsed = json::parse(capiText(h, RowlEngine_GetPlayerDriverStateJsonUtf8), nullptr, false);
        req(parsed.is_object(), "driver state JSON did not parse");
        return parsed;
    }
    bool skipActive() const { return state()["skip_active"].get<bool>(); }
    bool autoOn() const { return state()["auto"].get<bool>(); }
    float remaining() const { return state()["auto_wait_remaining"].get<float>(); }
    bool paused() const { return RowlEngine_IsPaused(h) == 1; }
    json profile() const {
        return json::parse(capiText(h, RowlEngine_ProfileGetJson), nullptr, false);
    }
    void expectNode(uint64_t id, const std::string& what) const {
        req(node() == id, what + " (node " + std::to_string(node()) + ", expected " + std::to_string(id) + ")");
    }
};

bool near(float a, float b, float tolerance = 0.02f) { return std::fabs(a - b) <= tolerance; }

}  // namespace

void test_auto_skip() {
    TEST_SECTION("Native Auto + read-aware Skip (D04-1)");

    g_root = fs::temp_directory_path() / "rowl_auto_skip";
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

    // ── 1. C API contract ──────────────────────────────────────────────────
    {
        RowlEngineHandle early = RowlEngine_Create();
        req(early != nullptr, "create failed");
        req(RowlEngine_SetAutoMode(early, 1) == ROWL_RESULT_STATE_ERROR, "SetAutoMode before Init");
        req(RowlEngine_SetSkipActive(early, 1) == ROWL_RESULT_STATE_ERROR, "SetSkipActive before Init");
        req(RowlEngine_SetSkipPolicy(early, 2) == ROWL_RESULT_STATE_ERROR, "SetSkipPolicy before Init");
        req(RowlEngine_SetAutoDelay(early, 1.0f) == ROWL_RESULT_STATE_ERROR, "SetAutoDelay before Init");
        uint32_t required = 0;
        req(RowlEngine_GetPlayerDriverStateJsonUtf8(early, nullptr, 0, &required) == ROWL_RESULT_STATE_ERROR,
            "GetPlayerDriverStateJsonUtf8 before Init");
        RowlEngine_Destroy(early);
        req(RowlEngine_SetAutoMode(nullptr, 1) == ROWL_RESULT_INVALID_HANDLE, "SetAutoMode(null)");
        req(RowlEngine_SetSkipActive(nullptr, 1) == ROWL_RESULT_INVALID_HANDLE, "SetSkipActive(null)");
        req(RowlEngine_SetSkipPolicy(nullptr, 1) == ROWL_RESULT_INVALID_HANDLE, "SetSkipPolicy(null)");
        req(RowlEngine_SetAutoDelay(nullptr, 1.0f) == ROWL_RESULT_INVALID_HANDLE, "SetAutoDelay(null)");
        req(RowlEngine_GetPlayerDriverStateJsonUtf8(nullptr, nullptr, 0, &required) == ROWL_RESULT_INVALID_HANDLE,
            "GetPlayerDriverStateJsonUtf8(null)");

        uint64_t capabilities = 0;
        req(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK &&
                (capabilities & ROWL_ENGINE_CAPABILITY_PLAYER_AUTO_SKIP) != 0 &&
                ROWL_ENGINE_CAPABILITY_PLAYER_AUTO_SKIP == UINT64_C(134217728),
            "PLAYER_AUTO_SKIP capability (134217728)");

        Rig rig(chain(3));
        const json initial = rig.state();
        req(initial["auto"] == false && initial["skip_active"] == false && initial["skip_policy"] == "read_only" &&
                near(initial["auto_delay"].get<float>(), 2.0f, 0.0001f) &&
                initial["auto_wait_remaining"].get<float>() == 0.0f,
            "driver state defaults: " + initial.dump());
        req(RowlEngine_SetSkipPolicy(rig.h, 3) == ROWL_RESULT_INVALID_ARGUMENT, "policy 3");
        req(RowlEngine_SetSkipPolicy(rig.h, -1) == ROWL_RESULT_INVALID_ARGUMENT, "policy -1");
        req(rig.state()["skip_policy"] == "read_only", "an invalid policy changes nothing");
        req(RowlEngine_SetSkipPolicy(rig.h, 2) == ROWL_RESULT_OK && rig.state()["skip_policy"] == "all", "policy all");
        req(RowlEngine_SetSkipPolicy(rig.h, 0) == ROWL_RESULT_OK && rig.state()["skip_policy"] == "off", "policy off");
        req(RowlEngine_SetSkipPolicy(rig.h, 1) == ROWL_RESULT_OK && rig.state()["skip_policy"] == "read_only",
            "policy read_only");
        req(RowlEngine_SetAutoDelay(rig.h, std::nanf("")) == ROWL_RESULT_INVALID_ARGUMENT, "NaN delay");
        req(RowlEngine_SetAutoDelay(rig.h, std::numeric_limits<float>::infinity()) == ROWL_RESULT_INVALID_ARGUMENT,
            "inf delay");
        req(near(rig.state()["auto_delay"].get<float>(), 2.0f, 0.0001f), "an invalid delay changes nothing");
        req(RowlEngine_SetAutoDelay(rig.h, 99.0f) == ROWL_RESULT_OK &&
                near(rig.state()["auto_delay"].get<float>(), 10.0f, 0.0001f), "delay clamps to 10");
        req(RowlEngine_SetAutoDelay(rig.h, -5.0f) == ROWL_RESULT_OK &&
                near(rig.state()["auto_delay"].get<float>(), 0.0f, 0.0001f), "delay clamps to 0");
        req(RowlEngine_SetAutoMode(rig.h, 7) == ROWL_RESULT_OK && rig.autoOn(), "any non-zero enables auto");
        req(RowlEngine_SetAutoMode(rig.h, 0) == ROWL_RESULT_OK && !rig.autoOn(), "zero disables auto");
        // Starting skip is refused while the policy is off or the game is paused.
        RowlEngine_SetSkipPolicy(rig.h, 0);
        req(RowlEngine_SetSkipActive(rig.h, 1) == ROWL_RESULT_STATE_ERROR && !rig.skipActive(),
            "SetSkipActive under policy off is refused");
        RowlEngine_SetSkipPolicy(rig.h, 1);
        RowlEngine_SetPaused(rig.h, 1);
        req(RowlEngine_SetSkipActive(rig.h, 1) == ROWL_RESULT_STATE_ERROR && !rig.skipActive(),
            "SetSkipActive while paused is refused");
        RowlEngine_SetPaused(rig.h, 0);
        req(RowlEngine_SetSkipActive(rig.h, 1) == ROWL_RESULT_OK && rig.skipActive(), "SetSkipActive on");
        req(RowlEngine_SetSkipActive(rig.h, 0) == ROWL_RESULT_OK && !rig.skipActive(), "SetSkipActive off");

        // Caller-buffer contract.
        uint32_t need = 0;
        req(RowlEngine_GetPlayerDriverStateJsonUtf8(rig.h, nullptr, 0, &need) == ROWL_RESULT_OK && need > 2,
            "size query");
        std::string tiny(2, 'x');
        uint32_t reported = 0;
        req(RowlEngine_GetPlayerDriverStateJsonUtf8(rig.h, tiny.data(), 2, &reported) ==
                ROWL_RESULT_BUFFER_TOO_SMALL && reported == need, "undersized buffer");
        req(RowlEngine_GetPlayerDriverStateJsonUtf8(rig.h, nullptr, 0, nullptr) == ROWL_RESULT_INVALID_ARGUMENT,
            "null outRequiredSize");
    }
    TEST_PASS("C API: contract, validation, clamps, defaults, caller buffer, capability");

    // ── 2. Auto ────────────────────────────────────────────────────────────
    // 2a. The line must be fully shown first: a 10-char line at 200 ms/char
    // (text_speed is ms per character) types for ~2 s, its wait floor is 0.8 s. Credit must start only after typing.
    {
        std::vector<Node> nodes;
        Node typed;
        typed.id = 1; typed.text = "abcdefghij"; typed.typing = true; typed.speed = 200.0f; typed.next = {2};
        nodes.push_back(typed);
        Node second;
        second.id = 2; second.text = "B2";
        nodes.push_back(second);
        Rig rig(nodes);
        rig.play();
        RowlEngine_SetAutoDelay(rig.h, 0.0f);
        RowlEngine_SetAutoMode(rig.h, 1);
        rig.step(0.25f, 6);  // 1.5 s: still typing
        rig.expectNode(1, "Auto must not advance while the line is still typing");
        req(rig.remaining() == 0.0f, "no wait credit while typing");
        rig.step(0.25f, 4);  // 2.5 s: typing done at ~2 s, only ~0.5 s of credit
        rig.expectNode(1, "Auto waits its delay after the line completes (not from the start of typing)");
        rig.step(0.25f, 5);  // 3.75 s
        rig.expectNode(2, "Auto advances once the line is complete and the wait elapsed");
    }
    TEST_PASS("Auto: nothing before the line is fully shown, wait starts after it");

    // 2b. Wait formula: delay + chars*0.04/textSpeed, [0.8, 30], UTF-8 code points.
    {
        std::vector<Node> nodes = chain(5);
        nodes[0].text = "Hi";
        // "çağrı" is 5 code points but 8 bytes: x10 = 50 code points, 80 bytes.
        const std::string turkish =
            "çağrıçağrıçağrıçağrıçağrıçağrıçağrıçağrıçağrıçağrı";
        nodes[1].text = turkish;
        nodes[2].text = std::string(1000, 'x');
        nodes[3].text = "Yo";
        Rig rig(nodes);
        rig.play();
        RowlEngine_SetAutoMode(rig.h, 1);
        rig.step(0.1f);
        req(near(rig.remaining(), 2.0f + 0.08f - 0.1f), "'Hi' + delay 2: " + std::to_string(rig.remaining()));
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.expectNode(2, "manual advance");
        size_t cps = 0;
        for (unsigned char b : turkish) if ((b & 0xC0u) != 0x80u) ++cps;
        RowlEngine_SetAutoDelay(rig.h, 0.0f);
        rig.step(0.1f);
        req(near(rig.remaining(), static_cast<float>(cps) * 0.04f - 0.1f),
            "UTF-8 code points, not bytes (" + std::to_string(cps) + " cp / " + std::to_string(turkish.size()) +
                " bytes): " + std::to_string(rig.remaining()));
        RowlEngine_SetTextSpeedMultiplier(rig.h, 2.0f);
        rig.step(0.1f);
        req(near(rig.remaining(), static_cast<float>(cps) * 0.04f / 2.0f - 0.2f),
            "text speed 2x halves the per-char part: " + std::to_string(rig.remaining()));
        RowlEngine_SetTextSpeedMultiplier(rig.h, 1.0f);
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.expectNode(3, "manual advance 2");
        rig.step(0.1f);
        req(near(rig.remaining(), 30.0f - 0.1f), "very long line clamps at 30 s: " + std::to_string(rig.remaining()));
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.expectNode(4, "manual advance 3");
        rig.step(0.1f);
        req(near(rig.remaining(), 0.8f - 0.1f), "short line + delay 0 clamps at 0.8 s: " + std::to_string(rig.remaining()));
    }
    TEST_PASS("Auto: wait formula, text-speed scaling, [0.8, 30] clamp, UTF-8 code points");

    // 2c. Reaching the end of the wait advances; wait actually elapses.
    {
        Rig rig(chain(3));
        rig.play();
        RowlEngine_SetAutoMode(rig.h, 1);  // delay 2 + 0.08
        rig.step(0.25f, 8);
        rig.expectNode(1, "2.0 s < 2.08 s");
        rig.step(0.1f);
        rig.expectNode(2, "2.1 s >= 2.08 s advances");
        req(rig.autoOn(), "Auto stays on after advancing");
    }
    TEST_PASS("Auto: advances exactly when the wait elapses");

    // 2d. Choice: Auto waits, stays on, resumes after the pick.
    {
        std::vector<Node> nodes = chain(5);
        nodes[0].next = {2};
        nodes[1].next = {};
        nodes[1].choices = {{"a", 3}, {"b", 4}};
        nodes[2].next = {5};
        Rig rig(nodes);
        rig.play();
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.expectNode(2, "choice node reached");
        RowlEngine_SetAutoMode(rig.h, 1);
        rig.step(0.25f, 80);  // 20 s
        rig.expectNode(2, "Auto must not answer a choice menu");
        req(rig.autoOn() && rig.remaining() == 0.0f, "Auto stays on but idle at the choice");
        req(RowlEngine_SelectChoice(rig.h, "a") == 1, "pick option a");
        rig.expectNode(3, "choice taken");
        rig.step(0.25f, 8);
        rig.expectNode(3, "wait starts fresh after the pick");
        rig.step(0.1f);
        rig.expectNode(5, "Auto resumes after the choice");
    }
    TEST_PASS("Auto: waits at a choice, stays on, resumes after the pick");

    // 2e. A running transition freezes the credit (same rule as #61).
    {
        Rig rig(chain(3));
        rig.play();
        RowlEngine_SetAutoMode(rig.h, 1);  // wait 2.08
        rig.step(0.25f, 4);                // credit 1.0
        RowlEngine_StartTransition(rig.h, "crossfade", 5.0f, nullptr);
        if (!RowlEngine_IsTransitionActive(rig.h)) {
            std::cout << "  (Auto transition freeze SKIP: transition does not engage on this driver)" << std::endl;
        } else {
            rig.step(0.25f, 10);
            rig.expectNode(1, "no advance mid-transition");
            rig.step(0.25f, 10);
            req(RowlEngine_IsTransitionActive(rig.h) == 0, "5 s transition finished");
            rig.expectNode(1, "no advance on the transition boundary");
            rig.step(0.25f, 3);  // +0.75 = 1.75 s credit
            rig.expectNode(1, "credit 1.75 < 2.08");
            rig.step(0.25f, 2);  // 2.25 s
            rig.expectNode(2, "pre-transition credit survived (frozen, not wiped)");
        }
    }
    TEST_PASS("Auto: a transition freezes the credit");

    // 2f. Manual advance / load / rewind / language change reset the credit.
    {
        Rig rig(chain(4));
        rig.play();
        RowlEngine_SetAutoMode(rig.h, 1);
        const float fresh = 2.0f + 0.08f - 0.1f;

        // manual advance
        rig.step(0.25f, 6);
        req(near(rig.remaining(), 2.08f - 1.5f), "credit built up: " + std::to_string(rig.remaining()));
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.1f);
        rig.expectNode(2, "manual advance moved on");
        req(near(rig.remaining(), fresh), "manual advance resets the credit: " + std::to_string(rig.remaining()));

        // load
        req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save slot 1");
        rig.step(0.25f, 6);
        req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load slot 1");
        rig.step(0.1f);
        req(near(rig.remaining(), fresh), "load resets the credit: " + std::to_string(rig.remaining()));

        // rewind
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.25f, 6);
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind");
        rig.step(0.1f);
        req(near(rig.remaining(), fresh), "rewind resets the credit: " + std::to_string(rig.remaining()));

        // language change
        rig.step(0.25f, 6);
        req(RowlEngine_SetLocale(rig.h, "en") == ROWL_RESULT_OK, "SetLocale(en)");
        rig.step(0.1f);
        req(near(rig.remaining(), fresh), "language change resets the credit: " + std::to_string(rig.remaining()));
    }
    TEST_PASS("Auto: manual advance, load, rewind and language change reset the credit");

    // 2g. The node's own voice clip is waited out (cap 30 s in total).
    {
        std::vector<Node> nodes = chain(4);
        nodes[1].voice = "v.wav";
        Rig rig(nodes, true);
        rig.play();
        RowlEngine_SetAutoDelay(rig.h, 0.0f);
        RowlEngine_SetAutoMode(rig.h, 1);
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.expectNode(2, "voiced node");
        auto* audio = rig.engine->getAudio();
        req(audio != nullptr && audio->isVoicePlaying(), "setup: the node voice clip must be playing");
        rig.step(0.5f, 6);  // 3 s >> 0.8 s wait
        rig.expectNode(2, "Auto waits for the voice clip to finish");
        audio->stopVoice();  // the clip is over
        rig.step(0.1f);
        rig.expectNode(3, "Auto advances once the voice is done");
    }
    {
        std::vector<Node> nodes = chain(4);
        nodes[1].voice = "v.wav";
        Rig rig(nodes, true);
        rig.play();
        RowlEngine_SetAutoDelay(rig.h, 0.0f);
        RowlEngine_SetAutoMode(rig.h, 1);
        RowlEngine_AdvanceNode(rig.h, 0);
        req(rig.engine->getAudio() && rig.engine->getAudio()->isVoicePlaying(), "setup: voice playing");
        rig.step(0.25f, 80);
        rig.expectNode(2, "20 s of a still-playing clip: still waiting");
        rig.step(0.25f, 41);
        rig.expectNode(3, "a stuck voice flag cannot hold Auto past 30 s");
    }
    TEST_PASS("Auto: waits for the node voice, never past 30 s");

    // 2h. Pause: nothing advances, no credit grows.
    {
        Rig rig(chain(3));
        rig.play();
        RowlEngine_SetAutoMode(rig.h, 1);
        RowlEngine_SetPaused(rig.h, 1);
        rig.step(0.25f, 60);
        rig.expectNode(1, "Auto must not advance while paused");
        RowlEngine_SetPaused(rig.h, 0);
        rig.step(0.25f, 8);
        rig.expectNode(1, "paused time was not credited");
        rig.step(0.2f);
        rig.expectNode(2, "wait elapses after resume");
    }
    TEST_PASS("Auto: paused time neither advances nor accrues");

    // 2i. A key: toggles, writes the profile, inert on title / pause / backlog.
    {
        Rig rig(chain(3));
        rig.play();
        req(rig.profile()["auto_enabled"] == false, "auto_enabled starts false");
        rig.queueKey(SDLK_A);
        rig.step(0.016f);
        req(rig.autoOn() && rig.profile()["auto_enabled"] == true, "A turns Auto on and writes auto_enabled");
        rig.queueKey(SDLK_A);
        rig.step(0.016f);
        req(!rig.autoOn() && rig.profile()["auto_enabled"] == false, "A again turns it off and writes false");

        RowlEngine_SetPaused(rig.h, 1);
        rig.queueKey(SDLK_A);
        rig.step(0.016f);
        req(!rig.autoOn() && rig.profile()["auto_enabled"] == false, "A is inert in the pause menu");
        RowlEngine_SetPaused(rig.h, 0);
        req(RowlEngine_OpenBacklog(rig.h) == ROWL_RESULT_OK, "open backlog");
        rig.queueKey(SDLK_A);
        rig.step(0.016f);
        req(!rig.autoOn(), "A is inert on the backlog page");
        RowlEngine_SetPaused(rig.h, 0);
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "open title");
        rig.queueKey(SDLK_A);
        rig.step(0.016f);
        req(!rig.autoOn() && rig.profile()["auto_enabled"] == false, "A is inert on the title screen");
    }
    TEST_PASS("Auto: A toggles and persists; inert on pause, backlog and title");

    // ── 3. Skip ────────────────────────────────────────────────────────────
    // 3a. ReadOnly: runs over read lines, stops on the first unread one.
    {
        Rig rig(chain(6));
        rig.preRead({1, 2, 3, 4});  // 5 and 6 are new
        rig.play();
        req(rig.state()["line_read_before"] == true, "node 1 was read before");
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f);
        rig.expectNode(2, "first skip step");
        req(rig.skipActive(), "skip active while it runs over read lines");
        rig.step(0.05f, 20);
        rig.expectNode(5, "read-only skip stops on the first unread line (node 5)");
        req(!rig.skipActive(), "skip switches itself off at the unread line");
        req(rig.state()["line_read_before"] == false, "the unread line is reported unread after presentation");
        rig.step(0.05f, 10);
        rig.expectNode(5, "and it stays there");
    }
    TEST_PASS("Skip read_only: passes read lines, stops on the first unread line");

    // 3b. Chain end, empty content id, choice menu.
    {
        Rig rig(chain(6));
        rig.preReadAll(6);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 30);
        rig.expectNode(6, "skip stops at the end of the chain");
        req(!rig.skipActive(), "skip off at the chain end");
    }
    {
        std::vector<Node> nodes = chain(6);
        nodes[2].hasCid = false;  // node 3: a line without a content id
        Rig rig(nodes);
        rig.preReadAll(6);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 30);
        rig.expectNode(3, "a line without a content id counts as unread (fail-closed)");
        req(!rig.skipActive() && rig.state()["line_read_before"] == false, "skip off, snapshot false");
    }
    {
        std::vector<Node> nodes = chain(6);
        nodes[3].next = {};
        nodes[3].choices = {{"a", 5}, {"b", 6}};
        Rig rig(nodes);
        rig.preReadAll(6);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 30);
        rig.expectNode(4, "skip stops at the choice menu");
        req(!rig.skipActive(), "skip off at the choice");
        req(RowlEngine_SelectChoice(rig.h, "a") == 1, "pick option a");
        rig.step(0.05f, 10);
        rig.expectNode(5, "skip does not restart by itself after the pick");
        req(!rig.skipActive(), "still off");
    }
    TEST_PASS("Skip: stops at the chain end, on a missing content id and at a choice menu");

    // 3c. All passes unread lines but still stops at choices / chain end; Off is inert.
    {
        Rig rig(chain(6));
        RowlEngine_SetSkipPolicy(rig.h, 2);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 30);
        rig.expectNode(6, "policy all skips unread lines up to the chain end");
        req(!rig.skipActive(), "off at the end");
    }
    {
        std::vector<Node> nodes = chain(6);
        nodes[3].next = {};
        nodes[3].choices = {{"a", 5}, {"b", 6}};
        Rig rig(nodes);
        RowlEngine_SetSkipPolicy(rig.h, 2);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 30);
        rig.expectNode(4, "policy all still stops at a choice menu");
    }
    {
        Rig rig(chain(6));
        rig.preReadAll(6);
        RowlEngine_SetSkipPolicy(rig.h, 0);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 10);
        req(!rig.skipActive(), "S is inert under policy off");
        rig.queue(InType::SkipHold);
        rig.step(0.05f, 10);
        rig.expectNode(1, "Ctrl is inert under policy off");
        // The policy flipping to off ends a running skip.
        RowlEngine_SetSkipPolicy(rig.h, 1);
        req(RowlEngine_SetSkipActive(rig.h, 1) == ROWL_RESULT_OK && rig.skipActive(), "skip on again");
        RowlEngine_SetSkipPolicy(rig.h, 0);
        req(!rig.skipActive(), "policy off stops a running skip");
    }
    TEST_PASS("Skip: all passes unread lines (not choices), off is inert");

    // 3d. At most one node per step, >= 0.05 s apart.
    {
        Rig rig(chain(9));
        rig.preReadAll(9);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.01f, 5);
        rig.expectNode(2, "5 x 0.01 s: only the immediate first step advanced");
        rig.step(0.01f);
        rig.expectNode(3, "0.05 s after the first advance: second node");
        rig.step(0.01f, 4);
        rig.expectNode(3, "no advance inside the interval");
        rig.step(0.01f);
        rig.expectNode(4, "third node");
        rig.step(0.25f);
        rig.expectNode(5, "a long step still advances only one node");
    }
    TEST_PASS("Skip: one node per step, 0.05 s apart");

    // 3e. Stops: menus, manual advance, load, rewind.
    {
        Rig rig(chain(8));
        rig.preReadAll(8);
        rig.play();
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 2);
        rig.expectNode(3, "running");
        rig.queue(InType::PauseToggle);
        rig.step(0.05f);
        req(rig.paused() && !rig.skipActive(), "opening the pause menu stops skip");
        RowlEngine_SetPaused(rig.h, 0);
        rig.step(0.05f, 10);
        rig.expectNode(3, "skip does not resume after the menu closes");

        RowlEngine_SetSkipActive(rig.h, 1);
        rig.step(0.05f);
        rig.expectNode(4, "running again");
        req(RowlEngine_OpenBacklog(rig.h) == ROWL_RESULT_OK, "open backlog");
        req(!rig.skipActive(), "opening the backlog stops skip");
        RowlEngine_SetPaused(rig.h, 0);
        rig.step(0.05f, 5);
        rig.expectNode(4, "stays after the backlog closes");

        // manual advance
        RowlEngine_SetSkipActive(rig.h, 1);
        rig.step(0.05f);
        rig.expectNode(5, "running (3)");
        rig.queueKey(SDLK_SPACE);
        rig.step(0.01f);
        rig.expectNode(6, "the manual advance moved one node");
        req(!rig.skipActive(), "a manual Advance stops skip");
        rig.step(0.05f, 10);
        rig.expectNode(6, "and skip does not carry on");
    }
    {
        Rig rig(chain(8));
        rig.preReadAll(8);
        rig.play();
        req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save slot 1 at node 1");
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 3);
        rig.expectNode(4, "running before the load");
        req(rig.skipActive(), "still active");
        req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load slot 1");
        req(!rig.skipActive(), "load stops skip");
        rig.step(0.05f, 10);
        rig.expectNode(1, "skip stays stopped after the load");

        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 3);
        rig.expectNode(4, "running before the rewind");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind");
        req(!rig.skipActive(), "rewind stops skip");
        const uint64_t afterRewind = rig.node();
        rig.step(0.05f, 10);
        rig.expectNode(afterRewind, "skip stays stopped after the rewind");
    }
    TEST_PASS("Skip: pause / backlog / manual advance / load / rewind stop it");

    // 3f. Ctrl held = skip while held; released = stops; S-started skip survives a Ctrl release.
    {
        Rig rig(chain(8));
        rig.preReadAll(8);
        rig.play();
        rig.queue(InType::SkipHold);
        rig.step(0.05f, 2);
        rig.expectNode(3, "holding Ctrl skips");
        req(!rig.skipActive(), "Ctrl hold does not set the S flag");
        rig.queueKeyUp(SDLK_LCTRL);
        rig.step(0.05f, 10);
        rig.expectNode(3, "releasing Ctrl stops it");

        rig.queueKey(SDLK_LCTRL);
        rig.step(0.05f);
        rig.expectNode(4, "Ctrl pressed again");
        rig.queueKeyUp(SDLK_RCTRL);  // either Ctrl key releases the hold
        rig.step(0.05f, 10);
        rig.expectNode(4, "release via the right Ctrl key");

        rig.queue(InType::ToggleSkip);
        rig.step(0.05f);
        rig.expectNode(5, "S started");
        rig.queue(InType::SkipHold);
        rig.step(0.05f);
        rig.queueKeyUp(SDLK_LCTRL);
        rig.step(0.05f);
        req(rig.skipActive() && rig.node() >= 7, "releasing Ctrl does not close an S-started skip");
    }
    {
        Rig rig(chain(6));
        rig.preRead({1, 2});  // 3 is new
        rig.play();
        rig.queue(InType::SkipHold);
        rig.step(0.05f, 20);
        rig.expectNode(3, "Ctrl-skip also stops on an unread line");
    }
    TEST_PASS("Skip: Ctrl hold / release, S-started skip survives release, unread line stops it");

    // 3g. Auto and Skip together: skip first, auto afterwards, never both at once.
    {
        Rig rig(chain(6));
        rig.preRead({1, 2, 3});  // 4 is new
        rig.play();
        RowlEngine_SetAutoMode(rig.h, 1);
        rig.queue(InType::ToggleSkip);
        rig.step(0.05f, 15);
        rig.expectNode(4, "skip ran over the read lines and stopped on the unread one");
        req(!rig.skipActive() && rig.autoOn(), "skip off, Auto still on");
        rig.step(0.1f, 10);
        rig.expectNode(4, "Auto waits its full delay at the unread line");
        rig.step(0.1f, 12);
        rig.expectNode(5, "then Auto advances");
    }
    TEST_PASS("Skip takes priority; Auto resumes where skip stopped");

    // ── 4. Read-before snapshot ────────────────────────────────────────────
    // 4a. First play: false at first presentation; second game on the same profile: true.
    {
        Rig rig(chain(4));
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "open title");
        RowlEngine_SetPlayState(rig.h, 1);          // behind the title: nothing is presented yet
        rig.step(0.016f);
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_CONFIRM);  // Yeni Oyun
        req(RowlEngine_IsPaused(rig.h) == 0, "New Game left the title");
        req(rig.state()["line_read_before"] == false, "first-ever presentation of node 1: not read before");
        int32_t isRead = 0;
        req(RowlEngine_ProfileIsRead(rig.h, cid(1).c_str(), &isRead) == ROWL_RESULT_OK && isRead == 1,
            "the presentation marked the line read");
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(rig.state()["line_read_before"] == false, "node 2 was new too");

        // second game: back to the title, New Game again
        RowlEngine_SetPaused(rig.h, 0);
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "title again");
        RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_CONFIRM);
        rig.expectNode(1, "second game starts at node 1");
        req(rig.state()["line_read_before"] == true, "second game: node 1 was read in the first one");
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(rig.state()["line_read_before"] == true, "second game: node 2 too");
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(rig.state()["line_read_before"] == false, "node 3 is new");
    }
    TEST_PASS("Snapshot: first presentation false, second game true (New Game counted once)");

    // 4b. Load / rewind compute from the read set.
    {
        Rig rig(chain(5));
        rig.play();
        req(RowlEngine_SaveGameSlot(rig.h, 1) == 1, "save at node 1");
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(rig.state()["line_read_before"] == false, "node 2 new");
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(rig.state()["line_read_before"] == false, "node 3 new");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind to node 2");
        rig.expectNode(2, "rewound");
        req(rig.state()["line_read_before"] == true, "rewind: node 2 is in the read set");
        req(RowlEngine_LoadGameSlot(rig.h, 1) == 1, "load node 1");
        rig.expectNode(1, "loaded");
        req(rig.state()["line_read_before"] == true, "load: node 1 is in the read set");
    }
    {
        // Same save, different profile state: the snapshot follows the profile.
        std::vector<Node> nodes = chain(4);
        Rig rig(nodes);
        rig.preRead({1, 2});
        rig.play();
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(RowlEngine_SaveGameSlot(rig.h, 2) == 1, "save at node 2");
        RowlEngine_AdvanceNode(rig.h, 0);
        rig.step(0.016f);
        req(rig.state()["line_read_before"] == false, "node 3 new");
        req(RowlEngine_SetLocale(rig.h, "en") == ROWL_RESULT_OK, "SetLocale");
        req(rig.state()["line_read_before"] == false, "a language change leaves the snapshot alone");
        req(RowlEngine_LoadGameSlot(rig.h, 2) == 1, "load node 2");
        req(rig.state()["line_read_before"] == true, "load: node 2 was read");
    }
    TEST_PASS("Snapshot: load / rewind / language change");

    // ── 5. Author auto_advance is untouched; inert keys on the title ───────
    {
        std::vector<Node> nodes = chain(3);
        nodes[0].authorAuto = true;
        nodes[0].authorDelay = 1.0f;
        Rig rig(nodes);
        rig.play();
        rig.step(0.25f, 2);
        rig.expectNode(1, "author delay 1.0 not reached");
        rig.step(0.25f, 3);
        rig.expectNode(2, "author auto_advance still advances at its own delay");
        req(!rig.autoOn(), "player Auto is separate and stayed off");
    }
    {
        std::vector<Node> nodes = chain(3);
        nodes[0].authorAuto = true;
        nodes[0].authorDelay = 1.0f;
        Rig rig(nodes);
        rig.play();
        RowlEngine_SetAutoDelay(rig.h, 5.0f);
        RowlEngine_SetAutoMode(rig.h, 1);
        rig.step(0.25f, 2);
        rig.step(0.25f, 3);
        rig.expectNode(2, "with player Auto on (delay 5) the author's 1.0 s still wins, once");
    }
    {
        Rig rig(chain(4));
        rig.preReadAll(4);
        rig.play();
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "title");
        rig.queueKey(SDLK_A);
        rig.queueKey(SDLK_S);
        rig.queueKey(SDLK_LCTRL);
        rig.step(0.05f, 10);
        req(!rig.autoOn() && !rig.skipActive(), "A / S are inert on the title");
        rig.expectNode(1, "Ctrl is inert on the title");
        RowlEngine_SetPaused(rig.h, 0);
        rig.step(0.05f, 10);
        rig.expectNode(1, "and nothing started behind the title");
    }
    TEST_PASS("Author auto_advance unchanged; A / S / Ctrl inert on the title");

    // Restore the environment.
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(g_root, ec);
}
