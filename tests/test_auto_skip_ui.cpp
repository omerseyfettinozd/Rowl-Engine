/**
 * test_auto_skip_ui.cpp — D04-2: the Auto / Skip surface of the player: the
 * two Preferences rows ("Otomatik Hızı", "Atlama") and the AUTO / SKIP
 * capsules on the story frame.
 *
 * Kilit:
 *  1. Tercihler sayfasi 12 satir (D05-2): Ana Ses, Muzik, SFX, Seslendirme,
 *     Metin Hizi, Otomatik Hizi ("2.0 sn"), Atlama ("Okunan"), Dil, Metin
 *     Boyutu, Yuksek Kontrast, Hareketi Azalt, Geri; mevcut 7 satirin
 *     indeksi/davranisi ayni; Geri en sonda (Enter donusu: title, secili 3).
 *  2. Otomatik Hizi: SOL/SAG +-0.5 sn, [0,10] kiskaci (0'da SOL / 10'da SAG
 *     degistirmez), Enter SAG gibi; motor degeri (GetPlayerDriverStateJson) ve
 *     profil "auto_advance_delay" degeri satirla birlikte gider.
 *  3. Atlama: Okunan -> Hepsi -> Kapali -> Okunan (SAG), ters yonde SOL; motor
 *     politikasi ve profil "skip_mode" ("read_only"|"all"|"off") eslesir;
 *     Kapali iken S / Ctrl atlamayi baslatmaz, Hepsi iken baslatir.
 *  4. Fare: eksi/arti bolgesi yeni satirlari ayni degistirir; orta bant yalniz
 *     secer (ikinci tik onaylar); Geri satirina tik title'a doner.
 *  5. Menu JSON: yeni satirlarin degeri; ana duraklatma sayfasi 7 satir (D05-2:
 *     ses / metin hizi satirlari Tercihler arkasina tasindi).
 *  6. Gosterge: A ile AUTO kapsulu sag ustte belirir (bolge farki, disi ayni),
 *     kapaninca kare ilk hikaye karesiyle BIT BIT ayni (bayat kare yok, cache'li
 *     kareler dahil); SKIP (Ctrl basili, okunmamis satir: ilerlemez) kapsulu;
 *     ikisi yan yana; duraklatma / baslik / gecmis acikken gorunmez; menu
 *     kapaninca geri gelir.
 *
 * Bilerek-boz: (a) Window::renderComposedFrame'deki m_bakedBadges
 * gecersizlestirmesini kaldirmak 6'daki "bayat kare"yi, (b)
 * persistPauseMenuPreference'ta 8/9 kollarini kaldirmak 2 ve 3'teki profil
 * kontrolunu, (c) pauseMenuValueRow'u Preferences'ta yalniz 0..4 eslemek 2-4'u,
 * (d) engine.cpp'de showAutoBadge'in !m_paused kapisini kaldirmak 6'daki
 * "duraklatmada gorunmez"i kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
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
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;
using LM = Rowl::Core::PauseMenuLayout;

constexpr const char* kSection = "auto-skip-ui";
constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 360;

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "20000000-0000-4000-8000-%012d", id);
    return text;
}

// 1..6 chain of plain lines with content ids (unread in a fresh profile).
std::string buildGraph() {
    json nodes = json::array();
    for (int id = 1; id <= 6; ++id) {
        json dialogue = {{"speaker", "Anlatici"},
                         {"dialogue", "Bu hikaye satiri " + std::to_string(id) + " icin yeterince uzun bir metin."},
                         {"typewriter_enabled", false},
                         {"content_id", cid(id)}};
        json components = json::array(
            {{{"type", "dialogue"}, {"id", "d" + std::to_string(id)}, {"enabled", true}, {"data", dialogue}}});
        json edges = json::array();
        if (id < 6) edges.push_back({{"id", id + 1}});
        nodes.push_back({{"id", id}, {"title", "N" + std::to_string(id)},
                         {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"},
                                                   {"is_active", true}, {"components", components}}})},
                         {"next_nodes", edges}});
    }
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
}

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

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    std::shared_ptr<InputHost> host;

    ~Rig() { if (h) { RowlEngine_Shutdown(h); RowlEngine_Destroy(h); } }

    json menu() const {
        const char* text = RowlEngine_GetPauseMenuJson(h);
        json parsed = json::parse(text ? text : "", nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    json profile() const {
        json parsed = json::parse(capiText(h, RowlEngine_ProfileGetJson), nullptr, false);
        req(parsed.is_object(), "profile JSON did not parse");
        return parsed;
    }
    json state() const {
        json parsed = json::parse(capiText(h, RowlEngine_GetPlayerDriverStateJsonUtf8), nullptr, false);
        req(parsed.is_object(), "driver state JSON did not parse");
        return parsed;
    }
    void cmd(int command, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_PauseMenuCommand(h, command);
    }
    void selectRow(int row) const {
        for (int i = 0; i < 16 && menu()["selected"].get<int>() != row; ++i) cmd(ROWL_PAUSE_MENU_DOWN);
        req(menu()["selected"].get<int>() == row, "could not select row " + std::to_string(row));
    }
    void step(float dt, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_Step(h, dt);
    }
    void key(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        step(0.016f);
    }
    void keyUp(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::KeyUp;
        event.key = sdlKey;
        host->input.push_back(event);
        step(0.016f);
    }
    // Row center in the virtual canvas: x picks the minus / middle / plus zone.
    void click(int row, float x) const {
        // The page's own row geometry (D05-2: Preferences uses the compact pitch).
        const int rowCount = static_cast<int>(menu()["rows"].size());
        engine->pauseMenuClick(x, LM::rowTop(row, rowCount) + LM::rowH(rowCount) * 0.5f);
    }
    std::string frame(int steps = 3) const {
        step(0.016f, steps);
        uint32_t w = 0, hh = 0;
        const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
        req(pixels != nullptr && w == kWidth && hh == kHeight, "offscreen pixel buffer");
        return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
    }
};

// Pixels that differ between two frames, split into the top-right badge strip
// (x >= 85% of the width, y < 12% of the height) and everything else.
struct Diff {
    size_t badge = 0;
    size_t elsewhere = 0;
};

Diff diff(const std::string& a, const std::string& b) {
    req(a.size() == b.size() && a.size() == size_t(kWidth) * kHeight * 4, "frame size");
    Diff out;
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const size_t at = (size_t(y) * kWidth + x) * 4;
            if (a.compare(at, 4, b, at, 4) == 0) continue;
            const bool inBadge = x >= kWidth * 85 / 100 && y < kHeight * 12 / 100;
            (inBadge ? out.badge : out.elsewhere)++;
        }
    }
    return out;
}

std::string delayText(const json& menu) { return menu["rows"][5]["value"].get<std::string>(); }
std::string skipText(const json& menu) { return menu["rows"][6]["value"].get<std::string>(); }

}  // namespace

void test_auto_skip_ui() {
    TEST_SECTION("Auto / Skip UI: Preferences rows + AUTO/SKIP badges (D04-2)");

    const fs::path root = fs::temp_directory_path() / "rowl_auto_skip_ui";
    std::error_code ec;
    fs::remove_all(root, ec);
    // Two games (= two profiles): the preference part runs Skip over the
    // story, the badge part needs lines nobody has read.
    const fs::path game = root / "game";
    writeBytes(game / "project.rowlproj",
               R"({"project_uuid":"7d3f1b75-7d3e-4e8f-8b21-1d3c8e6f9a77","name":"Auto Skip UI"})");
    writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph());
    const fs::path game2 = root / "game2";
    writeBytes(game2 / "project.rowlproj",
               R"({"project_uuid":"7d3f1b75-7d3e-4e8f-8b21-1d3c8e6f9a78","name":"Auto Skip UI Badges"})");
    writeBytes(game2 / "Assets" / "json" / "full_story_graph.json", buildGraph());

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    {
        Rig rig;
        rig.h = RowlEngine_Create();
        req(rig.h && RowlEngine_Init(rig.h, kWidth, kHeight, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(rig.h, game.string().c_str());
        std::error_code mk;
        fs::create_directories(capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8), mk);
        rig.engine = Rowl::Core::testEngineFromHandle(rig.h);
        req(rig.engine != nullptr, "no engine behind the handle");
        const auto context = rig.engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        rig.host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(rig.host);
        RowlEngineHandle h = rig.h;
        RowlEngine_SetPlayState(h, 1);
        rig.step(0.016f);

        // ── 1. Preferences page shape ──────────────────────────────────────
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "OpenTitleScreen failed");
        rig.selectRow(3);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        {
            const json m = rig.menu();
            req(m["mode"].get<int>() == 4, "Tercihler is mode 4");
            const json& rows = m["rows"];
            req(rows.size() == 12 && LM::kPreferenceRows == 12, "12 preference rows: " + m.dump());
            const char* labels[] = {"Ana Ses", "Müzik", "SFX", "Seslendirme", "Metin Hızı",
                                    "Otomatik Hızı", "Atlama", "Dil", "Metin Boyutu",
                                    "Yüksek Kontrast", "Hareketi Azalt", "Geri"};
            for (int i = 0; i < 12; ++i) {
                req(rows[i]["label"] == labels[i], std::string("row ") + std::to_string(i) + " label");
            }
            req(delayText(m) == "2.0 sn", "default delay row: " + delayText(m));
            req(skipText(m) == "Okunan", "default skip row: " + skipText(m));
            req(rows[11]["value"] == "" && rows[0]["value"] == "100%", "Geri has no value; mixer rows unchanged");
        }
        TEST_PASS("Preferences lists 12 rows: seven old ones in place, then Dil, Metin Boyutu, Yuksek Kontrast, Hareketi Azalt, Geri");

        // ── 2. Otomatik Hizi ───────────────────────────────────────────────
        rig.selectRow(5);
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(delayText(rig.menu()) == "2.5 sn", "Right adds 0.5 s: " + delayText(rig.menu()));
        req(std::fabs(rig.state()["auto_delay"].get<double>() - 2.5) < 1e-4, "engine delay follows the row");
        req(rig.profile()["auto_advance_delay"].get<double>() == 2.5, "profile delay: " + rig.profile().dump());
        rig.cmd(ROWL_PAUSE_MENU_LEFT, 2);
        req(delayText(rig.menu()) == "1.5 sn" && rig.profile()["auto_advance_delay"].get<double>() == 1.5,
            "Left twice: 1.5");
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);  // Enter steps like Right
        req(delayText(rig.menu()) == "2.0 sn", "Enter steps the delay like Right");
        rig.cmd(ROWL_PAUSE_MENU_LEFT, 6);
        req(delayText(rig.menu()) == "0.0 sn" && rig.state()["auto_delay"].get<double>() == 0.0 &&
                rig.profile()["auto_advance_delay"].get<double>() == 0.0,
            "delay floors at 0");
        rig.cmd(ROWL_PAUSE_MENU_RIGHT, 25);
        req(delayText(rig.menu()) == "10.0 sn" && rig.state()["auto_delay"].get<double>() == 10.0 &&
                rig.profile()["auto_advance_delay"].get<double>() == 10.0,
            "delay caps at 10");
        rig.cmd(ROWL_PAUSE_MENU_LEFT, 4);
        req(delayText(rig.menu()) == "8.0 sn", "back from the cap");
        // The other value rows keep their own behaviour and indexes.
        rig.selectRow(4);
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(rig.profile()["text_speed_multiplier"].get<double>() == 1.25 &&
                rig.profile()["auto_advance_delay"].get<double>() == 8.0,
            "Metin Hizi still works and does not touch the delay");
        TEST_PASS("Otomatik Hizi: +-0.5 s, [0,10] clamp, Enter = Right, engine + profile follow");

        // ── 3. Atlama ──────────────────────────────────────────────────────
        const auto expectSkip = [&](const char* text, const char* policy, const char* mode) {
            req(skipText(rig.menu()) == text, std::string("skip row shows ") + text + ": " + skipText(rig.menu()));
            req(rig.state()["skip_policy"] == policy, std::string("engine policy ") + policy);
            req(rig.profile()["skip_mode"] == mode, std::string("profile skip_mode ") + mode + ": " + rig.profile().dump());
        };
        rig.selectRow(6);
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectSkip("Hepsi", "all", "all");
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectSkip("Kapalı", "off", "off");
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectSkip("Okunan", "read_only", "read_only");
        rig.cmd(ROWL_PAUSE_MENU_LEFT);
        expectSkip("Kapalı", "off", "off");
        rig.cmd(ROWL_PAUSE_MENU_LEFT);
        expectSkip("Hepsi", "all", "all");
        rig.cmd(ROWL_PAUSE_MENU_LEFT);
        expectSkip("Okunan", "read_only", "read_only");
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);  // Enter = Right
        expectSkip("Hepsi", "all", "all");
        TEST_PASS("Atlama cycles Okunan -> Hepsi -> Kapali -> Okunan (Left reverses); engine + profile agree");

        // Policy is live in the story: Kapali refuses S / Ctrl, Hepsi starts them.
        rig.selectRow(6);
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);  // Kapali
        expectSkip("Kapalı", "off", "off");
        rig.cmd(ROWL_PAUSE_MENU_BACK);   // title root
        RowlEngine_SetPaused(h, 0);      // host override closes the title
        req(RowlEngine_IsPaused(h) == 0, "menu closed");
        rig.key(SDLK_S);
        req(rig.state()["skip_active"] == false, "S under Kapali does not start Skip");
        rig.key(SDLK_LCTRL);
        req(rig.state()["skip_active"] == false, "Ctrl under Kapali does not start Skip");
        rig.keyUp(SDLK_LCTRL);
        RowlEngine_SetSkipPolicy(h, 2);
        rig.key(SDLK_S);
        req(rig.state()["skip_active"] == true, "S under Hepsi starts Skip");
        rig.key(SDLK_S);
        req(rig.state()["skip_active"] == false, "S again stops it");
        TEST_PASS("Kapali blocks S / Ctrl, Hepsi allows them");

        // ── 4. Mouse + Geri ────────────────────────────────────────────────
        RowlEngine_SetSkipPolicy(h, 1);
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title");
        rig.selectRow(3);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.menu()["mode"].get<int>() == 4, "preferences again");
        const std::string delayBefore = delayText(rig.menu());
        rig.click(5, 1200.0f);  // plus zone: selects the row and adds 0.5 s at once
        req(rig.menu()["selected"].get<int>() == 5 && delayText(rig.menu()) == "8.5 sn",
            "plus-zone click steps the delay: " + delayBefore + " -> " + delayText(rig.menu()));
        rig.click(5, 700.0f);
        req(delayText(rig.menu()) == "8.0 sn", "minus-zone click steps it back");
        rig.click(6, 960.0f);  // middle band of an unselected value row: selects only
        req(rig.menu()["selected"].get<int>() == 6 && skipText(rig.menu()) == "Okunan", "middle band selects only");
        rig.click(6, 1200.0f);
        req(skipText(rig.menu()) == "Hepsi" && rig.profile()["skip_mode"] == "all", "plus-zone click cycles the skip row");
        rig.click(6, 700.0f);
        req(skipText(rig.menu()) == "Okunan", "minus-zone click cycles back");
        rig.click(11, 960.0f);  // Geri
        {
            const json m = rig.menu();
            req(m["mode"].get<int>() == 3 && m["title_active"].get<bool>() && m["selected"].get<int>() == 3,
                "clicking Geri returns to the title on Tercihler: " + m.dump());
        }
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        rig.selectRow(11);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.menu()["mode"].get<int>() == 3, "Enter on Geri returns to the title");
        TEST_PASS("Mouse: zones step the new rows, middle band selects, Geri clicks back to the title");

        // ── 5. Pause main page: D05-2 trimmed it to 7 rows ─────────────────
        RowlEngine_SetPaused(h, 0);
        RowlEngine_SetPaused(h, 1);
        {
            const json m = rig.menu();
            req(m["mode"].get<int>() == 0 && m["rows"].size() == 7 && m["rows"][3]["label"] == "Tercihler" &&
                    m["rows"][4]["label"] == "Geçmiş",
                "pause main page has 7 rows, Tercihler at 3: " + m.dump());
        }
        RowlEngine_SetPaused(h, 0);
        TEST_PASS("Pause main page: 7 rows (mixer / speed rows moved behind Tercihler)");

    }

    // ── 6. Badges (fresh profile: every line is unread) ──────────────────────
    {
        Rig rig;
        rig.h = RowlEngine_Create();
        req(rig.h && RowlEngine_Init(rig.h, kWidth, kHeight, 0) == 1, "engine init failed (badges)");
        RowlEngine_SetProjectDirectory(rig.h, game2.string().c_str());
        std::error_code mk;
        fs::create_directories(capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8), mk);
        rig.engine = Rowl::Core::testEngineFromHandle(rig.h);
        req(rig.engine != nullptr, "no engine behind the handle (badges)");
        const auto context = rig.engine->getContext();
        rig.host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(rig.host);
        RowlEngineHandle h = rig.h;
        RowlEngine_SetPlayState(h, 1);
        rig.step(0.016f);
        RowlEngine_SetAutoDelay(h, 10.0f);  // Auto must never advance during the test
        RowlEngine_SetSkipPolicy(h, 1);
        rig.step(0.1f, 20);  // Auto's wait is >= 10 s: nothing advances meanwhile
        // The very first render of a line differs from every later re-render (the
        // speaker label is brighter), independent of any capsule. Force one
        // re-render so the baseline is the steady-state story frame.
        RowlEngine_SetPaused(h, 1);
        rig.frame(3);
        RowlEngine_SetPaused(h, 0);
        const std::string story = rig.frame();
        req(RowlEngine_GetCurrentNodeId(h) == 1, "the test story is on node 1");

        rig.key(SDLK_A);
        req(rig.state()["auto"] == true, "A turns Auto on");
        const std::string autoOn = rig.frame();
        {
            const Diff d = diff(story, autoOn);
            req(d.badge > 200, "AUTO capsule pixels appear top-right: " + std::to_string(d.badge));
            req(d.elsewhere == 0, "nothing outside the badge strip changes: " + std::to_string(d.elsewhere));
        }
        req(rig.frame(6) == autoOn, "the AUTO frame is stable over cached frames");
        rig.key(SDLK_A);
        req(rig.state()["auto"] == false, "A turns Auto off");
        req(rig.frame() == story, "Auto off: the clean story frame returns bit for bit (no stale capsule)");
        req(rig.frame(8) == story, "...and stays clean over cached frames");
        // Rapid toggling leaves no ghost either.
        for (int i = 0; i < 4; ++i) {
            rig.key(SDLK_A);
            rig.frame(1);
            rig.key(SDLK_A);
        }
        req(rig.frame() == story, "toggle storm: story frame intact");

        // SKIP: Ctrl held on an unread line shows the capsule without moving the story.
        rig.key(SDLK_LCTRL);
        req(RowlEngine_GetCurrentNodeId(h) == 1, "read_only Ctrl on an unread line does not advance");
        const std::string skipOn = rig.frame();
        {
            const Diff d = diff(story, skipOn);
            req(d.badge > 200 && d.elsewhere == 0, "SKIP capsule top-right only: " +
                                                       std::to_string(d.badge) + "/" + std::to_string(d.elsewhere));
            req(diff(autoOn, skipOn).badge > 0, "SKIP and AUTO capsules look different");
        }
        rig.key(SDLK_A);  // both
        const std::string both = rig.frame();
        {
            const Diff d = diff(skipOn, both);
            req(d.badge > 200 && d.elsewhere == 0, "AUTO appears next to SKIP");
            req(diff(autoOn, both).badge > 0, "both differ from AUTO alone");
        }
        rig.keyUp(SDLK_LCTRL);
        req(rig.frame() == autoOn, "releasing Ctrl leaves exactly the AUTO frame");
        rig.key(SDLK_A);
        req(rig.frame() == story, "all off: clean story frame");

        // Menus hide the capsules: the frames equal the capsule-free menus.
        // Pause menu.
        RowlEngine_SetPaused(h, 1);
        const std::string pausedClean = rig.frame(4);
        RowlEngine_SetPaused(h, 0);
        RowlEngine_SetAutoMode(h, 1);
        rig.key(SDLK_LCTRL);
        req(diff(story, rig.frame()).badge > 200, "both capsules up before pausing");
        RowlEngine_SetPaused(h, 1);
        req(rig.frame(4) == pausedClean, "pause menu: no capsule under the overlay");
        RowlEngine_SetPaused(h, 0);
        rig.keyUp(SDLK_LCTRL);
        req(diff(story, rig.frame()).badge > 200, "capsule is back when the menu closes");
        // Backlog.
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open backlog");
        const std::string backlogAuto = rig.frame(4);
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        RowlEngine_SetAutoMode(h, 0);
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open backlog again");
        req(rig.frame(4) == backlogAuto, "backlog: no capsule");
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        // Title.
        RowlEngine_SetAutoMode(h, 1);
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "open title");
        const std::string titleAuto = rig.frame(4);
        RowlEngine_SetAutoMode(h, 0);
        req(rig.frame(4) == titleAuto, "title: no capsule");
        RowlEngine_SetPaused(h, 0);
        req(rig.frame() == story, "after the menus: clean story frame");
        TEST_PASS("AUTO/SKIP capsules: top-right only, no stale frame, hidden under pause / backlog / title");
    }

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(root, ec);
}
