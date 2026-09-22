#include "rowl/render/window_msdf_shaped.hpp"
#include "rowl/render/window_text_fallback.hpp"  // D10 core (fast-path kapsaması delegasyon karşılaştırması)

#include "rowl_test_harness.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "rowl/render/window.hpp"
#include "rowl/text/text_shaper.hpp"
#include "rowl/text/utf8.hpp"

namespace {

using Rowl::Render::ChoiceButtonRenderData;
using Rowl::Render::CharacterRenderData;
using Rowl::Render::DialogueRenderData;
using Rowl::Render::MsdfRenderer;
using Rowl::Render::Window;

[[noreturn]] void shapedProbeFail(const std::string& message) {
    rowlLockFail("d11-msdf-shaped-probe", message);
}

// ASCII 32..126 kapsayan prob atlası (U+2713 bilinçli DIŞARIDA — P4
// hepsi-ya-da-hiçi ihlali için). '<', '>', '/' de kapsanır ki P3
// markup-dışlamasının atlas-eksiğinden değil tasarımdan geldiği kanıtlansın.
std::string probeAtlasJson() {
    std::string json =
        R"({"pixel_range":4,"atlas_width":2,"atlas_height":2,"glyphs":[)";
    bool first = true;
    for (uint32_t cp = 32; cp <= 126; ++cp) {
        if (!first) json += ",";
        first = false;
        json += "{\"unicode\":" + std::to_string(cp) + ",\"advance\":0.6}";
    }
    json += "]}";
    return json;
}

std::vector<uint8_t> loadTestFont() {
    std::ifstream stream("Assets/fonts/default.ttf", std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

Rowl::Text::ShapeOptions shapeOptions(float fontSize, float maxWidth = 0.0f) {
    Rowl::Text::ShapeOptions options;
    options.fontSize = fontSize;
    options.maxWidth = maxWidth;
    return options;
}

// ShapedText::plainText skaler dizisi (helper ile AYNI decoder —
// rowl/text/utf8.hpp paylaşımlı strict çözücü; U+FFFD sözleşmesi ortak).
std::vector<uint32_t> decodeScalars(const std::string& text) {
    std::vector<uint32_t> out;
    for (std::size_t i = 0; i < text.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        if (decoded.length == 0) break;
        i += decoded.length;
        out.push_back(decoded.codepoint);
    }
    return out;
}

}  // namespace

int main() {
    TEST_SECTION("MSDF shaped-path parity probe (D11)");

    const auto font = loadTestFont();
    if (font.empty()) shapedProbeFail("test font is missing");
    Rowl::Text::TextShaper shaper;
    if (!Rowl::Text::TextShaper::isAdvancedBackendCompiled() ||
        !shaper.loadFontFromMemory(font.data(), font.size()) ||
        !shaper.isAdvancedBackendActive()) {
        shapedProbeFail("advanced shaping backend is unavailable");
    }

    MsdfRenderer atlas;
    if (!atlas.loadAtlasMetadata(probeAtlasJson())) {
        shapedProbeFail("probe atlas metadata did not load");
    }

    constexpr float kPx = 24.0f;

    // ── P1: wrap — helper satır-planı shaped.lines'a birebir uyar (kendi
    // wrap'i YOK); mutlak pin: dar genişlikte 2+ satır, genişlik sınırlı.
    {
        const auto unwrapped =
            shaper.shapeMarkup("one two three four", shapeOptions(kPx));
        const auto wrapped = shaper.shapeMarkup(
            "one two three four", shapeOptions(kPx, unwrapped.width * 0.45f));
        if (wrapped.lines.size() < 2) {
            shapedProbeFail("narrow wrap produced a single line");
        }
        if (wrapped.width > unwrapped.width) {
            shapedProbeFail("wrapped width exceeds unwrapped width");
        }
        const Rowl::Render::MsdfShapedDrawPlan plan =
            Rowl::Render::planMsdfShapedDraw(
                atlas, wrapped, "one two three four",
                wrapped.revealUnits.size(), kPx);
        if (!plan.ok) shapedProbeFail("full-coverage wrap plan reported failure");
        if (plan.fastPath) {
            shapedProbeFail("multi-line wrap plan claims fast-path");
        }
        if (plan.lineCount != wrapped.lines.size()) {
            shapedProbeFail("plan line count diverges from shaped.lines");
        }
        // Görünür glif kümesi tam-reveal'de shaped glyph dizisiyle birebir
        // (sıra + codepoint + konum): firstScalar/scalarCount otoritesi
        // shaped.lines'tadır, helper yeniden-sarmaz.
        if (plan.glyphs.size() != wrapped.glyphs.size()) {
            shapedProbeFail("plan glyph count diverges from shaped glyphs");
        }
        for (std::size_t i = 0; i < plan.glyphs.size(); ++i) {
            const auto& shapedGlyph = wrapped.glyphs[i];
            const std::vector<uint32_t> scalars = decodeScalars(wrapped.plainText);
            if (shapedGlyph.logicalScalar >= scalars.size()) {
                shapedProbeFail("shaped glyph escapes plain-text scalars");
            }
            if (plan.glyphs[i].codepoint != scalars[shapedGlyph.logicalScalar] ||
                plan.glyphs[i].x != shapedGlyph.x ||
                plan.glyphs[i].lineBaseline != shapedGlyph.y) {
                shapedProbeFail("plan glyph diverges from shaped layout");
            }
        }
        // Satır-grup büyüklükleri line.glyphCount ile birebir.
        for (const auto& line : wrapped.lines) {
            std::size_t inLine = 0;
            for (const auto& glyph : plan.glyphs) {
                if (glyph.lineBaseline == line.baseline) ++inLine;
            }
            if (inLine != line.glyphCount) {
                shapedProbeFail("plan line group diverges from shaped line");
            }
        }
        if (plan.visibleUnits != wrapped.revealUnits.size()) {
            shapedProbeFail("plan visibleUnits diverges from reveal total");
        }
    }
    TEST_PASS("Shaped wrap parity: line-plan follows shaped.lines");

    // ── P2: reveal — typewriter görünürlük kümesi TrueType otoritesiyle
    // aynı (font_renderer.cpp:390 predikatı); strict-altküme + boş-değil.
    {
        const auto shaped = shaper.shapeMarkup(
            "A<pause=0.5><speed=2>B</speed>C<pause=0.25>", shapeOptions(kPx));
        if (shaped.revealUnits.size() != 3 ||
            std::fabs(shaped.revealUnits[1].pauseBefore - 0.5f) > 1e-5f ||
            std::fabs(shaped.revealUnits[1].speed - 2.0f) > 1e-5f) {
            shapedProbeFail("markup timing did not attach to reveal units");
        }
        const auto early = Rowl::Text::evaluateReveal(shaped, 0.11, 100.0);
        const auto duringPause = Rowl::Text::evaluateReveal(shaped, 0.55, 100.0);
        const auto complete = Rowl::Text::evaluateReveal(shaped, 1.0, 100.0);
        if (early.visibleUnits != 1 || duringPause.visibleUnits != 1 ||
            !complete.complete || complete.visibleUnits != 3) {
            shapedProbeFail("pause/speed/trailing-pause timeline is inconsistent");
        }
        const Rowl::Render::MsdfShapedDrawPlan partial =
            Rowl::Render::planMsdfShapedDraw(
                atlas, shaped, "A<pause=0.5><speed=2>B</speed>C<pause=0.25>",
                early.visibleUnits, kPx);
        if (!partial.ok) {
            shapedProbeFail("partial-reveal plan reported failure");
        }
        if (partial.glyphs.size() != 1) {
            std::ostringstream oss;
            oss << "reveal t=0.11 visible " << partial.glyphs.size() << ", want 1";
            shapedProbeFail(oss.str());
        }
        if (decodeScalars(shaped.plainText).size() < 3 ||
            partial.glyphs.front().codepoint !=
                decodeScalars(shaped.plainText).front()) {
            shapedProbeFail("partial-reveal visible set is not the first unit");
        }
        const Rowl::Render::MsdfShapedDrawPlan full =
            Rowl::Render::planMsdfShapedDraw(
                atlas, shaped, "A<pause=0.5><speed=2>B</speed>C<pause=0.25>",
                complete.visibleUnits, kPx);
        if (full.glyphs.size() <= partial.glyphs.size() || full.glyphs.empty()) {
            shapedProbeFail("partial-reveal set is not a strict subset");
        }
        if (partial.visibleUnits != 1 || full.visibleUnits != 3) {
            shapedProbeFail("plan visibleUnits diverges from reveal timeline");
        }
    }
    TEST_PASS("Shaped reveal parity: typewriter subset matches TrueType");

    // ── P3: markup-strip — çizim-kümesi decode(plainText) dışına taşmaz,
    // etiket baytı ('<', '>', '/') YOK. Ligatür-toleranslı: "fi" tek glifte
    // birleşirse plan {A,f} olur — küme plainText'in ALTkümesidir, tag
    // baytı içermez, boş değildir (sıra + konum P1'deki gibi birebir).
    {
        const auto shaped =
            shaper.shapeMarkup("A<b>fi</b>", shapeOptions(kPx, 200.0f));
        if (shaped.plainText != "Afi") {
            shapedProbeFail("markup strip did not yield plainText 'Afi'");
        }
        const Rowl::Render::MsdfShapedDrawPlan plan =
            Rowl::Render::planMsdfShapedDraw(
                atlas, shaped, "A<b>fi</b>", shaped.revealUnits.size(), kPx);
        if (!plan.ok) shapedProbeFail("markup plan reported failure");
        if (plan.glyphs.empty()) shapedProbeFail("markup plan drew nothing");
        const std::vector<uint32_t> plain = decodeScalars("Afi");
        for (const auto& glyph : plan.glyphs) {
            if (glyph.codepoint == '<' || glyph.codepoint == '>' ||
                glyph.codepoint == '/') {
                shapedProbeFail("markup tag byte leaked into draw set");
            }
            bool inPlain = false;
            for (const uint32_t cp : plain) {
                if (cp == glyph.codepoint) {
                    inPlain = true;
                    break;
                }
            }
            if (!inPlain) {
                shapedProbeFail("draw set escapes decoded plainText");
            }
        }
    }
    TEST_PASS("Shaped markup strip: draw set carries no tag bytes");

    // ── P4: hepsi-ya-da-hiçi — atlas-dışı U+2713'lü shaped ok=false;
    // null renderer/state SDL'siz false (SDL dokunuşu YOK, init'siz güvenli).
    {
        const auto shaped =
            shaper.shapeMarkup("A\xE2\x9C\x93", shapeOptions(kPx));
        const Rowl::Render::MsdfShapedDrawPlan plan =
            Rowl::Render::planMsdfShapedDraw(atlas, shaped, "A\xE2\x9C\x93",
                                             shaped.revealUnits.size(), kPx);
        if (plan.ok) {
            shapedProbeFail("missing-glyph shaped plan reported success");
        }
        if (Rowl::Render::drawMsdfShapedAllOrNothing(
                nullptr, nullptr, &atlas, nullptr, shaped, "A\xE2\x9C\x93",
                0.0f, 0.0f, kPx, {255, 255, 255, 255},
                shaped.revealUnits.size(), 0.0f, "Left")) {
            shapedProbeFail("null render-state draw reported success");
        }
        if (Rowl::Render::drawMsdfShapedAllOrNothing(
                nullptr, nullptr, nullptr, nullptr, shaped, "A\xE2\x9C\x93",
                0.0f, 0.0f, kPx, {255, 255, 255, 255},
                shaped.revealUnits.size(), 0.0f, "Left")) {
            shapedProbeFail("null atlas draw reported success");
        }
    }
    TEST_PASS("Shaped all-or-nothing fails closed on missing glyph/null state");

    // ── P5: fast-path — düz tek-satır tam-reveal D10'a delege kararı.
    {
        const auto shaped = shaper.shapeMarkup("A", shapeOptions(kPx));
        const Rowl::Render::MsdfShapedDrawPlan plan =
            Rowl::Render::planMsdfShapedDraw(
                atlas, shaped, "A", shaped.revealUnits.size(), kPx);
        if (!plan.fastPath) shapedProbeFail("'A' plan missed fast-path");
        if (!plan.ok) shapedProbeFail("'A' plan reported failure");
        if (!Rowl::Render::msdfTextFullyCovered(atlas, "A")) {
            shapedProbeFail("D10 coverage diverges on fast-path input");
        }
    }
    TEST_PASS("Shaped fast-path delegates plain single-line full-reveal");

    // ── P6: golden — kanonik frame hash sabit (D10 ile aynı çerçeve).
    {
        CharacterRenderData ch;
        ch.sprite = "hero_neutral.png";
        ch.x = 1440.0f;
        ch.y = 340.0f;
        DialogueRenderData dlg;
        dlg.speaker = "Işıklı";
        dlg.dialogue = "Merhaba dünya — ğşı test ✓";
        dlg.typewriterEnabled = true;
        dlg.elapsedTypewriterTime = 0.5f;
        ChoiceButtonRenderData choice;
        choice.optionId = "opt_1";
        choice.text = "Devam et";
        constexpr uint64_t kCanonicalGolden = 0xabd1694c10a96efULL;
        const uint64_t got = Window::hashPackedFrameContent(
            true, "bg_beach_sunset.png", 0.0f, 0.0f, 1920.0f, 1080.0f,
            std::vector<CharacterRenderData>{ch},
            std::vector<DialogueRenderData>{dlg},
            std::vector<ChoiceButtonRenderData>{choice}, 0.0f, 1.0f, 1.0f, 1.0f);
        if (got != kCanonicalGolden) {
            std::ostringstream oss;
            oss << "canonical golden mismatch (got 0x" << std::hex << got
                << ", want 0x" << kCanonicalGolden << std::dec << ")";
            shapedProbeFail(oss.str());
        }
    }
    TEST_PASS("Canonical frame golden hash pinned");

    std::cout << "D11 msdf-shaped parity probe: GREEN" << std::endl;
    return 0;
}
