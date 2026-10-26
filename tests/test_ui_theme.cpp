/**
 * test_ui_theme.cpp — T1: the player-interface theme (Assets/json/ui_theme.json).
 *
 * Kilit:
 *  1. Ayristirici: dosyasiz/bos tema == bugunku sabitler birebir (her eski sabit tek tek);
 *     bozuk dosya (JSON degil / format yok / buyuk) -> tumu yerlesik + tek tani; bozuk alan
 *     (renk, NaN, tur, yol kacisi, hizalama) -> o alan varsayilan + tani; sinir disi sayi
 *     kistirilir + tani; bilinmeyen anahtar yok sayilir; en fazla 32 tani; kalitim
 *     (menu.title -> toast/rozet/etiket vurgusu) ve high_contrast ust yazimi; serilestirme
 *     ayristirmayla ayni tablodan (gidis-donus).
 *  2. Dosyasiz oyun: GetUiThemeJson loaded=false/source="" ve tema == yerlesik; menu, galeri,
 *     basarim, toast, rozet, diyalog, secim piksel renkleri bugunku sabitler (burada acikca
 *     sabitlenir; mevcut piksel testleri ayrica degismeden yesil).
 *  3. Temali piksel: menu karartma / panel dolgu / kenarlik kalinligi / secili satir dolgusu +
 *     serit / etiket + secili etiket + baslik renkleri; galeri kilitli kutu + hucre kenarlari;
 *     basarim plakasi; toast dolgusu; AUTO / SKIP rozeti; diyalog kutusu dolgu + kenarlik;
 *     konusmaci etiketi dolgu + kenarlik + (bilesen yoksa tema) rengi; secim dugmesi dolgu +
 *     bilesen ust yazimi; eksik arka plan / yer tutucu renkleri.
 *  4. Oncelik: bilesende YAZILI anahtar > tema > yerlesik; yalniz yazilmayan alanlar tema
 *     degisince (Reload) yeni degeri alir, yazilanlar kalir. Ayrica bilesen JSON'u degismez.
 *  5. Yazi tipi: fonts.menu / dialogue / speaker ayri yuklenir (piksel farki yalniz o bolgede),
 *     olmayan yol -> proje yazi tipi + tani; menu yazi tipi olcum/cizimde ayni ornek (Geçmiş
 *     sarma).
 *  6. Sekil alanlari: honor_shape_fields kapaliyken corner_radius / border_thickness cizilmez
 *     (bugunku piksel), aciksa yuvarlak kose + kalin kenarlik cizilir.
 *  7. Reload: dosya degisip RowlEngine_ReloadUiTheme sonrasi bir sonraki kare yeni renkte (bayat
 *     kare yok); SetUiThemeJson dosyasiz onizleme, kullanilamaz belge VALIDATION_ERROR + eski tema;
 *     C API: yetenek biti, Init oncesi STATE_ERROR, NULL, tampon sozlesmesi.
 *  8. Yuksek kontrast: high_contrast paleti yalniz yuksek kontrast acikken uygulanir.
 *
 * Bilerek-boz: Window::drawPause paneldeki dolgu renginin temadan okunmasi (panelFill) bozulursa
 * 3; engine.cpp'de dialogue/option hydrasyonunda themed.* yerine sabit yazilirsa 4; applyUiThemeTo'daki
 * authoredMask kapisi kaldirilirsa 4'un "yazilan kalir" kontrolu; Window::setUiTheme'deki
 * invalidateFrameCache kaldirilirsa 7'nin bayat kare kontrolu kirmizi olur.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"
#include "rowl/state/ui_theme.hpp"

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
using namespace Rowl::State;

constexpr const char* kSection = "ui-theme";
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

std::string readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
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

// ── 1. parser ───────────────────────────────────────────────────────────────
bool hasDiagnostic(const UiThemeLoad& load, const std::string& needle) {
    for (const auto& line : load.diagnostics) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

UiColor c(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return UiColor{r, g, b, a}; }

void parserDefaultsTests() {
    // The theme's defaults ARE the constants the renderer used before the theme existed.
    const UiTheme t = builtinUiTheme();
    req(t.menu.dim == c(0, 0, 0, 160) && t.menu.panelFill == c(15, 15, 26, 235) &&
            t.menu.panelBorder == c(56, 189, 248) && t.menu.panelBorderWidth == 1.0f,
        "menu dim / panel / border");
    req(t.menu.title == c(56, 189, 248) && t.menu.text == c(241, 245, 249) &&
            t.menu.dimText == c(148, 163, 184) && t.menu.disabledText == c(148, 163, 184) &&
            t.menu.hint == c(148, 163, 184) && t.menu.selectedText == c(56, 189, 248),
        "menu text colours");
    req(t.menu.selectedFill == c(14, 116, 144) && t.menu.selectedBar == c(56, 189, 248) &&
            t.menu.selectedBarWidth == 6.0f && t.menu.thumbBorder == c(56, 189, 248) &&
            t.menu.historySpeaker == c(56, 189, 248),
        "menu selection");
    req(t.menu.titlePx == 40.0f && t.menu.rowPx == 24.0f && t.menu.valuePx == 24.0f &&
            t.menu.detailPx == 16.0f && t.menu.hintPx == 19.0f && t.menu.historySpeakerPx == 20.0f,
        "menu text sizes");
    req(t.extras.cellFill == c(8, 10, 18) && t.extras.lockedFill == c(22, 25, 40) &&
            t.extras.cellBorderSelected == c(56, 189, 248) && t.extras.cellBorder == c(71, 85, 105) &&
            t.extras.lockedBorder == c(46, 56, 78) && t.extras.plate == c(22, 25, 42) &&
            t.extras.plateSelected == c(14, 116, 144) && t.extras.plateBar == c(56, 189, 248) &&
            t.extras.description == c(203, 213, 225) && t.extras.viewerDim == c(0, 0, 0, 240) &&
            t.extras.viewerFrame == c(56, 189, 248) && t.extras.markPx == 44.0f &&
            t.extras.captionPx == 16.0f && t.extras.viewerTitlePx == 30.0f,
        "extras");
    req(t.toast.fill == c(15, 15, 26, 215) && t.toast.rule == c(56, 189, 248, 190) &&
            t.toast.accent == c(56, 189, 248) && t.toast.text == c(241, 245, 249) &&
            t.toast.height == 56.0f && t.toast.marginTop == 28.0f && t.toast.padX == 30.0f &&
            t.toast.maxTextWidth == 1300.0f && t.toast.px == 26.0f && t.toast.rulePx == 2.0f,
        "toast");
    req(t.badges.autoFill == c(15, 15, 26, 200) && t.badges.autoText == c(56, 189, 248) &&
            t.badges.skipFill == c(14, 116, 144, 210) && t.badges.skipText == c(241, 245, 249) &&
            t.badges.width == 116.0f && t.badges.height == 44.0f && t.badges.gap == 12.0f &&
            t.badges.marginRight == 40.0f && t.badges.marginTop == 28.0f && t.badges.px == 24.0f,
        "badges");
    req(t.scene.letterbox == c(11, 15, 25) && t.scene.clear == c(26, 26, 36) &&
            t.scene.missingBackground == c(20, 24, 38) && t.scene.placeholderFill == c(30, 41, 59, 220) &&
            t.scene.placeholderBorder == c(56, 189, 248),
        "scene");
    // The story defaults are exactly the engine's former `value("key", default)` literals.
    req(uiColorToHex(t.story.dialogue.boxColor) == "#0F0F1A" && t.story.dialogue.boxOpacity == 0.88f &&
            uiColorToHex(t.story.dialogue.borderColor) == "#00F0FF" && t.story.dialogue.borderThickness == 2.0f &&
            uiColorToHex(t.story.dialogue.textColor) == "#F1F5F9" && t.story.dialogue.fontSize == 24.0f &&
            t.story.dialogue.speakerFontSize == 20.0f && uiColorToHex(t.story.dialogue.speakerColor) == "#38BDF8" &&
            t.story.dialogue.textAlignment == "Left" && t.story.dialogue.cornerRadius == 8.0f,
        "dialogue defaults");
    req(uiColorToHex(t.story.choice.backgroundColor) == "#1E293B" && uiColorToHex(t.story.choice.hoverColor) == "#0EA5E9" &&
            uiColorToHex(t.story.choice.borderColor) == "#38BDF8" && uiColorToHex(t.story.choice.textColor) == "#FFFFFF" &&
            t.story.choice.opacity == 1.0f && t.story.choice.fontSize == 22.0f &&
            t.story.choice.textAlignment == "Center" && t.story.choice.borderThickness == 2.0f &&
            t.story.choice.cornerRadius == 8.0f,
        "choice defaults");
    req(t.story.speakerTag.textColor == c(255, 255, 255) && t.story.speakerTag.borderColor == c(255, 255, 255, 180) &&
            t.story.speakerTag.borderWidth == 1.0f && t.story.choiceFocus.barWidth == 6.0f &&
            t.story.choiceFocus.inset == 3.0f && !t.story.choiceFocus.innerColor && !t.story.choiceFocus.barColor &&
            !t.story.honorShapeFields,
        "speaker tag / focus marks / shape flag");
    req(t.fonts.menu.empty() && t.fonts.dialogue.empty() && t.fonts.speaker.empty(), "no fonts");

    // An empty object (with the format) and "{}"-ish documents change nothing.
    const UiThemeLoad empty = parseUiTheme(R"({"format":1})");
    req(empty.usable && empty.diagnostics.empty() && empty.theme == t && !empty.highContrast, "empty theme == built-in");
    // Round trip: the serializer writes every key the parser reads.
    const UiThemeLoad again = parseUiTheme(uiThemeToJson(t));
    req(again.usable && again.diagnostics.empty() && again.theme == t,
        "serialize -> parse is the identity: " + (again.diagnostics.empty() ? uiThemeToJson(again.theme) : again.diagnostics[0]));
    TEST_PASS("parser: empty theme equals every former constant; serialize/parse round trip");
}

void parserBadInputTests() {
    const UiTheme builtin = builtinUiTheme();
    // Whole-file rejections: built-in theme + exactly one diagnostic.
    for (const char* bad : {"", "not json", "[1,2]", "{}", R"({"format":2})", R"({"format":"1"})", R"({"format":1.5})"}) {
        const UiThemeLoad load = parseUiTheme(bad);
        req(!load.usable && load.theme == builtin && load.diagnostics.size() == 1 && !load.highContrast,
            std::string("bad document '") + bad + "' -> built-in + one diagnostic");
    }
    {
        std::string huge = R"({"format":1,"pad":")" + std::string(kUiThemeMaxFileBytes, 'x') + "\"}";
        const UiThemeLoad load = parseUiTheme(huge);
        req(!load.usable && load.diagnostics.size() == 1 && hasDiagnostic(load, "larger than"), "oversized file ignored");
    }
    // Bad fields keep their default and say why; the good ones around them apply.
    const UiThemeLoad load = parseUiTheme(R"({
        "format": 1,
        "menu": {"panel_fill": "#102030", "dim": "red", "title": "#12345", "text": 7, "hint": "#GGGGGG",
                 "selected_bar_width": "wide", "row_px": null, "title_px": 1e308,
                 "panel_border_width": 99, "value_px": -5},
        "extras": [1, 2],
        "toast": {"fill": "#102030FF", "px": true},
        "story": {"dialogue": {"text_alignment": "diagonal", "box_color": "#abcdef", "border_thickness": 3},
                  "choice": "nope", "honor_shape_fields": "yes"},
        "fonts": {"menu": "../../etc/passwd.ttf", "dialogue": "/abs/font.ttf", "speaker": "fonts/ok.TTF"},
        "future_section": {"x": 1}, "menu_extra_key": 1
    })");
    req(load.usable, "fields do not sink the file");
    req(load.theme.menu.panelFill == c(0x10, 0x20, 0x30), "good colour applies");
    req(load.theme.menu.dim == builtin.menu.dim && load.theme.menu.title == builtin.menu.title &&
            load.theme.menu.text == builtin.menu.text && load.theme.menu.hint == builtin.menu.hint,
        "bad colours keep the default");
    req(load.theme.menu.selectedBarWidth == 6.0f && load.theme.menu.rowPx == 24.0f,
        "bad numbers keep the default");
    req(load.theme.menu.titlePx == 120.0f, "an enormous (finite) number is clamped, not trusted");
    req(load.theme.menu.panelBorderWidth == 8.0f && load.theme.menu.valuePx == 8.0f, "out of range is clamped");
    req(hasDiagnostic(load, "menu.dim") && hasDiagnostic(load, "menu.title") && hasDiagnostic(load, "menu.text") &&
            hasDiagnostic(load, "menu.hint") && hasDiagnostic(load, "menu.selected_bar_width") &&
            hasDiagnostic(load, "menu.panel_border_width") && hasDiagnostic(load, "menu.value_px") &&
            hasDiagnostic(load, "clamped"),
        "one diagnostic per rejected value");
    req(hasDiagnostic(load, "'extras' is not an object") && hasDiagnostic(load, "'story.choice' is not an object"),
        "a non-object section is reported");
    req(load.theme.toast.fill == c(0x10, 0x20, 0x30, 0xFF) && load.theme.toast.px == 26.0f && hasDiagnostic(load, "toast.px"),
        "#RRGGBBAA and a boolean for a number");
    req(load.theme.story.dialogue.textAlignment == "Left" && uiColorToHex(load.theme.story.dialogue.boxColor) == "#ABCDEF" &&
            load.theme.story.dialogue.borderThickness == 3.0f && !load.theme.story.honorShapeFields &&
            hasDiagnostic(load, "text_alignment") && hasDiagnostic(load, "honor_shape_fields"),
        "story fields: alignment and flag validated, hex canonicalised upper-case");
    req(load.theme.fonts.menu.empty() && load.theme.fonts.dialogue.empty() && load.theme.fonts.speaker == "fonts/ok.TTF" &&
            hasDiagnostic(load, "fonts.menu") && hasDiagnostic(load, "fonts.dialogue"),
        "font path escapes (.., absolute) are refused, a relative .TTF is kept");
    req(!hasDiagnostic(load, "future_section") && !hasDiagnostic(load, "menu_extra_key"), "unknown keys are ignored silently");

    // Hex strictness: only "#RRGGBB" / "#RRGGBBAA".
    for (const char* hex : {"#FFF", "FFFFFF", "#FFFF", "#GG0000", " #FFFFFF", "#FFFFFFF"}) {
        const UiThemeLoad l = parseUiTheme(std::string(R"({"format":1,"menu":{"panel_fill":")") + hex + "\"}}");
        req(l.theme.menu.panelFill == builtin.menu.panelFill && l.diagnostics.size() == 1,
            std::string("colour '") + hex + "' refused");
    }
    // Diagnostics are capped at 32 (+1 "omitted" line).
    {
        json many = {{"format", 1}, {"menu", json::object()}, {"extras", json::object()}, {"toast", json::object()},
                     {"badges", json::object()}, {"scene", json::object()}};
        for (const char* key : {"dim", "panel_fill", "panel_border", "title", "text", "dim_text", "disabled_text",
                                "selected_fill", "selected_bar", "selected_text", "hint", "thumb_border",
                                "history_speaker"}) {
            many["menu"][key] = "bad";
        }
        for (const char* key : {"cell_fill", "locked_fill", "cell_border", "plate", "description", "viewer_dim",
                                "viewer_frame", "locked_border", "plate_bar", "plate_selected", "text", "dim_text"}) {
            many["extras"][key] = "bad";
        }
        for (const char* key : {"fill", "text", "accent", "rule"}) many["toast"][key] = "bad";
        for (const char* key : {"auto_fill", "auto_text", "skip_fill", "skip_text"}) many["badges"][key] = "bad";
        for (const char* key : {"letterbox", "clear", "missing_background", "placeholder_fill"}) many["scene"][key] = "bad";
        const UiThemeLoad l = parseUiTheme(many.dump());
        req(l.diagnostics.size() == kUiThemeMaxDiagnostics + 1 && hasDiagnostic(l, "further diagnostics omitted"),
            "diagnostics capped: " + std::to_string(l.diagnostics.size()));
    }
    TEST_PASS("parser: bad file -> built-in + one line; bad field -> default + line; clamp; path escapes; unknown keys; 32-line cap");
}

void parserInheritanceTests() {
    // menu.title is the chrome's accent: toast head, AUTO text, selected label, History speaker follow it...
    const UiThemeLoad themed = parseUiTheme(R"({"format":1,"menu":{"title":"#FFAA00","panel_fill":"#112233",
        "text":"#EEEEEE","selected_fill":"#445566"}})");
    req(themed.theme.toast.accent == c(0xFF, 0xAA, 0x00) && themed.theme.badges.autoText == c(0xFF, 0xAA, 0x00) &&
            themed.theme.menu.selectedText == c(0xFF, 0xAA, 0x00) && themed.theme.menu.historySpeaker == c(0xFF, 0xAA, 0x00) &&
            themed.theme.extras.titleSelected == c(0xFF, 0xAA, 0x00),
        "title accent is inherited");
    req(themed.theme.toast.rule == c(0xFF, 0xAA, 0x00, 190) && themed.theme.toast.fill == c(0x11, 0x22, 0x33, 215) &&
            themed.theme.badges.autoFill == c(0x11, 0x22, 0x33, 200) &&
            themed.theme.badges.skipFill == c(0x44, 0x55, 0x66, 210) && themed.theme.extras.plateSelected == c(0x44, 0x55, 0x66) &&
            themed.theme.toast.text == c(0xEE, 0xEE, 0xEE) && themed.theme.extras.text == c(0xEE, 0xEE, 0xEE),
        "inherited colours keep their own alpha");
    // ...unless the file names the field: the explicit value wins.
    const UiThemeLoad explicitOwn = parseUiTheme(R"({"format":1,"menu":{"title":"#FFAA00"},"toast":{"accent":"#00FF00"}})");
    req(explicitOwn.theme.toast.accent == c(0, 255, 0) && explicitOwn.theme.badges.autoText == c(0xFF, 0xAA, 0x00),
        "an explicit field is not overwritten by inheritance");

    // high_contrast: a second palette over the same file; only the named keys differ.
    const UiThemeLoad hc = parseUiTheme(R"({"format":1,"menu":{"panel_fill":"#202020","title":"#FFAA00"},
        "story":{"dialogue":{"font_size":30}},
        "high_contrast":{"menu":{"panel_fill":"#000000","title":"#FFFF00"},"story":{"dialogue":{"font_size":99}},
                         "fonts":{"menu":"fonts/x.ttf"}}})");
    req(hc.usable && hc.highContrast.has_value(), "high_contrast palette parsed");
    req(hc.theme.menu.panelFill == c(0x20, 0x20, 0x20) && hc.highContrast->menu.panelFill == c(0, 0, 0), "panel differs");
    req(hc.highContrast->menu.title == c(255, 255, 0) && hc.highContrast->toast.accent == c(255, 255, 0) &&
            hc.theme.toast.accent == c(0xFF, 0xAA, 0x00),
        "inheritance re-runs over the high-contrast palette");
    req(hc.highContrast->menu.text == hc.theme.menu.text && hc.highContrast->story.dialogue.fontSize == 30.0f &&
            hc.highContrast->fonts.menu.empty(),
        "story and fonts are not overridable by high_contrast");
    const UiThemeLoad hcBad = parseUiTheme(R"({"format":1,"high_contrast":[1]})");
    req(hcBad.usable && !hcBad.highContrast && hcBad.diagnostics.size() == 1, "non-object high_contrast ignored");
    const UiThemeLoad hcDiag = parseUiTheme(R"({"format":1,"high_contrast":{"menu":{"dim":"bad"}}})");
    req(hcDiag.diagnostics.size() == 1 && hcDiag.diagnostics[0].find("high_contrast.menu.dim") != std::string::npos,
        "diagnostics name the high_contrast path");
    TEST_PASS("parser: chrome inheritance (title/panel/selection/text), explicit fields win, high_contrast palette");
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

enum class Story { Plain, Written, Scene };

struct Options {
    std::string theme;       // ui_theme.json text; empty = no file
    Story story = Story::Plain;
    bool catalog = false;    // extras.json with 2 locked gallery items + 2 achievements
    bool fonts = false;      // copies the bundled fonts into the project as fonts/{regular,bold}.ttf
};

// Story::Plain   node 1: dialogue (speaker "Ana", writes NO style key) + 2 options (A writes none, B writes
//                background_color + border_color)
// Story::Written node 1: dialogue writing box_color / border_color / speaker_color / font_size; a choice whose
//                option writes text_color / font_size
// Story::Scene   node 1: dialogue + a background and a character whose textures do not exist
std::string buildGraph(Story story) {
    json comps = json::array();
    json dialogue = {{"speaker", "Ana"}, {"dialogue", "Merhaba dunya"}, {"typewriter_enabled", false},
                     {"content_id", "40000000-0000-4000-8000-000000000001"}};
    json options = json::array({{{"option_id", "a"}, {"text", "Birinci"}},
                                {{"option_id", "b"}, {"text", "Ikinci"}, {"background_color", "#336699"},
                                 {"border_color", "#FF00FF"}}});
    if (story == Story::Written) {
        dialogue["box_color"] = "#223344";
        dialogue["border_color"] = "#AABBCC";
        dialogue["speaker_color"] = "#CC3300";
        dialogue["font_size"] = 31;
        options = json::array({{{"option_id", "a"}, {"text", "Birinci"}, {"text_color", "#00FF00"}, {"font_size", 25}}});
    }
    comps.push_back(component("dialogue", "d1", dialogue));
    if (story != Story::Scene) comps.push_back(component("choice", "c1", {{"options", options}}));
    if (story == Story::Scene) {
        comps.push_back(component("background", "bg", {{"texture", "nope.png"}}));
        comps.push_back(component("character", "ch", {{"sprite", "nope_sprite.png"}, {"x", 1440}, {"y", 340},
                                                      {"width", 360}, {"height", 540}}));
    }
    json nodes = json::array({{{"id", 1}, {"title", "N1"},
                               {"objects", json::array({{{"id", "o1"}, {"name", "D"}, {"is_active", true},
                                                         {"components", comps}}})},
                               {"next_nodes", json::array()}}});
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
}

std::string catalogText() {
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
        std::snprintf(uuid, sizeof(uuid), "7d9f4e36-5cbe-4f82-8b34-9e3f7c0a2d%02x", g_rigCounter);
        writeBytes(game / "project.rowlproj", std::string(R"({"project_uuid":")") + uuid + R"(","name":"UI Theme"})");
        writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph(options.story));
        if (!options.theme.empty()) writeBytes(game / "Assets" / "json" / "ui_theme.json", options.theme);
        if (options.catalog) writeBytes(game / "Assets" / "json" / "extras.json", catalogText());
        if (options.fonts) {
            fs::create_directories(game / "Assets" / "fonts");
            fs::copy_file("Assets/fonts/default.ttf", game / "Assets" / "fonts" / "regular.ttf");
            fs::copy_file("Assets/fonts/default_bold.ttf", game / "Assets" / "fonts" / "bold.ttf");
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

// Does the rectangle contain a pixel for which `pred` holds?
template <typename Pred>
bool anyPixel(const std::string& pixels, int x0, int y0, int x1, int y1, Pred pred) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            if (pred(at(pixels, x, y))) return true;
        }
    }
    return false;
}

// Pixels that differ between two frames inside / outside a rectangle.
struct Diff {
    size_t inside = 0;
    size_t outside = 0;
};
Diff diffIn(const std::string& a, const std::string& b, int x0, int y0, int x1, int y1) {
    req(a.size() == b.size() && a.size() == size_t(kW) * kH * 4, "frame size");
    Diff out;
    for (uint32_t y = 0; y < kH; ++y) {
        for (uint32_t x = 0; x < kW; ++x) {
            const size_t i = (size_t(y) * kW + x) * 4;
            if (a.compare(i, 4, b, i, 4) == 0) continue;
            const bool in = static_cast<int>(x) >= x0 && static_cast<int>(x) < x1 &&
                            static_cast<int>(y) >= y0 && static_cast<int>(y) < y1;
            (in ? out.inside : out.outside)++;
        }
    }
    return out;
}

// Physical positions (scale 0.5) of things the checks sample.
constexpr int kPanelX = 260, kPanelY = 55;         // panel top-left
constexpr int kPanelSampleX = 270, kPanelSampleY = 64;  // empty panel corner, clear of text
constexpr int kDimSampleX = 100, kDimSampleY = 300;     // outside the panel
constexpr int kDlgX = 40, kDlgY = 430;             // dialogue box top-left (80,860)
constexpr int kDlgW = 880, kDlgH = 90;
constexpr int kChoiceAX = 340, kChoiceAY = 260;    // option A (680,520) 280x32
constexpr int kChoiceBY = 300;                     // option B (680,600)

const char* kOpaqueMenuTheme = R"({"format":1,"menu":{
    "dim":"#112233","panel_fill":"#445566","panel_border":"#FFFF00","panel_border_width":3,
    "selected_fill":"#007700","selected_bar":"#FF0000","selected_bar_width":20,
    "title":"#FFA500","text":"#FF00FF","selected_text":"#00FFFF","dim_text":"#00FF00"}})";

// ── 2. file-less game ──────────────────────────────────────────────────────
void noFileTests() {
    Rig rig;
    const json t = rig.theme();
    req(t["format"] == 1 && t["loaded"] == false && t["source"] == "" && t["diagnostics"].empty() &&
            t["high_contrast"].is_null(),
        "no file: " + t.dump());
    req(t["theme"] == json::parse(uiThemeToJson(builtinUiTheme())), "the theme in force is the built-in one");
    req(rig.engine->uiTheme() == builtinUiTheme(), "Engine::uiTheme() is the built-in one");

    // The dialogue / choice defaults are the old literals; nothing is "written".
    const auto& dialogues = rig.engine->getActiveDialogues();
    req(dialogues.size() == 1 && dialogues[0].boxColor == "#0F0F1A" && dialogues[0].borderColor == "#00F0FF" &&
            dialogues[0].textColor == "#F1F5F9" && dialogues[0].speakerColor == "#38BDF8" && dialogues[0].fontSize == 24.0f &&
            dialogues[0].speakerFontSize == 20.0f && dialogues[0].boxOpacity == 0.88f && dialogues[0].borderThickness == 2.0f &&
            dialogues[0].cornerRadius == 8.0f && dialogues[0].textAlignment == "Left" && dialogues[0].authoredMask == 0,
        "dialogue hydrates with the former literals");
    const auto& buttons = rig.engine->getActiveChoiceButtons();
    req(buttons.size() == 2 && buttons[0].backgroundColor == "#1E293B" && buttons[0].hoverColor == "#0EA5E9" &&
            buttons[0].borderColor == "#38BDF8" && buttons[0].textColor == "#FFFFFF" && buttons[0].fontSize == 22.0f &&
            buttons[0].textAlignment == "Center" && buttons[0].cornerRadius == 8.0f && buttons[0].borderThickness == 2.0f,
        "choice hydrates with the former literals");
    req(buttons[1].backgroundColor == "#336699" && buttons[1].borderColor == "#FF00FF" && buttons[1].authoredMask != 0,
        "a written option key still wins");

    // Pixels of the former constants.
    const std::string story = rig.frame(4);
    requireAt(story, kDlgX + 840, kDlgY + 60, 15, 15, 26, "dialogue box fill (#0F0F1A)");
    requireAt(story, kDlgX + 440, kDlgY, 0, 240, 255, "dialogue border (#00F0FF)");
    requireAt(story, kDlgX + 440, kDlgY + 1, 0, 240, 255, "dialogue border, second ring (thickness 2)");
    requireAt(story, kChoiceAX + 270, kChoiceAY + 14, 30, 41, 59, "option A fill (#1E293B)");
    requireAt(story, kChoiceAX + 270, kChoiceBY + 14, 0x33, 0x66, 0x99, "option B fill (written #336699)");
    requireAt(story, kChoiceAX + 100, kChoiceAY, 56, 189, 248, "option border (#38BDF8)");
    requireAt(story, kChoiceAX + 100, kChoiceAY + 1, 30, 41, 59, "option border is one pixel thick (border_thickness is not drawn)");
    // Speaker tag: sits above the box; fill = speaker colour, border white-ish 180 alpha over it.
    requireAt(story, kDlgX + 12, kDlgY - 7, 56, 189, 248, "speaker tag fill (#38BDF8)");

    // Title screen: panel, dim, border, highlight and bar.
    req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "title");
    const std::string title = rig.frame(4);
    // Panel interior: (15,15,26) at alpha 235 over the dimmed story frame.
    requireAt(title, kPanelSampleX, kPanelSampleY, 15, 15, 26, "panel fill", 8);
    requireAt(title, kPanelX, 300, 56, 189, 248, "panel border (1 px accent)");
    requireAt(title, kPanelX + 1, 300, 15, 15, 26, "panel border is 1 px thick", 8);
    const int rowTop = static_cast<int>(LM::rowTop(0, static_cast<int>(rig.menu()["rows"].size())) * 0.5f);
    requireAt(title, 660, rowTop + 6, 14, 116, 144, "selected row fill (#0E7490)");
    requireAt(title, 291, rowTop + 6, 56, 189, 248, "selected row bar (#38BDF8)");
    requireAt(title, 296, rowTop + 6, 14, 116, 144, "bar is 6 virtual px (3 physical) wide");
    // Dimmed story frame outside the panel: a (0,0,0,160) veil.
    const Rgb veil = at(title, kDimSampleX, kDimSampleY);
    const Rgb behind = at(story, kDimSampleX, kDimSampleY);
    req(std::abs(veil.r - behind.r * 95 / 255) <= 3 && std::abs(veil.b - behind.b * 95 / 255) <= 3,
        "dim veil is (0,0,0,160): " + rgbText(behind) + " -> " + rgbText(veil));
    TEST_PASS("no ui_theme.json: GetUiThemeJson reports the built-in theme; every former constant paints the same pixel");
}

// ── 3. themed menus ─────────────────────────────────────────────────────────
void themedMenuTests() {
    Options o;
    o.theme = kOpaqueMenuTheme;
    Rig rig(o);
    const json t = rig.theme();
    req(t["loaded"] == true && t["source"] == "json/ui_theme.json" && t["diagnostics"].empty(),
        "themed: " + t.dump());
    req(t["theme"]["menu"]["panel_fill"] == "#445566" && t["theme"]["menu"]["selected_bar_width"] == 20,
        "the theme in force carries the values");
    req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "title");
    const std::string title = rig.frame(4);
    const int rowTop = static_cast<int>(LM::rowTop(0, static_cast<int>(rig.menu()["rows"].size())) * 0.5f);
    requireAt(title, kDimSampleX, kDimSampleY, 0x11, 0x22, 0x33, "dim (#112233 opaque)", 1);
    requireAt(title, kPanelSampleX + 20, kPanelSampleY, 0x44, 0x55, 0x66, "panel fill (#445566)", 1);
    for (int i = 0; i < 3; ++i) requireAt(title, kPanelX + i, 300, 255, 255, 0, "panel border ring " + std::to_string(i), 1);
    requireAt(title, kPanelX + 3, 300, 0x44, 0x55, 0x66, "border is 3 px thick, then the panel fill", 1);
    requireAt(title, 660, rowTop + 6, 0, 0x77, 0, "selected row fill (#007700)", 1);
    requireAt(title, 298, rowTop + 6, 255, 0, 0, "selected bar (20 virtual = 10 physical px, #FF0000)", 1);
    requireAt(title, 304, rowTop + 6, 0, 0x77, 0, "bar ends after 10 physical px", 1);
    // Text colours: a label pixel of each colour exists in its row (glyph cores are fully covered).
    const int rowH = static_cast<int>(LM::rowH(static_cast<int>(rig.menu()["rows"].size())) * 0.5f);
    const int rowCount = static_cast<int>(rig.menu()["rows"].size());
    const int row1 = static_cast<int>(LM::rowTop(1, rowCount) * 0.5f);  // "Devam Et": disabled without a save
    const int row3 = static_cast<int>(LM::rowTop(3, rowCount) * 0.5f);  // "Tercihler": enabled
    req(anyPixel(title, 296, rowTop, 480, rowTop + rowH, [](const Rgb& p) { return p.g > 200 && p.b > 200 && p.r < 90; }),
        "selected label drawn in selected_text (#00FFFF)");
    req(anyPixel(title, 296, row3, 480, row3 + rowH, [](const Rgb& p) { return p.r > 200 && p.b > 200 && p.g < 90; }),
        "unselected label drawn in text (#FF00FF)");
    req(anyPixel(title, 296, row1, 480, row1 + rowH, [](const Rgb& p) { return p.g > 200 && p.r < 90 && p.b < 90; }),
        "disabled label drawn in disabled_text (inherits dim_text #00FF00)");
    req(anyPixel(title, 270, 78, 480, 100, [](const Rgb& p) { return p.r > 200 && p.g > 120 && p.g < 200 && p.b < 90; }),
        "page title drawn in title (#FFA500)");
    TEST_PASS("themed menu: dim, panel fill, border thickness, selected fill + bar width, label / selected / title colours");
}

// ── 3b. Ekstralar, toast, badges ────────────────────────────────────────────
void themedExtrasToastBadgeTests() {
    Options o;
    o.catalog = true;
    o.theme = R"({"format":1,
        "extras":{"locked_fill":"#880000","locked_border":"#00AA00","cell_border":"#0000AA","plate":"#770077",
                  "plate_selected":"#006666","plate_bar":"#FFFFFF","cell_fill":"#222222"},
        "toast":{"fill":"#303030","rule_px":0},
        "badges":{"auto_fill":"#505050","skip_fill":"#707070","auto_text":"#FFFFFF"}})";
    Rig rig(o);
    // Gallery: both cells locked -> the dark box colour is the theme's, the border too.
    req(RowlEngine_OpenExtras(rig.h) == ROWL_RESULT_OK, "extras");
    RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_CONFIRM);  // Galeri
    req(rig.menu()["extras"]["page"] == "gallery", "gallery page");
    const std::string gallery = rig.frame(4);
    const int cx = static_cast<int>(LM::galleryCellX(1) * 0.5f);  // second cell: not selected
    const int cy = static_cast<int>(LM::galleryCellY(0) * 0.5f);
    requireAt(gallery, cx + 8, cy + 4, 0x88, 0, 0, "locked cell box (#880000)", 1);
    requireAt(gallery, cx, cy + 20, 0, 0xAA, 0, "locked cell border (#00AA00)", 1);
    // Achievements: the second plate is unselected.
    RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_BACK);
    RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
    RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_DOWN);
    RowlEngine_PauseMenuCommand(rig.h, ROWL_PAUSE_MENU_CONFIRM);
    req(rig.menu()["extras"]["page"] == "achievements", "achievements page");
    const json items = rig.menu()["extras"]["items"];
    req(items.size() == 2, "two achievement plates");
    const std::string ach = rig.frame(4);
    const int plate1 = static_cast<int>((items[1]["y"].get<float>()) * 0.5f);
    const int plate0 = static_cast<int>((items[0]["y"].get<float>()) * 0.5f);
    requireAt(ach, 640, plate1 + 4, 0x77, 0, 0x77, "unselected achievement plate (#770077)", 1);
    requireAt(ach, 640, plate0 + 4, 0, 0x66, 0x66, "selected achievement plate (#006666)", 1);
    requireAt(ach, 291, plate0 + 4, 255, 255, 255, "selected plate bar (#FFFFFF)", 1);

    // Toast: unlock an achievement while the story runs.
    RowlEngine_SetPaused(rig.h, 0);
    req(RowlEngine_ProfileUnlock(rig.h, "achievement", "a0") == ROWL_RESULT_OK, "unlock");
    const std::string toast = rig.frame(2);
    req(rig.engine->unlockToastText().rfind("Ba", 0) == 0, "toast is up: " + rig.engine->unlockToastText());
    // A capsule row clear of glyphs: the fill is the theme's, the rule (rule_px 0) is gone.
    req(anyPixel(toast, 380, 17, 580, 19, [](const Rgb& p) { return same(p, 0x30, 0x30, 0x30, 1); }),
        "toast capsule fill (#303030)");
    const int ruleY = (28 + 56 - 3) / 2;  // where the 2 px accent rule sat
    req(!anyPixel(toast, 380, ruleY, 580, ruleY + 1, [](const Rgb& p) { return same(p, 56, 189, 248, 6); }),
        "rule_px 0: no accent rule");

    // Badges: AUTO capsule at the top right.
    RowlEngine_SetAutoMode(rig.h, 1);
    rig.run(3.5f);  // the toast is over
    const std::string badge = rig.frame(2);
    req(anyPixel(badge, 884, 22, 892, 28, [](const Rgb& p) { return same(p, 0x50, 0x50, 0x50, 1); }),
        "AUTO capsule fill (#505050)");
    TEST_PASS("themed Ekstralar (locked box + border, plates, bar), toast fill + rule_px, badge fill");
}

// ── 4. story defaults, precedence, reload ───────────────────────────────────
const char* kStoryTheme = R"({"format":1,"story":{
    "dialogue":{"box_color":"#102030","border_color":"#FF8800","border_thickness":2,"text_color":"#CCFFCC",
                "font_size":30,"speaker_font_size":22,"speaker_color":"#2244AA","text_alignment":"Center"},
    "choice":{"background_color":"#AA2222","hover_color":"#22AA22","border_color":"#FFFF00","text_color":"#EEEEFF",
              "font_size":26,"text_alignment":"Left","opacity":1},
    "speaker_tag":{"border_color":"#00FF00","border_width":2,"text_color":"#101010"},
    "choice_focus":{"bar_color":"#FF00FF","bar_width":14,"inner_color":"#00FFFF","inset":6}}})";

void storyDefaultsTests() {
    Options o;
    o.theme = kStoryTheme;
    Rig rig(o);
    // A node whose component writes nothing takes the theme.
    {
        const auto& d = rig.engine->getActiveDialogues();
        req(d.size() == 1 && d[0].boxColor == "#102030" && d[0].borderColor == "#FF8800" && d[0].textColor == "#CCFFCC" &&
                d[0].fontSize == 30.0f && d[0].speakerFontSize == 22.0f && d[0].speakerColor == "#2244AA" &&
                d[0].textAlignment == "Center" && d[0].authoredMask == 0,
            "dialogue takes the theme defaults");
        const auto& b = rig.engine->getActiveChoiceButtons();
        req(b[0].backgroundColor == "#AA2222" && b[0].hoverColor == "#22AA22" && b[0].borderColor == "#FFFF00" &&
                b[0].textColor == "#EEEEFF" && b[0].fontSize == 26.0f && b[0].textAlignment == "Left",
            "option A takes the theme defaults");
        req(b[1].backgroundColor == "#336699" && b[1].borderColor == "#FF00FF" && b[1].hoverColor == "#22AA22",
            "option B: written keys win, the rest is the theme");
    }
    const std::string frame = rig.frame(4);
    requireAt(frame, kDlgX + 840, kDlgY + 60, 0x10, 0x20, 0x30, "themed dialogue box fill");
    requireAt(frame, kDlgX + 440, kDlgY, 255, 0x88, 0, "themed dialogue border");
    requireAt(frame, kChoiceAX + 270, kChoiceAY + 14, 0xAA, 0x22, 0x22, "themed option A fill");
    requireAt(frame, kChoiceAX + 100, kChoiceAY, 255, 255, 0, "themed option border");
    requireAt(frame, kChoiceAX + 270, kChoiceBY + 14, 0x33, 0x66, 0x99, "written option B fill beats the theme");
    // Speaker tag: fill = themed speaker colour, border = the theme's, 2 px.
    requireAt(frame, kDlgX + 12, kDlgY - 7, 0x22, 0x44, 0xAA, "themed speaker tag fill");
    // Keyboard focus: hover fill, themed focus marks (bar colour / width).
    rig.key(InType::MenuDown);
    const std::string focused = rig.frame(4);
    requireAt(focused, kChoiceAX + 3, kChoiceAY + 14, 255, 0, 255, "focus bar uses the theme (#FF00FF)");
    requireAt(focused, kChoiceAX + 9, kChoiceAY + 14, 0x22, 0xAA, 0x22, "...and is 14 virtual (7 physical) px wide: hover fill after it");
    requireAt(focused, kChoiceAX + 100, kChoiceAY + 3, 0, 255, 255, "inner focus frame uses inner_color, inset 6 virtual");
    requireAt(focused, kChoiceAX + 270, kChoiceAY + 14, 0x22, 0xAA, 0x22, "focused option fills with hover_color");
    // box_opacity: the renderer takes the box alpha from the colour's own alpha channel only
    // ("#RRGGBBAA"); the separate box_opacity number never reached the pixels (existing behaviour,
    // documented in docs/PLAYER_UI_THEME.md). A translucent theme colour does blend.
    {
        Options a;
        a.theme = R"({"format":1,"story":{"dialogue":{"box_color":"#FF000080","box_opacity":0.1}}})";
        Rig translucent(a);
        const Rgb px = at(translucent.frame(4), kDlgX + 840, kDlgY + 60);
        req(px.r > 110 && px.r < 150 && px.g < 20 && px.b < 30, "translucent box colour blends over the scene: " + rgbText(px));
    }
    TEST_PASS("story defaults: dialogue / choice / speaker tag / focus marks follow the theme; written option keys win");
}

void precedenceTests() {
    Options o;
    o.story = Story::Written;
    o.theme = kStoryTheme;
    Rig rig(o);
    {
        const auto& d = rig.engine->getActiveDialogues()[0];
        // Written: box_color, border_color, speaker_color, font_size. Not written: text_color, border_thickness ...
        req(d.boxColor == "#223344" && d.borderColor == "#AABBCC" && d.speakerColor == "#CC3300" && d.fontSize == 31.0f,
            "written keys win over the theme");
        req(d.textColor == "#CCFFCC" && d.speakerFontSize == 22.0f && d.textAlignment == "Center",
            "unwritten keys take the theme");
        const auto& b = rig.engine->getActiveChoiceButtons()[0];
        req(b.textColor == "#00FF00" && b.fontSize == 25.0f && b.backgroundColor == "#AA2222" && b.textAlignment == "Left",
            "option: written text_color / font_size win, the rest is the theme");
    }
    const std::string before = rig.frame(4);
    requireAt(before, kDlgX + 840, kDlgY + 60, 0x22, 0x33, 0x44, "written box_color paints");
    // Reload with a different theme: unwritten fields follow, written fields stay.
    rig.writeTheme(R"({"format":1,"story":{"dialogue":{"box_color":"#FF0000","text_color":"#0000FF","font_size":40},
        "choice":{"background_color":"#00AAAA","text_color":"#0000FF"}}})");
    req(RowlEngine_ReloadUiTheme(rig.h) == ROWL_RESULT_OK, "reload");
    {
        const auto& d = rig.engine->getActiveDialogues()[0];
        req(d.boxColor == "#223344" && d.fontSize == 31.0f && d.speakerColor == "#CC3300" && d.borderColor == "#AABBCC",
            "reload: written fields stay");
        req(d.textColor == "#0000FF" && d.speakerFontSize == 20.0f && d.textAlignment == "Left",
            "reload: unwritten fields take the new theme (and the built-in where it is silent)");
        const auto& b = rig.engine->getActiveChoiceButtons()[0];
        req(b.textColor == "#00FF00" && b.fontSize == 25.0f && b.backgroundColor == "#00AAAA" && b.textAlignment == "Center",
            "reload: option fields follow the same rule");
        req(rig.engine->getActiveDialogueData().textColor == "#0000FF", "the legacy single-dialogue copy follows too");
    }
    // The resting story frame (not a menu, which repaints every frame) is in the new colour at once.
    {
        const std::string reloaded = rig.frame(1);
        requireAt(reloaded, kChoiceAX + 270, kChoiceAY + 14, 0x00, 0xAA, 0xAA, "reload: the very next story frame shows the new option fill", 1);
        rig.writeTheme(R"({"format":1,"story":{"choice":{"background_color":"#AA00AA"}}})");
        req(RowlEngine_ReloadUiTheme(rig.h) == ROWL_RESULT_OK, "reload again");
        requireAt(rig.frame(1), kChoiceAX + 270, kChoiceAY + 14, 0xAA, 0x00, 0xAA, "reload: again, no stale frame", 1);
    }
    // The component JSON itself is untouched by theming (it is the project's data).
    const std::string graph = readBytes(rig.game / "Assets" / "json" / "full_story_graph.json");
    req(graph == buildGraph(Story::Written), "the story graph file is not rewritten");
    TEST_PASS("precedence: written > theme > built-in; reload moves only the unwritten fields; story file untouched");
}

void sceneColourTests() {
    Options o;
    o.story = Story::Scene;
    o.theme = R"({"format":1,"scene":{"missing_background":"#336600","placeholder_fill":"#AA5500","placeholder_border":"#00FFAA"}})";
    Rig rig(o);
    const std::string frame = rig.frame(4);
    requireAt(frame, 100, 100, 0x33, 0x66, 0, "missing-background fill", 1);
    requireAt(frame, 740, 200, 0xAA, 0x55, 0, "missing character placeholder fill", 1);
    requireAt(frame, 720, 300, 0, 0xFF, 0xAA, "placeholder border", 1);
    // Colours that are not in the frame hash (only the theme knows them): a reload repaints at once.
    rig.writeTheme(R"({"format":1,"scene":{"missing_background":"#660033"}})");
    req(RowlEngine_ReloadUiTheme(rig.h) == ROWL_RESULT_OK, "reload");
    requireAt(rig.frame(1), 100, 100, 0x66, 0, 0x33, "reload: the resting story frame repaints in the new colour (no stale frame)", 1);
    Options none;
    none.story = Story::Scene;
    Rig plain(none);
    const std::string builtin = plain.frame(4);
    requireAt(builtin, 100, 100, 20, 24, 38, "built-in missing-background fill", 1);
    TEST_PASS("scene: missing background + character placeholder colours follow the theme");
}

void shapeFieldTests() {
    const std::string themeOff = R"({"format":1,"story":{"dialogue":{"corner_radius":40,"border_thickness":5,
        "border_color":"#FF0000"},"choice":{"corner_radius":40,"border_thickness":4,"border_color":"#FF0000"}}})";
    std::string themeOn = themeOff;
    themeOn.insert(themeOn.size() - 2, R"(,"honor_shape_fields":true)");
    // Off: the fields are carried but never drawn: square corner, 2 rings at most.
    {
        Options o;
        o.theme = themeOff;
        Rig rig(o);
        const std::string frame = rig.frame(4);
        requireAt(frame, kDlgX, kDlgY, 255, 0, 0, "flag off: square corner pixel is the border");
        requireAt(frame, kDlgX + 440, kDlgY + 1, 255, 0, 0, "flag off: legacy second ring");
        requireAt(frame, kDlgX + 440, kDlgY + 2, 15, 15, 26, "flag off: never more than two rings");
        requireAt(frame, kChoiceAX, kChoiceAY, 255, 0, 0, "flag off: option square corner");
        requireAt(frame, kChoiceAX + 100, kChoiceAY + 1, 30, 41, 59, "flag off: option border stays one pixel");
    }
    // On: rounded corners (the corner pixel shows the scene) and thick borders.
    {
        Options o;
        o.theme = themeOn;
        Rig rig(o);
        req(rig.engine->uiTheme().story.honorShapeFields, "flag parsed");
        const std::string frame = rig.frame(4);
        const Rgb corner = at(frame, kDlgX, kDlgY);
        req(!same(corner, 255, 0, 0, 10) && !same(corner, 15, 15, 26, 2), "flag on: the dialogue corner is rounded away: " + rgbText(corner));
        for (int ring = 0; ring < 5; ++ring) requireAt(frame, kDlgX + 440, kDlgY + ring, 255, 0, 0, "flag on: ring " + std::to_string(ring));
        requireAt(frame, kDlgX + 440, kDlgY + 5, 15, 15, 26, "flag on: five rings, then the fill");
        const Rgb optCorner = at(frame, kChoiceAX, kChoiceAY);
        req(!same(optCorner, 255, 0, 0, 10), "flag on: the option corner is rounded away: " + rgbText(optCorner));
        for (int ring = 0; ring < 4; ++ring) requireAt(frame, kChoiceAX + 100, kChoiceAY + ring, 255, 0, 0, "flag on: option ring " + std::to_string(ring));
        requireAt(frame, kChoiceAX + 100, kChoiceAY + 4, 30, 41, 59, "flag on: four option rings");
    }
    TEST_PASS("shape fields: corner_radius / border_thickness are drawn only when honor_shape_fields is on (default pixels unchanged)");
}

// ── 5. fonts ────────────────────────────────────────────────────────────────
void fontTests() {
    Options base;
    base.fonts = true;
    // Menu font: the title screen differs inside the panel only.
    {
        Rig plain(base);
        req(RowlEngine_OpenTitleScreen(plain.h) == ROWL_RESULT_OK, "title");
        const std::string a = plain.frame(4);
        Options o = base;
        o.theme = R"({"format":1,"fonts":{"menu":"fonts/bold.ttf"}})";
        Rig themed(o);
        req(themed.engine->getWindow()->hasThemeFont(0) && !themed.engine->getWindow()->hasThemeFont(1) &&
                !themed.engine->getWindow()->hasThemeFont(2),
            "only the menu font loaded");
        req(RowlEngine_OpenTitleScreen(themed.h) == ROWL_RESULT_OK, "title");
        const std::string b = themed.frame(4);
        const Diff d = diffIn(a, b, kPanelX, kPanelY, kPanelX + 440, kPanelY + 430);
        req(d.inside > 100 && d.outside == 0, "menu font changes the panel text only: " + std::to_string(d.inside) + " / " + std::to_string(d.outside));
        // Measurement and drawing use the same instance.
        req(themed.engine->getWindow()->getMenuFontRenderer() != themed.engine->getWindow()->getFontRenderer() &&
                plain.engine->getWindow()->getMenuFontRenderer() == plain.engine->getWindow()->getFontRenderer(),
            "menu font renderer is its own instance (and the project font without a theme font)");
    }
    // Dialogue / speaker fonts.
    {
        Rig plain(base);
        const std::string a = plain.frame(4);
        Options o = base;
        o.theme = R"({"format":1,"fonts":{"dialogue":"fonts/bold.ttf"}})";
        Rig themed(o);
        const std::string b = themed.frame(4);
        const Diff d = diffIn(a, b, kDlgX, kDlgY - 2, kDlgX + kDlgW, kDlgY + kDlgH);
        // Choices default to the dialogue font as well, so their area may change too.
        req(d.inside > 50, "dialogue font changes the dialogue text: " + std::to_string(d.inside));
        const Diff above = diffIn(a, b, 0, 0, kW, 250);
        req(above.inside == 0, "nothing above the choices changes");
        const Diff tag = diffIn(a, b, kDlgX, kDlgY - 20, kDlgX + 120, kDlgY);
        req(tag.inside == 0, "the speaker tag keeps the project font when only the dialogue font is themed");
    }
    {
        Rig plain(base);
        const std::string a = plain.frame(4);
        Options o = base;
        o.theme = R"({"format":1,"fonts":{"speaker":"fonts/bold.ttf"}})";
        Rig themed(o);
        const std::string b = themed.frame(4);
        const Diff tag = diffIn(a, b, kDlgX, kDlgY - 20, kDlgX + 160, kDlgY + 1);
        req(tag.inside > 10, "speaker font changes the tag text: " + std::to_string(tag.inside));
        req(diffIn(a, b, kDlgX - 2, kDlgY - 24, kDlgX + 240, kDlgY + 22).outside == 0,
            "...and nothing outside the tag changes");
    }
    // A missing / unreadable font falls back to the project font and says so.
    {
        Options o = base;
        o.theme = R"({"format":1,"fonts":{"menu":"fonts/nope.ttf","dialogue":"../escape.ttf"}})";
        Rig rig(o);
        req(!rig.engine->getWindow()->hasThemeFont(0) && !rig.engine->getWindow()->hasThemeFont(1), "no theme font loaded");
        const json t = rig.theme();
        req(!t["diagnostics"].empty(), "the escape is reported: " + t.dump());
        req(rig.engine->getWindow()->getMenuFontRenderer() == rig.engine->getWindow()->getFontRenderer(),
            "missing font -> project font");
    }
    // Accessibility state reaches the theme fonts.
    {
        Options o = base;
        o.theme = R"({"format":1,"fonts":{"menu":"fonts/bold.ttf"}})";
        Rig rig(o);
        RowlEngine_SetTextScale(rig.h, 1.5f);
        RowlEngine_SetHighContrast(rig.h, 1);
        auto* menuFont = rig.engine->getWindow()->getMenuFontRenderer();
        req(menuFont->textScale() == 1.5f && menuFont->highContrast(), "text scale + high contrast reach the menu font");
    }
    TEST_PASS("fonts: menu / dialogue / speaker load separately (diffs only in their regions); missing path -> project font + diagnostic");
}

// ── 7. reload + C API ───────────────────────────────────────────────────────
void reloadAndCApiTests() {
    // Before Init / bad handles.
    {
        uint32_t size = 0;
        req(RowlEngine_GetUiThemeJson(nullptr, nullptr, 0, &size) == ROWL_RESULT_INVALID_HANDLE &&
                RowlEngine_ReloadUiTheme(nullptr) == ROWL_RESULT_INVALID_HANDLE &&
                RowlEngine_SetUiThemeJson(nullptr, "{}") == ROWL_RESULT_INVALID_HANDLE,
            "null handle");
        RowlEngineHandle early = RowlEngine_Create();
        req(RowlEngine_GetUiThemeJson(early, nullptr, 0, &size) == ROWL_RESULT_STATE_ERROR &&
                RowlEngine_ReloadUiTheme(early) == ROWL_RESULT_STATE_ERROR &&
                RowlEngine_SetUiThemeJson(early, "{}") == ROWL_RESULT_STATE_ERROR,
            "before Init: STATE_ERROR");
        RowlEngine_Destroy(early);
    }
    uint64_t caps = 0;
    req(RowlEngine_GetCapabilities(&caps) == ROWL_RESULT_OK && (caps & ROWL_ENGINE_CAPABILITY_UI_THEME) != 0 &&
            ROWL_ENGINE_CAPABILITY_UI_THEME == UINT64_C(536870912),
        "capability bit 536870912");

    Options o;
    o.theme = R"({"format":1,"menu":{"panel_fill":"#445566"}})";
    Rig rig(o);
    uint32_t need = 0;
    req(RowlEngine_GetUiThemeJson(rig.h, nullptr, 0, &need) == ROWL_RESULT_OK && need > 500, "size query");
    char tiny[8];
    req(RowlEngine_GetUiThemeJson(rig.h, tiny, sizeof(tiny), &need) == ROWL_RESULT_BUFFER_TOO_SMALL && tiny[0] == '\0',
        "tiny buffer");
    req(RowlEngine_GetUiThemeJson(rig.h, nullptr, 0, nullptr) == ROWL_RESULT_INVALID_ARGUMENT, "null size pointer");
    req(RowlEngine_SetUiThemeJson(rig.h, nullptr) == ROWL_RESULT_INVALID_ARGUMENT, "null document");

    // Reload: the next frame is in the new colour (no stale cached frame).
    RowlEngine_OpenTitleScreen(rig.h);
    const std::string one = rig.frame(4);
    requireAt(one, kPanelSampleX + 20, kPanelSampleY, 0x44, 0x55, 0x66, "first theme", 1);
    rig.writeTheme(R"({"format":1,"menu":{"panel_fill":"#665544"}})");
    req(RowlEngine_ReloadUiTheme(rig.h) == ROWL_RESULT_OK, "reload");
    const std::string two = rig.frame(1);
    requireAt(two, kPanelSampleX + 20, kPanelSampleY, 0x66, 0x55, 0x44, "the very next frame is in the new colour", 1);
    // Reload with the file gone restores the built-in look.
    fs::remove(rig.game / "Assets" / "json" / "ui_theme.json");
    req(RowlEngine_ReloadUiTheme(rig.h) == ROWL_RESULT_OK, "reload without file");
    req(rig.theme()["loaded"] == false && rig.engine->uiTheme() == builtinUiTheme(), "built-in again");
    // File-less preview.
    req(RowlEngine_SetUiThemeJson(rig.h, R"({"format":1,"menu":{"panel_fill":"#112211"},"toast":{"fill":"#bad"}})") == ROWL_RESULT_OK,
        "inline theme");
    {
        const json t = rig.theme();
        req(t["source"] == "(inline)" && t["loaded"] == true && t["theme"]["menu"]["panel_fill"] == "#112211" &&
                t["diagnostics"].size() == 1,
            "inline: " + t.dump());
        requireAt(rig.frame(1), kPanelSampleX + 20, kPanelSampleY, 0x11, 0x22, 0x11, "inline theme paints at once", 1);
    }
    // An unusable document is refused and the theme in force stays.
    req(RowlEngine_SetUiThemeJson(rig.h, "{ not json") == ROWL_RESULT_VALIDATION_ERROR &&
            RowlEngine_SetUiThemeJson(rig.h, R"({"format":9})") == ROWL_RESULT_VALIDATION_ERROR,
        "unusable documents refused");
    req(rig.theme()["theme"]["menu"]["panel_fill"] == "#112211", "...and the old theme stays");
    TEST_PASS("reload: next frame in the new colour, missing file restores built-in, SetUiThemeJson preview, bad document refused; C API contract");
}

// ── 8. high contrast ────────────────────────────────────────────────────────
void highContrastTests() {
    Options o;
    o.fonts = true;
    o.theme = R"({"format":1,"menu":{"panel_fill":"#445566"},"high_contrast":{"menu":{"panel_fill":"#000000"}}})";
    Rig rig(o);
    RowlEngine_OpenTitleScreen(rig.h);
    requireAt(rig.frame(4), kPanelSampleX + 20, kPanelSampleY, 0x44, 0x55, 0x66, "normal palette", 1);
    RowlEngine_SetHighContrast(rig.h, 1);
    requireAt(rig.frame(4), kPanelSampleX + 20, kPanelSampleY, 0, 0, 0, "high-contrast palette", 1);
    RowlEngine_SetHighContrast(rig.h, 0);
    requireAt(rig.frame(4), kPanelSampleX + 20, kPanelSampleY, 0x44, 0x55, 0x66, "back to the normal palette", 1);
    req(!rig.theme()["high_contrast"].is_null(), "the high-contrast palette is reported");
    // Without a palette high contrast changes colours not at all (only the glyph halo).
    Options plain;
    plain.fonts = true;
    plain.theme = R"({"format":1,"menu":{"panel_fill":"#445566"}})";
    Rig noPalette(plain);
    RowlEngine_OpenTitleScreen(noPalette.h);
    RowlEngine_SetHighContrast(noPalette.h, 1);
    requireAt(noPalette.frame(4), kPanelSampleX + 20, kPanelSampleY, 0x44, 0x55, 0x66, "no palette: colours unchanged", 1);
    TEST_PASS("high_contrast palette applies only while the player's high contrast is on");
}

}  // namespace

void test_ui_theme() {
    TEST_SECTION("UI theme: ui_theme.json parser, defaults == former constants, themed pixels, precedence, fonts, reload (T1)");

    g_root = fs::temp_directory_path() / "rowl_ui_theme";
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

    parserDefaultsTests();
    parserBadInputTests();
    parserInheritanceTests();
    noFileTests();
    themedMenuTests();
    themedExtrasToastBadgeTests();
    storyDefaultsTests();
    precedenceTests();
    sceneColourTests();
    shapeFieldTests();
    fontTests();
    reloadAndCApiTests();
    highContrastTests();

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(g_root, ec);
}
