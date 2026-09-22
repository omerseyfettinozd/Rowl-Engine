/**
 * test_text_fallback_red_probe.cpp — D10 font-fallback mutual-exclusion kilidi.
 *
 * Bağımsız ikili: rowl_text_fallback_red_probe (ctest -R text_fallback_red_probe).
 * D01/D02 konvansiyonu: rowl_tests gövdesine gömülmez; kırmızı-yeşil döngüsü
 * tüm süiti koşmadan saniyeler içinde kanıtlanır.
 *
 * Bacak 1 — yapısal RED (taşıma-öncesi gözlem):
 *   Prob, henüz var olmayan yeni TU karşılığını doğrular ve KIRMIZI düşer.
 *   Somut senaryo: window.cpp:1415-1480 font-fallback/TrueType karşılıklı-dışlama
 *   çekirdeği (`useFontFallbackPath`) + MSDF hepsi-ya-da-hiçi kapsama kararı
 *   (`msdfTextFullyCovered`) yeni `window_text_fallback` TU'sundan gelmelidir.
 *   Taşıma-öncesi bu dosyanın ilk include'u derlenemez → KIRMIZI.
 *   (Derleme-RED'i: `rowl/render/window_text_fallback.hpp` include'u taşıma
 *   öncesi çözülemez: "fatal error: rowl/render/window_text_fallback.hpp:
 *   No such file or directory", exit 1.)
 *   Taşıma-sonrası her assertion, taşınan çekirdeğin birebirliğini kilitler:
 *   (a) truth-table: (fontLoaded, surface) 4 kombinasyonunda fallback XOR
 *       truetype — çift-rasterizasyon YOK, çift-skip YOK;
 *   (b) kapsama: atlas-dışı glyph ("AB"deki B, çok-baytlı "✓" U+2713, geçersiz
 *       bayt → U+FFFD) → false (kısmi-çizim + true YOK); tam-kapsama ("A",
 *       boş metin) → true;
 *   (c) draw-guard: null renderState/atlas/texture → false, çökme YOK, SDL
 *       çağrısı YOK (SDL init'siz güvenli).
 *
 * Bacak 2 — parite (taşıma-öncesi YEŞİL, sonrası da YEŞİL kalmalı):
 *   Kanonik frame golden-hash 0xabd1694c10a96ef (test_frame_hash_lock ile aynı
 *   çerçeve) taşıma-sonrası birebir aynı olmalıdır; fark = davranış sızıntısı.
 *
 * GREEN (taşıma-sonrası): Bacak 1 yapısal assertion geçer (yeni TU derlenir/
 * bağlanır, çekirdek aynı kararları üretir) + Bacak 2 golden sabit → exit 0.
 *
 * KIRMIZI-GREEN SÖZLEŞMESİ: taşıma sırasında Bacak 2 goldeni değişirse fix TURU
 * sayılır (max 3); kırmızıda commit YOK.
 */
// D10-RED: bu include taşıma-öncesi çözülemez (bilerek ilk satırdadır ki RED
// gözlemi başka bir header-hatasıyla kirlenmesin).
#include "rowl/render/window_text_fallback.hpp"

#include "rowl_test_harness.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "rowl/render/window.hpp"

namespace {

using Rowl::Render::CharacterRenderData;
using Rowl::Render::ChoiceButtonRenderData;
using Rowl::Render::DialogueRenderData;
using Rowl::Render::MsdfRenderer;
using Rowl::Render::Window;

[[noreturn]] void fallbackProbeFail(const std::string& message) {
    rowlLockFail("d10-text-fallback-probe", message);
}

constexpr const char* kProbeAtlasJson =
    R"({"pixel_range":4,"atlas_width":2,"atlas_height":2,)"
    R"("glyphs":[{"unicode":65,"advance":0.6}]})";

void expectCovered(const MsdfRenderer& atlas, const std::string& text, bool want,
                   const char* label) {
    const bool got = Rowl::Render::msdfTextFullyCovered(atlas, text);
    if (got != want) {
        fallbackProbeFail(std::string("coverage '") + label + "' got " +
                          (got ? "true" : "false") + ", want " +
                          (want ? "true" : "false"));
    }
}

}  // namespace

int main() {
    TEST_SECTION("Text fallback probe (D10: mutual-exclusion + all-or-nothing MSDF)");

    // ── Bacak 1a: truth-table — fallback XOR truetype (4/4 kombinasyon).
    for (const bool fontLoaded : {false, true}) {
        for (const bool hasSurface : {false, true}) {
            const bool fallback =
                Rowl::Render::useFontFallbackPath(fontLoaded, hasSurface);
            const bool truetype = !fallback;
            // window.cpp:1415/1430/1455 fallback guard'ı:
            //   !font || !loaded || !surface  == useFontFallbackPath
            // window.cpp:1477 TrueType guard'ı:
            //   font && loaded && surface     == !useFontFallbackPath
            const bool legacyFallback = !fontLoaded || !hasSurface;
            const bool legacyTruetype = fontLoaded && hasSurface;
            if (fallback != legacyFallback || truetype != legacyTruetype) {
                fallbackProbeFail("kernel diverges from legacy guards");
            }
            if (fallback == truetype) {
                fallbackProbeFail("fallback and truetype are not exclusive");
            }
            if (!fallback && !truetype) {
                fallbackProbeFail("fallback and truetype both skip");
            }
        }
    }
    TEST_PASS("Fallback/truetype mutual exclusion holds in 4/4 states");

    // ── Bacak 1b: kapsama — eksik glyph → false (kısmi-çizim + true YOK).
    MsdfRenderer atlas;
    if (!atlas.loadAtlasMetadata(kProbeAtlasJson)) {
        fallbackProbeFail("probe atlas metadata did not load");
    }
    expectCovered(atlas, "A", true, "full-coverage ASCII");
    expectCovered(atlas, "", true, "empty text");
    expectCovered(atlas, "AB", false, "missing trailing glyph");
    expectCovered(atlas, "B", false, "missing single glyph");
    expectCovered(atlas, "A\xE2\x9C\x93", false, "missing multi-byte glyph U+2713");
    expectCovered(atlas, std::string("A\xFF", 2), false, "invalid byte U+FFFD");
    TEST_PASS("MSDF all-or-nothing coverage pins 6/6 cases");

    // ── Bacak 1c: draw-guard — null durum SDL'siz false döner.
    if (Rowl::Render::drawMsdfTextAllOrNothing(nullptr, nullptr, &atlas, nullptr,
                                               "A", 0.0f, 0.0f, 16.0f,
                                               {255, 255, 255, 255})) {
        fallbackProbeFail("null render-state draw reported success");
    }
    if (Rowl::Render::drawMsdfTextAllOrNothing(nullptr, nullptr, nullptr, nullptr,
                                               "A", 0.0f, 0.0f, 16.0f,
                                               {255, 255, 255, 255})) {
        fallbackProbeFail("null atlas draw reported success");
    }
    TEST_PASS("MSDF draw guard fails closed on null state");

    // ── Bacak 2: parite — kanonik frame golden-hash sabit.
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
        fallbackProbeFail(oss.str());
    }
    TEST_PASS("Canonical frame golden hash pinned");

    std::cout << "D10 text-fallback probe: GREEN" << std::endl;
    return 0;
}
