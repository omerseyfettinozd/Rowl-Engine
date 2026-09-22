// window_msdf_shaped.cpp — D11 şekilli-MSDF plan/çizim gövdesi.
//
// window.cpp'deki 3 MSDF çağrı noktası (speaker/dialogue/choice) buraya
// delege eder (window.cpp şişmez). D10 msdfTextFullyCovered KOPYALANMAZ;
// ham-metin fast-path kapsaması D10 drawMsdfTextAllOrNothing içine delege
// edilir. Konum otoritesi shaped-layout'tur (kendi wrap'i YOK).
#include "rowl/render/window_msdf_shaped.hpp"

#include "rowl/render/window_text_fallback.hpp"
#include "rowl/text/utf8.hpp"

namespace Rowl::Render {
namespace {

// ShapedText::plainText skaler dizisi: ShapedGlyph::logicalScalar bu dizini
// indeksler (text_shaper.cpp makeScalars(document.chars) sırası). Paylaşımlı
// strict çözücü (rowl/text/utf8.hpp); geçersiz dizi U+FFFD olur.
std::vector<uint32_t> decodePlainScalars(const std::string& plainText) {
    std::vector<uint32_t> out;
    out.reserve(plainText.size());
    for (std::size_t i = 0; i < plainText.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            plainText.data() + i, plainText.data() + plainText.size());
        if (decoded.length == 0) break;  // Savunma: boş-kalan=''de ilerleme YOK.
        i += decoded.length;
        out.push_back(decoded.codepoint);
    }
    return out;
}

// font_renderer.cpp:383-386 ile aynı hizalama matematiği (TrueType kolu).
float lineAlignmentOffset(float lineWidth, float maxWidth,
                          const std::string& alignment) noexcept {
    if (maxWidth <= 0.0f) return 0.0f;
    if (alignment == "Center") return (maxWidth - lineWidth) * 0.5f;
    if (alignment == "Right") return maxWidth - lineWidth;
    return 0.0f;
}

}  // namespace

MsdfShapedDrawPlan planMsdfShapedDraw(const MsdfRenderer& atlas,
                                      const Rowl::Text::ShapedText& shaped,
                                      const std::string& rawMarkup,
                                      std::size_t maxVisible, float px) {
    (void)px;  // Konumlar layout-birimindedir (shape fontSize == px çağrı
               // sözleşmesi; TrueType koluyla aynı ifade). İmza draw ile
               // simetriktir; px draw-fazında ölçekler.
    MsdfShapedDrawPlan plan;
    plan.lineCount = shaped.lines.size();
    plan.visibleUnits = std::min(maxVisible, shaped.revealUnits.size());
    // Fast-path: düz tek-satır tam-reveal ham-metin — draw D10'a delege eder
    // (birebir legacy piksel). Markup'lı girdilerde rawMarkup != plainText
    // olduğundan plan-yolu seçilir (legacy bu girdilerde zaten debug'a
    // düşerdi: etiket baytları atlas-kapsama-dışıdır).
    plan.fastPath = shaped.lines.size() == 1 &&
                    maxVisible >= shaped.revealUnits.size() &&
                    rawMarkup == shaped.plainText;
    const std::vector<uint32_t> scalars = decodePlainScalars(shaped.plainText);
    plan.ok = true;
    for (const auto& glyph : shaped.glyphs) {
        if (!msdfShapedGlyphVisible(glyph.revealIndex, maxVisible)) continue;
        const uint32_t codepoint =
            glyph.logicalScalar < scalars.size()
                ? scalars[glyph.logicalScalar]
                : Rowl::Text::kUtf8ReplacementCodepoint;
        // Hepsi-ya-da-hiçi: tek eksik-glyph planı düşürür (draw çizmez).
        if (atlas.findGlyph(codepoint) == nullptr) {
            plan.ok = false;
            continue;
        }
        MsdfShapedDrawGlyph out;
        out.codepoint = codepoint;
        out.x = glyph.x;
        out.lineBaseline = glyph.y;
        plan.glyphs.push_back(out);
    }
    return plan;
}

bool drawMsdfShapedAllOrNothing(SDL_Renderer* renderer,
                                SDL_GPURenderState* renderState,
                                MsdfRenderer* atlas, SDL_Texture* atlasTexture,
                                const Rowl::Text::ShapedText& shaped,
                                const std::string& rawMarkup, float x,
                                float topY, float px, SDL_Color color,
                                std::size_t maxVisible, float maxWidth,
                                const std::string& alignment) {
    // D10 ile aynı fail-closed sırası: önce null-guard (SDL dokunuşu YOK).
    if (renderState == nullptr || atlas == nullptr || atlasTexture == nullptr)
        return false;
    if (renderer == nullptr) return false;
    const MsdfShapedDrawPlan plan =
        planMsdfShapedDraw(*atlas, shaped, rawMarkup, maxVisible, px);
    if (plan.fastPath) {
        // D10'a DELEGE: kapsama + pen-matematiği + GPU-oturumu legacy
        // gövdede (kopya YOK). Taban-çizgisi layout'un ilk satırından.
        const float baseline =
            shaped.lines.empty() ? topY : topY + shaped.lines.front().baseline;
        return drawMsdfTextAllOrNothing(renderer, renderState, atlas,
                                        atlasTexture, shaped.plainText, x,
                                        baseline, px, color);
    }
    if (!plan.ok) return false;
    if (!SDL_SetGPURenderState(renderer, renderState)) return false;
    for (const auto& glyph : plan.glyphs) {
        const auto* metrics = atlas->findGlyph(glyph.codepoint);
        if (metrics == nullptr) continue;  // Savunma-artığı (plan.ok garantisi).
        float lineOffset = 0.0f;
        for (const auto& line : shaped.lines) {
            // Birebir float kopya (plan ← layout): eşitlik güvenlidir.
            if (line.baseline == glyph.lineBaseline) {
                lineOffset = lineAlignmentOffset(line.width, maxWidth, alignment);
                break;
            }
        }
        SDL_FRect src{metrics->atlasLeft, metrics->atlasTop,
                      metrics->atlasRight - metrics->atlasLeft,
                      metrics->atlasBottom - metrics->atlasTop};
        SDL_FRect dst{x + lineOffset + glyph.x + metrics->planeLeft * px,
                      topY + glyph.lineBaseline + metrics->planeTop * px,
                      (metrics->planeRight - metrics->planeLeft) * px,
                      (metrics->planeBottom - metrics->planeTop) * px};
        SDL_SetTextureColorMod(atlasTexture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(atlasTexture, color.a);
        SDL_RenderTexture(renderer, atlasTexture, &src, &dst);
    }
    SDL_SetGPURenderState(renderer, nullptr);
    return true;
}

}  // namespace Rowl::Render
