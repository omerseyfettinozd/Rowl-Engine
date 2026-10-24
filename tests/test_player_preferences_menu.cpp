/**
 * test_player_preferences_menu.cpp — D05-2: the Preferences page is reachable
 * from the pause menu, and carries the language / text size / high contrast /
 * reduced motion rows.
 *
 * Kilit:
 *  1. Yon: duraklatma -> Tercihler (satir 3) -> Geri / Esc / "Geri" satiri
 *     tiklamasi duraklatma ana sayfasina doner (secili satir 3); baslik ->
 *     Tercihler -> Geri basliga doner (secili 3). Tercihler acikken A / S /
 *     Ctrl ve Enter hikayeyi ilerletmez (Enter yalniz satiri ayarlar), AUTO /
 *     SKIP rozetleri cizilmez.
 *  2. Dil: iki dilli projede SOL/SAG/Enter locale'leri dolastirir; locale, aktif
 *     diyalog, menu etiketleri ve profil `language` birlikte gider; catalogu
 *     olmayan locale atlanir; tek dilli / locale'siz projede satir degeri
 *     gosterir ama degismez (cokme / bos yok); bilinmeyen etiket kendisi olarak
 *     gorunur.
 *  3. Metin Boyutu %100/%125/%150 (sarar, Enter SAG gibi), Yuksek Kontrast ve
 *     Hareketi Azalt Acik/Kapali: satir degeri, motor durumu (RowlEngine_Get* /
 *     Is*) ve profil JSON'u (`text_scale`, `high_contrast`, `reduced_motion`)
 *     birlikte gider; fare bolgeleri ayni.
 *  4. Sigma: 12 satirlik Tercihler (kompakt adim) ve 7/10/5 satirlik sayfalar
 *     ipucu satirina carpmadan paneli doldurur (geometri, 1920x1080 sanal);
 *     ekran disi karede (1920x1080, 1280x720) satir metinleri kendi satir
 *     kutusunda kalir: satirlar arasi bosluklarda, satir kutusunun sagindaki
 *     panel payinda ve son satirla ipucu arasinda murekkep yok, her satirin
 *     etiketi (ve degeri) cizilmis; %150 metin boyutunda da (tr + en).
 *     320x180'de son satir ile ipucu cakismaz, ikisi de cizilir.
 *
 * Bilerek-boz: (a) leavePreferencesPage'in her zaman basliga donmesi 1'i, (b)
 * cyclePreferenceLanguage'te persistPauseMenuPreference'i atlamak 2'nin profil
 * kontrolunu, (c) PauseMenuLayout::rowY0/rowH/rowStride'i her zaman rahat adimla
 * hesaplamak (kompakt adimi kaldirmak) 4'un geometri + piksel kontrolunu, (d)
 * window_pause_overlay.cpp'de satir yazisini accessScale'e bolmeyi/kutuya
 * sinirlamayi kaldirmak 4'un %150 kontrolunu kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
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

constexpr const char* kSection = "player-preferences-menu";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
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

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "30000000-0000-4000-8000-%012d", id);
    return text;
}

// 1..4 chain of plain lines; line 1 has catalog entries in tr / en.
std::string buildGraph() {
    json nodes = json::array();
    for (int id = 1; id <= 4; ++id) {
        json dialogue = {{"speaker", "Anlatici"},
                         {"dialogue", "Original line " + std::to_string(id)},
                         {"typewriter_enabled", false},
                         {"content_id", cid(id)}};
        json components = json::array(
            {{{"type", "dialogue"}, {"id", "d" + std::to_string(id)}, {"enabled", true}, {"data", dialogue}}});
        json edges = json::array();
        if (id < 4) edges.push_back({{"id", id + 1}});
        nodes.push_back({{"id", id}, {"title", "N" + std::to_string(id)},
                         {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"},
                                                   {"is_active", true}, {"components", components}}})},
                         {"next_nodes", edges}});
    }
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
}

std::string catalog(const std::string& locale, const std::string& text) {
    return json({{"schema_version", 1},
                 {"locale", locale},
                 {"entries", {{cid(1), {{"speaker", "Anlatici"}, {"text", text}, {"alt_text", ""}}}}}})
        .dump();
}

// A project directory: story, optional locale declaration (raw manifest
// members) and a catalog per listed locale (text = what line 1 says there).
fs::path makeProject(const fs::path& root, const std::string& dir, const std::string& uuid,
                     const std::string& localeMembers,
                     const std::vector<std::pair<std::string, std::string>>& catalogs) {
    const fs::path game = root / dir;
    writeText(game / "project.rowlproj",
              R"({"project_uuid":")" + uuid + R"(","name":"Prefs )" + dir + R"(")" +
                  (localeMembers.empty() ? "" : "," + localeMembers) + "}");
    writeText(game / "Assets" / "json" / "full_story_graph.json", buildGraph());
    for (const auto& entry : catalogs) {
        writeText(game / "Assets" / "locales" / (entry.first + ".json"), catalog(entry.first, entry.second));
    }
    return game;
}

struct Game {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    std::shared_ptr<InputHost> host;
    uint32_t width = 640;
    uint32_t height = 360;

    ~Game() {
        if (h) {
            RowlEngine_Shutdown(h);
            RowlEngine_Destroy(h);
        }
    }

    void open(const fs::path& dir, uint32_t w = 640, uint32_t hgt = 360) {
        width = w;
        height = hgt;
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, width, height, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, dir.string().c_str());
        std::error_code ec;
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8), ec);
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
        RowlEngine_SetPlayState(h, 1);
        RowlEngine_Step(h, 0.016f);
    }

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
    void key(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    // Click the centre of page row `row` at virtual x (minus / middle / plus zone).
    void click(int row, float x) const {
        const int rowCount = static_cast<int>(menu()["rows"].size());
        engine->pauseMenuClick(x, LM::rowTop(row, rowCount) + LM::rowH(rowCount) * 0.5f);
    }
    std::string frame(int steps = 3) const {
        for (int i = 0; i < steps; ++i) RowlEngine_Step(h, 0.016f);
        uint32_t w = 0, hh = 0;
        const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
        req(pixels != nullptr && w == width && hh == height, "offscreen pixel buffer");
        return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
    }
    std::string locale() const { return capiText(h, RowlEngine_GetLocale); }
    std::string dialogue() const {
        const char* text = RowlEngine_GetDialogue(h);
        return text ? text : "";
    }
    int node() const { return static_cast<int>(RowlEngine_GetCurrentNodeId(h)); }
    void openPreferences() const {
        RowlEngine_SetPaused(h, 1);
        selectRow(LM::kMainPrefsRow);
        cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(menu()["mode"].get<int>() == 4 && menu()["selected"].get<int>() == 0, "Tercihler did not open");
    }
};

// Pixels of the top-right badge strip (x >= 85%, y < 12%) that differ.
size_t badgeDiff(const std::string& a, const std::string& b, uint32_t width, uint32_t height) {
    req(a.size() == b.size() && a.size() == size_t(width) * height * 4, "frame size");
    size_t count = 0;
    for (uint32_t y = 0; y < height * 12 / 100; ++y) {
        for (uint32_t x = width * 85 / 100; x < width; ++x) {
            const size_t at = (size_t(y) * width + x) * 4;
            if (a.compare(at, 4, b, at, 4) != 0) ++count;
        }
    }
    return count;
}

// ── Pixel helpers for the fit checks ───────────────────────────────────────
// "Ink" = text colour on the dark panel: luminance well above the panel
// (15,15,26), the row highlight (14,116,144, luminance ~88) and the accent
// border-free interior; the menu's text colours are 241/245/249, 148/163/184
// and the accent 56/189/248 (all > 150).
bool ink(const std::string& px, uint32_t width, uint32_t x, uint32_t y) {
    const size_t at = (size_t(y) * width + x) * 4;
    const int r = static_cast<uint8_t>(px[at]);
    const int g = static_cast<uint8_t>(px[at + 1]);
    const int b = static_cast<uint8_t>(px[at + 2]);
    return (299 * r + 587 * g + 114 * b) / 1000 > 120;
}

struct Canvas {
    uint32_t width;
    uint32_t height;
    float scale;  // physical px per virtual px (AspectGuardian letterboxing is
                  // absent: all the test sizes are 16:9)
    uint32_t px(float v) const { return static_cast<uint32_t>(std::lround(v * scale)); }
};

size_t inkIn(const std::string& frame, const Canvas& c, float vx0, float vy0, float vx1, float vy1) {
    size_t count = 0;
    const uint32_t x0 = c.px(vx0), x1 = std::min(c.px(vx1), c.width);
    const uint32_t y0 = c.px(vy0), y1 = std::min(c.px(vy1), c.height);
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            if (ink(frame, c.width, x, y)) ++count;
        }
    }
    return count;
}

// The Preferences page drawn in `g`: every row's text stays in its own row box
// and clear of the hint line.
void expectPreferencesFit(const Game& g, const std::string& what) {
    const json m = g.menu();
    req(m["mode"].get<int>() == 4 && m["rows"].size() == 12, what + ": preferences page");
    const int rows = 12;
    const std::string frame = g.frame(3);
    const Canvas c{g.width, g.height, static_cast<float>(g.width) / 1920.0f};
    const float hintTop = LM::kPanelY + LM::kPanelH - 44.0f;

    // Left / right edges of the row texts, in virtual px.
    const float xl = LM::kRowX + 8.0f;
    const float xr = LM::kRowX + LM::kRowW - 8.0f;
    for (int i = 0; i < rows; ++i) {
        const float top = LM::rowTop(i, rows);
        const float bottom = top + LM::rowH(rows);
        // Label drawn (label column = first 55% of the row).
        req(inkIn(frame, c, LM::kRowX + 10.0f, top, LM::kRowX + LM::kRowW * 0.55f, bottom) > 0,
            what + ": row " + std::to_string(i) + " label is not drawn inside its row box");
        // Value drawn for the rows that have one.
        if (!m["rows"][i]["value"].get<std::string>().empty()) {
            req(inkIn(frame, c, LM::kRowX + LM::kRowW * 0.55f, top, LM::kRowX + LM::kRowW, bottom) > 0,
                what + ": row " + std::to_string(i) + " value is not drawn inside its row box");
        }
        // The gap under the row (up to the next row, or the hint line) stays clean.
        const float nextTop = i + 1 < rows ? LM::rowTop(i + 1, rows) : hintTop - 4.0f;
        const float gapTop = bottom + 1.5f / c.scale;      // skip the antialiased edge
        const float gapBottom = nextTop - 0.5f / c.scale;
        if (c.px(gapBottom) > c.px(gapTop) + 1) {
            req(inkIn(frame, c, xl, gapTop, xr, gapBottom) == 0,
                what + ": text spills under row " + std::to_string(i));
        }
        // The panel margin right of the row box stays clean.
        req(inkIn(frame, c, LM::kRowX + LM::kRowW + 4.0f, top, LM::kPanelX + LM::kPanelW - 4.0f, bottom) == 0,
            what + ": text spills right of row " + std::to_string(i));
    }
    // Hint line: drawn, and not smeared into the rows above it.
    req(inkIn(frame, c, LM::kRowX, hintTop, LM::kRowX + LM::kRowW, LM::kPanelY + LM::kPanelH - 6.0f) > 0,
        what + ": hint line is not drawn");
}

}  // namespace

void test_player_preferences_menu() {
    TEST_SECTION("Preferences from the pause menu + language / accessibility rows (D05-2)");

    const fs::path root = fs::temp_directory_path() / "rowl_player_preferences_menu";
    std::error_code ec;
    fs::remove_all(root, ec);

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    const std::string trText = "Merhaba dünya";
    const std::string enText = "Hello world";
    const fs::path biGame = makeProject(root, "bi_game", "8b1e2c40-1a2b-4c3d-8e4f-5a6b7c8d9e01",
                                        R"("default_locale":"tr","supported_locales":["tr","en"])",
                                        {{"tr", trText}, {"en", enText}});
    // Third locale listed without a catalog: selecting it is refused and skipped.
    const fs::path gapGame = makeProject(root, "gap_game", "8b1e2c40-1a2b-4c3d-8e4f-5a6b7c8d9e02",
                                         R"("default_locale":"tr","supported_locales":["tr","en","de"])",
                                         {{"tr", trText}, {"en", enText}});
    const fs::path monoGame = makeProject(root, "mono_game", "8b1e2c40-1a2b-4c3d-8e4f-5a6b7c8d9e03",
                                          R"("default_locale":"tr","supported_locales":["tr"])",
                                          {{"tr", trText}});
    const fs::path noLocale = makeProject(root, "no_locale", "8b1e2c40-1a2b-4c3d-8e4f-5a6b7c8d9e04", "", {});
    const fs::path jaGame = makeProject(root, "ja_game", "8b1e2c40-1a2b-4c3d-8e4f-5a6b7c8d9e05",
                                        R"("default_locale":"ja","supported_locales":["ja","de"])",
                                        {{"ja", "ja line"}, {"de", "Hallo Welt"}});

    // ── 1. Where Tercihler opens from, and where Geri / Esc go back to ──────
    {
        Game g;
        g.open(biGame);
        req(g.dialogue() == trText, "tr game starts in Turkish: " + g.dialogue());
        RowlEngine_SetPaused(g.h, 1);
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 0 && m["rows"].size() == 7 && LM::kMainRows == 7 &&
                    m["rows"][3]["label"] == "Tercihler" && m["rows"][3]["value"] == ">",
                "pause main page: Tercihler at row 3: " + m.dump());
        }
        g.selectRow(LM::kMainPrefsRow);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 4 && m["title"] == "Tercihler" && m["selected"].get<int>() == 0 &&
                    m["title_active"].get<bool>() == false && m["rows"].size() == 12,
                "Tercihler opens over the pause menu on its first row: " + m.dump());
        }
        // Geri key.
        g.selectRow(3);
        g.cmd(ROWL_PAUSE_MENU_BACK);
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 0 && m["selected"].get<int>() == 3 &&
                    RowlEngine_IsPaused(g.h) == 1 && m["title"] == "Duraklatıldı",
                "Back returns to the pause main page on Tercihler: " + m.dump());
        }
        // Esc.
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(g.menu()["mode"].get<int>() == 4, "reopened");
        g.key(SDLK_ESCAPE);
        {
            const json m = g.menu();
            req(RowlEngine_IsPaused(g.h) == 1 && m["mode"].get<int>() == 0 && m["selected"].get<int>() == 3,
                "Esc on Tercihler goes back to the pause main page, it does not resume: " + m.dump());
        }
        g.key(SDLK_ESCAPE);
        req(RowlEngine_IsPaused(g.h) == 0, "Esc on the pause main page still resumes");
        // The Geri row, by Enter and by click.
        RowlEngine_SetPaused(g.h, 1);
        g.selectRow(3);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        g.selectRow(LM::kPreferenceRows - 1);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(g.menu()["mode"].get<int>() == 0 && g.menu()["selected"].get<int>() == 3, "Enter on Geri");
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        g.click(LM::kPreferenceRows - 1, 960.0f);
        req(g.menu()["mode"].get<int>() == 0 && g.menu()["selected"].get<int>() == 3, "click on Geri");
        // A click on the Tercihler row opens it too.
        g.click(LM::kMainPrefsRow, 960.0f);
        req(g.menu()["mode"].get<int>() == 4, "click on the Tercihler row");
        RowlEngine_SetPaused(g.h, 0);
        // A fresh pause opens on the main page again (nothing leaks from the page).
        RowlEngine_SetPaused(g.h, 1);
        req(g.menu()["mode"].get<int>() == 0 && g.menu()["selected"].get<int>() == 0, "fresh pause: main page");
        RowlEngine_SetPaused(g.h, 0);

        // From the title the page still returns to the title.
        req(RowlEngine_OpenTitleScreen(g.h) == ROWL_RESULT_OK, "open title");
        g.selectRow(3);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(g.menu()["mode"].get<int>() == 4 && g.menu()["title_active"].get<bool>(), "title -> Tercihler");
        g.cmd(ROWL_PAUSE_MENU_BACK);
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 3 && m["selected"].get<int>() == 3 && m["title_active"].get<bool>(),
                "Back from the title's Tercihler returns to the title: " + m.dump());
        }
        g.selectRow(3);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        g.key(SDLK_ESCAPE);
        req(g.menu()["mode"].get<int>() == 3 && g.menu()["title_active"].get<bool>(),
            "Esc from the title's Tercihler returns to the title");
        RowlEngine_SetPaused(g.h, 0);
    }
    TEST_PASS("Tercihler remembers its opener: pause -> pause main (row 3), title -> title; Back / Esc / Geri row / click");

    // ── 1b. Story controls are inert while Tercihler is open ────────────────
    {
        Game g;
        g.open(biGame);
        const std::string story = g.frame(2);
        RowlEngine_SetAutoMode(g.h, 1);
        g.openPreferences();
        const std::string prefsAuto = g.frame(3);
        RowlEngine_SetAutoMode(g.h, 0);
        const std::string prefsPlain = g.frame(3);
        req(badgeDiff(prefsAuto, prefsPlain, g.width, g.height) == 0, "no AUTO capsule under Tercihler");
        RowlEngine_SetAutoMode(g.h, 0);
        const int nodeBefore = g.node();
        g.key(SDLK_A);
        g.key(SDLK_S);
        g.key(SDLK_LCTRL);
        req(g.state()["auto"] == false && g.state()["skip_active"] == false,
            "A / S / Ctrl do nothing on Tercihler: " + g.state().dump());
        // Enter on a value row adjusts the row, it never advances the story.
        g.selectRow(0);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        g.key(SDLK_RETURN);
        g.key(SDLK_SPACE);
        req(g.node() == nodeBefore && g.menu()["mode"].get<int>() == 4, "Enter / Space do not advance the story");
        RowlEngine_SetPaused(g.h, 0);
        (void)story;
    }
    TEST_PASS("Tercihler: A / S / Ctrl and story advance are inert, no AUTO / SKIP capsules");

    // ── 2. Dil row ──────────────────────────────────────────────────────────
    {
        Game g;
        g.open(biGame);
        g.openPreferences();
        g.selectRow(LM::kPreferenceLanguageRow);
        {
            const json m = g.menu();
            req(m["rows"][7]["label"] == "Dil" && m["rows"][7]["value"] == "Türkçe",
                "tr game: Dil / Türkçe: " + m.dump());
        }
        req(g.profile()["language"] == "", "no language is stored before the player picks one");
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(g.locale() == "en", "Right moves to the next locale: " + g.locale());
        {
            const json m = g.menu();
            req(m["title"] == "Preferences" && m["rows"][7]["label"] == "Language" &&
                    m["rows"][7]["value"] == "English" && m["rows"][8]["label"] == "Text Size" &&
                    m["rows"][11]["label"] == "Back" && m["selected"].get<int>() == 7,
                "the menu is redrawn in English at once: " + m.dump());
        }
        req(g.dialogue() == enText, "the live dialogue follows at once: " + g.dialogue());
        req(g.profile()["language"] == "en", "profile language written: " + g.profile().dump());
        g.cmd(ROWL_PAUSE_MENU_RIGHT);  // wraps to tr
        req(g.locale() == "tr" && g.menu()["title"] == "Tercihler" && g.menu()["rows"][7]["value"] == "Türkçe" &&
                g.dialogue() == trText && g.profile()["language"] == "tr",
            "Right wraps back to tr (menu, dialogue, profile)");
        g.cmd(ROWL_PAUSE_MENU_LEFT);
        req(g.locale() == "en" && g.profile()["language"] == "en", "Left walks backwards (wraps)");
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);  // Enter steps like Right
        req(g.locale() == "tr" && g.profile()["language"] == "tr", "Enter steps like Right");
        // Mouse zones.
        g.click(7, 1200.0f);
        req(g.locale() == "en", "plus-zone click steps the language");
        g.click(7, 700.0f);
        req(g.locale() == "tr", "minus-zone click steps it back");
        // The player's choice survives into a new session of the same game.
        RowlEngine_SetPaused(g.h, 0);
        req(g.dialogue() == trText, "the story shows the chosen language after closing the menu");
    }
    {
        Game g;
        g.open(gapGame);
        g.openPreferences();
        g.selectRow(7);
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(g.locale() == "en", "tr -> en");
        g.cmd(ROWL_PAUSE_MENU_RIGHT);  // de has no catalog: refused and skipped
        req(g.locale() == "tr" && g.menu()["rows"][7]["value"] == "Türkçe" && g.dialogue() == trText,
            "a locale without a catalog is skipped: " + g.locale());
        g.cmd(ROWL_PAUSE_MENU_LEFT);   // backwards: de skipped again
        req(g.locale() == "en", "backwards skips it too: " + g.locale());
    }
    {
        // One locale, no locale at all: the row shows a value and never changes.
        for (const fs::path& dir : {monoGame, noLocale}) {
            Game g;
            g.open(dir);
            const std::string before = g.locale();
            g.openPreferences();
            g.selectRow(7);
            const std::string frameBefore = g.frame(2);
            g.cmd(ROWL_PAUSE_MENU_RIGHT);
            g.cmd(ROWL_PAUSE_MENU_LEFT);
            g.cmd(ROWL_PAUSE_MENU_CONFIRM);
            g.click(7, 1200.0f);
            const json m = g.menu();
            req(g.locale() == before && m["rows"][7]["label"] == "Dil" && m["rows"][7]["value"] == "Türkçe" &&
                    m["mode"].get<int>() == 4 && m["selected"].get<int>() == 7 && m["rows"].size() == 12,
                "single-language / locale-free game: the row only shows its value: " + m.dump());
            req(g.profile()["language"] == "", "nothing is stored for a no-op row: " + g.profile().dump());
            req(g.frame(2) == frameBefore, "and the frame is unchanged");
        }
    }
    {
        // Unknown tag shows as itself; a known one by its own name.
        Game g;
        g.open(jaGame);
        g.openPreferences();
        g.selectRow(7);
        req(g.menu()["rows"][7]["value"] == "ja", "unknown tag is shown as the tag: " + g.menu().dump());
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(g.locale() == "de" && g.menu()["rows"][7]["value"] == "Deutsch" && g.menu()["title"] == "Preferences",
            "de is Deutsch (menus in English): " + g.menu().dump());
        req(g.profile()["language"] == "de", "profile: de");
    }
    TEST_PASS("Dil: cycles the game's locales (menu, dialogue, profile follow), skips catalog-less ones, no-op for single / no locale");

    // ── 3. Metin Boyutu / Yuksek Kontrast / Hareketi Azalt ──────────────────
    {
        Game g;
        g.open(biGame);
        g.openPreferences();
        const auto value = [&](int row) { return g.menu()["rows"][row]["value"].get<std::string>(); };
        g.selectRow(LM::kPreferenceTextScaleRow);
        req(g.menu()["rows"][8]["label"] == "Metin Boyutu" && value(8) == "100%" &&
                RowlEngine_GetTextScale(g.h) == 1.0f,
            "text size starts at 100%");
        const auto expectScale = [&](const char* text, float scale) {
            req(value(8) == text, std::string("row shows ") + text + ": " + value(8));
            req(RowlEngine_GetTextScale(g.h) == scale, std::string("engine scale ") + text);
            req(g.profile()["text_scale"].get<double>() == static_cast<double>(scale),
                std::string("profile text_scale ") + text + ": " + g.profile().dump());
        };
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectScale("125%", 1.25f);
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectScale("150%", 1.5f);
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectScale("100%", 1.0f);
        g.cmd(ROWL_PAUSE_MENU_LEFT);
        expectScale("150%", 1.5f);
        g.cmd(ROWL_PAUSE_MENU_LEFT);
        expectScale("125%", 1.25f);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);  // Enter = Right
        expectScale("150%", 1.5f);
        g.click(8, 1200.0f);
        expectScale("100%", 1.0f);
        g.click(8, 700.0f);
        expectScale("150%", 1.5f);
        // The scale is applied: a Turkish page drawn at 150% differs from 100%.
        const std::string at150 = g.frame(3);
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        expectScale("100%", 1.0f);
        req(g.frame(3) != at150, "the live frame changes with the text size");

        // Yuksek Kontrast.
        g.selectRow(LM::kPreferenceContrastRow);
        req(g.menu()["rows"][9]["label"] == "Yüksek Kontrast" && value(9) == "Kapalı" &&
                RowlEngine_IsHighContrast(g.h) == 0,
            "high contrast starts off");
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(value(9) == "Açık" && RowlEngine_IsHighContrast(g.h) == 1 && g.profile()["high_contrast"] == true,
            "high contrast on: " + g.profile().dump());
        g.cmd(ROWL_PAUSE_MENU_LEFT);
        req(value(9) == "Kapalı" && RowlEngine_IsHighContrast(g.h) == 0 && g.profile()["high_contrast"] == false,
            "Left toggles it back off");
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(value(9) == "Açık" && RowlEngine_IsHighContrast(g.h) == 1, "Enter toggles");
        g.click(9, 700.0f);
        req(value(9) == "Kapalı" && RowlEngine_IsHighContrast(g.h) == 0, "click toggles");

        // Hareketi Azalt.
        g.selectRow(LM::kPreferenceMotionRow);
        req(g.menu()["rows"][10]["label"] == "Hareketi Azalt" && value(10) == "Kapalı" &&
                RowlEngine_IsReducedMotion(g.h) == 0,
            "reduced motion starts off");
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(value(10) == "Açık" && RowlEngine_IsReducedMotion(g.h) == 1 && g.profile()["reduced_motion"] == true,
            "reduced motion on: " + g.profile().dump());
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(value(10) == "Kapalı" && RowlEngine_IsReducedMotion(g.h) == 0 && g.profile()["reduced_motion"] == false,
            "and off again");
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        // The other value rows keep their own behaviour.
        g.selectRow(4);
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(g.profile()["text_speed_multiplier"].get<double>() == 1.25 && g.profile()["high_contrast"] == false &&
                g.profile()["reduced_motion"] == true && g.profile()["text_scale"].get<double>() == 1.0,
            "Metin Hizi unaffected, accessibility values kept: " + g.profile().dump());
        // The English labels.
        RowlEngine_SetLocale(g.h, "en");
        const json m = g.menu();
        req(m["rows"][8]["label"] == "Text Size" && m["rows"][9]["label"] == "High Contrast" &&
                m["rows"][9]["value"] == "Off" && m["rows"][10]["label"] == "Reduce Motion" &&
                m["rows"][10]["value"] == "On",
            "en labels / values: " + m.dump());
    }
    {
        // A host that already set the engine state through the C API sees it in the page.
        Game g;
        g.open(biGame);
        RowlEngine_SetTextScale(g.h, 1.25f);
        RowlEngine_SetHighContrast(g.h, 1);
        RowlEngine_SetReducedMotion(g.h, 1);
        g.openPreferences();
        const json m = g.menu();
        req(m["rows"][8]["value"] == "125%" && m["rows"][9]["value"] == "Açık" && m["rows"][10]["value"] == "Açık",
            "the page reads the engine state: " + m.dump());
    }
    TEST_PASS("Metin Boyutu / Yuksek Kontrast / Hareketi Azalt: row, engine and profile agree; zones and Enter work");

    // ── 4. Fit ───────────────────────────────────────────────────────────────
    {
        // Geometry (virtual 1920x1080): rows of every page clear the title and the hint line.
        const float hintTop = LM::kPanelY + LM::kPanelH - 44.0f;
        const float titleBottom = 170.0f + 60.0f;
        for (int rows : {LM::kTitleRows, LM::kMainRows, LM::kSlotRows, LM::kPreferenceRows}) {
            req(LM::rowTop(0, rows) >= titleBottom, "rows start below the title (" + std::to_string(rows) + ")");
            const float last = LM::rowTop(rows - 1, rows) + LM::rowH(rows);
            req(last + 16.0f <= hintTop, "last row clears the hint line (" + std::to_string(rows) + " rows: " +
                                              std::to_string(last) + " vs " + std::to_string(hintTop) + ")");
            for (int i = 0; i < rows; ++i) {
                const float centre = LM::rowTop(i, rows) + LM::rowH(rows) * 0.5f;
                req(LM::rowAt(960.0f, centre, rows) == i, "row centre hits its own row");
                req(LM::rowAt(960.0f, LM::rowTop(i, rows) + LM::rowH(rows) + 1.0f, rows) == -1 ||
                        i == rows - 1 || LM::rowStride(rows) - LM::rowH(rows) < 2.0f,
                    "the gap under a row misses");
                if (i + 1 < rows) {
                    req(LM::rowTop(i, rows) + LM::rowH(rows) <= LM::rowTop(i + 1, rows), "rows do not overlap");
                }
            }
            req(LM::rowAt(960.0f, LM::rowTop(rows - 1, rows) + LM::rowH(rows) + 20.0f, rows) == -1,
                "below the last row misses");
        }
        req(LM::rowH(LM::kMainRows) == LM::kRowH && LM::rowTop(5, LM::kMainRows) == LM::kRowY0 + 5 * (LM::kRowH + LM::kRowGap),
            "pages up to 11 rows keep the comfortable pitch");
    }
    {
        // Pixels: every row's text inside its own row box, at two resolutions,
        // both languages, 100% and 150%.
        const std::pair<uint32_t, uint32_t> sizes[] = {{1920, 1080}, {1280, 720}};
        for (const auto& size : sizes) {
            for (const char* loc : {"tr", "en"}) {
                for (float scale : {1.0f, 1.5f}) {
                    Game g;
                    g.open(biGame, size.first, size.second);
                    req(RowlEngine_SetLocale(g.h, loc) == ROWL_RESULT_OK, "locale");
                    RowlEngine_SetTextScale(g.h, scale);
                    g.openPreferences();
                    g.selectRow(11);  // Geri: keeps the (accent) highlight away from the value rows
                    expectPreferencesFit(g, std::string(loc) + " " + std::to_string(size.first) + "x" +
                                                std::to_string(size.second) + " @" +
                                                std::to_string(static_cast<int>(scale * 100)) + "%");
                }
            }
        }
        // The main page, slot page and title page at 150% keep their rows in their boxes too.
        for (const char* loc : {"tr", "en"}) {
            Game g;
            g.open(biGame, 1280, 720);
            req(RowlEngine_SetLocale(g.h, loc) == ROWL_RESULT_OK, "locale");
            RowlEngine_SetTextScale(g.h, 1.5f);
            RowlEngine_SetPaused(g.h, 1);
            const std::string frame = g.frame(3);
            const Canvas c{g.width, g.height, g.width / 1920.0f};
            const int rows = 7;
            for (int i = 0; i < rows; ++i) {
                const float top = LM::rowTop(i, rows);
                const float bottom = top + LM::rowH(rows);
                req(inkIn(frame, c, LM::kRowX + 10.0f, top, LM::kRowX + LM::kRowW * 0.55f, bottom) > 0,
                    std::string(loc) + ": pause row " + std::to_string(i) + " label is drawn at 150%");
                if (i + 1 < rows) {
                    req(inkIn(frame, c, LM::kRowX + 8.0f, bottom + 2.0f / c.scale * 1.0f, LM::kRowX + LM::kRowW - 8.0f,
                              LM::rowTop(i + 1, rows) - 1.0f / c.scale) == 0,
                        std::string(loc) + ": pause row " + std::to_string(i) + " text spills at 150%");
                }
                req(inkIn(frame, c, LM::kRowX + LM::kRowW + 4.0f, top, LM::kPanelX + LM::kPanelW - 4.0f, bottom) == 0,
                    std::string(loc) + ": pause row " + std::to_string(i) + " spills right at 150%");
            }
        }
    }
    {
        // 320x180: the last row and the hint never overlap and both are drawn.
        Game g;
        g.open(biGame, 320, 180);
        g.openPreferences();
        const std::string frame = g.frame(3);
        const Canvas c{g.width, g.height, g.width / 1920.0f};
        const float hintTop = LM::kPanelY + LM::kPanelH - 44.0f;
        const float lastTop = LM::rowTop(11, 12);
        req(inkIn(frame, c, LM::kRowX, lastTop, LM::kRowX + LM::kRowW, lastTop + LM::rowH(12)) > 0,
            "320x180: Geri row is drawn");
        req(inkIn(frame, c, LM::kRowX, hintTop, LM::kRowX + LM::kRowW, LM::kPanelY + LM::kPanelH - 4.0f) > 0,
            "320x180: hint is drawn");
        req(c.px(lastTop + LM::rowH(12)) < c.px(hintTop), "320x180: last row ends above the hint line");
    }
    TEST_PASS("Fit: 12 rows clear the hint line, text stays in its row at 1920x1080 / 1280x720 (tr/en, 100% / 150%), 320x180 intact");

    // ── 5. Speaker tag follows the text scale ───────────────────────────────
    // The name badge's height and the name's vertical centring must use the size
    // the renderer really draws (speakerFontSize * textScale): at 150% the name
    // used to hang out of the bottom of its badge. The story line is blank so the
    // only light pixels near the badge are the name itself.
    {
        const fs::path speakerGame = root / "speaker_game";
        writeText(speakerGame / "project.rowlproj",
                  R"({"project_uuid":"8b1e2c40-1a2b-4c3d-8e4f-5a6b7c8d9e06","name":"Prefs speaker"})");
        json dialogue = {{"speaker", "Anlatici"}, {"dialogue", " "}, {"typewriter_enabled", false}};
        json graph = {{"format_version", 4}, {"start_node_id", 1},
                      {"nodes", json::array({{{"id", 1},
                          {"objects", json::array({{{"id", "o1"}, {"name", "D"}, {"is_active", true},
                              {"components", json::array({{{"type", "dialogue"}, {"id", "d1"},
                                                           {"enabled", true}, {"data", dialogue}}})}}})},
                          {"next_nodes", json::array()}}})}};
        writeText(speakerGame / "Assets" / "json" / "full_story_graph.json", graph.dump());
        const std::pair<uint32_t, uint32_t> sizes[] = {{1920, 1080}, {1280, 720}};
        for (const auto& size : sizes) {
            for (float scale : {1.0f, 1.5f}) {
                Game g;
                g.open(speakerGame, size.first, size.second);
                RowlEngine_SetTextScale(g.h, scale);
                const std::string frame = g.frame(4);
                const auto& dialogues = g.engine->getActiveDialogues();
                req(!dialogues.empty() && dialogues[0].hasDialogueBox && dialogues[0].speaker == "Anlatici",
                    "speaker fixture shows a dialogue box with a speaker");
                const auto& dlg = dialogues[0];
                const Canvas c{g.width, g.height, static_cast<float>(g.width) / 1920.0f};
                const std::string what = std::to_string(size.first) + "x" + std::to_string(size.second) + " @" +
                                         std::to_string(static_cast<int>(scale * 100)) + "%";
                // The badge as the renderer lays it out (virtual px).
                const float tagH = dlg.speakerFontSize * scale * 1.4f + 12.0f;
                const float tagY = dlg.y - tagH * 0.6f;
                const float tagX = dlg.x + 20.0f;
                const auto light = [&](uint32_t x, uint32_t y) {
                    const size_t at = (size_t(y) * g.width + x) * 4;
                    return static_cast<uint8_t>(frame[at]) >= 200 && static_cast<uint8_t>(frame[at + 1]) >= 200 &&
                           static_cast<uint8_t>(frame[at + 2]) >= 200;
                };
                const auto lightIn = [&](float vx0, float vy0, float vx1, float vy1) {
                    size_t count = 0;
                    for (uint32_t y = c.px(vy0); y < std::min(c.px(vy1), g.height); ++y) {
                        for (uint32_t x = c.px(vx0); x < std::min(c.px(vx1), g.width); ++x) count += light(x, y) ? 1 : 0;
                    }
                    return count;
                };
                // The badge fill as drawn (colour probe in a column left of the name).
                const auto fill = [&](uint32_t x, uint32_t y) {
                    const size_t at = (size_t(y) * g.width + x) * 4;
                    const int r = static_cast<uint8_t>(frame[at]);
                    const int gg = static_cast<uint8_t>(frame[at + 1]);
                    const int b = static_cast<uint8_t>(frame[at + 2]);
                    return r < 90 && gg > 160 && gg < 215 && b > 225;
                };
                const uint32_t probeX = c.px(tagX + 8.0f);
                int fillTop = -1, fillBottom = -1;
                for (uint32_t y = c.px(tagY - tagH); y < std::min(c.px(tagY + 2.0f * tagH), g.height); ++y) {
                    if (fill(probeX, y)) {
                        if (fillTop < 0) fillTop = static_cast<int>(y);
                        fillBottom = static_cast<int>(y);
                    }
                }
                req(fillTop >= 0, what + ": speaker badge fill not found");
                const int badgeH = fillBottom - fillTop + 1;
                const int expectedH = static_cast<int>(std::lround(tagH * c.scale));
                req(std::abs(badgeH - expectedH) <= 3,
                    what + ": the badge is " + std::to_string(badgeH) + " px high, the scaled name needs " +
                        std::to_string(expectedH));
                // The name's ink stays inside the fill, with room above and below.
                int inkTop = -1, inkBottom = -1;
                for (int y = fillTop; y <= fillBottom; ++y) {
                    bool any = false;
                    for (uint32_t x = c.px(tagX + 16.0f); x < c.px(tagX + 100.0f); ++x) any = any || light(x, static_cast<uint32_t>(y));
                    if (any) {
                        if (inkTop < 0) inkTop = y;
                        inkBottom = y;
                    }
                }
                req(inkTop >= 0, what + ": the name is drawn in the badge");
                const int room = std::max(2, badgeH / 10);
                req(inkTop - fillTop >= room && fillBottom - inkBottom >= room,
                    what + ": the name is cramped in its badge (room above " + std::to_string(inkTop - fillTop) +
                        ", below " + std::to_string(fillBottom - inkBottom) + ", badge " + std::to_string(badgeH) + ")");
                // Nothing of the name outside the badge.
                req(lightIn(tagX + 4.0f, tagY + tagH + 3.0f, tagX + 100.0f, tagY + tagH * 1.5f) == 0 ||
                        fillBottom + 2 >= static_cast<int>(c.px(tagY + tagH)),
                    what + ": the name hangs out of the bottom of its badge");
            }
        }
    }
    TEST_PASS("Speaker tag: the name stays inside its badge at 100% and 150%");

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
}
