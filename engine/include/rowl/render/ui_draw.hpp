#pragma once

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_render.h>

namespace Rowl::Render {

/// T1/T2: rounded box. One whole-pixel row per strip (sub-pixel rects vanish in
/// the software renderer); `radius` and `rings` are physical pixels. `drawFill`
/// false draws only the border rings.
void drawRoundedBox(SDL_Renderer* renderer, const SDL_FRect& rect, float radius, SDL_Color fill,
                    SDL_Color border, int rings, bool drawFill);

}  // namespace Rowl::Render
