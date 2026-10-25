/**
 * test_ui_skin.cpp — T2: skins (image frames + colours) for every engine-drawn piece of player UI.
 *
 * Kilit:
 *  1. Ayristirici: skin anahtarlari (image/mode/slice/slice_scale/tint/opacity/filter/fill/border/
 *     border_width/corner_radius/padding), kisa yazim ("yol", "none"/false), durum varyantlari ana skini
 *     miras alir; bozuk deger (mod, slice, yol kacisi, uzanti, sayi) -> o alan oldugu gibi + tani;
 *     serilestirme-ayristirma gidis-donus; uiThemeImagePaths; high_contrast'ta skinler cekilir
 *     (keep_skins ile kalir).
 *  2. Gorselsiz skin == bugunku pikseller (yalniz padding/etiket yazili temada kare birebir ayni).
 *  3. Menu paneli: 9-dilim (kose gerilmez, kenar gerilir, orta dolar), "none" (panel yok), sayfa
 *     bazli ust yazma, karartma + tam ekran katman gorseli.
 *  4. Satirlar: normal / secili / devre disi skin, secili bar; yuva satiri bos/dolu/bozuk/secili;
 *     kucuk resim cercevesi + yer tutucu + "yeni kayit" gorseli.
 *  5. Baslik ekrani: arka plan gorseli hikaye karesini gizler (alt sayfalarda da), logo, hide_story_frame.
 *  6. Hikaye: diyalog kutusu skini (bilesenin custom_box_texture'i oncelikli), metin padding'i,
 *     konusmaci etiketi skini + ofset, secim dugmesi durum skinleri, gizli secenegi gosterme,
 *     bilesenin hover_image'i temayi yener, devam gostergesi (metin bitince; hareketi azalt'ta durur).
 *  7. Ekstralar: galeri hucresi (kilitli gorsel "?" yerine), basarim plakasi + ikon (kilitliye yok),
 *     goruntuleyici cercevesi; Gecmis girdisi; toast / rozet skini + kose.
 *  8. Tercihler kaydirici: izlek/dolgu/topuz, tikla + surukle degeri ayarlar.
 *  9. Bozuk / eksik / cok buyuk gorsel -> renkli cizim + GetUiThemeJson tanisi (oyun acilir).
 * 10. Imlec (gorunur pencerede) ve extras.json `icon` alani.
 *
 * Bilerek-boz: Window::resolveSkin'de "gorsel varsa yuzey rengini cizme" kuralini kaldirmak 3; 9-dilim
 * kose kaynak hesabini bozmak 3; panelSkinFor'da sayfa ustyazimini yoksaymak 3'un sayfa kontrolu;
 * drawTitleBackdrop'u kapatmak 5; choice pick(Disabled) yerine normal 6; sliderFractionAt'i bozmak 8.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"
#include "rowl/state/extras_catalog.hpp"
#include "rowl/state/ui_theme.hpp"
#include "rowl/vfs/vfs.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;
using LM = Rowl::Core::PauseMenuLayout;
using namespace Rowl::State;

constexpr const char* kSection = "ui-skin";
constexpr uint32_t kW = 960;  // half of the 1920x1080 virtual canvas: 1 physical px = 2 virtual px
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

// ── a tiny PNG writer (stored deflate: no compressor needed) ────────────────
using Pixel = std::array<uint8_t, 4>;

uint32_t crc32(const std::string& data) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (const unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

void putBe32(std::string& out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>((v >> shift) & 0xFF));
}

void putChunk(std::string& png, const char* type, const std::string& body) {
    putBe32(png, static_cast<uint32_t>(body.size()));
    const std::string typed = std::string(type) + body;
    png += typed;
    putBe32(png, crc32(typed));
}

std::string pngBytes(int w, int h, const std::function<Pixel(int, int)>& pixel) {
    std::string raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back('\0');
        for (int x = 0; x < w; ++x) {
            const Pixel p = pixel(x, y);
            raw.append(reinterpret_cast<const char*>(p.data()), 4);
        }
    }
    std::string z = "\x78\x01";
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? '\1' : '\0');
        z.push_back(static_cast<char>(n & 0xFF));
        z.push_back(static_cast<char>((n >> 8) & 0xFF));
        z.push_back(static_cast<char>(~n & 0xFF));
        z.push_back(static_cast<char>((~n >> 8) & 0xFF));
        z.append(raw, pos, n);
        pos += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (const unsigned char c : raw) {
        a = (a + c) % 65521u;
        b = (b + a) % 65521u;
    }
    putBe32(z, (b << 16) | a);
    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    putBe32(ihdr, static_cast<uint32_t>(w));
    putBe32(ihdr, static_cast<uint32_t>(h));
    ihdr += std::string("\x08\x06\x00\x00\x00", 5);
    putChunk(png, "IHDR", ihdr);
    putChunk(png, "IDAT", z);
    putChunk(png, "IEND", "");
    return png;
}

std::string solidPng(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return pngBytes(w, h, [=](int, int) { return Pixel{r, g, b, a}; });
}

// A 24x24 frame: 8 px corners (red, one marked pixel at (3,2)), blue top / bottom edges, green side
// edges, yellow centre. slice [8,8,8,8].
std::string framePng(bool hollow = false) {
    return pngBytes(24, 24, [hollow](int x, int y) -> Pixel {
        const bool left = x < 8, right = x >= 16, top = y < 8, bottom = y >= 16;
        if ((left || right) && (top || bottom)) {
            if (x == 3 && y == 2) return {10, 20, 30, 255};
            return {255, 0, 0, 255};
        }
        if (top || bottom) return {0, 0, 255, 255};
        if (left || right) return {0, 255, 0, 255};
        return {255, 255, 0, static_cast<uint8_t>(hollow ? 0 : 255)};
    });
}

// Debug aid: ROWL_T2_DUMP=<dir> writes every checked frame as <dir>/<n>.png (look at them with an image viewer).
void dumpFrame(const std::string& pixels) {
    static int counter = 0;
    const char* dir = std::getenv("ROWL_T2_DUMP");
    if (dir == nullptr || *dir == '\0') return;
    const std::string png = pngBytes(static_cast<int>(kW), static_cast<int>(kH), [&](int x, int y) {
        const size_t i = (static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4;
        return Pixel{static_cast<uint8_t>(pixels[i]), static_cast<uint8_t>(pixels[i + 1]), static_cast<uint8_t>(pixels[i + 2]), 255};
    });
    writeBytes(fs::path(dir) / (std::to_string(++counter) + ".png"), png);
}

// ── engine rig ──────────────────────────────────────────────────────────────
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

json component(const std::string& type, const std::string& id, const json& data) {
    return {{"type", type}, {"id", id}, {"enabled", true}, {"data", data}};
}

struct Options {
    std::string theme;                              // ui_theme.json text; empty = no file
    std::map<std::string, std::string> files;       // extra Assets/<path> -> bytes
    json dialogue = json::object();                 // extra keys of the dialogue component
    json options = json::array();                   // choice options; empty array = default two (A, B)
    bool choices = true;
    bool catalog = false;                           // extras.json with 2 gallery items + 2 achievements
    json catalogOverride;                           // replaces the catalog when not null
    bool second = false;                            // a second node after the first (for the history page)
    bool fonts = true;                              // copies the bundled fonts into the project
};

std::string buildGraph(const Options& o) {
    json comps = json::array();
    json dialogue = {{"speaker", "Ana"}, {"dialogue", "Merhaba dunya"}, {"typewriter_enabled", false},
                     {"content_id", "40000000-0000-4000-8000-000000000001"}};
    for (auto it = o.dialogue.begin(); it != o.dialogue.end(); ++it) dialogue[it.key()] = it.value();
    json options = o.options.empty()
        ? json::array({{{"option_id", "a"}, {"text", "Birinci"}}, {{"option_id", "b"}, {"text", "Ikinci"}}})
        : o.options;
    comps.push_back(component("dialogue", "d1", dialogue));
    if (o.choices) comps.push_back(component("choice", "c1", {{"options", options}}));
    json nodes = json::array({{{"id", 1}, {"title", "N1"},
                               {"objects", json::array({{{"id", "o1"}, {"name", "D"}, {"is_active", true},
                                                         {"components", comps}}})},
                               {"next_nodes", o.second ? json::array({{{"id", 2}}}) : json::array()}}});
    if (o.second) {
        for (int id = 2; id <= 3; ++id) {
            json dialogueN = {{"speaker", "Ben"}, {"dialogue", "Satir " + std::to_string(id)}, {"typewriter_enabled", false},
                              {"content_id", "40000000-0000-4000-8000-00000000000" + std::to_string(id)}};
            nodes.push_back({{"id", id}, {"title", "N" + std::to_string(id)},
                             {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"}, {"is_active", true},
                                                      {"components", json::array({component("dialogue", "d" + std::to_string(id), dialogueN)})}}})},
                             {"next_nodes", id == 2 ? json::array({{{"id", 3}}}) : json::array()}});
        }
    }
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
}

std::string catalogText(const Options& o) {
    if (!o.catalogOverride.is_null()) return o.catalogOverride.dump();
    return json({{"format", 1},
                 {"gallery", json::array({{{"id", "g0"}, {"title", "Bir"}, {"image", "red.bmp"}},
                                          {{"id", "g1"}, {"title", "Iki"}, {"image", "red.bmp"}}})},
                 {"achievements", json::array({{{"id", "a0"}, {"title", "Birinci"}, {"description", "Aciklama"}},
                                               {{"id", "a1"}, {"title", "Ikinci"}, {"description", "Aciklama"}}})}})
        .dump();
}

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
        std::snprintf(uuid, sizeof(uuid), "7d9f4e36-5cbe-4f82-8b34-9e3f7c0b3d%02x", g_rigCounter);
        writeBytes(game / "project.rowlproj", std::string(R"({"project_uuid":")") + uuid + R"(","name":"UI Skin"})");
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph(options));
        if (!options.theme.empty()) writeBytes(game / "Assets" / "json" / "ui_theme.json", options.theme);
        if (options.catalog) writeBytes(game / "Assets" / "json" / "extras.json", catalogText(options));
        for (const auto& [path, bytes] : options.files) writeBytes(game / "Assets" / path, bytes);
        if (options.fonts) {
            fs::create_directories(game / "Assets" / "fonts");
            fs::copy_file("Assets/fonts/default.ttf", game / "Assets" / "fonts" / "default.ttf");
        }
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
    void run(float seconds) const {
        for (int i = 0, n = static_cast<int>(std::ceil(seconds / 0.2f)); i < n; ++i) step(0.2f);
    }
    void event(RuntimeInputEvent e) {
        host->input.push_back(e);
        step(0.016f);
    }
    void key(InType type) { RuntimeInputEvent e{}; e.type = type; event(e); }
    void pointer(InType type, float x, float y) { RuntimeInputEvent e{}; e.type = type; e.x = x; e.y = y; event(e); }
    json theme() const {
        const json parsed = json::parse(capiText(h, RowlEngine_GetUiThemeJson), nullptr, false);
        req(parsed.is_object(), "theme JSON did not parse");
        return parsed;
    }
    json menu() const {
        const char* text = RowlEngine_GetPauseMenuJson(h);
        json parsed = json::parse(text ? text : "", nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    void writeTheme(const std::string& text) const { writeBytes(game / "Assets" / "json" / "ui_theme.json", text); }
    void command(int c) const { RowlEngine_PauseMenuCommand(h, c); }

    std::string frame(int steps = 4) const {
        step(0.016f, steps);
        uint32_t w = 0, hh = 0;
        const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
        req(pixels != nullptr && w == kW && hh == kH, "offscreen pixel buffer");
        std::string out(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
        dumpFrame(out);
        return out;
    }
};

Rgb at(const std::string& pixels, int x, int y) {
    const size_t i = (static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4;
    return {static_cast<uint8_t>(pixels[i]), static_cast<uint8_t>(pixels[i + 1]), static_cast<uint8_t>(pixels[i + 2])};
}

bool same(const Rgb& a, int r, int g, int b, int tolerance = 2) {
    return std::abs(a.r - r) <= tolerance && std::abs(a.g - g) <= tolerance && std::abs(a.b - b) <= tolerance;
}

std::string rgbText(const Rgb& c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + ")";
}

void requireAt(const std::string& pixels, int x, int y, int r, int g, int b, const std::string& what, int tolerance = 2) {
    const Rgb got = at(pixels, x, y);
    req(same(got, r, g, b, tolerance), what + " at (" + std::to_string(x) + "," + std::to_string(y) + "): got " +
                                           rgbText(got) + ", want (" + std::to_string(r) + "," + std::to_string(g) +
                                           "," + std::to_string(b) + ")");
}

template <typename Pred>
bool anyPixel(const std::string& pixels, int x0, int y0, int x1, int y1, Pred pred) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (pred(at(pixels, x, y))) return true;
        }
    }
    return false;
}

std::vector<std::string> json_to_vec(const json& array) {
    std::vector<std::string> out;
    for (const auto& item : array) out.push_back(item.get<std::string>());
    return out;
}

// "n pixels differ in x0..x1 / y0..y1" - or "" when the frames are equal.
std::string diffSummary(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return "sizes differ";
    int n = 0, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            const size_t i = (size_t(y) * kW + x) * 4;
            if (a.compare(i, 4, b, i, 4) == 0) continue;
            ++n;
            x0 = std::min<int>(x0, x); y0 = std::min<int>(y0, y); x1 = std::max<int>(x1, x); y1 = std::max<int>(y1, y);
        }
    }
    if (n == 0) return "";
    return std::to_string(n) + " pixels differ in x " + std::to_string(x0) + ".." + std::to_string(x1) + " / y " +
           std::to_string(y0) + ".." + std::to_string(y1);
}

bool hasDiagnostic(const std::vector<std::string>& lines, const std::string& needle) {
    for (const auto& line : lines) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

// Physical positions (scale 0.5) of things the checks sample.
constexpr int kPanelX = 260, kPanelY = 55;          // panel top-left (520,110) virtual
constexpr int kDimSampleX = 100, kDimSampleY = 300;  // outside the panel
constexpr int kDlgX = 40, kDlgY = 430;               // dialogue box top-left (80,860)
constexpr int kChoiceAX = 340, kChoiceAY = 260;      // option A (680,520) 280x32
constexpr int kChoiceBY = 300;                       // option B (680,600)

// Moves the menu selection to `row` (Up/Down) and confirms it.
void openRow(Rig& rig, int row) {
    for (int i = 0; i < 14 && rig.menu()["selected"].get<int>() != row; ++i) {
        rig.command(rig.menu()["selected"].get<int>() < row ? ROWL_PAUSE_MENU_DOWN : ROWL_PAUSE_MENU_UP);
    }
    req(rig.menu()["selected"].get<int>() == row, "could not select row " + std::to_string(row));
    rig.command(ROWL_PAUSE_MENU_CONFIRM);
}

int rowTopPx(const Rig& rig, int index) {
    const int rows = static_cast<int>(rig.menu()["rows"].size());
    return static_cast<int>(LM::rowTop(index, rows) * 0.5f);
}
int rowHeightPx(const Rig& rig) {
    return static_cast<int>(LM::rowH(static_cast<int>(rig.menu()["rows"].size())) * 0.5f);
}

// ── 1. parser ───────────────────────────────────────────────────────────────
void parserTests() {
    // Every skin key, a variant that inherits, the shorthand and "none".
    const UiThemeLoad load = parseUiTheme(R"({"format":1,
        "menu":{"panel_skin":{"image":"ui/frame.png","mode":"nine_slice","slice":[8,9,10,11],"slice_scale":2,
                              "tint":"#FF8800CC","opacity":0.5,"filter":"nearest","fill":"#102030","border":"#405060",
                              "border_width":3,"corner_radius":12,"padding":[1,2,3,4],
                              "selected":{"tint":"#00FF00"}},
                "row_skin":"ui/row.png"},
        "screens":{"save":{"panel_skin":"none","row_skin":{"image":"ui/save_row.png","empty":{"image":"ui/empty.png"}},
                           "title_hidden":true,"title_color":"#112233","title_px":30,"dim":"#00000000",
                           "counter_px":20,"hint_hidden":true},
                   "pause":{"panel_skin":false}}})");
    req(load.usable && load.diagnostics.empty(), "skin theme parses clean: " + (load.diagnostics.empty() ? "" : load.diagnostics[0]));
    const UiSkin& panel = load.theme.menu.panelSkin.normal;
    req(panel.image == "ui/frame.png" && panel.mode == UiSkinMode::NineSlice && panel.slice[0] == 8 && panel.slice[1] == 9 &&
            panel.slice[2] == 10 && panel.slice[3] == 11 && panel.sliceScale == 2.0f &&
            panel.tint == UiColor{255, 136, 0, 204} && panel.opacity == 0.5f && !panel.smooth &&
            panel.fill == UiColor{16, 32, 48, 255} && panel.border == UiColor{64, 80, 96, 255} &&
            panel.borderWidth == 3.0f && panel.cornerRadius == 12.0f && panel.padding && (*panel.padding)[3] == 4.0f,
        "panel skin keys");
    const UiSkin* selected = load.theme.menu.panelSkin.find(UiSkinState::Selected);
    req(selected != nullptr && selected->image == "ui/frame.png" && selected->tint == UiColor{0, 255, 0, 255} &&
            selected->mode == UiSkinMode::NineSlice,
        "a variant starts as a copy of the normal skin and overrides only what it names");
    req(load.theme.menu.panelSkin.find(UiSkinState::Hover) == nullptr ||
            load.theme.menu.panelSkin.find(UiSkinState::Hover) == selected,
        "hover stands in for selected only");
    req(load.theme.menu.rowSkin.normal.image == "ui/row.png", "a bare string is the image");
    const int save = uiScreenIndex("save");
    req(save >= 0 && load.theme.screens[save].panel && load.theme.screens[save].panel->none &&
            load.theme.screens[save].row && load.theme.screens[save].row->normal.image == "ui/save_row.png" &&
            load.theme.screens[save].row->variant[static_cast<size_t>(UiSkinState::Empty)]->image == "ui/empty.png" &&
            load.theme.screens[save].titleHidden && load.theme.screens[save].titleColor == UiColor{17, 34, 51, 255} &&
            load.theme.screens[save].titlePx == 30.0f && load.theme.screens[save].hintHidden &&
            load.theme.screens[save].dim == UiColor{0, 0, 0, 0} && load.theme.screens[save].counterPx == 20.0f,
        "per-page overrides");
    req(load.theme.screens[uiScreenIndex("pause")].panel->none, "false is \"none\" too");
    req(!load.theme.screens[uiScreenIndex("load")].panel, "a page without a key has no override");

    // Round trip: the serializer writes every key the parser reads.
    const UiThemeLoad again = parseUiTheme(uiThemeToJson(load.theme));
    req(again.usable && again.diagnostics.empty() && again.theme == load.theme,
        "serialize -> parse is the identity: " + (again.diagnostics.empty() ? uiThemeToJson(again.theme) : again.diagnostics[0]));
    const UiTheme builtin = builtinUiTheme();
    const UiThemeLoad builtinAgain = parseUiTheme(uiThemeToJson(builtin));
    req(builtinAgain.diagnostics.empty() && builtinAgain.theme == builtin, "built-in theme round trips");
    req(!builtin.menu.panelSkin.normal.visible() && !builtin.story.dialogue.skin.normal.visible() &&
            builtin.preferences.valueStyle == "text" && !builtin.story.choice.showDisabled &&
            builtin.story.continueIndicator.skin.image.empty() && builtin.cursor.image.empty() &&
            builtin.titleScreen.background.image.empty() && !builtin.titleScreen.hideStoryFrame,
        "every skin is off by default");

    // Bad values: the field keeps its value, one diagnostic each.
    const UiThemeLoad bad = parseUiTheme(R"({"format":1,"menu":{"panel_skin":{
        "image":"../secret.png","mode":"diagonal","slice":[1,2],"slice_scale":"big","tint":"red","opacity":9,
        "filter":"blurry","fill":"#12","border_width":-3,"padding":"wide"},
        "row_skin":{"image":"ui/not_an_image.txt"},"overlay_skin":[1]},
        "slots":{"thumb_placeholder":7},"cursor":{"image":"/etc/passwd.png"},
        "title_screen":{"music":"../x.ogg"},"preferences":{"value_style":"knob"}})");
    req(bad.usable, "a file with bad skin fields still parses");
    const UiSkin& badPanel = bad.theme.menu.panelSkin.normal;
    req(badPanel.image.empty() && badPanel.mode == UiSkinMode::Stretch && badPanel.sliceScale == 1.0f &&
            badPanel.tint == UiColor{255, 255, 255, 255} && badPanel.opacity == 1.0f && badPanel.smooth &&
            !badPanel.fill && badPanel.borderWidth == 0.0f && !badPanel.padding,
        "bad skin fields keep their values (opacity and border_width clamp)");
    for (const char* needle : {"panel_skin.image", "panel_skin.mode", "panel_skin.slice", "panel_skin.slice_scale",
                               "panel_skin.tint", "panel_skin.opacity", "panel_skin.filter", "panel_skin.fill",
                               "panel_skin.border_width", "panel_skin.padding", "row_skin.image", "overlay_skin",
                               "thumb_placeholder", "cursor.image", "title_screen.music", "preferences.value_style"}) {
        req(hasDiagnostic(bad.diagnostics, needle), std::string("a diagnostic names ") + needle);
    }

    // uiThemeImagePaths lists each image once, with the key that names it.
    const auto paths = uiThemeImagePaths(load.theme);
    req(paths.size() == 4, "four distinct images: " + std::to_string(paths.size()));

    // High contrast: skins step aside unless the file says otherwise.
    const UiThemeLoad hc = parseUiTheme(R"({"format":1,"menu":{"panel_skin":{"image":"a.png"}},
        "screens":{"save":{"panel_skin":"none"}},"title_screen":{"background":{"image":"bg.png"}}})");
    req(hc.highContrast.has_value() && hc.highContrast->menu.panelSkin.normal.image.empty() &&
            !hc.highContrast->screens[static_cast<size_t>(uiScreenIndex("save"))].panel->none &&
            hc.highContrast->titleScreen.background.image.empty(),
        "no high_contrast key: the synthesized palette has no skins");
    req(hc.theme.menu.panelSkin.normal.image == "a.png", "...and the normal palette keeps them");
    const UiThemeLoad keep = parseUiTheme(R"({"format":1,"menu":{"panel_skin":{"image":"a.png"}},
        "high_contrast":{"keep_skins":true}})");
    req(keep.highContrast.has_value() && keep.highContrast->menu.panelSkin.normal.image == "a.png", "keep_skins keeps them");
    const UiThemeLoad plain = parseUiTheme(R"({"format":1,"menu":{"panel_fill":"#112233"}})");
    req(!plain.highContrast.has_value(), "no skins and no key: still no high-contrast palette");
    TEST_PASS("parser: every skin key, variants inherit, shorthand / none, bad values + diagnostics, round trip, high contrast");
}

// ── 2. a skin without an image is the old look ──────────────────────────────
void noImageIsTheOldLookTests() {
    Rig plain;
    Options o;
    // Skins that carry only a text inset / a page label change nothing visible.
    o.theme = R"({"format":1,"menu":{"panel_skin":{"padding":[10,10,10,10]},"row_skin":{"padding":[18,0,0,0]}},
        "story":{"dialogue":{"skin":{"padding":[24,28,24,8]}},"choice":{"skin":{"padding":[12,0,12,0]}}},
        "screens":{"title":{"counter_px":24}},"preferences":{"value_style":"text"}})";
    Rig dressed(o);
    req(dressed.theme()["diagnostics"].empty(), "no diagnostics");
    req(plain.frame(4) == dressed.frame(4), "story frame: identical pixels");
    RowlEngine_OpenTitleScreen(plain.h);
    RowlEngine_OpenTitleScreen(dressed.h);
    req(plain.frame(4) == dressed.frame(4), "title screen: identical pixels");
    plain.command(ROWL_PAUSE_MENU_DOWN);
    dressed.command(ROWL_PAUSE_MENU_DOWN);
    req(plain.frame(4) == dressed.frame(4), "title screen, other selection: identical pixels");
    TEST_PASS("skins without an image or colour change no pixel (story, title)");
}

// ── 3. panel ────────────────────────────────────────────────────────────────
Options skinGame(const std::string& theme) {
    Options o;
    o.files["ui/frame.png"] = framePng();
    o.files["ui/ring.png"] = framePng(true);
    o.files["ui/blue.png"] = solidPng(8, 8, 0, 0, 200);
    o.files["ui/pink.png"] = solidPng(8, 8, 200, 50, 200);
    o.files["ui/green.png"] = solidPng(8, 8, 20, 180, 20);
    o.files["ui/orange.png"] = solidPng(8, 8, 230, 120, 0);
    o.files["ui/purple.png"] = solidPng(8, 8, 120, 0, 200);
    o.theme = theme;
    return o;
}

void panelTests() {
    // 9-slice: slice_scale 2 on a half-size window = 1 image px per physical px.
    Rig rig(skinGame(R"({"format":1,"menu":{"panel_skin":{"image":"ui/frame.png","mode":"nine_slice",
        "slice":[8,8,8,8],"slice_scale":2}}})"));
    req(rig.theme()["diagnostics"].empty(), "frame image found: " + rig.theme().dump());
    RowlEngine_OpenTitleScreen(rig.h);
    const std::string title = rig.frame(4);
    requireAt(title, kPanelX + 3, kPanelY + 2, 10, 20, 30, "the marked corner pixel is NOT stretched (1:1)");
    requireAt(title, kPanelX + 1, kPanelY + 1, 255, 0, 0, "corner body", 1);
    requireAt(title, kPanelX + 220, kPanelY + 3, 0, 0, 255, "top edge stretches along the panel", 3);
    requireAt(title, kPanelX + 2, kPanelY + 200, 0, 255, 0, "left edge", 3);
    requireAt(title, kPanelX + 437, kPanelY + 200, 0, 255, 0, "right edge", 3);
    requireAt(title, kPanelX + 220, kPanelY + 427, 0, 0, 255, "bottom edge", 3);
    requireAt(title, kPanelX + 436, kPanelY + 427, 255, 0, 0, "bottom-right corner", 1);
    requireAt(title, kPanelX + 400, kPanelY + 14, 255, 255, 0, "centre fills", 3);
    // The panel colours are not drawn under an image.
    req(!anyPixel(title, kPanelX + 12, kPanelY + 12, kPanelX + 430, kPanelY + 24, [](const Rgb& p) { return same(p, 15, 15, 26, 8); }),
        "no panel fill under the picture");
    // The thin accent border is gone as well (the picture is the frame): the top-left pixel is the corner red.
    requireAt(title, kPanelX, kPanelY, 255, 0, 0, "no accent border over the frame", 1);

    // A screen's own panel beats the menu default; "none" removes the panel.
    Rig pages(skinGame(R"({"format":1,"menu":{"panel_skin":{"image":"ui/blue.png"}},
        "screens":{"title":{"panel_skin":{"image":"ui/pink.png"}},"preferences":{"panel_skin":"none"}}})"));
    RowlEngine_OpenTitleScreen(pages.h);
    requireAt(pages.frame(4), kPanelX + 300, kPanelY + 12, 200, 50, 200, "title page: its own panel skin", 2);
    // Title -> Preferences (row 3 without extras): no panel at all.
    pages.command(ROWL_PAUSE_MENU_DOWN);
    pages.command(ROWL_PAUSE_MENU_DOWN);
    pages.command(ROWL_PAUSE_MENU_DOWN);
    pages.command(ROWL_PAUSE_MENU_CONFIRM);
    req(pages.menu()["page"] == "preferences", "preferences page: " + pages.menu().dump());
    const std::string prefs = pages.frame(4);
    const Rgb inside = at(prefs, kPanelX + 300, kPanelY + 12);
    const Rgb outside = at(prefs, kDimSampleX, kDimSampleY);
    req(same(inside, outside.r, outside.g, outside.b, 2), "\"none\": the panel area is the dimmed story, got " + rgbText(inside));
    // Other pages: the menu default (blue) - the pause main page.
    RowlEngine_SetPaused(pages.h, 0);
    RowlEngine_SetPaused(pages.h, 1);
    req(pages.menu()["page"] == "pause", "pause page");
    requireAt(pages.frame(4), kPanelX + 300, kPanelY + 12, 0, 0, 200, "pause page: the menu default panel skin", 2);

    // Dim + a full-window overlay picture (half opaque) over it.
    Rig dim(skinGame(R"({"format":1,"menu":{"dim":"#00000000","overlay_skin":{"image":"ui/orange.png","opacity":0.5}}})"));
    RowlEngine_OpenTitleScreen(dim.h);
    const std::string overlay = dim.frame(4);
    Rig base;
    RowlEngine_OpenTitleScreen(base.h);
    const Rgb under = at(base.frame(4), kDimSampleX, kDimSampleY);  // veil 160 over the scene
    const Rgb over = at(overlay, kDimSampleX, kDimSampleY);
    req(over.r > 100 && over.r < 140 && over.g > 50 && over.g < 70 && over.b < 30,
        "overlay picture blends at half opacity (and the veil colour is clear): " + rgbText(over) + " vs veil " + rgbText(under));
    TEST_PASS("panel: 9-slice corners 1:1 + stretched edges, colours not drawn under a picture, page override, \"none\", dim + overlay picture");
}


// ── 4. rows, slot rows, thumbnails ──────────────────────────────────────────
void rowTests() {
    Rig rig(skinGame(R"({"format":1,"menu":{"row_skin":{"image":"ui/blue.png",
        "selected":{"image":"ui/pink.png"},"disabled":{"image":"ui/green.png"}}}})"));
    RowlEngine_OpenTitleScreen(rig.h);
    const std::string title = rig.frame(4);
    const int h = rowHeightPx(rig);
    const int r0 = rowTopPx(rig, 0), r1 = rowTopPx(rig, 1), r2 = rowTopPx(rig, 2);
    requireAt(title, 662, r0 + 4, 200, 50, 200, "selected row: its skin (no cyan highlight, no bar)", 2);
    requireAt(title, 292, r0 + 6, 200, 50, 200, "...and no accent bar on its left edge", 2);
    requireAt(title, 662, r1 + 4, 20, 180, 20, "disabled row (Devam Et without a save): disabled skin", 2);
    requireAt(title, 662, r2 + 4, 0, 0, 200, "ordinary row: normal skin", 2);
    req(h > 20, "row height sane");

    // Only a normal skin: the selected row keeps the picture and gets the accent bar, not the cyan fill.
    Rig normalOnly(skinGame(R"({"format":1,"menu":{"row_skin":{"image":"ui/blue.png"}}})"));
    RowlEngine_OpenTitleScreen(normalOnly.h);
    const std::string f = normalOnly.frame(4);
    requireAt(f, 662, r0 + 4, 0, 0, 200, "selected row without a selected skin keeps the normal picture", 2);
    requireAt(f, 292, r0 + 6, 56, 189, 248, "...with the accent bar", 2);

    // Per-page row skins: the title page differs from the Load page.
    Rig pages(skinGame(R"({"format":1,"menu":{"row_skin":{"image":"ui/blue.png"}},
        "screens":{"title":{"row_skin":{"image":"ui/orange.png"}}}})"));
    RowlEngine_OpenTitleScreen(pages.h);
    requireAt(pages.frame(4), 662, r2 + 4, 230, 120, 0, "title page: its own row skin", 2);
    pages.command(ROWL_PAUSE_MENU_DOWN);
    pages.command(ROWL_PAUSE_MENU_DOWN);
    pages.command(ROWL_PAUSE_MENU_CONFIRM);  // Oyunu Yukle
    req(pages.menu()["page"] == "load", "load page");
    const std::string load = pages.frame(4);
    requireAt(load, 662, rowTopPx(pages, 3) + 4, 0, 0, 200, "load page: the menu default row skin", 2);
    TEST_PASS("rows: normal / selected / disabled skins, bar-only selection over a normal picture, per-page row skins");
}

void slotTests() {
    Options o = skinGame(R"({"format":1,
        "slots":{"row_skin":{"image":"ui/blue.png","empty":{"image":"ui/green.png"},"corrupt":{"image":"ui/orange.png"},
                             "selected":{"image":"ui/pink.png"}},
                 "thumb_placeholder":"ui/purple.png","new_image":"ui/orange.png",
                 "thumb_frame_skin":{"image":"ui/ring.png","mode":"nine_slice","slice":[8,8,8,8],"slice_scale":2,
                                     "padding":[8,8,8,8]}}})");
    Rig rig(o);
    // A real save in slot 2 and a corrupt file in slot 5.
    req(RowlEngine_SaveGameSlot(rig.h, 2) == 1, "save slot 2");
    const fs::path saves = capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8);
    writeBytes(saves / "save_slot_5.json", "\x01\x02 not json");
    RowlEngine_SetPaused(rig.h, 1);
    openRow(rig, 1);  // Kaydet
    req(rig.menu()["page"] == "save", "save page: " + rig.menu().dump());
    const json m = rig.menu();
    req(m["rows"].size() == 10 && m["rows"][2]["value"] == "dolu" && m["rows"][5]["value"] == "bozuk", "slot states: " + m.dump());
    const std::string save = rig.frame(4);
    const int rowH = rowHeightPx(rig);
    auto rowY = [&](int i) { return rowTopPx(rig, i) + 4; };
    requireAt(save, 662, rowY(0), 200, 50, 200, "selected slot 0: selected skin", 2);
    requireAt(save, 662, rowY(1), 20, 180, 20, "empty slot: empty skin", 2);
    requireAt(save, 662, rowY(2), 0, 0, 200, "filled slot: normal skin", 2);
    requireAt(save, 662, rowY(5), 230, 120, 0, "corrupt slot: corrupt skin", 2);
    req(rowH > 10, "row height");
    // Selected empty slot on the Save page: the "new save" picture in the thumbnail box (540..636 x 75..129).
    requireAt(save, 640, 100, 230, 120, 0, "new-save picture in the thumbnail box", 2);
    requireAt(save, 589, 73, 10, 20, 30, "thumbnail frame corner is drawn around the box (1:1, marked pixel)", 1);
    // Select the filled slot: no PNG thumbnail on a quick save without a frame capture -> the placeholder.
    rig.command(ROWL_PAUSE_MENU_DOWN);
    rig.command(ROWL_PAUSE_MENU_DOWN);
    const json m2 = rig.menu();
    req(m2["selected"] == 2, "slot 2 selected");
    const bool hasThumb = m2["selected_has_thumbnail"].get<bool>();
    const std::string filled = rig.frame(4);
    if (!hasThumb) requireAt(filled, 640, 100, 120, 0, 200, "placeholder picture where the slot has no thumbnail", 2);
    // Load page: an empty selected slot shows the placeholder, never the "new save" picture.
    rig.command(ROWL_PAUSE_MENU_BACK);
    openRow(rig, 2);  // Yukle
    req(rig.menu()["page"] == "load", "load page");
    requireAt(rig.frame(4), 640, 100, 120, 0, 200, "load page: placeholder, not the new-save picture", 2);
    TEST_PASS("slots: empty / filled / corrupt / selected row skins, new-save picture, thumbnail placeholder + frame");
}

// ── 5. title screen ─────────────────────────────────────────────────────────
void titleScreenTests() {
    Rig plain;
    RowlEngine_OpenTitleScreen(plain.h);
    const std::string old = plain.frame(4);
    req(!same(at(old, kDimSampleX, kDimSampleY), 0, 0, 0, 1), "the old title shows the (dimmed) story: sanity");

    Rig rig(skinGame(R"({"format":1,"title_screen":{"background":{"image":"ui/green.png","mode":"stretch"},
        "logo":{"image":"ui/orange.png"}}})"));
    req(rig.theme()["diagnostics"].empty(), "images found");
    // Before the title opens, the story shows normally.
    requireAt(rig.frame(2), kDimSampleX, kDimSampleY, at(old, kDimSampleX, kDimSampleY).r * 255 / 95,
              at(old, kDimSampleX, kDimSampleY).g * 255 / 95, at(old, kDimSampleX, kDimSampleY).b * 255 / 95, "story before the title", 40);
    RowlEngine_OpenTitleScreen(rig.h);
    const std::string title = rig.frame(4);
    requireAt(title, kDimSampleX, kDimSampleY, 20, 180, 20, "the background picture fills the window (story frame hidden, no veil)", 2);
    requireAt(title, 20, 20, 20, 180, 20, "...also in the corner", 2);
    requireAt(title, 480, 60, 230, 120, 0, "the logo square sits at the top centre", 2);
    requireAt(title, kPanelX + 300, kPanelY + 12, 15, 28, 24, "the old panel (fill 235/255) over the picture", 4);
    // The Load page opened from the title keeps the backdrop (and has no logo).
    rig.command(ROWL_PAUSE_MENU_DOWN);
    rig.command(ROWL_PAUSE_MENU_DOWN);
    rig.command(ROWL_PAUSE_MENU_CONFIRM);
    req(rig.menu()["page"] == "load", "load page");
    const std::string load = rig.frame(4);
    requireAt(load, kDimSampleX, kDimSampleY, 20, 180, 20, "sub-pages opened from the title keep the backdrop", 2);
    req(!same(at(load, 480, 60), 230, 120, 0, 10), "...but the logo is the title page's only");
    // New game: the story is back, no backdrop.
    rig.command(ROWL_PAUSE_MENU_BACK);
    rig.command(ROWL_PAUSE_MENU_UP);
    rig.command(ROWL_PAUSE_MENU_UP);
    rig.command(ROWL_PAUSE_MENU_CONFIRM);
    req(RowlEngine_IsPaused(rig.h) == 0, "new game left the title");
    req(!same(at(rig.frame(4), kDimSampleX, kDimSampleY), 20, 180, 20, 5), "the backdrop is gone with the title");
    // The pause menu mid-story does not use the title backdrop.
    RowlEngine_SetPaused(rig.h, 1);
    req(!same(at(rig.frame(4), kDimSampleX, kDimSampleY), 20, 180, 20, 5), "no backdrop on the pause menu");

    // hide_story_frame without a picture: plain black (or the named fill) instead of the dimmed story.
    Rig hidden(skinGame(R"({"format":1,"title_screen":{"hide_story_frame":true,"background":{"fill":"#102030"}}})"));
    RowlEngine_OpenTitleScreen(hidden.h);
    requireAt(hidden.frame(4), kDimSampleX, kDimSampleY, 0x10, 0x20, 0x30, "hide_story_frame: the named fill", 2);
    Rig black(skinGame(R"({"format":1,"title_screen":{"hide_story_frame":true}})"));
    RowlEngine_OpenTitleScreen(black.h);
    requireAt(black.frame(4), kDimSampleX, kDimSampleY, 0, 0, 0, "hide_story_frame: black", 1);
    // A veil can be put back on the title.
    Rig veiled(skinGame(R"({"format":1,"title_screen":{"background":{"image":"ui/green.png"},"dim":"#00000080"}})"));
    RowlEngine_OpenTitleScreen(veiled.h);
    const Rgb v = at(veiled.frame(4), kDimSampleX, kDimSampleY);
    req(v.g > 80 && v.g < 100 && v.r < 15, "title_screen.dim veils the picture: " + rgbText(v));
    // A missing picture keeps the old look (and says so).
    Rig missing(skinGame(R"({"format":1,"title_screen":{"background":{"image":"ui/nope.png"}}})"));
    RowlEngine_OpenTitleScreen(missing.h);
    req(missing.frame(4) == [&] { Rig p; RowlEngine_OpenTitleScreen(p.h); return p.frame(4); }(), "missing background: the old title pixels");
    req(hasDiagnostic(json_to_vec(missing.theme()["diagnostics"]), "nope.png"), "...with a diagnostic");
    TEST_PASS("title screen: background hides the story frame (sub-pages too), logo, hide_story_frame + fill, dim, missing picture");
}


// ── 6. story: dialogue box, speaker tag, choices, continue indicator ────────
// Left-most / lowest pixel of "light" glyph ink (text colour #F1F5F9) in a rectangle.
int leftmostInk(const std::string& f, int x0, int y0, int x1, int y1) {
    for (int x = x0; x < x1; ++x) {
        if (anyPixel(f, x, y0, x + 1, y1, [](const Rgb& p) { return p.r > 150 && p.g > 150 && p.b > 150; })) return x;
    }
    return -1;
}

void dialogueTests() {
    // The theme's box skin.
    Rig rig(skinGame(R"({"format":1,"story":{"dialogue":{"skin":{"image":"ui/blue.png"}}}})"));
    const std::string f = rig.frame(4);
    requireAt(f, kDlgX + 840, kDlgY + 60, 0, 0, 200, "box skin replaces the box fill", 2);
    requireAt(f, kDlgX + 440, kDlgY, 0, 0, 200, "...and the cyan border (a picture is the frame)", 2);

    // A named border colour / width is drawn over the picture.
    Rig named(skinGame(R"({"format":1,"story":{"dialogue":{"skin":{"image":"ui/blue.png","border":"#FF0000","border_width":2}}}})"));
    const std::string n = named.frame(4);
    requireAt(n, kDlgX + 440, kDlgY, 255, 0, 0, "named border over the picture", 2);
    requireAt(n, kDlgX + 440, kDlgY + 1, 255, 0, 0, "border_width 2", 2);
    requireAt(n, kDlgX + 440, kDlgY + 2, 0, 0, 200, "...then the picture", 2);

    // The component's own custom_box_texture wins over the theme's image - and works without a theme.
    Options own = skinGame(R"({"format":1,"story":{"dialogue":{"skin":{"image":"ui/blue.png"}}}})");
    own.dialogue["custom_box_texture"] = "ui/pink.png";
    Rig beats(own);
    requireAt(beats.frame(4), kDlgX + 840, kDlgY + 60, 200, 50, 200, "custom_box_texture beats the theme image", 2);
    Options lone = skinGame("");
    lone.dialogue["custom_box_texture"] = "ui/pink.png";
    Rig alone(lone);
    requireAt(alone.frame(4), kDlgX + 840, kDlgY + 60, 200, 50, 200, "custom_box_texture draws without any theme (it was dead before)", 2);

    // Text padding: the left inset moves the text.
    Rig tight(skinGame(""));
    const int inkDefault = leftmostInk(tight.frame(4), kDlgX + 2, kDlgY + 10, kDlgX + 400, kDlgY + 70);
    Rig padded(skinGame(R"({"format":1,"story":{"dialogue":{"skin":{"padding":[200,28,24,8]}}}})"));
    const int inkPadded = leftmostInk(padded.frame(4), kDlgX + 2, kDlgY + 10, kDlgX + 400, kDlgY + 70);
    req(inkDefault >= kDlgX + 8 && inkDefault < kDlgX + 40, "default text inset: " + std::to_string(inkDefault - kDlgX));
    req(inkPadded >= kDlgX + 96 && inkPadded < kDlgX + 130, "padding.left 200 virtual moves the text to ~100 px: " + std::to_string(inkPadded - kDlgX));

    // Speaker tag: skin + offset.
    Rig tag(skinGame(R"({"format":1,"story":{"speaker_tag":{"skin":{"image":"ui/orange.png"},"offset_x":100,"offset_y":-10}}})"));
    const std::string t = tag.frame(4);
    requireAt(t, kDlgX + 12 + 50, kDlgY - 7 - 5, 230, 120, 0, "speaker tag skin, moved by the offset (100,-10 virtual)", 2);
    req(!same(at(t, kDlgX + 12, kDlgY - 7), 56, 189, 248, 6) && !same(at(t, kDlgX + 12, kDlgY - 7), 230, 120, 0, 6),
        "...and no longer at its old place");
    TEST_PASS("dialogue box: skin replaces fill + border, named border, custom_box_texture wins (and lives), padding moves the text, speaker tag skin + offset");
}

void choiceTests() {
    Options o = skinGame(R"({"format":1,"story":{"choice":{"skin":{"image":"ui/blue.png",
        "focused":{"image":"ui/pink.png"},"disabled":{"image":"ui/green.png"}}}}})");
    o.options = json::array({{{"option_id", "a"}, {"text", "Birinci"}},
                             {{"option_id", "b"}, {"text", "Ikinci"}, {"hover_image", "ui/orange.png"}},
                             {{"option_id", "c"}, {"text", "Kilitli"}, {"enabled", false}}});
    Rig rig(o);
    const int cy = 260;  // option A top; B +40; C +80
    std::string f = rig.frame(4);
    requireAt(f, kChoiceAX + 270, cy + 14, 0, 0, 200, "option A: normal skin", 2);
    requireAt(f, kChoiceAX + 270, cy + 40 + 14, 0, 0, 200, "option B: normal skin", 2);
    // Without show_disabled the locked option stays hidden, as before.
    req(!same(at(f, kChoiceAX + 270, cy + 80 + 14), 20, 180, 20, 6) && !same(at(f, kChoiceAX + 270, cy + 80 + 14), 0, 0, 200, 6),
        "the locked option is not drawn by default");
    rig.key(InType::MenuDown);  // focus A
    f = rig.frame(4);
    requireAt(f, kChoiceAX + 270, cy + 14, 200, 50, 200, "focused option A: the theme's focused skin", 2);
    rig.key(InType::MenuDown);  // focus B
    f = rig.frame(4);
    requireAt(f, kChoiceAX + 270, cy + 40 + 14, 230, 120, 0, "focused option B: its own hover_image beats the theme", 2);
    requireAt(f, kChoiceAX + 270, cy + 14, 0, 0, 200, "A is back to normal", 2);

    // show_disabled draws the locked option with the disabled skin; it still cannot be chosen.
    Options shown = o;
    shown.theme = R"({"format":1,"story":{"choice":{"show_disabled":true,"skin":{"image":"ui/blue.png",
        "disabled":{"image":"ui/green.png"}}}}})";
    Rig showRig(shown);
    f = showRig.frame(4);
    requireAt(f, kChoiceAX + 270, cy + 80 + 14, 20, 180, 20, "show_disabled: the locked option wears the disabled skin", 2);
    showRig.key(InType::MenuDown);
    showRig.key(InType::MenuDown);
    showRig.key(InType::MenuDown);
    const std::string focusedId = showRig.engine->getLaidOutChoiceButtons()[2].focused ? "c" : "";
    req(focusedId.empty(), "the locked option never takes the focus");
    TEST_PASS("choice buttons: normal / focused / disabled skins, option's own hover_image wins, show_disabled draws locked options (unselectable)");
}

void continueIndicatorTests() {
    Options o = skinGame(R"({"format":1,"story":{"continue_indicator":{"skin":{"image":"ui/orange.png"},
        "width":32,"height":32,"animation":"bounce","period":1,"amplitude":8}}})");
    o.choices = false;
    o.dialogue["typewriter_enabled"] = true;
    o.dialogue["text_speed"] = 100;  // 100 ms per character: a few seconds for the line
    o.dialogue["dialogue"] = "Merhaba dunya, bu satir yazilirken gosterge gorunmez.";
    Rig rig(o);
    // Indicator rect: bottom-right of the box (1840,1040) + (-24,-20), 32x32 virtual -> physical 892..908 x 494..510.
    const auto indicatorHere = [](const std::string& f, int y0, int y1) {
        return anyPixel(f, 892, y0, 908, y1, [](const Rgb& p) { return same(p, 230, 120, 0, 6); });
    };
    rig.step(0.05f, 2);
    std::string typing = rig.frame(1);
    req(!indicatorHere(typing, 480, 520), "while the line is typing the mark is hidden");
    rig.run(8.0f);
    // Find the clock phase and aim for phase 0 (no lift) and 0.5 (full lift, 4 physical px).
    const auto stepToPhase = [&](double phase) {
        const double now = rig.engine->getWindow()->uiClock();
        double delta = phase - (now - std::floor(now));
        if (delta < 0.0) delta += 1.0;
        if (delta < 0.05) delta += 1.0;
        for (double left = delta; left > 1e-4; left -= 0.05) rig.step(static_cast<float>(std::min(0.05, left)));
        return rig.frame(1);  // a 16 ms step: the phase stays within 0.02 of the aim
    };
    const std::string rest = stepToPhase(0.0);
    req(indicatorHere(rest, 494, 510), "text done: the mark shows");
    const std::string lifted = stepToPhase(0.5);
    req(indicatorHere(lifted, 488, 492), "bounce: at half period the mark is lifted ~4 px");
    req(!indicatorHere(rest, 488, 492), "...and rests lower at phase 0");
    // Reduced motion: the mark stands still.
    RowlEngine_SetReducedMotion(rig.h, 1);
    const std::string still1 = stepToPhase(0.0);
    const std::string still2 = stepToPhase(0.5);
    req(indicatorHere(still1, 494, 510) && still1 == still2, "reduced motion: no animation, same pixels at any phase");
    // Blink animation.
    Options b = o;
    b.theme = R"({"format":1,"story":{"continue_indicator":{"skin":{"image":"ui/orange.png"},"animation":"blink","period":1}}})";
    Rig blink(b);
    blink.run(8.0f);
    const auto phaseFrame = [&](double phase) {
        const double now = blink.engine->getWindow()->uiClock();
        double delta = phase - (now - std::floor(now));
        if (delta < 0.0) delta += 1.0;
        if (delta < 0.05) delta += 1.0;
        for (double left = delta; left > 1e-4; left -= 0.05) blink.step(static_cast<float>(std::min(0.05, left)));
        return blink.frame(1);
    };
    const std::string on = phaseFrame(0.1);
    const std::string off = phaseFrame(0.6);
    const Rgb pOn = at(on, 900, 502), pOff = at(off, 900, 502);
    req(pOn.r > pOff.r + 40, "blink: bright then dim: " + rgbText(pOn) + " / " + rgbText(pOff));
    // With a choice menu on screen the mark is off (the story waits for an answer).
    Options c = o;
    c.choices = true;
    Rig withChoices(c);
    withChoices.run(8.0f);
    req(!indicatorHere(withChoices.frame(2), 480, 520), "no mark while options are up");
    // Without an image nothing is drawn (default).
    Options d;
    d.choices = false;
    d.dialogue = o.dialogue;
    Rig none(d);
    none.run(8.0f);
    req(!indicatorHere(none.frame(2), 480, 520), "off by default");
    TEST_PASS("continue indicator: hidden while typing, shown when done, bounce / blink, still under reduced motion, off with options up");
}


// ── 7. Ekstralar, History, toast, badges ────────────────────────────────────
Options extrasGame(const std::string& theme, bool lockedIconInCatalog = true) {
    Options o = skinGame(theme);
    o.catalog = true;
    json a0 = {{"id", "a0"}, {"title", "Birinci"}, {"description", "Aciklama"}, {"icon", "ui/orange.png"}};
    json a1 = {{"id", "a1"}, {"title", "Ikinci"}, {"description", "Aciklama"}};
    if (lockedIconInCatalog) a1["icon"] = "ui/orange.png";
    o.catalogOverride = {{"format", 1},
                         {"gallery", json::array({{{"id", "g0"}, {"title", "Bir"}, {"image", "ui/green.png"}},
                                                  {{"id", "g1"}, {"title", "Iki"}, {"image", "ui/green.png"}}})},
                         {"achievements", json::array({a0, a1})}};
    return o;
}

void extrasTests() {
    Rig rig(extrasGame(R"({"format":1,"extras":{
        "cell_skin":{"image":"ui/ring.png","mode":"nine_slice","slice":[8,8,8,8],"slice_scale":2,
                     "locked":{"image":"ui/green.png","mode":"stretch"},"selected":{"image":"ui/pink.png","mode":"stretch"}},
        "locked_image":"ui/purple.png",
        "viewer_frame_skin":{"image":"ui/ring.png","mode":"nine_slice","slice":[8,8,8,8],"slice_scale":2,
                             "padding":[8,8,8,8]},
        "plate_skin":{"image":"ui/blue.png","locked":{"image":"ui/green.png"},"selected":{"image":"ui/pink.png"}},
        "locked_icon":"ui/purple.png","icon_size":40}})"));
    req(rig.theme()["diagnostics"].empty(), "images found: " + rig.theme().dump());
    req(RowlEngine_ProfileUnlock(rig.h, "cg", "g1") == ROWL_RESULT_OK, "unlock g1");
    req(RowlEngine_ProfileUnlock(rig.h, "achievement", "a0") == ROWL_RESULT_OK, "unlock a0");
    rig.engine->step(0.016f);  // let the toast clock tick before the menu opens
    req(RowlEngine_OpenExtras(rig.h) == ROWL_RESULT_OK, "extras");
    openRow(rig, 0);  // Galeri
    req(rig.menu()["page"] == "gallery", "gallery");
    const int c0x = static_cast<int>(LM::galleryCellX(0) * 0.5f), c1x = static_cast<int>(LM::galleryCellX(1) * 0.5f);
    const int cy = static_cast<int>(LM::galleryCellY(0) * 0.5f);
    std::string f = rig.frame(4);
    // Cell 0: locked + selected. Plate (selected skin) around it, locked skin on the box, the locked picture in it.
    requireAt(f, c0x - 1, cy + 25, 200, 50, 200, "selected cell: the selected skin behind the cell", 2);
    requireAt(f, c0x + 3, cy + 3, 20, 180, 20, "locked cell box: the locked skin", 2);
    requireAt(f, c0x + 44, cy + 25, 120, 0, 200, "the locked picture replaces the \"?\"", 2);
    // Cell 1: unlocked, not selected - the picture sits on the backdrop, the ring frame goes over it.
    requireAt(f, c1x + 44, cy + 25, 20, 180, 20, "unlocked cell: the unlocked picture", 2);
    requireAt(f, c1x + 3, cy + 2, 10, 20, 30, "frame ring over the picture (marked corner pixel, 1:1)", 1);
    requireAt(f, c1x + 12, cy + 25, 8, 10, 18, "the backdrop (cell_fill) shows beside the fitted picture", 3);
    // Viewer: Right selects cell 1, Confirm opens the picture; its frame goes around it.
    rig.command(ROWL_PAUSE_MENU_RIGHT);
    rig.command(ROWL_PAUSE_MENU_CONFIRM);
    req(rig.menu()["extras"]["viewing"] == true, "viewer open");
    f = rig.frame(4);
    req(anyPixel(f, 240, 0, 252, 14, [](const Rgb& p) { return same(p, 10, 20, 30, 1); }), "viewer frame corner around the picture");
    rig.command(ROWL_PAUSE_MENU_BACK);
    rig.command(ROWL_PAUSE_MENU_BACK);
    openRow(rig, 2);  // Basarimlar
    req(rig.menu()["page"] == "achievements", "achievements");
    const json items = rig.menu()["extras"]["items"];
    const int p0 = static_cast<int>(items[0]["y"].get<float>() * 0.5f), p1 = static_cast<int>(items[1]["y"].get<float>() * 0.5f);
    f = rig.frame(4);
    requireAt(f, 640, p0 + 4, 200, 50, 200, "selected unlocked plate: selected skin", 2);
    requireAt(f, 640, p1 + 4, 20, 180, 20, "locked plate: locked skin", 2);
    requireAt(f, 309, p0 + 24, 230, 120, 0, "unlocked achievement: its own icon", 2);
    requireAt(f, 309, p1 + 24, 120, 0, 200, "locked achievement: the theme's locked icon, never its own", 2);
    // And when the theme has no locked icon, a locked plate shows nothing of the catalog's icon.
    Rig bare(extrasGame(R"({"format":1,"extras":{"plate_skin":{"image":"ui/blue.png","locked":{"image":"ui/green.png"}}}})"));
    RowlEngine_OpenExtras(bare.h);
    openRow(bare, 2);
    const json bi = bare.menu()["extras"]["items"];
    const std::string bf = bare.frame(4);
    const int q1 = static_cast<int>(bi[1]["y"].get<float>() * 0.5f);
    req(!anyPixel(bf, 295, q1 + 2, 340, q1 + 46, [](const Rgb& p) { return same(p, 230, 120, 0, 10); }),
        "a locked achievement's catalog icon is never drawn");
    TEST_PASS("Ekstralar: cell skins (locked picture instead of \"?\"), plates (locked / selected), icons (never a locked one's), viewer frame");
}

void historyToastBadgeTests() {
    // History: entries wear the page's row skin.
    Options o = skinGame(R"({"format":1,"screens":{"history":{"row_skin":{"image":"ui/blue.png","selected":{"image":"ui/pink.png"}}}}})");
    o.choices = false;
    o.second = true;
    Rig rig(o);
    rig.key(InType::Advance);
    rig.key(InType::Advance);
    rig.step(0.05f, 2);
    req(RowlEngine_OpenBacklog(rig.h) == ROWL_RESULT_OK, "backlog");
    req(rig.menu()["page"] == "history", "history page");
    const auto view = rig.engine->getPauseMenuView();
    req(view.backlog.entries.size() >= 2, "two history entries, got " + std::to_string(view.backlog.entries.size()) + " total " + std::to_string(view.backlog.total) + " node " + std::to_string(RowlEngine_GetCurrentNodeId(rig.h)));
    const std::string f = rig.frame(4);
    for (const auto& entry : view.backlog.entries) {
        const int y = static_cast<int>(entry.y * 0.5f) + 2;
        if (entry.selected) requireAt(f, 660, y, 200, 50, 200, "selected history entry: selected skin", 2);
        else requireAt(f, 660, y, 0, 0, 200, "history entry: normal skin", 2);
    }

    // Toast: skin and corner.
    Options t = extrasGame(R"({"format":1,"toast":{"skin":{"image":"ui/blue.png"},"anchor":"bottom_left","margin_x":20,"margin_top":30}})");
    Rig toast(t);
    req(RowlEngine_ProfileUnlock(toast.h, "achievement", "a0") == ROWL_RESULT_OK, "unlock");
    const std::string tf = toast.frame(2);
    req(toast.engine->unlockToastText().rfind("Ba", 0) == 0, "toast is up");
    requireAt(tf, 14, 505, 0, 0, 200, "toast capsule: skin, at the bottom-left corner", 2);
    req(!anyPixel(tf, 380, 20, 580, 60, [](const Rgb& p) { return same(p, 56, 189, 248, 6); }), "no accent rule at the top centre any more");

    // Badges: skin and corner. AUTO stacks to the right of the corner (SKIP follows it).
    Options b = skinGame(R"({"format":1,"badges":{"anchor":"bottom_left","margin_right":20,"margin_top":30,
        "auto_skin":{"image":"ui/blue.png"},"skip_skin":{"image":"ui/pink.png"}}})");
    Rig badge(b);
    RowlEngine_SetAutoMode(badge.h, 1);
    const std::string bf = badge.frame(2);
    requireAt(bf, 20, 512, 0, 0, 200, "AUTO badge: skin, bottom-left", 2);
    req(!anyPixel(bf, 884, 22, 892, 28, [](const Rgb& p) { return same(p, 0, 0, 200, 6); }), "nothing at the old top-right place");
    TEST_PASS("History entries wear the page row skin; toast and AUTO/SKIP badges take skins and corners");
}

// ── 8. Preferences slider ───────────────────────────────────────────────────
void sliderTests() {
    Options o = skinGame(R"({"format":1,"preferences":{"value_style":"slider"}})");
    Rig rig(o);
    RowlEngine_OpenTitleScreen(rig.h);
    openRow(rig, 3);  // Tercihler (no extras)
    req(rig.menu()["page"] == "preferences", "preferences");
    // Row 0 (master volume) on the compact pitch: top 250 virtual, 50 high.
    const int rowY = 125 + 12;
    const float x0 = LM::kSliderX0 * 0.5f, x1 = LM::kSliderX1 * 0.5f;
    std::string f = rig.frame(4);
    // The slider replaces the "100%" text beside the label: the value moves to the right edge.
    requireAt(f, static_cast<int>(x0) + 5, rowY, 56, 189, 248, "full slider: the filled track in the accent colour", 3);
    // Click at 50%.
    const float mid = x0 + (x1 - x0) * 0.5f;
    rig.pointer(InType::PointerDown, mid, static_cast<float>(rowY));
    req(rig.menu()["rows"][0]["value"] == "50%", "click on the track sets 50%: " + rig.menu()["rows"][0].dump());
    f = rig.frame(4);
    requireAt(f, static_cast<int>(x0) + 8, rowY, 56, 189, 248, "left half is filled", 3);
    req(!same(at(f, static_cast<int>(x1) - 6, rowY), 56, 189, 248, 12), "right half is not");
    // Drag to 25%, release, move again: the value stays.
    rig.pointer(InType::PointerMotion, x0 + (x1 - x0) * 0.25f, static_cast<float>(rowY));
    req(rig.menu()["rows"][0]["value"] == "25%", "dragging follows the pointer: " + rig.menu()["rows"][0].dump());
    rig.pointer(InType::PointerUp, x0 + (x1 - x0) * 0.25f, static_cast<float>(rowY));
    rig.pointer(InType::PointerMotion, x0 + (x1 - x0) * 0.9f, static_cast<float>(rowY));
    req(rig.menu()["rows"][0]["value"] == "25%", "after the release the pointer no longer moves it");
    // Keys still step it; text speed row maps its own range.
    rig.command(ROWL_PAUSE_MENU_RIGHT);
    req(rig.menu()["rows"][0]["value"] == "30%", "keys still work with the slider style");
    // Row 4 (text speed 0.25..4x): click the right end = 4.00x.
    const int row4Y = static_cast<int>(LM::rowTop(4, 12) * 0.5f) + 12;
    rig.pointer(InType::PointerDown, x1 + 2.0f, static_cast<float>(row4Y));
    rig.pointer(InType::PointerUp, x1 + 2.0f, static_cast<float>(row4Y));
    req(rig.menu()["rows"][4]["value"] == "4.00x", "text speed row: right end = 4.00x: " + rig.menu()["rows"][4].dump());
    // The default style ignores the track: a click there steps with the legacy zones.
    Rig legacy;
    RowlEngine_OpenTitleScreen(legacy.h);
    openRow(legacy, 3);
    legacy.pointer(InType::PointerDown, mid, static_cast<float>(rowY));
    req(legacy.menu()["rows"][0]["value"] != "50%", "value_style text: no slider behaviour");

    // Knob / track / fill skins.
    Rig skinned(skinGame(R"({"format":1,"preferences":{"value_style":"slider","show_value":false,
        "track_skin":{"image":"ui/blue.png"},"fill_skin":{"image":"ui/green.png"},
        "knob_skin":{"image":"ui/pink.png","pressed":{"image":"ui/orange.png"}},"knob_size":16,"track_height":10}})"));
    RowlEngine_OpenTitleScreen(skinned.h);
    openRow(skinned, 3);
    skinned.pointer(InType::PointerDown, mid, static_cast<float>(rowY));
    f = skinned.frame(4);
    requireAt(f, static_cast<int>(x0) + 10, rowY, 20, 180, 20, "fill skin", 2);
    requireAt(f, static_cast<int>(x1) - 10, rowY, 0, 0, 200, "track skin", 2);
    requireAt(f, static_cast<int>(mid), rowY, 230, 120, 0, "knob while dragging: pressed skin", 2);
    skinned.pointer(InType::PointerUp, mid, static_cast<float>(rowY));
    requireAt(skinned.frame(4), static_cast<int>(mid), rowY, 200, 50, 200, "knob at rest: normal skin", 2);
    TEST_PASS("Preferences slider: click + drag set the value, release stops it, keys still step, track / fill / knob skins");
}


// ── 9. diagnostics, reload, high contrast, page text styles ─────────────────
void diagnosticTests() {
    Options o = skinGame(R"({"format":1,"menu":{"panel_skin":{"image":"ui/missing.png"},
        "row_skin":{"image":"ui/broken.png"}},"story":{"dialogue":{"skin":{"image":"ui/huge.png"}}}})");
    o.files["ui/broken.png"] = "this is not an image at all";
    o.files["ui/huge.png"] = pngBytes(8200, 1, [](int, int) { return Pixel{1, 2, 3, 255}; });
    Rig rig(o);
    const json t = rig.theme();
    std::vector<std::string> lines = json_to_vec(t["diagnostics"]);
    req(t["loaded"] == true, "the game starts");
    req(hasDiagnostic(lines, "ui/missing.png") && hasDiagnostic(lines, "was not found"), "missing image reported: " + t.dump());
    req(hasDiagnostic(lines, "ui/broken.png") && hasDiagnostic(lines, "not a readable image"), "broken image reported");
    req(hasDiagnostic(lines, "ui/huge.png") && hasDiagnostic(lines, "too large"), "oversized image reported");
    req(hasDiagnostic(lines, "menu.panel_skin.image"), "...with the key that names it");
    // The surfaces keep their colours.
    RowlEngine_OpenTitleScreen(rig.h);
    const std::string title = rig.frame(4);
    Rig plain;
    RowlEngine_OpenTitleScreen(plain.h);
    req(diffSummary(title, plain.frame(4)).empty(),
        "with every image unusable the title is pixel for pixel the old one: " + diffSummary(title, plain.frame(4)));
    const std::string story = [&] { RowlEngine_SetPaused(rig.h, 0); return rig.frame(4); }();
    requireAt(story, kDlgX + 840, kDlgY + 60, 15, 15, 26, "oversized dialogue picture: the box keeps its fill", 2);

    // Reload: a picture that appears later is used; one that changes on disk is re-read.
    Rig live(skinGame(R"({"format":1,"menu":{"panel_skin":{"image":"ui/later.png"}}})"));
    RowlEngine_OpenTitleScreen(live.h);
    requireAt(live.frame(4), kPanelX + 300, kPanelY + 12, 15, 15, 26, "not there yet: the panel fill", 8);
    req(live.theme()["diagnostics"].size() == 1, "one diagnostic");
    writeBytes(live.game / "Assets" / "ui" / "later.png", solidPng(8, 8, 0, 0, 200));
    req(RowlEngine_ReloadUiTheme(live.h) == ROWL_RESULT_OK, "reload");
    requireAt(live.frame(1), kPanelX + 300, kPanelY + 12, 0, 0, 200, "reload picks the new picture at once", 2);
    req(live.theme()["diagnostics"].empty(), "...and the diagnostic is gone");
    writeBytes(live.game / "Assets" / "ui" / "later.png", solidPng(8, 8, 200, 50, 200));
    RowlEngine_ReloadUiTheme(live.h);
    requireAt(live.frame(1), kPanelX + 300, kPanelY + 12, 200, 50, 200, "a picture changed on disk is re-read on reload (no stale texture)", 2);
    // SetUiThemeJson previews without a file.
    req(RowlEngine_SetUiThemeJson(live.h, R"({"format":1,"menu":{"panel_skin":{"image":"ui/later.png","tint":"#00FF00"}}})") == ROWL_RESULT_OK,
        "inline preview");
    const Rgb tinted = at(live.frame(1), kPanelX + 300, kPanelY + 12);
    req(tinted.r < 20 && tinted.g > 40 && tinted.g < 60 && tinted.b < 20, "tint multiplies the picture: " + rgbText(tinted));
    // The theme JSON carries the resolved skins for the editor.
    const json resolved = live.theme()["theme"]["menu"]["panel_skin"];
    req(resolved["image"] == "ui/later.png" && resolved["tint"] == "#00FF00" && resolved["mode"] == "stretch",
        "GetUiThemeJson reports the skin: " + resolved.dump());
    TEST_PASS("diagnostics: missing / broken / oversized pictures are named and the old colours stay; reload re-reads pictures; tint");
}

void highContrastTests() {
    const std::string theme = R"({"format":1,"menu":{"panel_skin":{"image":"ui/blue.png"}}})";
    Rig rig(skinGame(theme));
    RowlEngine_OpenTitleScreen(rig.h);
    requireAt(rig.frame(4), kPanelX + 300, kPanelY + 12, 0, 0, 200, "normal: the picture", 2);
    RowlEngine_SetHighContrast(rig.h, 1);
    requireAt(rig.frame(4), kPanelX + 300, kPanelY + 12, 15, 15, 26, "high contrast: the plain panel", 8);
    RowlEngine_SetHighContrast(rig.h, 0);
    requireAt(rig.frame(4), kPanelX + 300, kPanelY + 12, 0, 0, 200, "back to the picture", 2);
    Rig keep(skinGame(R"({"format":1,"menu":{"panel_skin":{"image":"ui/blue.png"}},"high_contrast":{"keep_skins":true}})"));
    RowlEngine_OpenTitleScreen(keep.h);
    RowlEngine_SetHighContrast(keep.h, 1);
    requireAt(keep.frame(4), kPanelX + 300, kPanelY + 12, 0, 0, 200, "keep_skins: the picture stays", 2);
    TEST_PASS("high contrast drops skins unless keep_skins");
}

void pageTextStyleTests() {
    Rig rig(skinGame(R"({"format":1,"screens":{"title":{"title_hidden":true,"hint_hidden":true},
        "preferences":{"title_color":"#FF0000","title_px":60}}})"));
    RowlEngine_OpenTitleScreen(rig.h);
    const std::string f = rig.frame(4);
    // The game-name title (accent cyan, y ~ 85..110) and the hint line (dim grey, y ~ 463..478) are gone.
    req(!anyPixel(f, 290, 85, 670, 115, [](const Rgb& p) { return same(p, 56, 189, 248, 25); }), "title_hidden: no page title");
    req(!anyPixel(f, 290, 460, 670, 485, [](const Rgb& p) { return p.r > 100 && p.g > 110 && p.b > 130; }), "hint_hidden: no hint line");
    Rig plain;
    RowlEngine_OpenTitleScreen(plain.h);
    const std::string p = plain.frame(4);
    req(anyPixel(p, 290, 85, 670, 115, [](const Rgb& c) { return same(c, 56, 189, 248, 25); }), "(the old title does draw both)");
    req(anyPixel(p, 290, 460, 670, 485, [](const Rgb& c) { return c.r > 100 && c.g > 110 && c.b > 130; }), "(hint too)");
    // Preferences: red 60 px title.
    openRow(rig, 3);
    const std::string prefs = rig.frame(4);
    req(anyPixel(prefs, 290, 85, 670, 125, [](const Rgb& c) { return c.r > 200 && c.g < 60 && c.b < 60; }), "title_color on the Preferences page");
    TEST_PASS("page text styles: title / hint hidden, title colour + size per page");
}

// ── 10. visible window: the same drawing + the theme cursor ─────────────────
void visibleWindowTests() {
    const fs::path project = fs::temp_directory_path() / "rowl_ui_skin_visible" / "project";
    std::error_code removeEc;
    fs::remove_all(project.parent_path(), removeEc);
    writeBytes(project / "Assets" / "ui" / "blue.png", solidPng(8, 8, 0, 0, 200));
    writeBytes(project / "Assets" / "ui" / "cursor.png", pngBytes(16, 16, [](int x, int y) {
        return Pixel{255, 255, 255, static_cast<uint8_t>(x + y < 16 ? 255 : 0)};
    }));
    Rowl::VFS::VFSManager vfs;
    vfs.remountProject(project.string());
    Rowl::Render::Window window(&vfs);
    if (!window.initialize("ui-skin", 960, 540, false)) rowlLockFail(kSection, "visible window could not be initialized");
    window.reloadFonts();
    req(!window.hasThemeCursor(), "no cursor before a theme");
    const UiThemeLoad load = parseUiTheme(R"({"format":1,"menu":{"panel_skin":{"image":"ui/blue.png"}},
        "cursor":{"image":"ui/cursor.png","hotspot_x":1,"hotspot_y":1,"scale":2}})");
    req(load.diagnostics.empty(), "theme parses");
    window.setUiTheme(load.theme, load.highContrast);
    req(window.skinDiagnostics().empty(), "pictures found");
    req(window.hasThemeCursor(), "the theme cursor is installed on the window");

    Rowl::Core::PauseMenuView view;
    view.open = true;
    view.page = "pause";
    view.title = "Paused";
    view.hint = "Esc";
    for (const char* label : {"Resume", "Save", "Load"}) view.rows.push_back(Rowl::Core::makePauseRow(label, ""));
    view.selected = 1;
    SDL_Renderer* r = window.getRenderer();
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderClear(r);
    window.renderPauseMenuOverlay(view);
    SDL_Surface* surface = SDL_RenderReadPixels(r, nullptr);
    req(surface != nullptr, "read pixels");
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    req(rgba != nullptr, "convert");
    const uint8_t* px = static_cast<const uint8_t*>(rgba->pixels) + 70 * rgba->pitch + 560 * 4;
    const bool blue = px[0] < 20 && px[1] < 20 && px[2] > 180;
    SDL_DestroySurface(rgba);
    req(blue, "the panel picture draws on the visible-window path (GPU text branch)");
    // Replacing the theme drops the cursor again.
    window.setUiTheme(builtinUiTheme());
    req(!window.hasThemeCursor(), "a theme without a cursor restores the system one");
    const UiThemeLoad bad = parseUiTheme(R"({"format":1,"cursor":{"image":"ui/nope.png"}})");
    window.setUiTheme(bad.theme);
    req(!window.hasThemeCursor() && !window.skinDiagnostics().empty() &&
            window.skinDiagnostics()[0].find("system cursor stays") != std::string::npos,
        "a missing cursor image keeps the system cursor and says so");
    window.shutdown();
    fs::remove_all(project.parent_path(), removeEc);
    TEST_PASS("visible window: skins draw on the GPU text path; the theme cursor is installed and removed");
}

// ── 11. extras.json icon ────────────────────────────────────────────────────
void catalogIconTests() {
    const ExtrasCatalog good = parseExtrasCatalog(R"({"format":1,"achievements":[
        {"id":"a","title":"A","icon":"ui/a.png"},{"id":"b","title":"B"}]})");
    req(good.achievements.size() == 2 && good.achievements[0].icon == "ui/a.png" && good.achievements[1].icon.empty() &&
            good.diagnostics.empty(),
        "icon is an optional achievement field");
    const ExtrasCatalog bad = parseExtrasCatalog(R"({"format":1,"achievements":[
        {"id":"a","title":"A","icon":"../a.png"},{"id":"b","title":"B","icon":5},{"id":"c","title":"C"}]})");
    req(bad.achievements.size() == 1 && bad.achievements[0].id == "c" && bad.diagnostics.size() == 2,
        "a bad icon path skips that item with a diagnostic (like a bad thumbnail)");
    TEST_PASS("extras.json: achievement icon field");
}


// ── 12. title music ─────────────────────────────────────────────────────────
std::string wavBytes(int samples = 4000) {
    std::string pcm;
    for (int i = 0; i < samples; ++i) {
        const int16_t v = static_cast<int16_t>(2000 * std::sin(i * 0.2));
        pcm.push_back(static_cast<char>(v & 0xFF));
        pcm.push_back(static_cast<char>((v >> 8) & 0xFF));
    }
    auto le32 = [](uint32_t v) { std::string o; for (int i = 0; i < 4; ++i) o.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); return o; };
    auto le16 = [](uint16_t v) { std::string o; o.push_back(static_cast<char>(v & 0xFF)); o.push_back(static_cast<char>(v >> 8)); return o; };
    std::string wav = "RIFF" + le32(static_cast<uint32_t>(36 + pcm.size())) + "WAVEfmt " + le32(16) + le16(1) + le16(1) +
                      le32(8000) + le32(16000) + le16(2) + le16(16) + "data" + le32(static_cast<uint32_t>(pcm.size()));
    return wav + pcm;
}

void titleMusicTests() {
    const auto bgm = [](Rig& rig) {
        auto* audio = rig.engine->getAudio();
        return audio != nullptr && audio->isBgmPlaying() ? audio->getCurrentBgmPath() : std::string();
    };
    // No music configured: the title is silent, as before.
    {
        Options o;
        o.files["audio/title.wav"] = wavBytes();
        Rig rig(o);
        RowlEngine_OpenTitleScreen(rig.h);
        req(bgm(rig).empty(), "no title_screen.music: nothing plays");
    }
    // A story with no BGM of its own: the title music plays on the title and stops with it.
    Options o = skinGame(R"({"format":1,"title_screen":{"music":"audio/title.wav"}})");
    o.files["audio/title.wav"] = wavBytes();
    o.files["audio/story.wav"] = wavBytes(6000);
    Rig rig(o);
    req(rig.theme()["diagnostics"].empty(), "music path accepted");
    RowlEngine_OpenTitleScreen(rig.h);
    req(bgm(rig) == "audio/title.wav", "the title plays its music: '" + bgm(rig) + "'");
    // Opening the music room's parent page and coming back keeps (restarts) it; Preferences does not touch it.
    openRow(rig, 3);
    req(bgm(rig) == "audio/title.wav", "sub-pages keep the title music");
    rig.command(ROWL_PAUSE_MENU_BACK);
    openRow(rig, 0);  // Yeni Oyun
    req(RowlEngine_IsPaused(rig.h) == 0, "new game started");
    req(bgm(rig).empty(), "a story without BGM does not inherit the title music: '" + bgm(rig) + "'");
    // The host closing the title does the same.
    RowlEngine_OpenTitleScreen(rig.h);
    req(bgm(rig) == "audio/title.wav", "reopened title plays again");
    RowlEngine_SetPaused(rig.h, 0);
    req(bgm(rig).empty(), "SetPaused(0) closes the title and its music");
    // A story that brings its own BGM replaces the title music.
    Options withStory = o;
    withStory.dialogue = json::object();
    Rig other(withStory);
    {
        // add an audio component to the first node by rewriting the graph
        json graph = json::parse(buildGraph(withStory));
        graph["nodes"][0]["objects"][0]["components"].push_back(component("audio", "au1", {{"bgm_track", "audio/story.wav"}, {"volume", 1.0}}));
        writeBytes(other.game / "Assets" / "json" / "full_story_graph.json", graph.dump());
    }
    RowlEngine_SetProjectDirectory(other.h, other.game.string().c_str());
    RowlEngine_OpenTitleScreen(other.h);
    req(bgm(other) == "audio/title.wav", "title music first");
    openRow(other, 0);
    other.step(0.05f, 3);
    req(bgm(other) == "audio/story.wav", "the story's own BGM takes over: '" + bgm(other) + "'");
    // A path outside the project / a bad extension never reaches the audio engine.
    const UiThemeLoad bad = parseUiTheme(R"({"format":1,"title_screen":{"music":"../x.ogg"}})");
    req(bad.theme.titleScreen.music.empty() && !bad.diagnostics.empty(), "unsafe music path refused");
    TEST_PASS("title music: plays on the title (sub-pages keep it), stops with it unless the story brings its own BGM");
}

}  // namespace

void test_ui_skin() {
    TEST_SECTION("UI skins: image frames + colours for every piece of player UI (T2)");

    g_root = fs::temp_directory_path() / "rowl_ui_skin";
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
    noImageIsTheOldLookTests();
    panelTests();
    rowTests();
    slotTests();
    titleScreenTests();
    dialogueTests();
    choiceTests();
    continueIndicatorTests();
    extrasTests();
    historyToastBadgeTests();
    sliderTests();
    diagnosticTests();
    highContrastTests();
    pageTextStyleTests();
    catalogIconTests();
    titleMusicTests();

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(g_root, ec);
}

// Runs right after the pause-overlay visible-window test: a visible SDL window needs the event
// dispatch thread unclaimed, which later engine tests no longer guarantee.
void test_ui_skin_visible_window() {
    TEST_SECTION("UI skins on a visible (non-offscreen) window + theme cursor (T2)");
    visibleWindowTests();
}
