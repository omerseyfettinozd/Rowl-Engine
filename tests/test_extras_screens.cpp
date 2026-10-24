/**
 * test_extras_screens.cpp — D07-2: the title screen's "Ekstralar" pages (Galeri,
 * Muzik Odasi, Basarimlar) and the achievement toast.
 *
 * Kilit:
 *  1. Baslik satiri: katalog varken "Ekstralar" (Tercihler'den once, 6 satir),
 *     yokken / bos extras.json'da yok (5 satir, eski sira); duraklatma menusunde
 *     yok. Ekstralar koku: Galeri / Muzik Odasi / Basarimlar / Geri, her satirin
 *     degeri "acik/toplam"; Back/Esc/sag tik bir ust sayfaya, kokten basliga
 *     (secili satir Ekstralar). Tercihler Ekstralar'dan sonra da kendi satirina doner.
 *     OpenExtras: Init oncesi / katalogsuz STATE_ERROR, oyundayken basligi acar.
 *  2. Galeri: 4 sutunlu izgara, kilitli oge koyu kutu (resmi YUKLENMEZ, piksel
 *     kutu rengi, JSON'da yol yok), acik oge cizilir; ok tuslari / fare ile secim;
 *     Enter/tik acik ogeyi tam ekran acar (en-boy korunur, ortalanir), kilitliyi
 *     acmaz; tam ekranda SOL/SAG yalniz ACIK ogeler arasinda (sarar), Esc/tik/sag
 *     tik izgaraya doner; 18 ogede kaydirma; kucuk doku onbellegi sinirli ve genel
 *     doku onbellegine dokunmaz.
 *  3. Muzik Odasi: kilitli "???"; Enter acik parcayi calar (calan satir isaretli),
 *     tekrar Enter durdurur; GameState (activeBgm) degismez; Ekstralar'dan cikinca
 *     onceki ses geri gelir (baslikta, devam eden oyundan sonra baslikta, durumsuz).
 *  4. Basarimlar: acik (baslik + aciklama + tarih), kilitli (baslik gorunur,
 *     aciklama "???"), gizli+kilitli (baslik da "???"); acilinca gercek metin.
 *  5. Bildirim: yeni basarimda ust-orta kapsul; ~3 sn sonra temiz kare (bayat kare
 *     yok); iki bildirim sirayla; menu acikken gosterilmez ve kuyrukta bekler;
 *     gosterilen kuyruktan duser (pending_notices tutarli); menu zamani dondurur;
 *     Auto oyunu bekletmez.
 *  6. Yalitim: Ekstralar'da gezmek degiskenleri, GameState'i (activeBgm dahil),
 *     kayit yuvalarini (bayt bayt), okundu/profili, gecmisi ve unlock'lari degistirmez.
 *  7. tr/en etiketleri (satirlar, ipuclari, bildirim, katalog basliklari).
 *
 * Bilerek-boz: buildExtrasView'da kilitli hucreye thumbnail yolu vermek 2'yi,
 * endExtrasSession'da ses geri yuklemesini kaldirmak 3'u, Window::renderComposedFrame'deki
 * m_bakedToast gecersizlestirmesini kaldirmak 5'in bayat kare kontrolunu, engine.cpp'de
 * updateUnlockToast'in !m_paused kapisini kaldirmak 5'in menu kontrollerini kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/player_ui_strings.hpp"
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
#include <map>
#include <memory>
#include <regex>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;
using LM = Rowl::Core::PauseMenuLayout;
using Rowl::Core::UiLang;
using Rowl::Core::UiText;

constexpr const char* kSection = "extras-screens";
constexpr uint32_t kW = 960;   // half of the 1920x1080 virtual canvas: 1 physical px = 2 virtual px
constexpr uint32_t kH = 540;

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

struct Rgb {
    int r, g, b;
};
constexpr Rgb kRed{220, 40, 40};
constexpr Rgb kGreen{40, 200, 60};
constexpr Rgb kBlue{50, 70, 230};
constexpr Rgb kLockedBox{22, 25, 40};  // window_extras_overlay.cpp locked fill

std::string cid(int id) {
    char text[64];
    std::snprintf(text, sizeof(text), "40000000-0000-4000-8000-%012d", id);
    return text;
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

json component(const std::string& type, const std::string& id, const json& data) {
    return {{"type", type}, {"id", id}, {"enabled", true}, {"data", data}};
}

// 1 -> 2 -> 3 -> 4 -> 5, every node the same text and no speaker (so the story
// frame is identical node to node and a capsule is the only change).
//   node 1: bgm base.wav (not in the catalog)      node 2: unlocks a0 + a2
//   node 3: unlocks a1 (hidden)                    node 4: bgm audio/m1.wav (music m1)
std::string buildGraph() {
    json nodes = json::array();
    for (int id = 1; id <= 5; ++id) {
        json components = json::array();
        components.push_back(component("dialogue", "d" + std::to_string(id),
                                       {{"speaker", ""}, {"dialogue", "Ayni satir"},
                                        {"typewriter_enabled", false}, {"content_id", cid(id)}}));
        if (id == 1) components.push_back(component("audio", "au1", {{"bgm_track", "base.wav"}}));
        if (id == 2) {
            components.push_back(component("unlock", "u2a", {{"kind", "achievement"}, {"id", "a0"}}));
            components.push_back(component("unlock", "u2b", {{"kind", "achievement"}, {"id", "a2"}}));
        }
        if (id == 3) components.push_back(component("unlock", "u3", {{"kind", "achievement"}, {"id", "a1"}}));
        if (id == 4) components.push_back(component("audio", "au4", {{"bgm_track", "audio/m1.wav"}}));
        json edges = json::array();
        if (id < 5) edges.push_back({{"id", id + 1}});
        nodes.push_back({{"id", id}, {"title", "N" + std::to_string(id)},
                         {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"},
                                                   {"is_active", true}, {"components", components}}})},
                         {"next_nodes", edges}});
    }
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
}

std::string makeCatalog(int galleryCount, int achievementCount = 3) {
    static const char* const kImages[] = {"red.bmp", "green.bmp", "blue.bmp"};
    json gallery = json::array();
    for (int i = 0; i < galleryCount; ++i) {
        char id[16];
        std::snprintf(id, sizeof(id), "g%02d", i);
        gallery.push_back({{"id", id},
                           {"title", {{"tr", "Resim " + std::to_string(i)}, {"en", "Picture " + std::to_string(i)}}},
                           {"image", kImages[i % 3]}});
    }
    json music = json::array();
    for (int i = 0; i < 3; ++i) {
        const std::string n = std::to_string(i);
        music.push_back({{"id", "m" + n},
                         {"title", {{"tr", "Parça " + n}, {"en", "Track " + n}}},
                         {"track", "audio/m" + n + ".wav"}});
    }
    json achievements = json::array({
        {{"id", "a0"}, {"title", {{"tr", "Birinci"}, {"en", "First"}}},
         {"description", {{"tr", "Birinci açıklama."}, {"en", "First description."}}}},
        {{"id", "a1"}, {"title", {{"tr", "Gizli"}, {"en", "Secret"}}}, {"hidden", true},
         {"description", {{"tr", "Gizli açıklama."}, {"en", "Secret description."}}}},
        {{"id", "a2"}, {"title", {{"tr", "Üçüncü"}, {"en", "Third"}}},
         {"description", {{"tr", "Üçüncü açıklama."}, {"en", "Third description."}}}},
    });
    for (int i = 3; i < achievementCount; ++i) {
        const std::string n = std::to_string(i);
        achievements.push_back({{"id", "a" + n},
                                {"title", {{"tr", "Ek " + n}, {"en", "Extra " + n}}},
                                {"description", {{"tr", "Ek açıklama " + n + "."}, {"en", "Extra description " + n + "."}}}});
    }
    return json({{"format", 1}, {"gallery", gallery}, {"music", music}, {"achievements", achievements}}).dump();
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

struct Options {
    bool story = true;           // a story graph (false = a bare title, no GameState)
    std::string catalog;         // extras.json text; empty = none; "default" = makeCatalog(galleryCount)
    int galleryCount = 6;
    int achievementCount = 3;
    bool locales = false;        // declares tr + en (SetLocale("en") flips the UI)
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
        std::snprintf(uuid, sizeof(uuid), "6c8f3e25-4bad-4f71-9a23-8d2e6b0f1c%02x", g_rigCounter);
        std::string manifest = std::string(R"({"project_uuid":")") + uuid + R"(","name":"Extras Screens")";
        if (options.locales) manifest += R"(,"default_locale":"tr","supported_locales":["tr","en"])";
        writeBytes(game / "project.rowlproj", manifest + "}");
        if (options.story) writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph());
        const std::string catalog = options.catalog == "default" ? makeCatalog(options.galleryCount, options.achievementCount) : options.catalog;
        if (!catalog.empty()) writeBytes(game / "Assets" / "json" / "extras.json", catalog);
        writeBytes(game / "Assets" / "images" / "red.bmp", makeBmp(64, 36, kRed.r, kRed.g, kRed.b));
        writeBytes(game / "Assets" / "images" / "green.bmp", makeBmp(64, 36, kGreen.r, kGreen.g, kGreen.b));
        writeBytes(game / "Assets" / "images" / "blue.bmp", makeBmp(64, 36, kBlue.r, kBlue.g, kBlue.b));
        for (const char* name : {"base", "m0", "m1", "m2"}) {
            writeBytes(game / "Assets" / "audio" / (std::string(name) + ".wav"), makeWav(1));
        }
        if (options.locales) {
            for (const char* locale : {"en", "tr"}) {
                writeBytes(game / "Assets" / "locales" / (std::string(locale) + ".json"),
                           std::string(R"({"schema_version":1,"locale":")") + locale + R"(","entries":{}})");
            }
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

    // ── state readers ──
    json menu() const {
        const char* text = RowlEngine_GetPauseMenuJson(h);
        json parsed = json::parse(text ? text : "", nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    json extras() const {
        json parsed = json::parse(capiText(h, RowlEngine_GetExtrasJson), nullptr, false);
        req(parsed.is_object(), "extras JSON did not parse");
        return parsed;
    }
    json profile() const {
        json parsed = json::parse(capiText(h, RowlEngine_ProfileGetJson), nullptr, false);
        req(parsed.is_object(), "profile JSON did not parse");
        return parsed;
    }
    int mode() const { return menu()["mode"].get<int>(); }
    std::vector<std::string> labels() const {
        std::vector<std::string> out;
        const json m = menu();
        for (const json& row : m["rows"]) out.push_back(row["label"].get<std::string>());
        return out;
    }

    // ── actions ──
    void step(float dt, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_Step(h, dt);
    }
    // Whole seconds of play time: the engine clamps one step to 0.25 s.
    void run(float seconds) const {
        for (int i = 0, n = static_cast<int>(std::ceil(seconds / 0.2f)); i < n; ++i) step(0.2f);
    }
    void cmd(int command, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_PauseMenuCommand(h, command);
    }
    void event(RuntimeInputEvent e) {
        host->input.push_back(e);
        step(0.016f);
    }
    void esc() { RuntimeInputEvent e{}; e.type = InType::PauseToggle; event(e); }
    void rightClick() { RuntimeInputEvent e{}; e.type = InType::SecondaryClick; event(e); }
    void wheel(float dy) { RuntimeInputEvent e{}; e.type = InType::Scroll; e.y = dy; event(e); }
    void unlock(const char* kind, const std::string& id) const {
        req(RowlEngine_ProfileUnlock(h, kind, id.c_str()) == ROWL_RESULT_OK, std::string("unlock ") + kind + ":" + id);
    }
    void advance() const { RowlEngine_AdvanceNode(h, 0); }
    void toTitleExtras() const {
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "OpenTitleScreen");
        req(RowlEngine_OpenExtras(h) == ROWL_RESULT_OK, "OpenExtras");
    }
    // Opens a sub-page of the Extras root (0 gallery, 1 music, 2 achievements).
    void openPage(int row) const {
        req(mode() == 6, "on the extras root before opening a page");
        const int selected = menu()["selected"].get<int>();
        cmd(ROWL_PAUSE_MENU_DOWN, (row - selected + 4) % 4);  // the root wraps
        cmd(ROWL_PAUSE_MENU_CONFIRM);
    }
    void clickVirtual(float x, float y) const { engine->pauseMenuClick(x, y); }
    // Centre of gallery cell (column, visible row).
    static void cellCentre(int column, int row, float& x, float& y) {
        x = LM::galleryCellX(column) + LM::kGalleryCellW * 0.5f;
        y = LM::galleryCellY(row) + LM::kGalleryThumbH * 0.5f;
    }

    // ── pixels ──
    std::string frame(int steps = 4) const {
        step(0.016f, steps);
        uint32_t w = 0, hh = 0;
        const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
        req(pixels != nullptr && w == kW && hh == kH, "offscreen pixel buffer");
        return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
    }
};

// Mean colour of the physical rectangle [x0, x0+w) x [y0, y0+h).
Rgb meanColour(const std::string& pixels, int x0, int y0, int w, int h) {
    long r = 0, g = 0, b = 0;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const size_t at = (static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4;
            // The buffer is RGBA32: R, G, B, A in memory order.
            r += static_cast<uint8_t>(pixels[at]);
            g += static_cast<uint8_t>(pixels[at + 1]);
            b += static_cast<uint8_t>(pixels[at + 2]);
        }
    }
    const long n = static_cast<long>(w) * h;
    return {static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n)};
}

bool near(const Rgb& a, const Rgb& b, int tolerance = 14) {
    return std::abs(a.r - b.r) <= tolerance && std::abs(a.g - b.g) <= tolerance && std::abs(a.b - b.b) <= tolerance;
}

std::string rgbText(const Rgb& c) {
    return "(" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + ")";
}

// Thumbnail rectangle of a gallery cell in physical pixels (scale 0.5), inset
// to stay clear of the border and of the centred "?" of a locked cell.
Rgb cellColour(const std::string& pixels, int column, int row) {
    const int x = static_cast<int>(LM::galleryCellX(column) * 0.5f) + 5;
    const int y = static_cast<int>(LM::galleryCellY(row) * 0.5f) + 4;
    return meanColour(pixels, x, y, 14, 8);
}

// Differing pixels between two frames inside / outside a physical rectangle.
struct Diff {
    size_t inside = 0;
    size_t outside = 0;
};
Diff diffIn(const std::string& a, const std::string& b, int x0, int y0, int x1, int y1) {
    req(a.size() == b.size() && a.size() == size_t(kW) * kH * 4, "frame size");
    Diff out;
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            const size_t at = (size_t(y) * kW + x) * 4;
            if (a.compare(at, 4, b, at, 4) == 0) continue;
            const bool in = static_cast<int>(x) >= x0 && static_cast<int>(x) < x1 &&
                            static_cast<int>(y) >= y0 && static_cast<int>(y) < y1;
            (in ? out.inside : out.outside)++;
        }
    }
    return out;
}

const json& cellOf(const json& menu, int index) {
    for (const json& cell : menu["extras"]["cells"]) {
        if (cell["index"].get<int>() == index) return cell;
    }
    rowlLockFail(kSection, "no visible gallery cell " + std::to_string(index));
    static const json none;
    return none;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ── 1. title row + page flow ────────────────────────────────────────────────
void titleAndPagesTests() {
    // No extras.json, and an extras.json without items: the older five rows.
    for (const char* variant : {"none", "empty"}) {
        Options o;
        o.catalog = std::string(variant) == "none" ? "" : R"({"format":1})";
        Rig rig(o);
        req(RowlEngine_OpenExtras(rig.h) == ROWL_RESULT_STATE_ERROR, std::string(variant) + ": OpenExtras needs items");
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "OpenTitleScreen");
        const auto labels = rig.labels();
        req(labels == std::vector<std::string>({"Yeni Oyun", "Devam Et", "Oyunu Yükle", "Tercihler", "Çıkış"}),
            std::string(variant) + ": no Ekstralar row: " + rig.menu().dump());
        rig.cmd(ROWL_PAUSE_MENU_DOWN, 3);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.mode() == 4, std::string(variant) + ": row 3 is still Tercihler");
    }
    TEST_PASS("title without a catalog keeps the five old rows (Tercihler at row 3); OpenExtras refuses");

    Options o;
    o.catalog = "default";
    Rig rig(o);
    req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "OpenTitleScreen");
    {
        const auto labels = rig.labels();
        req(labels == std::vector<std::string>({"Yeni Oyun", "Devam Et", "Oyunu Yükle", "Ekstralar", "Tercihler", "Çıkış"}),
            "Ekstralar sits before Tercihler: " + rig.menu().dump());
        req(LM::kTitleRowsWithExtras == 6 && LM::kTitleExtrasRow == 3, "layout constants");
    }
    // The pause menu never lists it.
    RowlEngine_SetPaused(rig.h, 0);
    RowlEngine_SetPaused(rig.h, 1);
    {
        const auto labels = rig.labels();
        req(labels.size() == 7, "pause main page is still 7 rows");
        for (const auto& label : labels) req(label != "Ekstralar", "no Ekstralar in the pause menu");
    }
    RowlEngine_SetPaused(rig.h, 0);
    req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "OpenTitleScreen again");

    // Root page + values (3 gallery pictures unlocked, 1 track, 1 achievement).
    rig.unlock("cg", "g00");
    rig.unlock("cg", "g02");
    rig.unlock("cg", "g03");
    rig.unlock("music", "m1");
    rig.unlock("achievement", "a0");
    rig.cmd(ROWL_PAUSE_MENU_DOWN, 3);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    {
        const json m = rig.menu();
        req(m["mode"] == 6 && m["title"] == "Ekstralar", "root page: " + m.dump());
        req(rig.labels() == std::vector<std::string>({"Galeri", "Müzik Odası", "Başarımlar", "Geri"}), "root rows");
        req(m["rows"][0]["value"] == "3/6" && m["rows"][1]["value"] == "1/3" && m["rows"][2]["value"] == "1/3" &&
                m["rows"][3]["value"] == "",
            "values are unlocked/total: " + m["rows"].dump());
        req(m["extras"]["page"] == "root", "extras object on the root");
    }
    // Each sub-page and Back returns to the root with that row selected.
    const char* const pageNames[] = {"gallery", "music", "achievements"};
    for (int row = 0; row < 3; ++row) {
        rig.openPage(row);
        req(rig.mode() == 7 + row && rig.menu()["extras"]["page"] == pageNames[row],
            "sub-page " + std::to_string(row) + ": " + rig.menu().dump());
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        const json m = rig.menu();
        req(m["mode"] == 6 && m["selected"].get<int>() == row, "Back lands on the root row " + std::to_string(row));
    }
    // Geri row and Esc return to the title with Ekstralar selected; Esc never starts the story.
    rig.cmd(ROWL_PAUSE_MENU_DOWN);  // 2 -> 3 (Geri)
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    {
        const json m = rig.menu();
        req(m["mode"] == 3 && m["selected"] == 3 && m["title_active"] == true, "Geri -> title, Ekstralar selected");
        req(!m.contains("extras"), "the title carries no extras object");
    }
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.esc();
    req(rig.mode() == 3 && rig.menu()["selected"] == 3 && rig.menu()["title_active"] == true, "Esc on the root -> title");
    rig.esc();
    req(rig.menu()["open"] == true && rig.menu()["title_active"] == true, "Esc on the title never starts the story");
    // Tercihler and Cikis moved down one row.
    rig.cmd(ROWL_PAUSE_MENU_DOWN);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    req(rig.mode() == 4, "row 4 opens Tercihler");
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    req(rig.mode() == 3 && rig.menu()["selected"] == 4, "Tercihler returns to its own row (4)");
    rig.cmd(ROWL_PAUSE_MENU_DOWN);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    req(rig.menu()["rows"][5]["value"] == "emin misin?" && RowlEngine_GetPauseMenuJson(rig.h) != nullptr,
        "Cikis (row 5) arms its two-step confirmation");
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    // Right click goes one level up (list pages) / closes nothing on the title.
    rig.cmd(ROWL_PAUSE_MENU_UP, 2);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.openPage(2);
    rig.rightClick();
    req(rig.mode() == 6, "right click on a list page goes up to the root");
    TEST_PASS("title row appears only with a catalog; root values are unlocked/total; Back/Esc/right click walk up; Tercihler keeps its place");

    // OpenExtras straight from the running story opens the title first.
    {
        Rig game(o);
        RowlEngine_SetPlayState(game.h, 1);
        game.step(0.016f);
        req(game.menu()["open"] == false, "the story is running");
        req(RowlEngine_OpenExtras(game.h) == ROWL_RESULT_OK, "OpenExtras from the story");
        const json m = game.menu();
        req(m["open"] == true && m["mode"] == 6 && m["title_active"] == true, "title + extras root: " + m.dump());
        req(RowlEngine_OpenExtras(game.h) == ROWL_RESULT_OK && game.mode() == 6, "already open is OK");
        RowlEngineHandle bad = RowlEngine_Create();
        req(RowlEngine_OpenExtras(bad) == ROWL_RESULT_STATE_ERROR, "OpenExtras before Init is a state error");
        RowlEngine_Destroy(bad);
    }
    TEST_PASS("RowlEngine_OpenExtras: story -> title + root, idempotent, STATE_ERROR before Init");
}

// ── 2. gallery ──────────────────────────────────────────────────────────────
void galleryTests() {
    Options o;
    o.catalog = "default";
    o.galleryCount = 6;
    Rig rig(o);
    // g00 red, g01 green (locked), g02 blue, g03 red (locked), g04 green, g05 blue (locked)
    rig.unlock("cg", "g00");
    rig.unlock("cg", "g02");
    rig.unlock("cg", "g04");
    rig.toTitleExtras();
    rig.openPage(0);
    {
        const json m = rig.menu();
        req(m["mode"] == 7 && m["extras"]["columns"] == 4 && m["extras"]["total"] == 6 &&
                m["extras"]["unlocked"] == 3 && m["extras"]["viewing"] == false,
            "gallery state: " + m.dump());
        req(m["extras"]["cells"].size() == 6, "all six cells fit on the first page");
        for (int i = 0; i < 6; ++i) {
            const json& cell = cellOf(m, i);
            const bool open = (i == 0 || i == 2 || i == 4);
            req(cell["unlocked"] == open, "cell unlocked flag " + std::to_string(i));
            req(cell["title"] == (open ? "Resim " + std::to_string(i) : std::string("???")),
                "locked cells show ???: " + cell.dump());
            // A locked cell carries no path at all - nothing to leak.
            req(cell.contains("thumbnail") == open, "locked cell has no image path: " + cell.dump());
        }
        req(cellOf(m, 0)["selected"] == true && cellOf(m, 0)["x"].get<float>() == LM::galleryCellX(0), "geometry");
    }
    // Pixels: select a non-first cell so cell 0 shows its plain colour.
    rig.cmd(ROWL_PAUSE_MENU_RIGHT);  // cell 1 (locked) selected
    const size_t texturesBefore = rig.engine->getWindow()->getTextureCacheTextureCount();
    const uint64_t cacheBytesBefore = rig.engine->getWindow()->getTextureCacheBytes();
    const std::string grid = rig.frame(8);  // thumbnails fill over a few frames
    {
        const Rgb red = cellColour(grid, 0, 0);
        const Rgb lockedGreen = cellColour(grid, 1, 0);
        const Rgb blue = cellColour(grid, 2, 0);
        const Rgb lockedRed = cellColour(grid, 3, 0);
        const Rgb green = cellColour(grid, 0, 1);
        const Rgb lockedBlue = cellColour(grid, 1, 1);
        req(near(red, kRed), "open cell 0 shows its picture " + rgbText(red));
        req(near(blue, kBlue), "open cell 2 shows its picture " + rgbText(blue));
        req(near(green, kGreen), "open cell 4 shows its picture " + rgbText(green));
        req(near(lockedGreen, kLockedBox), "locked cell 1 is the dark box " + rgbText(lockedGreen));
        req(near(lockedRed, kLockedBox), "locked cell 3 is the dark box " + rgbText(lockedRed));
        req(near(lockedBlue, kLockedBox), "locked cell 5 is the dark box " + rgbText(lockedBlue));
    }
    // The grid keeps its own small textures: exactly the three open pictures'
    // files (red, blue, green) - never a locked one - and the shared 64 MB texture
    // cache is untouched.
    req(rig.engine->getWindow()->getGalleryThumbnailCount() == 3,
        "thumbnails cached: " + std::to_string(rig.engine->getWindow()->getGalleryThumbnailCount()));
    req(rig.engine->getWindow()->getTextureCacheTextureCount() == texturesBefore &&
            rig.engine->getWindow()->getTextureCacheBytes() == cacheBytesBefore,
        "the grid does not load full textures into the shared cache");
    TEST_PASS("gallery grid: open cells draw their picture, locked cells are a flat dark box with no path; small thumbnail cache only");

    // Keyboard selection: Right/Left step, Down/Up jump a grid row (clamped), edges stay.
    rig.cmd(ROWL_PAUSE_MENU_LEFT, 5);
    req(rig.menu()["extras"]["selected"] == 0, "Left clamps at the first cell");
    rig.cmd(ROWL_PAUSE_MENU_DOWN);
    req(rig.menu()["extras"]["selected"] == 4, "Down jumps a row of 4");
    rig.cmd(ROWL_PAUSE_MENU_DOWN);
    req(rig.menu()["extras"]["selected"] == 4, "no wrapping below the last row");
    rig.cmd(ROWL_PAUSE_MENU_UP);
    rig.cmd(ROWL_PAUSE_MENU_RIGHT, 2);
    rig.cmd(ROWL_PAUSE_MENU_DOWN);
    req(rig.menu()["extras"]["selected"] == 5, "Down from a column the short last row lacks lands on the final item");
    rig.cmd(ROWL_PAUSE_MENU_UP, 3);
    req(rig.menu()["extras"]["selected"] == 1, "Up jumps a row, then stays at the top");
    rig.cmd(ROWL_PAUSE_MENU_RIGHT, 9);
    req(rig.menu()["extras"]["selected"] == 5, "Right clamps at the last cell");

    // A locked cell does not open the viewer.
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    req(rig.menu()["extras"]["viewing"] == false, "Enter on a locked cell opens nothing");
    float x = 0, y = 0;
    Rig::cellCentre(3, 0, x, y);  // cell 3 is locked
    rig.clickVirtual(x, y);
    req(rig.menu()["extras"]["viewing"] == false && rig.menu()["extras"]["selected"] == 3,
        "clicking a locked cell only selects it");

    // Viewer by Enter on cell 0.
    rig.cmd(ROWL_PAUSE_MENU_LEFT, 5);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    {
        const json m = rig.menu();
        req(m["extras"]["viewing"] == true && m["extras"]["view_index"] == 0 && m["extras"]["view_title"] == "Resim 0",
            "viewer open on cell 0: " + m.dump());
        req(m["hint"] == "SOL/SAĞ: önceki/sonraki   ESC/tık: kapat", "viewer hint");
    }
    {
        const std::string view = rig.frame(2);
        // 64x36 (16:9) fits the 1880x930 area at 930 high: a 1653x930 picture, centred
        // -> physical (480,235) is inside it; the pillar corner is the dim backdrop.
        const Rgb centre = meanColour(view, 470, 225, 20, 20);
        const Rgb corner = meanColour(view, 4, 4, 8, 8);
        req(near(centre, kRed), "the viewer shows the red picture centred " + rgbText(centre));
        req(corner.r < 25 && corner.g < 25 && corner.b < 25, "dark backdrop in the corner " + rgbText(corner));
        // Aspect ratio kept: 16:9 picture in a 960x540 frame leaves the 20 px margins
        // on the sides and top: the left margin column is backdrop, the picture starts near x=...
        const Rgb leftEdge = meanColour(view, 14, 270, 4, 4);
        req(leftEdge.r < 40, "the margin beside the picture stays backdrop " + rgbText(leftEdge));
    }
    // The full image goes through the shared texture cache - one entry.
    req(rig.engine->getWindow()->getTextureCacheTextureCount() == texturesBefore + 1,
        "the viewer loads exactly one full texture");
    // Left/Right: only open items, wrapping.
    rig.cmd(ROWL_PAUSE_MENU_RIGHT);
    req(rig.menu()["extras"]["view_index"] == 2, "Right skips the locked cell 1");
    {
        const Rgb centre = meanColour(rig.frame(2), 470, 225, 20, 20);
        req(near(centre, kBlue), "viewer now shows the blue picture " + rgbText(centre));
    }
    rig.cmd(ROWL_PAUSE_MENU_RIGHT);
    req(rig.menu()["extras"]["view_index"] == 4, "Right skips the locked cell 3");
    rig.cmd(ROWL_PAUSE_MENU_RIGHT);
    req(rig.menu()["extras"]["view_index"] == 0, "Right wraps past the locked cell 5 to the first open cell");
    rig.cmd(ROWL_PAUSE_MENU_LEFT);
    req(rig.menu()["extras"]["view_index"] == 4, "Left wraps back, open cells only");
    rig.cmd(ROWL_PAUSE_MENU_LEFT, 2);
    req(rig.menu()["extras"]["view_index"] == 0, "Left again, open cells only");
    // Esc closes the viewer back to the grid (selection follows), a second Esc leaves the page.
    rig.cmd(ROWL_PAUSE_MENU_RIGHT, 2);  // view_index 4
    rig.esc();
    {
        const json m = rig.menu();
        req(m["mode"] == 7 && m["extras"]["viewing"] == false && m["extras"]["selected"] == 4,
            "Esc closes the viewer; the grid selects the viewed item: " + m.dump());
    }
    {
        const std::string closed = rig.frame(3);
        req(!near(meanColour(closed, 470, 225, 20, 20), kRed), "the closed viewer leaves no stale full-screen picture");
        const Rgb corner = meanColour(closed, 4, 4, 8, 8);
        req(corner.r > 0 || corner.g > 0 || corner.b > 0, "the menu panel/dim is drawn again");
    }
    rig.esc();
    req(rig.mode() == 6, "second Esc leaves the gallery");
    // Mouse: click opens, click closes; right click closes.
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    Rig::cellCentre(2, 0, x, y);
    rig.clickVirtual(x, y);
    req(rig.menu()["extras"]["viewing"] == true && rig.menu()["extras"]["view_index"] == 2, "click opens the viewer");
    rig.clickVirtual(900.0f, 500.0f);
    req(rig.menu()["extras"]["viewing"] == false, "click closes the viewer");
    rig.clickVirtual(x, y);
    rig.rightClick();
    req(rig.menu()["extras"]["viewing"] == false && rig.mode() == 7, "right click closes the viewer, stays on the grid");
    // A physical pointer press goes the same way (960x540: virtual = physical * 2).
    {
        RuntimeInputEvent press{};
        press.type = InType::PointerDown;
        press.x = x * 0.5f;
        press.y = y * 0.5f;
        rig.event(press);
        req(rig.menu()["extras"]["viewing"] == true, "physical click opens the viewer");
        rig.event(press);
        req(rig.menu()["extras"]["viewing"] == false, "and closes it");
    }
    TEST_PASS("viewer: Enter/click open (locked refuses), aspect kept + centred, Left/Right over open items only (wrap), Esc/click/right click close");

    // Scrolling: 18 pictures = 5 grid rows, 4 visible.
    Options big;
    big.catalog = "default";
    big.galleryCount = 18;
    Rig many(big);
    for (int i = 0; i < 18; ++i) {
        char id[16];
        std::snprintf(id, sizeof(id), "g%02d", i);
        many.unlock("cg", id);
    }
    many.toTitleExtras();
    many.openPage(0);
    req(many.menu()["extras"]["cells"].size() == 16 && many.menu()["extras"]["scroll"] == 0, "16 cells on page one");
    many.cmd(ROWL_PAUSE_MENU_DOWN, 4);  // index 16 = grid row 4: scroll by one row
    {
        const json m = many.menu();
        req(m["extras"]["selected"] == 16 && m["extras"]["scroll"] == 1, "scrolled one row: " + m["extras"].dump().substr(0, 160));
        req(m["extras"]["cells"][0]["index"] == 4 && m["extras"]["cells"].size() == 14, "window starts at row 1");
    }
    const std::string scrolled = many.frame(10);
    req(near(cellColour(scrolled, 0, 3), kGreen) && near(cellColour(scrolled, 1, 3), kBlue),
        "the scrolled window draws catalog row 4 (items 16, 17) in its last visible row: " +
            rgbText(cellColour(scrolled, 0, 3)) + rgbText(cellColour(scrolled, 1, 3)));
    many.cmd(ROWL_PAUSE_MENU_UP, 4);
    req(many.menu()["extras"]["scroll"] == 0, "scrolls back up");
    many.wheel(-1.0f);  // wheel down: one grid row
    req(many.menu()["extras"]["selected"] == 4, "wheel down moves a row");
    // All 18 pictures use 3 files: the cache never grows past what is distinct.
    many.frame(10);
    req(many.engine->getWindow()->getGalleryThumbnailCount() <= 3, "three files, three thumbnails at most");
    TEST_PASS("gallery scrolls by whole grid rows (keys + wheel); the thumbnail cache stays bounded");
}

// ── 3. music room ───────────────────────────────────────────────────────────
void musicTests() {
    Options o;
    o.catalog = "default";
    {
        // From a fresh title: the GameState carries base.wav (node 1's BGM).
        Rig rig(o);
        auto* audio = rig.engine->getAudio();
        req(audio != nullptr, "audio engine");
        const auto stateBefore = rig.engine->getGameState();
        req(stateBefore != nullptr, "the mounted story has a GameState");
        const std::string baseBgm = stateBefore->activeBgm;
        req(endsWith(baseBgm, "base.wav") && stateBefore->bgmPlaying, "node 1 started base.wav: '" + baseBgm + "'");
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "base.wav"), "base.wav is playing");

        rig.unlock("music", "m0");
        rig.unlock("music", "m2");
        rig.toTitleExtras();
        rig.openPage(1);
        {
            const json m = rig.menu();
            req(m["mode"] == 8 && m["extras"]["page"] == "music" && m["extras"]["total"] == 3, "music page: " + m.dump());
            req(rig.labels() == std::vector<std::string>({"Parça 0", "???", "Parça 2"}),
                "locked track shows ???: " + m["rows"].dump());
            req(m["extras"]["playing"] == "", "nothing plays yet");
            req(m["rows"][0]["value"] == "" && m["rows"][1]["enabled"] == false && m["rows"][0]["enabled"] == true,
                "locked row is dim");
        }
        // Enter on a locked row does nothing.
        rig.cmd(ROWL_PAUSE_MENU_DOWN);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.menu()["extras"]["playing"] == "" && endsWith(audio->getCurrentBgmPath(), "base.wav"),
            "Enter on a locked track neither plays nor changes the sound");
        // Play m0: the audio engine switches, the GameState does not.
        rig.cmd(ROWL_PAUSE_MENU_UP);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        {
            const json m = rig.menu();
            req(m["extras"]["playing"] == "m0" && m["rows"][0]["value"] == "çalıyor",
                "the playing row is marked: " + m["rows"].dump());
            req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "m0.wav"), "m0.wav is on the BGM channel");
            req(rig.engine->getGameState() == stateBefore && rig.engine->getGameState()->activeBgm == baseBgm,
                "GameState (activeBgm) is untouched while the music room plays");
        }
        // Another track replaces it; the same row again stops it.
        rig.cmd(ROWL_PAUSE_MENU_DOWN, 2);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.menu()["extras"]["playing"] == "m2" && endsWith(audio->getCurrentBgmPath(), "m2.wav"), "m2 replaces m0");
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(rig.menu()["extras"]["playing"] == "" && !audio->isBgmPlaying() && rig.menu()["rows"][2]["value"] == "",
            "Enter on the playing row stops it");
        rig.cmd(ROWL_PAUSE_MENU_UP, 2);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);  // m0 plays again
        req(audio->isBgmPlaying(), "m0 plays");
        // The track keeps playing across Ekstralar pages, then the sound returns on leaving.
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        rig.openPage(0);
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "m0.wav"), "music survives a page change");
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        rig.cmd(ROWL_PAUSE_MENU_BACK);  // root -> title
        req(rig.mode() == 3, "back on the title");
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "base.wav"),
            "leaving Ekstralar brings the title's sound back: '" + audio->getCurrentBgmPath() + "'");
        req(rig.engine->getGameState() == stateBefore, "still the same GameState object");

        // Click: row click plays (one tap); the pause overlay never offers this page.
        rig.toTitleExtras();
        rig.openPage(1);
        rig.clickVirtual(700.0f, LM::rowTop(2, 3) + LM::rowH(3) * 0.5f);
        req(rig.menu()["extras"]["playing"] == "m2", "clicking an open row plays it");
        // Leaving by closing the menu (host SetPaused(0)) also restores the sound.
        RowlEngine_SetPaused(rig.h, 0);
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "base.wav"), "closing the menu restores the sound too");
    }
    TEST_PASS("music room from the title: locked ???, Enter plays/stops, row marked, GameState untouched, sound restored on leaving");

    {
        // A continued game: node 4's BGM (m1) is the game's music; the player returns to the title.
        Rig rig(o);
        auto* audio = rig.engine->getAudio();
        for (int i = 0; i < 3; ++i) rig.advance();  // 1 -> 4
        rig.step(0.016f);
        req(RowlEngine_GetCurrentNodeId(rig.h) == 4, "node 4");
        const auto state = rig.engine->getGameState();
        req(endsWith(state->activeBgm, "m1.wav") && state->bgmPlaying && endsWith(audio->getCurrentBgmPath(), "m1.wav"),
            "node 4 plays m1");
        rig.unlock("music", "m0");
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK && RowlEngine_OpenExtras(rig.h) == ROWL_RESULT_OK, "open");
        rig.openPage(1);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);  // m0
        req(endsWith(audio->getCurrentBgmPath(), "m0.wav"), "m0 plays over the continued game");
        req(rig.engine->getGameState() == state && endsWith(rig.engine->getGameState()->activeBgm, "m1.wav"),
            "activeBgm stays m1");
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "m1.wav"),
            "back on the title after a continued game: the game's m1 returns");
        RowlEngine_SetPaused(rig.h, 0);
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "m1.wav") && RowlEngine_GetCurrentNodeId(rig.h) == 4,
            "resuming the story hears the same music");
    }
    TEST_PASS("music room after a continued game: the game's own BGM returns on leaving; GameState.activeBgm never changes");

    {
        // No story at all: no GameState to restore from -> silence.
        Options bare = o;
        bare.story = false;
        Rig rig(bare);
        auto* audio = rig.engine->getAudio();
        rig.unlock("music", "m0");
        rig.toTitleExtras();
        rig.openPage(1);
        rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(audio->isBgmPlaying() && endsWith(audio->getCurrentBgmPath(), "m0.wav"), "m0 plays on a bare title");
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        rig.cmd(ROWL_PAUSE_MENU_BACK);
        req(!audio->isBgmPlaying(), "a bare title falls silent after the music room");
    }
    TEST_PASS("music room without a GameState: leaving stops the music");
}

// ── 4. achievements ─────────────────────────────────────────────────────────
void achievementsTests() {
    Options o;
    o.catalog = "default";
    Rig rig(o);
    rig.unlock("achievement", "a0");
    rig.toTitleExtras();
    rig.openPage(2);
    const std::string at = rig.extras()["achievements"][0]["at"].get<std::string>();
    {
        const json m = rig.menu();
        req(m["mode"] == 9 && m["extras"]["page"] == "achievements" && m["extras"]["total"] == 3 &&
                m["extras"]["unlocked"] == 1,
            "achievements page: " + m.dump());
        req(m["rows"].empty(), "the two-line rows are not generic rows: " + m["rows"].dump());
        const json& items = m["extras"]["items"];
        req(items.size() == 3, "three items");
        req(items[0]["title"] == "Birinci" && items[0]["description"] == "Birinci açıklama." && items[0]["unlocked"] == true,
            "open: title + description: " + items[0].dump());
        req(std::regex_match(items[0]["at"].get<std::string>(), std::regex(R"(\d{4}-\d{2}-\d{2})")) &&
                at.rfind(items[0]["at"].get<std::string>(), 0) == 0,
            "open: the unlock date " + items[0]["at"].dump() + " from " + at);
        req(items[1]["title"] == "???" && items[1]["description"] == "???" && items[1]["at"].is_null() &&
                items[1]["unlocked"] == false,
            "hidden and locked: title and description are ???: " + items[1].dump());
        req(items[2]["title"] == "Üçüncü" && items[2]["description"] == "???" && items[2]["at"].is_null(),
            "locked, not hidden: the title shows, the description does not: " + items[2].dump());
        req(items[0]["y"].get<float>() == LM::achRowTop(0) && items[0]["h"].get<float>() == LM::kAchRowH &&
                items[1]["y"].get<float>() == LM::achRowTop(1),
            "item geometry follows the page layout");
    }
    // The hidden one reads normally once unlocked.
    rig.unlock("achievement", "a1");
    {
        const json items = rig.menu()["extras"]["items"];
        req(items[1]["title"] == "Gizli" && items[1]["description"] == "Gizli açıklama." && items[1]["unlocked"] == true,
            "an unlocked hidden achievement shows its text: " + items[1].dump());
    }
    // A read-only list: Enter does nothing, Down walks, Back goes up.
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.cmd(ROWL_PAUSE_MENU_DOWN, 5);
    req(rig.menu()["extras"]["selected"] == 2 && rig.mode() == 9, "the list clamps at the last entry");
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    req(rig.mode() == 6 && rig.menu()["selected"] == 2, "Back returns to the root's Basarimlar row");
    TEST_PASS("achievements: open = title + description + date, locked = title + ???, hidden+locked = ??? / ???");
}

// Achievements have their own two-line row layout: geometry, scrolling, click
// mapping and the clear band between a row's title and its description.
void achievementLayoutTests() {
    // Geometry lock: the description's ink starts well below the title's ink
    // (>= 6 design pixels clear) and both lines stay inside the row box.
    const float titleInkBottom = LM::kAchTitleTop + 1.2f * LM::kAchTitlePx;
    const float descInkTop = LM::kAchDescTop + 0.15f * LM::kAchDescPx;
    const float descInkBottom = LM::kAchDescTop + 1.2f * LM::kAchDescPx;
    req(descInkTop - titleInkBottom >= 6.0f,
        "title / description gap is " + std::to_string(descInkTop - titleInkBottom) + " design px, need >= 6");
    req(descInkBottom <= LM::kAchRowH, "the description stays inside the row box");
    req(LM::kAchVisibleRows >= 5 && LM::achRowTop(LM::kAchVisibleRows - 1) + LM::kAchRowH <= LM::kAchY1,
        "the visible rows fit the list area");

    Options o;
    o.catalog = "default";
    o.achievementCount = 9;
    Rig rig(o);
    rig.unlock("achievement", "a0");
    rig.unlock("achievement", "a1");
    rig.toTitleExtras();
    rig.openPage(2);
    {
        const json m = rig.menu();
        req(m["extras"]["total"] == 9 && m["extras"]["items"].size() == static_cast<size_t>(LM::kAchVisibleRows) &&
                m["extras"]["scroll"] == 0,
            "a window of " + std::to_string(LM::kAchVisibleRows) + " rows: " + m["extras"].dump().substr(0, 120));
    }
    // Scrolling by item: the window follows the selection down and back up.
    rig.cmd(ROWL_PAUSE_MENU_DOWN, LM::kAchVisibleRows);
    {
        const json m = rig.menu();
        req(m["extras"]["selected"] == LM::kAchVisibleRows && m["extras"]["scroll"] == 1 &&
                m["extras"]["items"][0]["index"] == 1,
            "scrolled by one: " + m["extras"].dump().substr(0, 140));
    }
    rig.cmd(ROWL_PAUSE_MENU_DOWN, 5);
    req(rig.menu()["extras"]["selected"] == 8 && rig.menu()["extras"]["scroll"] == 3 &&
            rig.menu()["extras"]["items"].size() == static_cast<size_t>(LM::kAchVisibleRows),
        "the last window holds the final items");
    rig.cmd(ROWL_PAUSE_MENU_UP, 8);
    req(rig.menu()["extras"]["selected"] == 0 && rig.menu()["extras"]["scroll"] == 0, "back to the top");
    // Click mapping on the two-line geometry (and the scrolled window).
    rig.clickVirtual(700.0f, LM::achRowTop(2) + 40.0f);
    req(rig.menu()["extras"]["selected"] == 2, "a click inside row 2 selects it");
    rig.clickVirtual(700.0f, LM::achRowTop(0) + LM::kAchRowH + 3.0f);
    req(rig.menu()["extras"]["selected"] == 2, "a click in the gap between rows keeps the selection");
    rig.clickVirtual(700.0f, LM::achRowTop(LM::kAchVisibleRows - 1) + LM::kAchRowH - 2.0f);
    req(rig.menu()["extras"]["selected"] == LM::kAchVisibleRows - 1, "the last visible row's bottom edge still maps to it");
    rig.cmd(ROWL_PAUSE_MENU_DOWN, 4);  // selected 9 would be out of range: clamps, scroll 3
    rig.clickVirtual(700.0f, LM::achRowTop(1) + 10.0f);
    req(rig.menu()["extras"]["selected"] == rig.menu()["extras"]["scroll"].get<int>() + 1,
        "a click in a scrolled window maps to catalog index scroll + row");
    rig.cmd(ROWL_PAUSE_MENU_UP, 20);

    // Pixels (960x540, scale 0.5): row 1 (hidden + locked: "???" / "???") is the
    // case that used to collide; rows 0 and 2 too. Select row 3 so none is the
    // highlighted one, then scan the text column of each row for ink: two ink bands
    // (title, description) with a clear band between them.
    rig.cmd(ROWL_PAUSE_MENU_DOWN, 3);
    const std::string page = rig.frame(4);
    const float scale = 0.5f;
    for (int row = 0; row < 3; ++row) {
        const int x0 = static_cast<int>((LM::kRowX + LM::kAchPadX) * scale);
        const int x1 = x0 + 90;
        const int yTop = static_cast<int>(LM::achRowTop(row) * scale) + 1;
        const int yBottom = static_cast<int>((LM::achRowTop(row) + LM::kAchRowH) * scale) - 1;
        const int bgX = static_cast<int>((LM::kRowX + LM::kRowW - 12.0f) * scale);
        const size_t bgAt = (static_cast<size_t>(yTop + 3) * kW + static_cast<size_t>(bgX)) * 4;
        const int bg = (static_cast<uint8_t>(page[bgAt]) + static_cast<uint8_t>(page[bgAt + 1]) +
                        static_cast<uint8_t>(page[bgAt + 2])) / 3;
        std::vector<bool> ink;
        for (int y = yTop; y <= yBottom; ++y) {
            bool any = false;
            for (int x = x0; x < x1 && !any; ++x) {
                const size_t at = (static_cast<size_t>(y) * kW + static_cast<size_t>(x)) * 4;
                const int luma = (static_cast<uint8_t>(page[at]) + static_cast<uint8_t>(page[at + 1]) +
                                  static_cast<uint8_t>(page[at + 2])) / 3;
                any = luma - bg > 45;
            }
            ink.push_back(any);
        }
        // Runs of ink scanlines.
        std::vector<std::pair<int, int>> runs;
        for (int i = 0; i < static_cast<int>(ink.size()); ++i) {
            if (!ink[i]) continue;
            int j = i;
            while (j + 1 < static_cast<int>(ink.size()) && ink[j + 1]) ++j;
            runs.emplace_back(i, j);
            i = j;
        }
        // Glyph rows of one line can split (accents, i-dots): merge runs closer than 2 px.
        std::vector<std::pair<int, int>> bands;
        for (const auto& run : runs) {
            if (!bands.empty() && run.first - bands.back().second <= 2) bands.back().second = run.second;
            else bands.push_back(run);
        }
        req(bands.size() == 2, "row " + std::to_string(row) + ": two text lines (title, description), found " +
                                   std::to_string(bands.size()));
        const int gap = bands[1].first - bands[0].second - 1;
        req(gap >= static_cast<int>(std::ceil(6.0f * scale)),
            "row " + std::to_string(row) + ": clear band between title and description is " + std::to_string(gap) +
                " px (need >= " + std::to_string(static_cast<int>(std::ceil(6.0f * scale))) + ")");
    }
    // The selected row's highlight covers both lines (bottom corner of the row box).
    rig.cmd(ROWL_PAUSE_MENU_UP, 2);  // selected 1
    {
        const std::string selected = rig.frame(3);
        const int x = static_cast<int>((LM::kRowX + LM::kRowW - 60.0f) * scale);
        const int y = static_cast<int>((LM::achRowTop(1) + LM::kAchRowH - 6.0f) * scale);
        const Rgb c = meanColour(selected, x, y, 6, 2);
        req(near(c, Rgb{14, 116, 144}, 8), "the highlight reaches the bottom of the two-line row " + rgbText(c));
    }
    TEST_PASS("achievements layout: two-line rows, >= 6 px clear between title and description (geometry + pixels), scrolling and click mapping over 9 items");
}

// ── 5. toast ────────────────────────────────────────────────────────────────
// Capsule area: virtual (700..1220, 28..84) -> physical (350..610, 14..42); a wide
// box keeps the check independent of the title length.
constexpr int kToastX0 = 320, kToastY0 = 8, kToastX1 = 640, kToastY1 = 50;

void toastTests() {
    Options o;
    o.catalog = "default";
    {
    Rig rig(o);
    RowlEngine_SetPlayState(rig.h, 1);
    // Settle node 1 (the very first line render is not the steady frame).
    const std::string clean = [&] { rig.frame(4); return rig.frame(4); }();
    req(rig.frame(4) == clean, "steady story frame");

    rig.advance();  // node 2: unlocks a0 and a2
    {
        const json e = rig.extras();
        req(e["pending_notices"].size() == 2 && e["toast"].is_null(), "two notices queued before the step: " + e.dump());
    }
    const std::string first = rig.frame(1);
    {
        const json e = rig.extras();
        req(e["toast"].is_object() && e["toast"]["id"] == "a0" && e["toast"]["visible"] == true, "a0 is on screen: " + e.dump());
        req(e["pending_notices"].size() == 1 && e["pending_notices"][0]["id"] == "a2",
            "the shown notice left the queue; a2 waits: " + e["pending_notices"].dump());
        req(rig.engine->unlockToastText() == "Başarım: Birinci", "toast text: " + rig.engine->unlockToastText());
    }
    {
        const Diff d = diffIn(clean, first, kToastX0, kToastY0, kToastX1, kToastY1);
        req(d.inside > 300, "capsule pixels top-centre: " + std::to_string(d.inside));
        req(d.outside == 0, "nothing outside the capsule area changes: " + std::to_string(d.outside));
    }
    req(rig.frame(6) == first, "the toast frame is stable over cached frames");
    // The queued second notice follows without a clean frame between.
    rig.run(3.0f);  // a0 has now been up for more than 3 s
    {
        const json e = rig.extras();
        req(e["toast"].is_object() && e["toast"]["id"] == "a2" && e["pending_notices"].empty(),
            "a2 follows a0 at once: " + e.dump());
        req(rig.engine->unlockToastText() == "Başarım: Üçüncü", "second toast text");
    }
    const std::string second = rig.frame(2);
    req(second != first && diffIn(first, second, kToastX0, kToastY0, kToastX1, kToastY1).outside == 0,
        "the second capsule differs from the first, only inside the capsule area");
    rig.run(3.2f);  // a2 expires
    {
        const json e = rig.extras();
        req(e["toast"].is_null() && e["pending_notices"].empty(), "queue empty, no toast: " + e.dump());
        req(rig.engine->unlockToastText().empty(), "no toast text");
    }
    req(rig.frame(2) == clean, "after the toasts the clean story frame returns bit for bit (no stale capsule)");
    req(rig.frame(8) == clean, "...and stays clean over cached frames");
    TEST_PASS("toast: capsule top-centre for ~3 s, queue drains one by one, clean frame after (no stale frame), pending_notices consistent");

    // Menus hide it, freeze its clock and keep new notices queued.
    rig.advance();  // node 3: unlocks hidden a1
    RowlEngine_SetPaused(rig.h, 1);
    const std::string pausedA = rig.frame(4);
    rig.step(5.0f, 3);
    {
        const json e = rig.extras();
        req(e["toast"].is_null() && e["pending_notices"].size() == 1 && e["pending_notices"][0]["id"] == "a1",
            "paused: not shown, still queued: " + e.dump());
        req(rig.engine->unlockToastText().empty(), "no toast text under a menu");
    }
    RowlEngine_SetPaused(rig.h, 0);
    rig.step(0.016f);
    {
        const json e = rig.extras();
        req(e["toast"].is_object() && e["toast"]["id"] == "a1" && e["pending_notices"].empty(), "shown after the menu closes: " + e.dump());
        req(rig.engine->unlockToastText() == "Başarım: Gizli", "an unlocked hidden achievement shows its real title");
    }
    // Opening a menu mid-toast hides it and freezes the remaining time.
    rig.run(1.0f);
    const double remaining = rig.extras()["toast"]["remaining_seconds"].get<double>();
    req(remaining > 1.9 && remaining < 2.1, "about two seconds left: " + std::to_string(remaining));
    RowlEngine_SetPaused(rig.h, 1);
    rig.step(5.0f, 2);
    {
        const json e = rig.extras();
        req(e["toast"].is_object() && e["toast"]["visible"] == false &&
                std::fabs(e["toast"]["remaining_seconds"].get<double>() - remaining) < 1e-4,
            "paused mid-toast: hidden, clock frozen: " + e.dump());
    }
    RowlEngine_SetPaused(rig.h, 0);
    rig.run(1.0f);
    req(rig.extras()["toast"].is_object(), "the toast continues after the menu");
    rig.run(1.6f);
    req(rig.extras()["toast"].is_null(), "...and ends when its time is up");
    // The title screen is a menu as well.
    TEST_PASS("toast: hidden and frozen under any menu, notices wait in the queue and show afterwards");
    }

    // Auto does not wait for the toast: the story keeps advancing under it.
    Rig autoRig(o);
    RowlEngine_SetPlayState(autoRig.h, 1);
    autoRig.step(0.016f, 2);
    RowlEngine_SetAutoDelay(autoRig.h, 1.0f);
    RowlEngine_SetAutoMode(autoRig.h, 1);
    autoRig.advance();  // node 2 (manual): toast a0 starts
    autoRig.step(0.016f);
    req(autoRig.extras()["toast"].is_object(), "toast under Auto");
    for (int i = 0; i < 40 && RowlEngine_GetCurrentNodeId(autoRig.h) == 2; ++i) autoRig.step(0.1f);
    req(RowlEngine_GetCurrentNodeId(autoRig.h) == 3 && autoRig.extras()["toast"].is_object(),
        "Auto advanced to node 3 while the toast was up");
    TEST_PASS("toast under Auto: the story is not held up");
}

// ── 6. isolation ────────────────────────────────────────────────────────────
std::map<std::string, std::string> snapshotDirectory(const fs::path& dir) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return out;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) out[fs::relative(entry.path(), dir).string()] = readBytes(entry.path());
    }
    return out;
}

void isolationTests() {
    Options o;
    o.catalog = "default";
    Rig rig(o);
    for (int i = 0; i < 3; ++i) rig.advance();  // node 4: m1 is the game's music, a0/a1/a2 unlocked
    rig.step(0.016f);
    rig.run(12.0f);  // let every toast play out: the queue and the toast are empty
    for (const char* id : {"g00", "g01", "g02"}) rig.unlock("cg", id);
    rig.engine->setScriptVariable("hp", "7");
    req(rig.engine->saveGameSlot(0), "write a save slot");
    RowlEngine_ProfileFlush(rig.h);

    const std::string saveDir = capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8);
    const auto savesBefore = snapshotDirectory(saveDir);
    req(!savesBefore.empty(), "the save directory holds the slot");
    const auto stateBefore = rig.engine->getGameState();
    const std::string profileBefore = rig.profile().dump();
    const std::string extrasBefore = rig.extras().dump();
    const auto historyBefore = rig.engine->getDialogueHistory();
    const std::string variableBefore = rig.engine->getScriptVariable("hp");
    const uint64_t stepBefore = rig.engine->getCurrentStepId();
    const std::string bgmBefore = stateBefore->activeBgm;
    const bool bgmPlayingBefore = stateBefore->bgmPlaying;

    // A full tour: title -> Ekstralar -> gallery (viewer, stepping) -> music (play, stop,
    // play) -> achievements -> back out, with frames drawn on the way.
    req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK && RowlEngine_OpenExtras(rig.h) == ROWL_RESULT_OK, "open");
    rig.frame(2);
    rig.openPage(0);
    rig.frame(8);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.frame(2);
    rig.cmd(ROWL_PAUSE_MENU_RIGHT, 2);
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.openPage(1);
    rig.unlock("music", "m0");  // (the host unlocking is outside the tour: re-compared below)
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.frame(2);
    rig.cmd(ROWL_PAUSE_MENU_DOWN);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.cmd(ROWL_PAUSE_MENU_UP);
    rig.cmd(ROWL_PAUSE_MENU_CONFIRM);
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.openPage(2);
    rig.cmd(ROWL_PAUSE_MENU_DOWN, 2);
    rig.frame(2);
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    req(rig.mode() == 3, "back on the title");
    RowlEngine_SetPaused(rig.h, 0);
    rig.step(0.016f, 2);

    req(rig.engine->getGameState() == stateBefore, "GameState object unchanged (activeBgm '" + bgmBefore + "')");
    req(stateBefore->activeBgm == bgmBefore && stateBefore->bgmPlaying == bgmPlayingBefore && rig.engine->getCurrentStepId() == stepBefore,
        "activeBgm / bgmPlaying / stepId unchanged");
    req(rig.engine->getScriptVariable("hp") == variableBefore && variableBefore == "7", "variables unchanged");
    req(snapshotDirectory(saveDir) == savesBefore, "save slots unchanged byte for byte");
    const auto historyAfter = rig.engine->getDialogueHistory();
    req(historyAfter.size() == historyBefore.size(), "history length unchanged");
    for (size_t i = 0; i < historyBefore.size(); ++i) {
        req(historyAfter[i].dialogue == historyBefore[i].dialogue && historyAfter[i].nodeId == historyBefore[i].nodeId &&
                historyAfter[i].read == historyBefore[i].read && historyAfter[i].contentId == historyBefore[i].contentId,
            "history row unchanged");
    }
    // Only the host's own unlock of m0 (inside the tour) may differ in the profile.
    json profileAfter = rig.profile();
    json expected = json::parse(profileBefore);
    expected["unlocks"]["music"].push_back("m0");
    req(profileAfter == expected, "profile (read marks, preferences, unlocks) unchanged apart from the host's m0 unlock");
    json extrasAfter = rig.extras();
    json extrasExpected = json::parse(extrasBefore);
    req(extrasAfter["pending_notices"] == extrasExpected["pending_notices"] && extrasAfter["pending_notices"].empty(),
        "no notices were created or consumed");
    req(extrasAfter["counts"]["achievements"] == extrasExpected["counts"]["achievements"] &&
            extrasAfter["counts"]["gallery"] == extrasExpected["counts"]["gallery"],
        "gallery / achievement unlock counts unchanged");
    // The audio is the game's again.
    auto* audio = rig.engine->getAudio();
    req(audio->isBgmPlaying() == bgmPlayingBefore && endsWith(audio->getCurrentBgmPath(), "m1.wav"), "the game's BGM plays again");
    TEST_PASS("isolation: a full Ekstralar tour leaves variables, GameState (activeBgm), save slots (bytes), read marks / profile, history and unlocks as they were");
}

// ── 7. tr / en ──────────────────────────────────────────────────────────────
void languageTests() {
    // The string table: every new label differs per language except the mark.
    const UiText ids[] = {UiText::Extras, UiText::Gallery, UiText::MusicRoom, UiText::Achievements,
                          UiText::NothingYet, UiText::NowPlaying, UiText::HintExtras, UiText::HintGallery,
                          UiText::HintGalleryViewer, UiText::HintMusicRoom, UiText::HintAchievements,
                          UiText::ToastAchievement};
    for (UiText id : ids) {
        const std::string tr = Rowl::Core::uiText(id, UiLang::Turkish);
        const std::string en = Rowl::Core::uiText(id, UiLang::English);
        req(!tr.empty() && !en.empty() && tr != en, "tr/en entry exists and differs: " + tr + " | " + en);
    }
    req(std::string(Rowl::Core::uiText(UiText::LockedMark, UiLang::Turkish)) == "???", "locked mark");

    Options o;
    o.catalog = "default";
    o.locales = true;
    Rig rig(o);
    rig.unlock("achievement", "a0");
    rig.unlock("cg", "g00");
    rig.unlock("music", "m0");
    req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "title");
    req(rig.labels()[3] == "Ekstralar", "tr title row");
    req(RowlEngine_OpenExtras(rig.h) == ROWL_RESULT_OK, "extras");
    req(rig.labels() == std::vector<std::string>({"Galeri", "Müzik Odası", "Başarımlar", "Geri"}), "tr root");
    const std::string trHint = rig.menu()["hint"].get<std::string>();
    rig.openPage(0);
    req(rig.menu()["extras"]["cells"][0]["title"] == "Resim 0" && rig.menu()["title"].get<std::string>().rfind("Galeri", 0) == 0,
        "tr gallery caption + title");
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.openPage(2);
    req(rig.menu()["extras"]["items"][0]["title"] == "Birinci" &&
            rig.menu()["extras"]["items"][0]["description"] == "Birinci açıklama.", "tr achievement");
    rig.cmd(ROWL_PAUSE_MENU_BACK);

    req(RowlEngine_SetLocale(rig.h, "en") == 0, "switch to English");
    req(rig.labels() == std::vector<std::string>({"Gallery", "Music Room", "Achievements", "Back"}), "en root: " + rig.menu().dump());
    req(rig.menu()["title"] == "Extras" && rig.menu()["hint"] != trHint, "en title + hint");
    rig.openPage(0);
    req(rig.menu()["extras"]["cells"][0]["title"] == "Picture 0" && rig.menu()["extras"]["cells"][1]["title"] == "???",
        "en gallery captions");
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.openPage(1);
    req(rig.labels()[0] == "Track 0", "en music title");
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.openPage(2);
    {
        const json items = rig.menu()["extras"]["items"];
        req(items[0]["title"] == "First" && items[0]["description"] == "First description." &&
                items[1]["title"] == "???" && items[2]["title"] == "Third",
            "en achievements: " + items.dump());
    }
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    rig.cmd(ROWL_PAUSE_MENU_BACK);
    req(rig.labels()[3] == "Extras", "en title row");

    // The toast follows the language too.
    RowlEngine_SetPaused(rig.h, 0);
    rig.advance();  // node 2: a0 was unlocked above (its notice waits), a2 is new
    rig.step(0.016f);
    req(rig.engine->unlockToastText() == "Achievement: First", "en toast: " + rig.engine->unlockToastText());
    rig.run(3.2f);
    req(rig.engine->unlockToastText() == "Achievement: Third", "en toast, second: " + rig.engine->unlockToastText());
    RowlEngine_SetLocale(rig.h, "tr");
    req(rig.engine->unlockToastText() == "Başarım: Üçüncü", "the toast follows a language switch: " + rig.engine->unlockToastText());
    TEST_PASS("tr/en: title row, root, hints, gallery captions, music/achievement texts and the toast follow the UI language");
}

}  // namespace

void test_extras_screens() {
    TEST_SECTION("Extras screens: title row, Gallery / Music Room / Achievements, toast (D07-2)");

    g_root = fs::temp_directory_path() / "rowl_extras_screens";
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

    titleAndPagesTests();
    galleryTests();
    musicTests();
    achievementsTests();
    achievementLayoutTests();
    toastTests();
    isolationTests();
    languageTests();

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(g_root, ec);
}
