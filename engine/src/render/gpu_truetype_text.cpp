// gpu_truetype_text.cpp — GPU yolunda kaliteli TrueType metin yedeği (A11).
// Gerekçe ve sözleşme: rowl/render/gpu_truetype_text.hpp.
#include "rowl/render/gpu_truetype_text.hpp"

#include <algorithm>
#include <cmath>

#include "rowl/render/font_renderer.hpp"
#include "rowl/text/text_shaper.hpp"

namespace Rowl::Render {
namespace {

// Glif taşmaları (negatif bearing, alt uzantılar, yüksek kontrast dış çizgisi)
// kırpılmasın diye yüzeyin her yanına bırakılan pay.
constexpr int kPad = 4;
// Hostile/bozuk ölçüye karşı tavan: 4K'nın iki katı yeter, sınırsız ayırma yok.
constexpr int kMaxSide = 8192;

int sideFor(float bound, float measured) {
    const float base = bound > 0.0f ? bound : measured;
    if (!std::isfinite(base) || base <= 0.0f) return 0;
    return std::min(kMaxSide, static_cast<int>(std::ceil(base)) + 2 * kPad);
}

}  // namespace

bool drawTrueTypeTextOnGpu(SDL_Renderer* renderer, FontRenderer* font,
                           const Rowl::Text::ShapedText* shaped,
                           const std::string& text, float x, float y,
                           float fontPx, SDL_Color color,
                           float maxWidth, float maxHeight,
                           const std::string& alignment,
                           std::size_t visibleUnits) {
    if (renderer == nullptr || font == nullptr || !font->isLoaded()) return false;
    if (text.empty() || !(fontPx > 0.0f)) return false;

    // Sınırsız eksende ölçüye dayan: şekilli metin kendi kutusunu bilir; düz
    // metin tek satır genişliği + satır yüksekliği payıyla ölçülür.
    const float measuredW = shaped != nullptr ? shaped->width
                                              : font->measureTextWidth(text, fontPx);
    const float measuredH = shaped != nullptr && shaped->height > 0.0f
                                ? shaped->height
                                : fontPx * 1.6f;
    const int width = sideFor(maxWidth, measuredW);
    const int height = sideFor(maxHeight, measuredH);
    if (width <= 0 || height <= 0) return false;

    SDL_Surface* surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) return false;
    SDL_ClearSurface(surface, 0.0f, 0.0f, 0.0f, 0.0f);

    // FontRenderer saydam yüzeye "src-over" yazar: rgb = renk*alfa, a = alfa —
    // yani premultiplied. Doku da öyle karıştırılır (koyu kenar saçağı yok).
    if (shaped != nullptr) {
        font->renderShapedText(surface, *shaped, static_cast<float>(kPad),
                               static_cast<float>(kPad), fontPx, color,
                               maxWidth, maxHeight, alignment, visibleUnits);
    } else {
        font->renderText(surface, text, static_cast<float>(kPad),
                         static_cast<float>(kPad), fontPx, color,
                         maxWidth, maxHeight, alignment, visibleUnits);
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    if (texture == nullptr) return false;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
    const SDL_FRect dst{x - kPad, y - kPad, static_cast<float>(width),
                        static_cast<float>(height)};
    const bool drawn = SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_DestroyTexture(texture);
    return drawn;
}

}  // namespace Rowl::Render
