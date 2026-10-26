/**
 * test_menu_layout.cpp — T2b: every menu page's place, size and arrangement from the author's theme
 * (`screens.<page>.layout`) + the slot pages (save_slot_count, paging, card grid, number base).
 *
 * Kilit:
 *  1. Ayristirici: layout anahtarlari (panel/title/rows/hint/counter/thumbnail/slider/gallery/
 *     achievements/history/cards/slots/pager) okunur, yazilmayanlar JSON'a girmez, gidis-donus;
 *     bozuk hizalama / aralik disi sayi / nesne olmayan bolum -> tani.
 *  2. Varsayilan: dosyasiz tema her sayfada MenuLayout{} ve PauseMenuLayout statikleriyle ayni sayilar.
 *  3. Gecersiz yerleşim (ekran disi, sifir boyut, satirlar panele sigmiyor, 0 sutun) -> GetUiThemeJson tanisi
 *     + o sayfa varsayilan; baska sayfa etkilenmez.
 *  4. Tek kaynak: ozel yerlesimde her sayfanin CIZILEN satiri / hucresi / plakasi ile TIKLAMA isabeti ayni
 *     yerde (Tercihler satirlari, baslik menusu sol altta, galeri 3 ve 6 sutun, basarim satir yuksekligi),
 *     eski konum bos kalir; ipucu yeni konumda; panelsiz + dar ortali menu; onay sayfasi alttaki sayfanin
 *     satirlarini korur (ya da kendi yerlesimini alir); kaydirici izlegi ozel.
 *  5. Yuva sayfalari: save_slot_count (varsayilan 10, sayfa yok), sayfalama (Sol/Sag, sayfa seridi, tekerlek),
 *     0-9 gorunen sayfanin siralari, number_base 1 etiketleri + rakam esleme, kart duzeni (kart tiklama,
 *     Yukari/Asagi satir, Sol/Sag kart ve sayfa tasma), %150'de yuva satiri buyur ve sayfa kisalir (B-12).
 *  6. B-11: %150'de "Devam Et" degeri (saat dahil) sol yana buyuyerek gorunur; %100'de sutun bugunku gibi.
 *
 * Bilerek-boz: pauseMenuClick'te layout.rowAt yerine PauseMenuLayout::rowAt kullanmak 4'u; pencerede
 * LM.rowX yerine sabit kRowX 4'un cizim kontrolunu; resolveMenuLayouts'te rowX'i yoksaymak 4 ve 3'u;
 * slotAtRow'da sayfa ofsetini dusurmek 5'i; menuLayoutForTextScale'de rowH carpmasini kaldirmak 5'in %150 maddesini;
 * Window'daki B-11 genisletmesini kaldirmak 6'yi kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/menu_layout.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/state/ui_theme.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
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
using PL = Rowl::Core::PauseMenuLayout;
using Rowl::Core::MenuLayout;
using namespace Rowl::State;
using ResolvedLayouts = Rowl::Core::ResolvedMenuLayouts;

constexpr const char* kSection = "menu-layout";
constexpr uint32_t kW = 960;  // half of the 1920x1080 canvas: 1 physical px = 2 virtual px
constexpr uint32_t kH = 540;

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
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

// 24-bit BMP of one flat colour (stb_image decodes it): the gallery pictures.
std::string makeBmp(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    const int rowBytes = ((width * 3 + 3) / 4) * 4;
    const uint32_t pixelBytes = static_cast<uint32_t>(rowBytes) * static_cast<uint32_t>(height);
    std::string out;
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    const auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    out += "BM"; u32(54u + pixelBytes); u16(0); u16(0); u32(54);
    u32(40); u32(static_cast<uint32_t>(width)); u32(static_cast<uint32_t>(height)); u16(1); u16(24);
    u32(0); u32(pixelBytes); u32(2835); u32(2835); u32(0); u32(0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out.push_back(static_cast<char>(b));
            out.push_back(static_cast<char>(g));
            out.push_back(static_cast<char>(r));
        }
        out.append(static_cast<size_t>(rowBytes - width * 3), '\0');
    }
    return out;
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

int g_rigCounter = 0;
fs::path g_root;

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "40000000-0000-4000-8000-%012d", id);
    return text;
}

std::string buildGraph() {
    json nodes = json::array();
    for (int id = 1; id <= 3; ++id) {
        json dialogue = {{"type", "dialogue"}, {"id", "d" + std::to_string(id)}, {"enabled", true},
                         {"data", {{"speaker", "Ana"}, {"dialogue", "Satir " + std::to_string(id)},
                                   {"typewriter_enabled", false}, {"content_id", cid(id)}}}};
        json edges = json::array();
        if (id < 3) edges.push_back({{"id", id + 1}});
        nodes.push_back({{"id", id}, {"title", "N" + std::to_string(id)},
                         {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"},
                                                   {"is_active", true}, {"components", json::array({dialogue})}}})},
                         {"next_nodes", edges}});
    }
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
}

std::string makeCatalog(int galleryCount, int achievementCount) {
    json gallery = json::array();
    for (int i = 0; i < galleryCount; ++i) {
        char id[16];
        std::snprintf(id, sizeof(id), "g%02d", i);
        gallery.push_back({{"id", id}, {"title", "Resim " + std::to_string(i)}, {"image", "red.bmp"}});
    }
    json achievements = json::array();
    for (int i = 0; i < achievementCount; ++i) {
        char id[16];
        std::snprintf(id, sizeof(id), "a%02d", i);
        achievements.push_back({{"id", id}, {"title", "Basarim " + std::to_string(i)}, {"description", "Aciklama"}});
    }
    return json({{"format", 1}, {"gallery", gallery}, {"achievements", achievements}}).dump();
}

struct Options {
    std::string theme;           // ui_theme.json text; empty = no file
    int saveSlotCount = 0;       // project.rowlproj save_slot_count; 0 = key absent
    int gallery = 0;             // gallery items in extras.json (0 = no extras.json)
    int achievements = 0;
};

struct Rgb {
    int r, g, b;
};

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    std::shared_ptr<InputHost> host;
    fs::path game;

    explicit Rig(const Options& options = Options{}) {
        ++g_rigCounter;
        game = g_root / ("game" + std::to_string(g_rigCounter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "5b7e2d14-3acd-4e60-8a12-7c1d5f9e2a%02x", g_rigCounter);
        std::string manifest = std::string(R"({"project_uuid":")") + uuid + R"(","name":"Menu Layout")";
        if (options.saveSlotCount > 0) manifest += ",\"save_slot_count\":" + std::to_string(options.saveSlotCount);
        writeBytes(game / "project.rowlproj", manifest + "}");
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph());
        if (!options.theme.empty()) writeBytes(game / "Assets" / "json" / "ui_theme.json", options.theme);
        if (options.gallery > 0 || options.achievements > 0) {
            writeBytes(game / "Assets" / "json" / "extras.json", makeCatalog(options.gallery, options.achievements));
            writeBytes(game / "Assets" / "images" / "red.bmp", makeBmp(64, 36, 220, 40, 40));
        }
        fs::create_directories(game / "Assets" / "fonts");
        fs::copy_file("Assets/fonts/default.ttf", game / "Assets" / "fonts" / "default.ttf");
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, kW, kH, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        const auto context = engine->getContext();
        host = std::make_shared<InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
        RowlEngine_SetPlayState(h, 1);
        step(0.016f);
    }
    ~Rig() { if (h) { RowlEngine_Shutdown(h); RowlEngine_Destroy(h); } }
    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void step(float dt, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_Step(h, dt);
    }
    json menu() const {
        const char* text = RowlEngine_GetPauseMenuJson(h);
        json parsed = json::parse(text ? text : "", nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    json theme() const {
        const json parsed = json::parse(capiText(h, RowlEngine_GetUiThemeJson), nullptr, false);
        req(parsed.is_object(), "theme JSON did not parse");
        return parsed;
    }
    int mode() const { return menu()["mode"].get<int>(); }
    int selected() const { return menu()["selected"].get<int>(); }
    void cmd(int command, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_PauseMenuCommand(h, command);
    }
    void click(float vx, float vy) const { engine->pauseMenuClick(vx, vy); }
    void event(RuntimeInputEvent e) {
        host->input.push_back(e);
        step(0.016f);
    }
    void digit(int d) { RuntimeInputEvent e{}; e.type = InType::SelectSlot; e.slot = d; event(e); }
    void wheel(float dy) { RuntimeInputEvent e{}; e.type = InType::Scroll; e.y = dy; event(e); }
    void unlock(const char* kind, const std::string& id) const {
        req(RowlEngine_ProfileUnlock(h, kind, id.c_str()) == ROWL_RESULT_OK, std::string("unlock ") + kind + ":" + id);
    }

    // Pause main page -> row (Down x n, Confirm).
    void pauseOpen(int row) const {
        RowlEngine_SetPaused(h, 1);
        cmd(ROWL_PAUSE_MENU_DOWN, row);
        cmd(ROWL_PAUSE_MENU_CONFIRM);
    }
    void openPreferences() const { pauseOpen(PL::kMainPrefsRow); req(mode() == 4, "Preferences page open"); }
    void openSave() const { pauseOpen(1); req(mode() == 1, "Save page open"); }
    void openLoad() const { pauseOpen(2); req(mode() == 2, "Load page open"); }
    void openTitle() const { req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "OpenTitleScreen"); }
    void openGallery() const {
        openTitle();
        req(RowlEngine_OpenExtras(h) == ROWL_RESULT_OK, "OpenExtras");
        cmd(ROWL_PAUSE_MENU_CONFIRM);  // root row 0 = Galeri
        req(mode() == 7, "Gallery page open");
    }
    void openAchievements() const {
        openTitle();
        req(RowlEngine_OpenExtras(h) == ROWL_RESULT_OK, "OpenExtras");
        cmd(ROWL_PAUSE_MENU_DOWN, 2);
        cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(mode() == 9, "Achievements page open");
    }

    std::string frame(int steps = 4) const {
        step(0.016f, steps);
        uint32_t w = 0, hh = 0;
        const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
        req(pixels != nullptr && w == kW && hh == kH, "offscreen pixel buffer");
        return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
    }
};

Rgb at(const std::string& pixels, int x, int y) {
    const size_t i = (static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4;
    return {static_cast<uint8_t>(pixels[i]), static_cast<uint8_t>(pixels[i + 1]), static_cast<uint8_t>(pixels[i + 2])};
}
// Virtual-canvas point -> pixel.
Rgb atV(const std::string& pixels, float vx, float vy) { return at(pixels, static_cast<int>(vx * 0.5f), static_cast<int>(vy * 0.5f)); }

bool near(const Rgb& a, const Rgb& b, int tolerance = 10) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}
std::string rgbText(const Rgb& c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + ")";
}

constexpr Rgb kSelectedFill{14, 116, 144};  // menu.selected_fill default

// Pixels of the virtual rectangle that differ from `reference`.
int inkIn(const std::string& pixels, float vx0, float vy0, float vx1, float vy1, const Rgb& reference, int tolerance = 18) {
    int count = 0;
    const int x0 = std::max(0, static_cast<int>(vx0 * 0.5f)), x1 = std::min<int>(kW, static_cast<int>(vx1 * 0.5f));
    const int y0 = std::max(0, static_cast<int>(vy0 * 0.5f)), y1 = std::min<int>(kH, static_cast<int>(vy1 * 0.5f));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (!near(at(pixels, x, y), reference, tolerance)) ++count;
        }
    }
    return count;
}

bool hasDiagnostic(const json& themeJson, const std::string& needle) {
    for (const auto& line : themeJson["diagnostics"]) {
        if (line.get<std::string>().find(needle) != std::string::npos) return true;
    }
    return false;
}

// A theme document with one `layout` object on `page` (and optional extra top-level members).
std::string themeWith(const std::string& page, const json& layout, const json& extra = json::object()) {
    json doc = {{"format", 1}, {"screens", {{page, {{"layout", layout}}}}}};
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        if (doc.contains(it.key()) && doc[it.key()].is_object()) doc[it.key()].update(it.value());
        else doc[it.key()] = it.value();
    }
    return doc.dump();
}

bool sameNumbers(const MenuLayout& a, const MenuLayout& b) {
    const auto list = [](const MenuLayout& m) {
        return std::vector<float>{m.panelX, m.panelY, m.panelW, m.panelH, m.titleX, m.titleY, m.titleW, m.titleH, m.rowX,
            m.rowW, m.rowY0, m.rowH, m.rowGap, static_cast<float>(m.comfortRows), m.compactRowY0, m.compactRowH,
            m.compactRowGap, m.labelWidth, m.valueX, m.valueWidth, m.rowPadX, m.adjustMinusW, m.adjustPlusX, m.hintX,
            m.hintY, m.hintW, m.counterX, m.counterY, m.counterW, m.thumbX, m.thumbY, m.thumbW, m.thumbH,
            m.sliderStart, m.sliderEndInset, m.sliderValueInset, m.backlogY0, m.backlogY1, m.backlogPadX, m.backlogGap,
            static_cast<float>(m.galleryColumns), m.galleryY0, m.galleryY1, m.galleryGapX, m.galleryGapY, m.galleryCellW,
            m.galleryThumbH, m.galleryCaptionH, m.achY0, m.achY1, m.achRowH, m.achRowGap, static_cast<float>(m.slotsPerPage),
            m.pagerX, m.pagerY, m.pagerW, m.pagerH, static_cast<float>(m.cardColumns), m.cardH, m.cardGapX, m.cardGapY,
            m.cardY0, m.cardY1};
    };
    return list(a) == list(b) && a.titleAlign == b.titleAlign && a.labelAlign == b.labelAlign &&
           a.valueAlign == b.valueAlign && a.hintAlign == b.hintAlign && a.cards == b.cards && a.thumbHidden == b.thumbHidden;
}

// ── 1. parser ───────────────────────────────────────────────────────────────
void parserTests() {
    const UiThemeLoad load = parseUiTheme(R"({"format":1,"screens":{"title":{"layout":{
        "panel":{"x":10,"y":20,"w":300,"h":400},
        "title":{"x":11,"y":22,"w":333,"align":"CENTER"},
        "rows":{"x":5,"y":6,"w":7,"h":8,"gap":9,"compact_y":10,"compact_h":11,"compact_gap":12,"comfort_rows":13,
                "label_align":"right","value_align":"center","label_width":0.4,"value_x":0.5,"value_width":0.3,"pad_x":14},
        "hint":{"x":1,"y":2,"w":3,"align":"right"},
        "counter":{"x":4,"y":5,"w":6},
        "thumbnail":{"x":7,"y":8,"w":9,"h":10,"hidden":true},
        "slider":{"start":0.25,"end_inset":30,"value_inset":40},
        "gallery":{"columns":3,"cell_w":100,"thumb_h":60,"gap_x":8,"gap_y":9,"y":10,"bottom":900,"caption_h":20},
        "achievements":{"y":11,"bottom":800,"row_h":70,"gap":5},
        "history":{"y":12,"bottom":700,"pad_x":6,"gap":4},
        "cards":{"columns":3,"h":120,"gap_x":10,"gap_y":11,"y":240,"bottom":860},
        "slots":{"per_page":6},
        "pager":{"x":50,"y":60,"w":70,"h":30}}}},
        "slots":{"layout":"cards","number_base":1}})");
    req(load.usable && load.diagnostics.empty(),
        "layout theme parses clean: " + (load.diagnostics.empty() ? std::string() : load.diagnostics[0]));
    const UiScreenLayout& l = load.theme.screens[static_cast<size_t>(uiScreenIndex("title"))].layout;
    req(l.panelX == 10.0f && l.panelH == 400.0f && l.titleAlign == "center" && l.titleW == 333.0f && l.rowX == 5.0f &&
            l.rowGap == 9.0f && l.compactGap == 12.0f && l.comfortRows == 13.0f && l.labelAlign == "right" &&
            l.valueAlign == "center" && l.labelWidth == 0.4f && l.rowPadX == 14.0f && l.hintAlign == "right" &&
            l.counterW == 6.0f && l.thumbHidden && l.sliderEndInset == 30.0f && l.galleryColumns == 3.0f &&
            l.galleryBottom == 900.0f && l.achRowH == 70.0f && l.historyPadX == 6.0f && l.cardColumns == 3.0f &&
            l.slotsPerPage == 6.0f && l.pagerH == 30.0f,
        "every layout key lands in its field");
    req(load.theme.slots.layout == "cards" && load.theme.slots.numberBase == 1.0f, "slots.layout / number_base");

    // Round trip: serialise, parse again, same theme; unset keys never reach the document.
    const std::string text = uiThemeToJson(load.theme);
    const UiThemeLoad again = parseUiTheme(text);
    req(again.usable && again.diagnostics.empty() && again.theme == load.theme, "layout survives uiThemeToJson -> parse");
    const std::string plain = uiThemeToJson(UiTheme{});
    req(plain.find("\"layout\":{") == std::string::npos && plain.find("\"panel\":{") == std::string::npos,
        "a default theme writes no layout object");
    req(plain.find("\"layout\":\"list\"") != std::string::npos && plain.find("\"number_base\":0") != std::string::npos,
        "the default slot style is written");

    // Bad values: reported, the field keeps its default / is clamped.
    const UiThemeLoad bad = parseUiTheme(R"({"format":1,"screens":{"pause":{"layout":{
        "rows":{"label_align":"middle","w":99999,"h":"tall"},"panel":[1,2],"hint":{"align":5}}},
        "load":{"layout":7}},"slots":{"layout":"carousel"}})");
    req(bad.usable, "a layout with bad values still parses");
    const UiScreenLayout& b = bad.theme.screens[static_cast<size_t>(uiScreenIndex("pause"))].layout;
    req(b.labelAlign.empty() && b.rowW == 4000.0f && !b.rowH && b.hintAlign.empty() && !b.panelX,
        "bad alignment kept unset, w clamped to 4000, non-number ignored");
    int hits = 0;
    for (const auto& line : bad.diagnostics) {
        if (line.find("label_align") != std::string::npos || line.find("rows.w") != std::string::npos ||
            line.find("rows.h") != std::string::npos || line.find("layout.panel") != std::string::npos ||
            line.find("hint.align") != std::string::npos || line.find("screens.load.layout") != std::string::npos ||
            line.find("slots.layout") != std::string::npos) {
            ++hits;
        }
    }
    req(hits >= 6, "each bad layout value has a diagnostic (found " + std::to_string(hits) + ")");
    req(bad.theme.slots.layout == "list", "an unknown slots.layout keeps list");
}

// ── 2. defaults ─────────────────────────────────────────────────────────────
void defaultTests() {
    const auto defaults = Rowl::Core::resolveMenuLayouts(UiTheme{});
    req(defaults.diagnostics.empty(), "the default theme has no layout diagnostics");
    const MenuLayout builtin;
    for (size_t i = 0; i < kUiScreenCount; ++i) {
        req(sameNumbers(defaults.pages[i], builtin), std::string("page ") + kUiScreenNames[i] + " == MenuLayout{}");
        req(!defaults.declared[i], "no page declares a layout");
    }
    // MenuLayout{} reproduces the PauseMenuLayout statics.
    for (const int rows : {PL::kMainRows, PL::kSlotRows, PL::kPreferenceRows, PL::kTitleRows, PL::kExtrasRootRows}) {
        for (int i = 0; i < rows; ++i) {
            req(builtin.rowTop(i, rows) == PL::rowTop(i, rows) && builtin.rowHOf(rows) == PL::rowH(rows),
                "row geometry == statics");
            const float cy = PL::rowTop(i, rows) + PL::rowH(rows) * 0.5f;
            for (const float x : {590.0f, 880.0f, 881.0f, 960.0f, 1040.0f, 1041.0f, 1330.0f, 1500.0f}) {
                req(builtin.rowAt(x, cy, rows) == PL::rowAt(x, cy, rows) &&
                        builtin.adjustDirection(x) == PL::adjustDirection(x),
                    "hit-tests == statics at x=" + std::to_string(x));
            }
        }
    }
    req(builtin.sliderX0() == PL::kSliderX0 && builtin.sliderX1() == PL::kSliderX1 &&
            builtin.sliderValuePos() == PL::kSliderValueX && builtin.sliderFractionAt(1000.0f) == PL::sliderFractionAt(1000.0f) &&
            builtin.inSliderZone(900.0f) == PL::inSliderZone(900.0f),
        "slider geometry == statics");
    req(builtin.galleryCellW == PL::kGalleryCellW && builtin.galleryCellH() == PL::kGalleryCellH &&
            builtin.galleryVisibleRows() == PL::kGalleryVisibleRows && builtin.achVisibleRows() == PL::kAchVisibleRows &&
            builtin.achRowTop(2) == PL::achRowTop(2) && builtin.backlogTextW() == PL::kBacklogTextW &&
            builtin.hintY == PL::kPanelY + PL::kPanelH - 44.0f,
        "gallery / achievements / history numbers == statics");
    int column = 0, row = 0, column2 = 0, row2 = 0;
    for (float x = 560.0f; x < 1360.0f; x += 23.0f) {
        for (float y = 240.0f; y < 920.0f; y += 17.0f) {
            const bool a = builtin.galleryCellAt(x, y, column, row);
            const bool b = PL::galleryCellAt(x, y, column2, row2);
            req(a == b && (!a || (column == column2 && row == row2)), "galleryCellAt == static");
            req(builtin.achRowAt(x, y, PL::kAchVisibleRows) == PL::achRowAt(x, y, PL::kAchVisibleRows), "achRowAt == static");
        }
    }
    // A theme that only moves the panel moves what hangs on it; the rest keeps its offsets.
    UiTheme moved;
    moved.screens[static_cast<size_t>(uiScreenIndex("save"))].layout.panelX = 600.0f;
    moved.screens[static_cast<size_t>(uiScreenIndex("save"))].layout.panelY = 60.0f;
    const MenuLayout m = Rowl::Core::resolveMenuLayouts(moved).pages[static_cast<size_t>(uiScreenIndex("save"))];
    req(m.panelX == 600.0f && m.rowX == 660.0f && m.titleY == 120.0f && m.rowY0 == 220.0f && m.hintY == 60.0f + 860.0f - 44.0f &&
            m.thumbX == 600.0f + 660.0f && m.thumbY == 100.0f && m.counterX == 660.0f + 760.0f - 150.0f,
        "a moved panel carries title, rows, hint, thumbnail and counter along");
}

// ── 3. invalid layouts ──────────────────────────────────────────────────────
void invalidTests() {
    struct Case {
        const char* page;
        json layout;
        const char* words;
    };
    const Case cases[] = {
        {"pause", {{"panel", {{"x", -100}}}}, "outside"},
        {"pause", {{"panel", {{"w", 0}}}}, "no size"},
        {"pause", {{"rows", {{"h", 0}}}}, "no size"},
        {"preferences", {{"rows", {{"compact_h", 120}}}}, "do not fit"},
        {"pause", {{"rows", {{"x", 1500}, {"w", 760}}}}, "fit"},
        {"gallery", {{"gallery", {{"columns", 12}, {"cell_w", 400}}}}, "leaves the canvas"},
        {"achievements", {{"achievements", {{"row_h", 700}}}}, "shorter than one row"},
        {"history", {{"history", {{"y", 500}, {"bottom", 520}}}}, "too short"},
        {"title", {{"title", {{"x", 1900}, {"w", 400}}}}, "title box"},
    };
    for (const Case& c : cases) {
        Rig rig(Options{themeWith(c.page, c.layout)});
        const json t = rig.theme();
        req(hasDiagnostic(t, std::string("screens.") + c.page + ".layout") && hasDiagnostic(t, c.words),
            std::string(c.page) + " " + c.layout.dump() + ": diagnostic mentions '" + c.words + "': " + t["diagnostics"].dump());
        req(hasDiagnostic(t, "built-in layout"), "the diagnostic says the built-in layout is used");
    }
    // The refused page is the built-in one; another page's valid layout is untouched.
    {
        const json both = {{"format", 1},
                           {"screens", {{"pause", {{"layout", {{"panel", {{"x", -5}}}, {"rows", {{"x", 200}}}}}}},
                                        {"history", {{"layout", {{"panel", {{"x", 60}, {"w", 1000}}}, {"rows", {{"x", 100}, {"w", 900}}}}}}}}}};
        Rig rig(Options{both.dump()});
        RowlEngine_SetPaused(rig.h, 1);
        json m = rig.menu();
        req(m["layout"]["panel"]["x"] == 520 && m["layout"]["row_x"] == 580, "refused pause layout -> built-in rows: " + m["layout"].dump());
        rig.cmd(ROWL_PAUSE_MENU_DOWN, PL::kMainBacklogRow);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.mode() == 5, "history page open");
        m = rig.menu();
        req(m["layout"]["row_x"] == 100 && m["layout"]["row_w"] == 900, "the valid history layout applies: " + m["layout"].dump());
        const json t = rig.theme();
        req(!hasDiagnostic(t, "screens.history.layout"), "history layout has no diagnostic");
    }
}

// ── 4. one geometry for drawing and hits ────────────────────────────────────
void singleSourceTests() {
    // Preferences with a moved, resized panel and its own row pitch.
    {
        const json layout = {{"panel", {{"x", 200}, {"y", 40}, {"w", 1100}, {"h", 1000}}},
                             {"rows", {{"x", 260}, {"w", 980}, {"y", 200}, {"compact_y", 180}, {"compact_h", 52}, {"compact_gap", 8}}}};
        Rig rig(Options{themeWith("preferences", layout)});
        rig.openPreferences();
        const json m = rig.menu();
        // F2 (B-13): single-language game has 11 preference rows (Dil hidden).
        req(m["layout"]["row_x"] == 260 && m["layout"]["row_w"] == 980 && m["layout"]["rows"].size() == 11,
            "the page reports its layout: " + m["layout"].dump());
        const MenuLayout expected = Rowl::Core::resolveMenuLayouts(parseUiTheme(themeWith("preferences", layout)).theme)
                                        .pages[static_cast<size_t>(uiScreenIndex("preferences"))];
        for (int i = 0; i < 11; ++i) {
            req(std::fabs(m["layout"]["rows"][i]["y"].get<float>() - expected.rowTop(i, 11)) < 0.01f,
                "row " + std::to_string(i) + " y in JSON == resolver");
        }
        // Drawn == hit: select row i by pointer in its middle band, see the highlight at that very place.
        const std::string reference = rig.frame();
        const Rgb panelInk = atV(reference, 300.0f, 1000.0f);
        for (int i = 0; i < 10; ++i) {
            const float top = expected.rowTop(i, 11);
            const float centre = top + expected.rowHOf(11) * 0.5f;
            rig.click(260.0f + 980.0f * 0.5f, centre);
            req(rig.selected() == i, "click on the centre of row " + std::to_string(i) + " selects it, got " + std::to_string(rig.selected()));
            const std::string frame = rig.frame();
            req(near(atV(frame, 260.0f + 490.0f, top + 2.0f), kSelectedFill), "row " + std::to_string(i) +
                    " highlight is drawn where it is hit: " + rgbText(atV(frame, 750.0f, top + 2.0f)));
            req(!near(atV(frame, 260.0f + 490.0f, top - 3.0f), kSelectedFill), "nothing is drawn in the gap above row " + std::to_string(i));
        }
        // The hint line sits at the moved panel's bottom, the page title at the layout's title place.
        rig.cmd(ROWL_PAUSE_MENU_UP, 10);
        const std::string frame = rig.frame();
        req(inkIn(frame, 260.0f, 40.0f + 1000.0f - 44.0f, 1240.0f, 40.0f + 1000.0f - 14.0f, panelInk) > 20,
            "the hint is drawn at the moved panel's bottom");
        req(inkIn(frame, 260.0f, 40.0f + 60.0f, 700.0f, 40.0f + 120.0f, panelInk) > 20, "the page title is drawn at the moved panel's top");
        // The last row (Geri) is hit at its moved place and leaves the page.
        rig.click(750.0f, expected.rowTop(10, 11) + expected.rowHOf(11) * 0.5f);
        req(rig.mode() == 0, "a click on the moved Geri row leaves the Preferences page");
    }
    // Title screen: a vertical list bottom-left, no panel; clicks follow.
    {
        const json doc = {{"format", 1},
                          {"screens", {{"title", {{"panel_skin", "none"},
                                                  {"layout", {{"rows", {{"x", 80}, {"y", 560}, {"w", 520}, {"h", 56}, {"gap", 8}}},
                                                              {"title", {{"x", 80}, {"y", 440}, {"w", 900}}},
                                                              {"hint", {{"x", 80}, {"y", 1030}, {"w", 900}}}}}}}}}};
        Rig rig(Options{doc.dump()});
        rig.openTitle();
        json m = rig.menu();
        req(m["mode"] == 3 && m["layout"]["rows"].size() == 5, "title screen with 5 rows");
        for (int i = 0; i < 5; ++i) {
            req(m["layout"]["rows"][i]["y"] == 560 + i * 64 && m["layout"]["rows"][i]["h"] == 56,
                "title row " + std::to_string(i) + " place: " + m["layout"]["rows"][i].dump());
        }
        const std::string frame = rig.frame();
        const Rgb ground = atV(frame, 1500.0f, 300.0f);
        // Row 0 (New Game) is selected: its fill is bottom-left, nothing where the centred panel rows used to be.
        req(near(atV(frame, 340.0f, 562.0f), kSelectedFill), "the selected title row is drawn bottom-left");
        req(!near(atV(frame, 960.0f, 280.0f), kSelectedFill) && inkIn(frame, 580.0f, 270.0f, 1340.0f, 330.0f, ground) == 0,
            "the old title row place stays empty");
        req(inkIn(frame, 80.0f, 1030.0f, 980.0f, 1060.0f, ground) > 20, "the hint is drawn at the layout's hint place");
        // A click on the "Tercihler" row's new place opens Preferences; a click on its old place does nothing.
        rig.click(700.0f, 440.0f);
        req(rig.mode() == 3, "a click on the old row place misses");
        rig.click(340.0f, 560.0f + 3 * 64 + 28.0f);
        req(rig.mode() == 4, "a click on the moved Tercihler row opens Preferences, mode " + std::to_string(rig.mode()));
    }
    // Panel-less narrow centred menu, centred labels.
    {
        const json doc = {{"format", 1},
                          {"screens", {{"pause", {{"panel_skin", "none"},
                                                  {"layout", {{"rows", {{"x", 760}, {"w", 400}, {"y", 300}, {"h", 60}, {"gap", 10},
                                                                         {"label_align", "center"}, {"label_width", 1.0}}}}}}}}}};
        Rig rig(Options{doc.dump()});
        RowlEngine_SetPaused(rig.h, 1);
        const json m = rig.menu();
        req(m["layout"]["row_x"] == 760 && m["layout"]["row_w"] == 400 && m["layout"]["rows"][2]["y"] == 440, "narrow menu place: " + m["layout"].dump());
        const std::string frame = rig.frame();
        const Rgb ground = atV(frame, 100.0f, 100.0f);
        req(near(atV(frame, 960.0f, 302.0f), kSelectedFill), "selected row drawn in the narrow column");
        // Centred label: the ink of row 0 ("Devam Et") is balanced around the column centre (x = 960).
        int left = 0, right = 0;
        for (int x = 760; x < 1160; x += 2) {
            const int ink = inkIn(frame, static_cast<float>(x), 310.0f, static_cast<float>(x + 2), 350.0f, kSelectedFill, 40);
            if (x < 960) left += ink; else right += ink;
        }
        req(left > 30 && right > 30 && std::abs(left - right) < std::max(left, right) / 2, "the label is centred: left ink " +
            std::to_string(left) + ", right ink " + std::to_string(right));
        (void)ground;
        rig.click(960.0f, 440.0f + 30.0f);  // row 2 = Oyunu Yukle -> Load page
        req(rig.mode() == 2, "clicking the narrow column's row 2 opens the Load page");
        rig.click(300.0f, 440.0f + 30.0f);
        req(rig.mode() == 2, "clicks left of the narrow column miss");
    }
    // Confirm keeps the rows of the page it was armed on, unless the theme gives it its own layout.
    for (const bool own : {false, true}) {
        json screens = {{"title", {{"panel_skin", "none"}, {"layout", {{"rows", {{"x", 100}, {"w", 600}, {"y", 500}, {"h", 50}, {"gap", 6}}}}}}}};
        if (own) screens["confirm"] = {{"panel_skin", "none"}, {"layout", {{"rows", {{"x", 300}, {"w", 700}, {"y", 400}, {"h", 50}, {"gap", 6}}}}}};
        Rig rig(Options{json({{"format", 1}, {"screens", screens}}).dump()});
        rig.openTitle();
        rig.cmd(ROWL_PAUSE_MENU_UP);  // wraps to Cikis
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        const json m = rig.menu();
        req(m["page"] == "confirm", "the Quit row is armed");
        req(m["layout"]["row_x"] == (own ? 300 : 100) && m["layout"]["rows"][0]["y"] == (own ? 400 : 500),
            std::string(own ? "own confirm layout: " : "confirm falls back to the title layout: ") + m["layout"].dump());
    }
    // Slider geometry from the layout: a press on the (moved) track sets the value there.
    {
        const json doc = {{"format", 1}, {"preferences", {{"value_style", "slider"}}},
                          {"screens", {{"preferences", {{"layout", {{"slider", {{"start", 0.2}, {"end_inset", 80}}}}}}}}}};
        Rig rig(Options{doc.dump()});
        rig.openPreferences();
        const float x0 = 580.0f + 760.0f * 0.2f, x1 = 580.0f + 760.0f - 80.0f;
        const int prefRows = static_cast<int>(rig.menu()["rows"].size());
        const float cy = PL::rowTop(0, prefRows) + PL::rowH(prefRows) * 0.5f;
        rig.click(x0 + (x1 - x0) * 0.5f, cy);
        req(rig.menu()["rows"][0]["value"] == "50%", "a press mid-track sets 50%: " + rig.menu()["rows"][0].dump());
        rig.click(x0 + (x1 - x0) * 0.25f, cy);
        req(rig.menu()["rows"][0]["value"] == "25%", "a press at 25% of the moved track sets 25%: " + rig.menu()["rows"][0].dump());
    }
}


const MenuLayout& pageLayout(const ResolvedLayouts& all, const char* page) {
    return all.pages[static_cast<size_t>(uiScreenIndex(page))];
}

// ── 4b. extras pages ────────────────────────────────────────────────────────
void extrasLayoutTests() {
    // Gallery with 3 and 6 columns: cells where the layout puts them; click == cell; the picture is drawn there.
    for (const int columns : {3, 6}) {
        const std::string theme = themeWith("gallery", {{"gallery", {{"columns", columns}}}});
        const MenuLayout L = pageLayout(Rowl::Core::resolveMenuLayouts(parseUiTheme(theme).theme), "gallery");
        req(L.galleryColumns == columns && L.galleryCellW > 20.0f, "gallery layout resolved for " + std::to_string(columns) + " columns");
        {
            Rig rig(Options{theme, 0, 14, 0});
            rig.openGallery();
            const json m = rig.menu();
            const json& cells = m["extras"]["cells"];
            req(m["extras"]["columns"] == columns, "the view reports " + std::to_string(columns) + " columns");
            const int visible = std::min(14, columns * L.galleryVisibleRows());
            req(static_cast<int>(cells.size()) == visible, "cells in view: " + std::to_string(cells.size()) + " want " + std::to_string(visible));
            int lastIndex = -1;
            for (const json& cell : cells) {
                const int index = cell["index"].get<int>();
                lastIndex = index;
                req(std::fabs(cell["x"].get<float>() - L.galleryCellX(index % columns)) < 0.05f &&
                        std::fabs(cell["y"].get<float>() - L.galleryCellY(index / columns)) < 0.05f &&
                        std::fabs(cell["w"].get<float>() - L.galleryCellW) < 0.05f,
                    "cell " + std::to_string(index) + " sits where the layout puts it: " + cell.dump());
                rig.click(cell["x"].get<float>() + cell["w"].get<float>() * 0.5f, cell["y"].get<float>() + L.galleryThumbH * 0.5f);
                req(rig.menu()["extras"]["selected"] == index, "a click on the centre of cell " + std::to_string(index) + " selects it");
            }
            // A click in the gap between two columns misses.
            rig.click(L.galleryCellX(0) + L.galleryCellW + L.galleryGapX * 0.5f, L.galleryCellY(0) + 10.0f);
            req(rig.menu()["extras"]["selected"] == lastIndex, "the gap between cells keeps the selection");
        }
        {
            Rig rig(Options{theme, 0, 14, 0});
            for (int i = 0; i < 14; ++i) {
                char id[16];
                std::snprintf(id, sizeof(id), "g%02d", i);
                rig.unlock("cg", id);
            }
            rig.openGallery();
            const std::string frame = rig.frame(12);
            for (const int index : {0, 1, columns - 1, columns}) {
                const float cx = L.galleryCellX(index % columns) + L.galleryCellW * 0.5f;
                const float cy = L.galleryCellY(index / columns) + L.galleryThumbH * 0.5f;
                req(near(atV(frame, cx, cy), Rgb{220, 40, 40}, 40), std::to_string(columns) + " columns: cell " + std::to_string(index) +
                        " picture drawn at its hit place " + rgbText(atV(frame, cx, cy)));
            }
        }
    }
    // Achievements: own row height / gap.
    {
        const std::string theme = themeWith("achievements", {{"achievements", {{"row_h", 70}, {"gap", 12}}}});
        const MenuLayout L = pageLayout(Rowl::Core::resolveMenuLayouts(parseUiTheme(theme).theme), "achievements");
        Rig rig(Options{theme, 0, 0, 12});
        rig.openAchievements();
        const json m = rig.menu();
        const json& items = m["extras"]["items"];
        req(static_cast<int>(items.size()) == std::min(12, L.achVisibleRows()), "achievement rows in view: " + std::to_string(items.size()));
        for (size_t i = 0; i < items.size(); ++i) {
            req(std::fabs(items[i]["y"].get<float>() - (L.achY0 + static_cast<float>(i) * 82.0f)) < 0.05f && items[i]["h"] == 70,
                "achievement row " + std::to_string(i) + " place: " + items[i].dump());
        }
        const std::string before = rig.frame();
        for (const int row : {1, 3}) {
            rig.click(700.0f, items[static_cast<size_t>(row)]["y"].get<float>() + 35.0f);
            req(rig.menu()["extras"]["selected"] == row, "click selects achievement row " + std::to_string(row));
            const std::string frame = rig.frame();
            req(near(atV(frame, 1200.0f, items[static_cast<size_t>(row)]["y"].get<float>() + 3.0f), Rgb{14, 116, 144}, 14),
                "the selected achievement plate is drawn at the hit place");
        }
        // The gap under a row (12 px) misses; the old 96 px row pitch no longer applies.
        rig.click(700.0f, items[0]["y"].get<float>() + 70.0f + 6.0f);
        req(rig.menu()["extras"]["selected"] == 3, "the gap between achievement rows misses");
        (void)before;
    }
}

// ── 5. slot pages ───────────────────────────────────────────────────────────
void slotPageTests() {
    // Default: 10 slots, one page, no pager - the pre-T2b page.
    {
        Rig rig;
        rig.openLoad();
        const json m = rig.menu();
        req(m["rows"].size() == 10 && m["slot_page"]["page_count"] == 1 && m["slot_page"]["slot_count"] == 10 &&
                !m["slot_page"].contains("pager") && m["rows"][3]["label"] == "Yuva 3",
            "default: ten slots, no pager: " + m["slot_page"].dump());
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(rig.menu()["slot_page"]["page"] == 0, "Right on a one-page list does nothing");
        rig.digit(7);
        req(rig.mode() == 2, "a digit on a Load page with an empty slot leaves the page open");
    }
    // 30 slots in a list: three pages of ten.
    {
        Rig rig(Options{"", 30});
        rig.openSave();
        json m = rig.menu();
        const MenuLayout L;
        req(m["slot_page"]["page_count"] == 3 && m["slot_page"]["first_slot"] == 0 && m["rows"].size() == 10 &&
                m["slot_page"]["pager"]["y"] == L.pagerY,
            "30 slots: three pages and a pager strip: " + m["slot_page"].dump());
        const std::string frame0 = rig.frame();
        const Rgb ground = atV(frame0, 1500.0f, 300.0f);
        const Rgb panelInk = atV(frame0, 540.0f, 940.0f);
        (void)ground;
        req(inkIn(frame0, L.pagerX + 100.0f, L.pagerY + 6.0f, L.pagerX + L.pagerW - 100.0f, L.pagerY + L.pagerH - 6.0f, panelInk) > 30,
            "the pager strip is drawn");
        // Right / Left turn the page, keyboard and pointer.
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        m = rig.menu();
        req(m["slot_page"]["page"] == 1 && m["slot_page"]["first_slot"] == 10 && m["rows"][0]["label"] == "Yuva 10" &&
                m["rows"][9]["label"] == "Yuva 19",
            "Right shows slots 10..19: " + m.dump().substr(0, 300));
        rig.click(L.pagerX + L.pagerW * 0.95f, L.pagerY + L.pagerH * 0.5f);
        req(rig.menu()["slot_page"]["page"] == 2 && rig.menu()["slot_page"]["first_slot"] == 20, "a click on the pager's right end turns to page 3");
        rig.click(L.pagerX + L.pagerW * 0.5f, L.pagerY + L.pagerH * 0.5f);
        req(rig.menu()["slot_page"]["page"] == 2, "the pager's middle does nothing");
        rig.click(L.pagerX + L.pagerW * 0.05f, L.pagerY + L.pagerH * 0.5f);
        req(rig.menu()["slot_page"]["page"] == 1, "a click on the pager's left end goes back");
        rig.wheel(-1.0f);
        req(rig.menu()["slot_page"]["page"] == 2, "wheel down turns forward");
        rig.wheel(1.0f);
        req(rig.menu()["slot_page"]["page"] == 1, "wheel up turns back");
        // 0-9 pick the visible page's rows: page 2 (slots 10..19), digit 3 = slot 13.
        rig.digit(3);
        req(RowlEngine_HasSaveSlot(rig.h, 13) == 1 && RowlEngine_HasSaveSlot(rig.h, 3) == 0, "digit 3 on page 2 saves slot 13, not slot 3");
        // Confirm / click act on the page's row.
        rig.cmd(ROWL_PAUSE_MENU_DOWN, 4);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(RowlEngine_HasSaveSlot(rig.h, 14) == 1, "Confirm on row 4 of page 2 saves slot 14");
        rig.click(900.0f, PL::rowTop(7, 10) + 20.0f);
        req(RowlEngine_HasSaveSlot(rig.h, 17) == 1, "a click on row 7 of page 2 saves slot 17");
        // The first page again: slot 3.
        rig.cmd(ROWL_PAUSE_MENU_LEFT);
        rig.digit(3);
        req(RowlEngine_HasSaveSlot(rig.h, 3) == 1, "digit 3 on page 1 saves slot 3");
        req(rig.menu()["selected"].get<int>() >= 0, "a selection survives paging");
    }
    // Number base 1: labels count from 1 and the digits follow the labels.
    {
        const std::string theme = R"({"format":1,"slots":{"number_base":1}})";
        Rig rig(Options{theme, 12});
        rig.openSave();
        const json m = rig.menu();
        req(m["rows"][0]["label"] == "Yuva 1" && m["rows"][9]["label"] == "Yuva 10" && m["slot_page"]["page_count"] == 2,
            "number_base 1 labels: " + m["rows"][0].dump() + m["rows"][9].dump());
        rig.digit(1);
        req(RowlEngine_HasSaveSlot(rig.h, 0) == 1, "digit 1 saves slot 0 (file save_slot_0) - the first row");
        rig.digit(0);
        req(RowlEngine_HasSaveSlot(rig.h, 9) == 1, "digit 0 saves the tenth row");
        const std::string file = capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8) + "/save_slot_0.json";
        req(fs::exists(file), "the file name stays save_slot_0.json: " + file);
    }
    // A short project: 5 slots, a digit beyond them does nothing.
    {
        Rig rig(Options{"", 5});
        rig.openSave();
        req(rig.menu()["rows"].size() == 5 && rig.menu()["slot_page"]["page_count"] == 1, "save_slot_count 5: five rows");
        rig.digit(7);
        req(RowlEngine_HasSaveSlot(rig.h, 7) == 0, "digit 7 is out of range");
        rig.digit(4);
        req(RowlEngine_HasSaveSlot(rig.h, 4) == 1, "digit 4 saves slot 4");
    }
    // Card grid.
    {
        Rig rig(Options{R"({"format":1,"slots":{"layout":"cards"}})", 30});
        rig.openSave();
        json m = rig.menu();
        const MenuLayout L = pageLayout(Rowl::Core::resolveMenuLayouts(parseUiTheme(R"({"format":1,"slots":{"layout":"cards"}})").theme), "save");
        req(m["slot_page"]["cards"] == true && m["cards"].size() == 8 && m["slot_page"]["page_count"] == 4,
            "cards: 2 x 4 per page, four pages: " + m["slot_page"].dump());
        for (size_t i = 0; i < m["cards"].size(); ++i) {
            const json& card = m["cards"][i];
            req(std::fabs(card["x"].get<float>() - L.cardX(static_cast<int>(i))) < 0.05f &&
                    std::fabs(card["y"].get<float>() - L.cardY(static_cast<int>(i))) < 0.05f && card["slot"] == static_cast<int>(i),
                "card " + std::to_string(i) + " place: " + card.dump());
        }
        // The selected card's plate is drawn where it is hit.
        const std::string frame = rig.frame();
        req(near(atV(frame, L.cardX(0) + L.cardW() * 0.5f, L.cardY(0) + 3.0f), kSelectedFill), "card 0 (selected) is drawn at its place");
        req(!near(atV(frame, L.cardX(1) + L.cardW() * 0.5f, L.cardY(1) + 3.0f), kSelectedFill), "card 1 (not selected) has no highlight");
        // Click == card.
        rig.click(L.cardX(5) + L.cardW() * 0.5f, L.cardY(5) + L.cardH * 0.5f);
        req(RowlEngine_HasSaveSlot(rig.h, 5) == 1 && RowlEngine_HasSaveSlot(rig.h, 4) == 0, "a click on card 5 saves slot 5");
        // Keys: Up / Down walk a card line, Left / Right a card; the page turns at its edge.
        rig.cmd(ROWL_PAUSE_MENU_UP, 8);
        rig.cmd(ROWL_PAUSE_MENU_DOWN);
        req(rig.selected() == 2, "Down moves a card line (selected 0 -> 2), got " + std::to_string(rig.selected()));
        rig.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(rig.selected() == 3, "Right moves one card");
        rig.cmd(ROWL_PAUSE_MENU_UP);
        rig.cmd(ROWL_PAUSE_MENU_LEFT);
        req(rig.selected() == 0 && rig.menu()["slot_page"]["page"] == 0, "Up / Left walk back to card 0");
        rig.cmd(ROWL_PAUSE_MENU_LEFT);
        req(rig.selected() == 0 && rig.menu()["slot_page"]["page"] == 0, "Left at the very first card stays");
        rig.cmd(ROWL_PAUSE_MENU_DOWN, 3);
        rig.cmd(ROWL_PAUSE_MENU_DOWN);
        m = rig.menu();
        req(m["slot_page"]["page"] == 1 && m["slot_page"]["first_slot"] == 8 && m["selected"] == 0,
            "Down off the last card line turns the page: " + m["slot_page"].dump() + " sel " + m["selected"].dump());
        rig.cmd(ROWL_PAUSE_MENU_UP);
        req(rig.menu()["slot_page"]["page"] == 0 && rig.selected() == 6, "Up off the first line goes to the previous page's last line");
        // Load page of the card layout: a filled slot's card has its date / summary line in the view.
        rig.cmd(ROWL_PAUSE_MENU_BACK);  // F2 (D-D2): Back from Save lands on row 1 (Oyunu Kaydet)
        rig.cmd(ROWL_PAUSE_MENU_DOWN, 1);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.mode() == 2, "Load page open");
    }
}

// ── 6. B-11 / B-12 ──────────────────────────────────────────────────────────
void fitTests() {
    // B-11: at 150% the Continue row's value (slot + date + time) takes the room left of its column.
    for (const float scale : {1.0f, 1.5f}) {
        Rig rig;
        req(RowlEngine_SaveGameSlot(rig.h, 3) == 1, "a save to continue from");
        RowlEngine_SetTextScale(rig.h, scale);
        rig.openTitle();
        const json m = rig.menu();
        req(m["selected"] == 1 && m["rows"][1]["value"].get<std::string>().rfind("Yuva 3  ", 0) == 0, "Continue row selected: " + m["rows"][1].dump());
        const std::string frame = rig.frame();
        const float top = m["layout"]["rows"][1]["y"].get<float>();
        const MenuLayout L;
        const float oldValueX = L.rowX + L.rowW * 0.55f;
        const int leftOfColumn = inkIn(frame, 800.0f, top + 6.0f, oldValueX - 8.0f, top + 48.0f, kSelectedFill, 40);
        const int pastRow = inkIn(frame, L.rowX + L.rowW + 2.0f, top, L.panelX + L.panelW - 4.0f, top + 54.0f, atV(frame, 1380.0f, 120.0f), 30);
        if (scale < 1.2f) {
            req(leftOfColumn == 0, "100%: the value stays in its column (ink left of it: " + std::to_string(leftOfColumn) + ")");
        } else {
            req(leftOfColumn > 60, "150%: the long value grows to the left, text visible (ink " + std::to_string(leftOfColumn) + ")");
        }
        req(pastRow == 0, "the value never leaves the row (ink right of it: " + std::to_string(pastRow) + ")");
    }
    // B-12: at 150% a filled slot's row is tall enough for its detail line; fewer slots per page.
    {
        Rig rig;
        req(RowlEngine_SaveGameSlot(rig.h, 0) == 1 && RowlEngine_SaveGameSlot(rig.h, 1) == 1, "two saves");
        RowlEngine_SetTextScale(rig.h, 1.5f);
        rig.openLoad();
        json m = rig.menu();
        const int rows = static_cast<int>(m["rows"].size());
        req(rows == 7 && m["slot_page"]["page_count"] == 2, "150%: 7 slots fit a page, the pager holds the rest: " + m["slot_page"].dump());
        const float rowH = m["layout"]["rows"][0]["h"].get<float>();
        req(std::fabs(rowH - PL::kRowH * 1.5f) < 0.01f, "the row grows with the text: " + std::to_string(rowH));
        req(m["layout"]["rows"][6]["y"].get<float>() + rowH <= m["slot_page"]["pager"]["y"].get<float>(), "the rows end above the pager strip");
        const std::string frame = rig.frame();
        const Rgb panelInk = atV(frame, 540.0f, 930.0f);
        for (int i = 0; i + 1 < rows; ++i) {
            const float bottom = m["layout"]["rows"][static_cast<size_t>(i)]["y"].get<float>() + rowH;
            const float nextTop = m["layout"]["rows"][static_cast<size_t>(i + 1)]["y"].get<float>();
            req(inkIn(frame, PL::kRowX + 8.0f, bottom + 0.5f, PL::kRowX + PL::kRowW - 8.0f, nextTop - 0.5f, panelInk, 24) == 0,
                "nothing spills into the gap below row " + std::to_string(i));
        }
        const float top0 = m["layout"]["rows"][0]["y"].get<float>();
        // Row 0 is selected (fill): its detail line (date, chapter) is inked in the lower third.
        req(inkIn(frame, PL::kRowX + 20.0f, top0 + rowH * 0.62f, PL::kRowX + 500.0f, top0 + rowH - 2.0f, kSelectedFill, 30) > 40,
            "the detail line is drawn inside the tall row");
        // The detail line is as large as 150% of the design size: its ink band is taller than the 100% one.
        int band = 0;
        for (int y = static_cast<int>((top0 + rowH * 0.55f) * 0.5f); y < static_cast<int>((top0 + rowH) * 0.5f); ++y) {
            if (inkIn(frame, PL::kRowX + 20.0f, static_cast<float>(y) * 2.0f, PL::kRowX + 500.0f, static_cast<float>(y) * 2.0f + 2.0f, kSelectedFill, 30) > 0) ++band;
        }
        req(band >= 9, "the 150% detail line is not the tiny 16 px of before (ink rows: " + std::to_string(band) + ")");
        // A selection beyond the shrunk page is pulled back onto it; Confirm then acts on a visible row.
        RowlEngine_SetTextScale(rig.h, 1.0f);
        rig.cmd(ROWL_PAUSE_MENU_UP);  // wraps from row 0 to row 9
        req(rig.selected() == 9, "row 9 selected at 100%");
        RowlEngine_SetTextScale(rig.h, 1.5f);
        req(rig.selected() == 6 && rig.menu()["rows"].size() == 7, "the selection stays on the 7-row page: " + std::to_string(rig.selected()));
        // 100%: ten rows of 54 as before.
        RowlEngine_SetTextScale(rig.h, 1.0f);
        m = rig.menu();
        req(m["rows"].size() == 10 && m["layout"]["rows"][0]["h"] == 54 && m["slot_page"]["page_count"] == 1, "100%: back to ten rows of 54");
    }
}

}  // namespace

void test_menu_layout() {
    TEST_SECTION("Menu layout: per-page place / size / arrangement from the theme, slot pages (T2b)");

    g_root = fs::temp_directory_path() / "rowl_menu_layout";
    std::error_code ec;
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root);
    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (g_root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (g_root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (g_root / "xdg").string().c_str(), 1);
#endif

    parserTests();
    TEST_PASS("layout parser: keys, round trip, bad values");
    defaultTests();
    TEST_PASS("default layout == the pre-T2b numbers on every page");
    invalidTests();
    TEST_PASS("invalid layouts: diagnostic + built-in page");
    singleSourceTests();
    TEST_PASS("drawn place == hit place (rows, title menu bottom-left, narrow column, confirm, slider)");
    extrasLayoutTests();
    TEST_PASS("gallery 3 / 6 columns and achievement rows: drawn cell == hit cell");
    slotPageTests();
    TEST_PASS("slot pages: save_slot_count, paging, digits, number base, card grid");
    fitTests();
    TEST_PASS("B-11 Continue value at 150%, B-12 tall slot rows with a readable detail line");

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(g_root, ec);
}
