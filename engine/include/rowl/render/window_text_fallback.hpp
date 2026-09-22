#pragma once

#include <string>

#include <SDL3/SDL.h>

#include "rowl/render/msdf_renderer.hpp"

namespace Rowl::Render {

// D10: font-fallback mutual-exclusion çekirdeği (window.cpp:1415-1480).
//
// window.cpp'teki iki bölge koşulu birbirinin De Morgan tümleyenidir:
//   fallback (GPU-MSDF → debug):  !font || !loaded || !surface
//   TrueType (offscreen):          font && loaded && surface
// Her iki çağrı noktası da bu çekirdeğe delege eder; böylece iki bölge aynı
// karede ASLA çift-rasterizasyon yapmaz ve ASLA çift-skip'e düşmez.
// "Locked-sözleşme" yoktur (saf constexpr predikat; kilit üstte tutulur).
constexpr bool useFontFallbackPath(bool fontLoaded, bool hasOffscreenSurface) noexcept {
    return !fontLoaded || !hasOffscreenSurface;
}

// D10 (hepsi-ya-da-hiçi kapsama): metindeki HER skaler atlas'ta bulunuyorsa
// true; tek eksik-glyph'te bile false. Bayt değil skaler iterasyonu
// (çok-baytlı karakterler tek glyph aramasına düşer); geçersiz dizi U+FFFD
// olur, atlas'ta yoksa kapsama-dışıdır. SDL'ye dokunmaz (test-probu SDL
// init'siz çağırabilir).
bool msdfTextFullyCovered(const MsdfRenderer& atlas, const std::string& text);

// D10 (doğrula-çiz): önce kapsama taraması, sonra çizim. Eksik glyph'te
// renderer'a HİÇ dokunmadan false döner — çağıran debug-fallback'u eksiksiz
// çizer (kısmi-MSDF + true YOK, çift-çizim YOK). Tam-kapsamada tüm glyph'leri
// çizer ve true döner. Null renderState/atlas/texture/renderer'da SDL'ye
// dokunmadan false (fail-closed).
bool drawMsdfTextAllOrNothing(SDL_Renderer* renderer, SDL_GPURenderState* renderState,
                              MsdfRenderer* atlas, SDL_Texture* atlasTexture,
                              const std::string& text, float x, float baseline,
                              float px, SDL_Color color);

}  // namespace Rowl::Render
