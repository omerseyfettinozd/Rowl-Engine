#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "rowl/render/msdf_renderer.hpp"
#include "rowl/text/text_shaper.hpp"

namespace Rowl::Render {

// D11: şekilli-MSDF plan/predikat API (saf, kilitsiz).
//
// Ham-MSDF yolu (window.cpp → renderGpuMsdfText → drawMsdfTextAllOrNothing)
// şekilsizdir: wrap'siz (tek-satır taşma), reveal'siz (typewriter'ı görmezden
// gelir) ve markup-kördür (etiket baytlarını atlas'ta arar). Bu TU, TrueType
// otoritesiyle (font_renderer.cpp:444-460 wrap, :554-558 render, window.cpp
// :1514-1533 shape+reveal+render) AYNI ShapedText üzerinden MSDF çizer.
//
// D10 çekirdeği (window_text_fallback.hpp/cpp) include/kopya EDİLMEZ; yalnız
// msdf_renderer.hpp + text_shaper.hpp public API kullanılır. Ham-metin
// fast-path kapsaması D10 core'a delege edilir (aşağıda).

// Tek çizilebilir glif: atlas-codepoint + layout konumu (hizalama-ofsetsiz;
// hizalama draw-fazında shaped.lines genişlikleriyle uygulanır) + satır
// taban-çizgisi (layout kopyası; satır-eşlemede birebir float karşılaştırma).
struct MsdfShapedDrawGlyph {
    uint32_t codepoint = 0;
    float x = 0.0f;
    float lineBaseline = 0.0f;
};

// Çizim planı: ok=false iken draw SDL'ye dokunmadan false döner
// (hepsi-ya-da-hiçi; çağıran debug-fallback'u eksiksiz çizer).
struct MsdfShapedDrawPlan {
    bool ok = false;
    bool fastPath = false;
    std::vector<MsdfShapedDrawGlyph> glyphs;
    std::size_t visibleUnits = 0;
    std::size_t lineCount = 0;
};

// Görünürlük predikatı — font_renderer.cpp:390 ile birebir aynı ifade:
// shapedGlyph.revealIndex >= maxVisibleRevealUnits ise atlanır.
inline bool msdfShapedGlyphVisible(uint32_t revealIndex,
                                   std::size_t maxVisible) noexcept {
    return revealIndex < maxVisible;
}

// SDL'siz plan kurucu: görünür glif kümesinde atlas-kapsama (findGlyph),
// konum layout'tan (glyph.x, glyph.y). Atlas-advance pen matematiği
// fast-path dışında kullanılmaz (bilinçli sapma; P1/P2 ile kilitli).
MsdfShapedDrawPlan planMsdfShapedDraw(const MsdfRenderer& atlas,
                                      const Rowl::Text::ShapedText& shaped,
                                      const std::string& rawMarkup,
                                      std::size_t maxVisible, float px);

// Null-guard fail-closed (SDL dokunuşu YOK). fastPath'te D10
// drawMsdfTextAllOrNothing'e DELEGE (birebir legacy piksel); değilse ve
// ok ise tek GPU-state oturumunda planı çizer; ok=false ise çizmeden false.
bool drawMsdfShapedAllOrNothing(SDL_Renderer* renderer,
                                SDL_GPURenderState* renderState,
                                MsdfRenderer* atlas, SDL_Texture* atlasTexture,
                                const Rowl::Text::ShapedText& shaped,
                                const std::string& rawMarkup, float x,
                                float topY, float px, SDL_Color color,
                                std::size_t maxVisible, float maxWidth,
                                const std::string& alignment);

}  // namespace Rowl::Render
