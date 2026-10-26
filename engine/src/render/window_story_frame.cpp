// window_story_frame.cpp - F2 (B-1): a menu-free copy of the story frame for save
// thumbnails.
//
// A save thumbnail used to be read from the offscreen surface at save time. That had
// two defects: a visible (GPU) player window has no readable surface at all (no
// thumbnail), and an offscreen runtime saved from the pause menu captured the menu
// itself (the last frame drawn includes the overlay). The engine now asks the window to
// snapshot the frame in the one moment it is clean - after the story, characters and
// scene objects are drawn and before the pause overlay / present - and hands THAT to
// the thumbnail encoder.

#include "rowl/render/window.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/render/aspect_guardian.hpp"

#include <SDL3/SDL.h>

#include <cstring>

namespace Rowl::Render {

bool Window::captureStoryFrame() {
    if (!m_initialized || !m_sdlRenderer) return false;
    SDL_FlushRenderer(m_sdlRenderer);  // software renderer: queued rects land in the surface

    SDL_Surface* source = nullptr;
    bool ownsSource = false;
    if (m_offscreenSurface) {
        source = m_offscreenSurface;
    } else {
        // Visible window: read the story canvas back from the renderer (the letterbox bars
        // are not part of the picture). Only valid between drawing and SDL_RenderPresent.
        const ViewportMetrics metrics = AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);
        SDL_Rect canvas{metrics.x, metrics.y, metrics.width, metrics.height};
        source = canvas.w > 0 && canvas.h > 0 ? SDL_RenderReadPixels(m_sdlRenderer, &canvas)
                                              : SDL_RenderReadPixels(m_sdlRenderer, nullptr);
        ownsSource = true;
    }
    if (!source) {
        ROWL_LOG_WARN(std::string("Story frame capture failed: ") + SDL_GetError());
        return false;
    }

    SDL_Surface* rgba = source;
    bool ownsRgba = false;
    if (source->format != SDL_PIXELFORMAT_RGBA32) {
        rgba = SDL_ConvertSurface(source, SDL_PIXELFORMAT_RGBA32);
        ownsRgba = true;
    }
    bool ok = false;
    if (rgba && rgba->pixels && rgba->w > 0 && rgba->h > 0) {
        const size_t rowBytes = static_cast<size_t>(rgba->w) * 4u;
        m_storyFrame.resize(rowBytes * static_cast<size_t>(rgba->h));
        const auto* src = static_cast<const uint8_t*>(rgba->pixels);
        for (int y = 0; y < rgba->h; ++y) {
            std::memcpy(m_storyFrame.data() + static_cast<size_t>(y) * rowBytes,
                        src + static_cast<size_t>(y) * static_cast<size_t>(rgba->pitch), rowBytes);
        }
        m_storyFrameWidth = static_cast<uint32_t>(rgba->w);
        m_storyFrameHeight = static_cast<uint32_t>(rgba->h);
        ok = true;
    }
    if (ownsRgba && rgba) SDL_DestroySurface(rgba);
    if (ownsSource) SDL_DestroySurface(source);
    if (!ok) clearStoryFrame();
    return ok;
}

void Window::clearStoryFrame() {
    m_storyFrame.clear();
    m_storyFrame.shrink_to_fit();
    m_storyFrameWidth = 0;
    m_storyFrameHeight = 0;
}

}  // namespace Rowl::Render
