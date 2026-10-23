#pragma once
// gpu_truetype_text.hpp — GPU yolunda kaliteli TrueType metin yedeği (A11).
//
// Gerçek ekran denemesi (2026-10-20): standalone oyuncu GPU yolunda metni yalnız
// MSDF atlasıyla çiziyordu; atlas yoksa ya da metindeki TEK bir karakter atlasta
// değilse (varsayılan atlas yalnız ASCII 32..126) SDL_RenderDebugText'e (8x8 ASCII
// bitmap) düşüyordu — Türkçe "ş/ı/ğ" ve "—" kutu olarak çıkıyor, editör önizlemesi
// (offscreen TrueType) ile dağıtılan oyun farklı görünüyordu.
//
// Bu yardımcı, offscreen kolun kullandığı AYNI FontRenderer ile metni saydam bir
// RGBA32 yüzeye rasterleştirir, premultiplied alfa dokusu olarak çizer. Debug
// metin yalnız bu da başarısız olursa son çare kalır.
#include <cstddef>
#include <cstdint>
#include <string>

#include <SDL3/SDL.h>

namespace Rowl::Text { struct ShapedText; }

namespace Rowl::Render {

class FontRenderer;

/// Draws `text` (or the pre-shaped `shaped`, with `visibleUnits` reveal limit)
/// with `font` at physical (x, y) — the same top-left origin the offscreen
/// TrueType path passes to FontRenderer. `maxWidth`/`maxHeight` bound the
/// layout exactly like the offscreen call (0 = unbounded, measured instead).
/// Returns false without touching the renderer when nothing can be drawn
/// (null renderer/font, unloaded font, empty text, surface/texture failure),
/// so the caller can still fall back.
bool drawTrueTypeTextOnGpu(SDL_Renderer* renderer, FontRenderer* font,
                           const Rowl::Text::ShapedText* shaped,
                           const std::string& text, float x, float y,
                           float fontPx, SDL_Color color,
                           float maxWidth, float maxHeight,
                           const std::string& alignment = "Left",
                           std::size_t visibleUnits = SIZE_MAX);

}  // namespace Rowl::Render
