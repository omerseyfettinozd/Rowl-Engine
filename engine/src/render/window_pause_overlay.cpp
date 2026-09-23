// window_pause_overlay.cpp — W8-d(2): pause-menu overlay renderi.
//
// window.cpp'den birebir tasinmistir:
//   944-1022  Window::renderPauseMenuOverlay (dim/panel/highlight + m_fontRenderer
//             metinleri; 1920x1080 sanal -> fiziksel AspectGuardian donusumu)
// Kosul, sira ve renkler birebirdir. D10 test_frame_hash_lock kilidi korunur.
// Yeni export YOK, `RowlEngine_` sembolu YOK.

#include "rowl/render/window.hpp"

#include "rowl/core/pause_menu.hpp"
#include "rowl/render/aspect_guardian.hpp"

#include <SDL3/SDL.h>

namespace Rowl::Render {

void Window::renderPauseMenuOverlay(const Rowl::Core::PauseMenuView& view) {
    if (!m_sdlRenderer || !m_offscreenSurface || !view.open) return;
    using LM = Rowl::Core::PauseMenuLayout;
    const ViewportMetrics metrics =
        AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);
    if (metrics.scaleFactor <= 0.0f) return;

    auto toPhys = [&](float vx, float vy, float& px, float& py) {
        AspectGuardian::virtualToPhysical(vx, vy, metrics, px, py);
    };

    // Dim the frozen story frame.
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_sdlRenderer, 0, 0, 0, 160);
    const SDL_FRect full{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)};
    SDL_RenderFillRect(m_sdlRenderer, &full);

    // Panel + accent border.
    float panelX = 0.0f, panelY = 0.0f;
    toPhys(LM::kPanelX, LM::kPanelY, panelX, panelY);
    const SDL_FRect panel{panelX, panelY, LM::kPanelW * metrics.scaleFactor,
                          LM::kPanelH * metrics.scaleFactor};
    SDL_SetRenderDrawColor(m_sdlRenderer, 15, 15, 26, 235);
    SDL_RenderFillRect(m_sdlRenderer, &panel);
    SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
    SDL_RenderRect(m_sdlRenderer, &panel);

    // Offscreen software path: flush queued rects before direct text writes.
    if (m_isOffscreen) {
        SDL_RenderPresent(m_sdlRenderer);
    }
    if (!m_fontRenderer || !m_fontRenderer->isLoaded()) return;

    const SDL_Color titleColor{56, 189, 248, 255};
    const SDL_Color textColor{241, 245, 249, 255};
    const SDL_Color dimColor{148, 163, 184, 255};
    float tx = 0.0f, ty = 0.0f;
    toPhys(LM::kRowX, 170.0f, tx, ty);
    m_fontRenderer->renderText(m_offscreenSurface, view.title, tx, ty,
                               40.0f * metrics.scaleFactor, titleColor,
                               LM::kRowW * metrics.scaleFactor, 60.0f * metrics.scaleFactor, "Left");

    const int rows = static_cast<int>(view.rows.size());
    for (int i = 0; i < rows; ++i) {
        const auto& row = view.rows[i];
        const float rowTop = LM::kRowY0 + i * (LM::kRowH + LM::kRowGap);
        float rx = 0.0f, ry = 0.0f;
        toPhys(LM::kRowX, rowTop, rx, ry);
        const float rowW = LM::kRowW * metrics.scaleFactor;
        const float rowH = LM::kRowH * metrics.scaleFactor;
        const bool selected = (i == view.selected);
        if (selected) {
            const SDL_FRect highlight{rx, ry, rowW, rowH};
            SDL_SetRenderDrawColor(m_sdlRenderer, 14, 116, 144, 255);
            SDL_RenderFillRect(m_sdlRenderer, &highlight);
            const SDL_FRect bar{rx, ry, 6.0f * metrics.scaleFactor, rowH};
            SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
            SDL_RenderFillRect(m_sdlRenderer, &bar);
            if (m_isOffscreen) {
                SDL_RenderPresent(m_sdlRenderer);
            }
        }
        const float fontPx = 24.0f * metrics.scaleFactor;
        const float textY = ry + rowH * 0.5f - fontPx * 0.35f;
        m_fontRenderer->renderText(m_offscreenSurface, row.label, rx + 18.0f * metrics.scaleFactor,
                                   textY, fontPx, selected ? titleColor : textColor,
                                   rowW * 0.55f, rowH, "Left");
        if (!row.value.empty()) {
            m_fontRenderer->renderText(m_offscreenSurface, row.value, rx + rowW * 0.55f,
                                       textY, fontPx, dimColor, rowW * 0.4f, rowH, "Left");
        }
    }

    float hx = 0.0f, hy = 0.0f;
    toPhys(LM::kRowX, LM::kPanelY + LM::kPanelH - 44.0f, hx, hy);
    m_fontRenderer->renderText(m_offscreenSurface, view.hint, hx, hy,
                               19.0f * metrics.scaleFactor, dimColor,
                               LM::kRowW * metrics.scaleFactor, 30.0f * metrics.scaleFactor, "Left");
}

}  // namespace Rowl::Render
