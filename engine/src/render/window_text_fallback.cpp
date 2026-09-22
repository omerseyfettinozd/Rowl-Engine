// window_text_fallback.cpp — D10 font-fallback mutual-exclusion çekirdeği.
//
// Window::renderGpuMsdfText gövdesi + window.cpp:1415-1480 fallback/TrueType
// karar çekirdeği buraya birebir taşındı; window.cpp çağrı noktaları delege
// eder (şişirme YOK, window.cpp net-küçülür). Tek davranış FIX'i (D10):
// eksik-glyph'te kısmi-çizim + true DÖNMEZ; çizmeden false döner ve çağıran
// debug-fallback'u eksiksiz çizer.
#include "rowl/render/window_text_fallback.hpp"

#include "rowl/text/utf8.hpp"

namespace Rowl::Render {

bool msdfTextFullyCovered(const MsdfRenderer& atlas, const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        if (decoded.length == 0) break;  // Savunma: boş-kalan=''de ilerleme YOK.
        i += decoded.length;
        if (atlas.findGlyph(decoded.codepoint) == nullptr) return false;
    }
    return true;
}

bool drawMsdfTextAllOrNothing(SDL_Renderer* renderer, SDL_GPURenderState* renderState,
                              MsdfRenderer* atlas, SDL_Texture* atlasTexture,
                              const std::string& text, float x, float baseline,
                              float px, SDL_Color color) {
    if (renderState == nullptr || atlas == nullptr || atlasTexture == nullptr) return false;
    if (renderer == nullptr) return false;
    // D10 (hepsi-ya-da-hiçi): eksik-glyph'te HİÇ çizmeden false.
    if (!msdfTextFullyCovered(*atlas, text)) return false;
    if (!SDL_SetGPURenderState(renderer, renderState)) return false;
    float pen = x;
    // A3-tur4 (metin turu): bayt degil skaler iterasyonu — cok-baytli
    // karakterler tek glyph aramasina duser (onceki her bayti ayri arardi;
    // ASCII davranisi ayni). Gecersiz dizi U+FFFD olur, atlas'ta yoksa
    // yarim-adim ilerler (onceki cop-lookup ile ayni).
    // D10-notu: bu döngüye yalnızca tam-kapsama sonrası girilir; aşağıdaki
    // `continue` kolu savunma-artığıdır (kapsama taraması zaten false vermiş
    // olurdu) — çizim gövdesi birebir korunur.
    for (std::size_t i = 0; i < text.size();) {
        const Rowl::Text::Utf8Scalar decoded = Rowl::Text::decodeUtf8Scalar(
            text.data() + i, text.data() + text.size());
        i += decoded.length;
        const auto* glyph = atlas->findGlyph(decoded.codepoint);
        if (!glyph) { pen += px * .5f; continue; }
        SDL_FRect src{glyph->atlasLeft, glyph->atlasTop, glyph->atlasRight-glyph->atlasLeft, glyph->atlasBottom-glyph->atlasTop};
        SDL_FRect dst{pen + glyph->planeLeft*px, baseline + glyph->planeTop*px,
                      (glyph->planeRight-glyph->planeLeft)*px, (glyph->planeBottom-glyph->planeTop)*px};
        SDL_SetTextureColorMod(atlasTexture, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(atlasTexture, color.a);
        SDL_RenderTexture(renderer, atlasTexture, &src, &dst);
        pen += glyph->advance*px;
    }
    SDL_SetGPURenderState(renderer, nullptr);
    return true;
}

}  // namespace Rowl::Render
