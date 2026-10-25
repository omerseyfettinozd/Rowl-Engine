// window_pause_overlay.cpp — W8-d(2): pause-menu overlay renderi.
//
// window.cpp'den birebir tasinmistir:
//   944-1022  Window::renderPauseMenuOverlay (dim/panel/highlight + m_fontRenderer
//             metinleri; 1920x1080 sanal -> fiziksel AspectGuardian donusumu)
// Kosul, sira ve renkler birebirdir. D10 test_frame_hash_lock kilidi korunur.
// Yeni export YOK, `RowlEngine_` sembolu YOK.

#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"

#include "rowl/core/pause_menu.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/gpu_truetype_text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace Rowl::Render {

void Window::releasePauseThumbnail() {
    if (m_pauseThumbTexture) {
        SDL_DestroyTexture(m_pauseThumbTexture);
        m_pauseThumbTexture = nullptr;
    }
    m_pauseThumbKey.clear();
    m_pauseThumbFailedKey.clear();
}

void Window::drawPauseThumbnail(const Rowl::Core::PauseMenuView& view,
                                const ViewportMetrics& metrics) {
    using LM = Rowl::Core::PauseMenuLayout;
    if (!view.thumbnailPng || view.thumbnailPng->empty() || view.thumbnailKey.empty()) return;
    // D02: decoded once per (slot, saved_at) and kept as a single texture.
    if (m_pauseThumbKey != view.thumbnailKey) {
        if (m_pauseThumbFailedKey == view.thumbnailKey) return;
        releasePauseThumbnail();
        int w = 0, h = 0, channels = 0;
        unsigned char* data = nullptr;
        int infoW = 0, infoH = 0, infoC = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(view.thumbnailPng->data());
        const int count = static_cast<int>(view.thumbnailPng->size());
        if (stbi_info_from_memory(bytes, count, &infoW, &infoH, &infoC) &&
            infoW > 0 && infoH > 0 && infoW <= 4096 && infoH <= 4096) {
            data = stbi_load_from_memory(bytes, count, &w, &h, &channels, 4);
        }
        if (data) {
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w * 4)) {
                m_pauseThumbTexture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
                SDL_DestroySurface(surface);
            }
            stbi_image_free(data);
        }
        if (m_pauseThumbTexture) {
            m_pauseThumbKey = view.thumbnailKey;
        } else {
            m_pauseThumbFailedKey = view.thumbnailKey;
            return;
        }
    }
    float tx = 0.0f, ty = 0.0f;
    AspectGuardian::virtualToPhysical(LM::kThumbX, LM::kThumbY, metrics, tx, ty);
    const SDL_FRect dst{tx, ty, LM::kThumbW * metrics.scaleFactor, LM::kThumbH * metrics.scaleFactor};
    SDL_RenderTexture(m_sdlRenderer, m_pauseThumbTexture, nullptr, &dst);
    const auto& thumbBorder = uiTheme().menu.thumbBorder;
    SDL_SetRenderDrawColor(m_sdlRenderer, thumbBorder.r, thumbBorder.g, thumbBorder.b, thumbBorder.a);
    SDL_RenderRect(m_sdlRenderer, &dst);
}

void Window::renderPauseMenuOverlay(const Rowl::Core::PauseMenuView& view) {
    if (!m_sdlRenderer || !view.open) return;
    using LM = Rowl::Core::PauseMenuLayout;
    const ViewportMetrics metrics =
        AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);
    if (metrics.scaleFactor <= 0.0f) return;

    auto toPhys = [&](float vx, float vy, float& px, float& py) {
        AspectGuardian::virtualToPhysical(vx, vy, metrics, px, py);
    };

    // D07-2: the gallery's full-screen viewer replaces the whole menu.
    if (view.extras.active && view.extras.viewing) {
        renderGalleryViewer(view, metrics);
        return;
    }

    const Rowl::State::UiMenuTheme& menu = uiTheme().menu;
    FontRenderer* const menuFont = getMenuFontRenderer();  // T1: theme menu font or the project font

    // Dim the frozen story frame.
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_sdlRenderer, menu.dim.r, menu.dim.g, menu.dim.b, menu.dim.a);
    const SDL_FRect full{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)};
    SDL_RenderFillRect(m_sdlRenderer, &full);

    // Panel + accent border.
    float panelX = 0.0f, panelY = 0.0f;
    toPhys(LM::kPanelX, LM::kPanelY, panelX, panelY);
    const SDL_FRect panel{panelX, panelY, LM::kPanelW * metrics.scaleFactor,
                          LM::kPanelH * metrics.scaleFactor};
    SDL_SetRenderDrawColor(m_sdlRenderer, menu.panelFill.r, menu.panelFill.g, menu.panelFill.b, menu.panelFill.a);
    SDL_RenderFillRect(m_sdlRenderer, &panel);
    SDL_SetRenderDrawColor(m_sdlRenderer, menu.panelBorder.r, menu.panelBorder.g, menu.panelBorder.b,
                           menu.panelBorder.a);
    strokeRect(m_sdlRenderer, panel, static_cast<int>(std::lround(menu.panelBorderWidth)));
    drawPauseThumbnail(view, metrics);  // D02: selected slot's thumbnail
    drawExtrasGraphics(view, metrics);  // D07-2: gallery grid boxes + thumbnails

    // C02 BUG-2: gorunur pencerede overlay her kare dim basar; donmus hikaye
    // karesi onbellekten atlanirsa arka tampon bos kalir / dim ust uste biner
    // (siyah ekran, resize sonrasi dahil). Sonraki kare hikayeyi tam cizsin.
    // Offscreen yuzeyde de ayni: onbellekteki kare onceki overlay'i tasir —
    // dim her karede koyulasir, sayfa degisince eski menu soluk kalirdi.
    invalidateFrameCache();

    // Offscreen software path: flush queued rects before direct text writes.
    if (m_isOffscreen) {
        SDL_RenderPresent(m_sdlRenderer);
    }
    if (!menuFont || !menuFont->isLoaded()) return;

    // C02 BUG-2: gorunur (GPU/standalone) pencerede offscreen yuzey yok; metin
    // ayni FontRenderer ile dokuya cizilip renderer kuyruguna eklenir (rect'lerle
    // dogru sirada). Offscreen kolda dogrudan yuzeye yazilir.
    auto drawText = [&](const std::string& text, float x, float y, float px, SDL_Color color,
                        float w, float h) {
        if (m_offscreenSurface) {
            menuFont->renderText(m_offscreenSurface, text, x, y, px, color, w, h, "Left");
        } else {
            drawTrueTypeTextOnGpu(m_sdlRenderer, menuFont, nullptr, text, x, y, px,
                                  color, w, h, "Left");
        }
    };

    const auto sdl = [](const Rowl::State::UiColor& c) { return SDL_Color{c.r, c.g, c.b, c.a}; };
    const SDL_Color titleColor = sdl(menu.title);
    const SDL_Color textColor = sdl(menu.text);
    const SDL_Color dimColor = sdl(menu.dimText);
    const SDL_Color selectedTextColor = sdl(menu.selectedText);
    const SDL_Color disabledColor = sdl(menu.disabledText);
    const SDL_Color hintColor = sdl(menu.hint);
    const SDL_Color historySpeakerColor = sdl(menu.historySpeaker);
    // D05-2: the renderer multiplies every font size by the accessibility text
    // scale. The fixed-pitch menu boxes (title, hint, row detail) must not
    // outgrow their rows, so those texts are drawn at their design size (the
    // base is divided by the scale); a row's label/value may grow with the
    // scale up to what its row box holds (see rowPx below). The backlog list
    // keeps scaling: the engine laid it out with the same factor.
    const float accessScale = std::max(1.0f, menuFont->textScale());
    float tx = 0.0f, ty = 0.0f;
    toPhys(LM::kRowX, 170.0f, tx, ty);
    drawText(view.title, tx, ty, menu.titlePx * metrics.scaleFactor / accessScale, titleColor,
             LM::kRowW * metrics.scaleFactor, 60.0f * metrics.scaleFactor);

    drawExtrasText(view, metrics);  // D07-2: counter + gallery captions

    // D03-2: backlog page — engine-laid-out entries (wrapped lines, y/h in the
    // virtual canvas) instead of the fixed-pitch row list. An empty history
    // falls through to the generic loop with its single dim "history is empty" row.
    const bool backlogEntries = view.backlog.active && !view.backlog.entries.empty();
    if (view.backlog.active) {
        float cx = 0.0f, cy = 0.0f;
        toPhys(LM::kRowX + LM::kRowW - 150.0f, 178.0f, cx, cy);
        drawText(view.backlog.counter, cx, cy, 24.0f * metrics.scaleFactor, dimColor,
                 150.0f * metrics.scaleFactor, 40.0f * metrics.scaleFactor);
    }
    if (backlogEntries) {
        // The font's accessibility scale grows glyphs and line pitch alike
        // (the engine laid the list out with the same factor).
        const float textScale = std::max(1.0f, menuFont->textScale());
        const float lineH = LM::kBacklogLineH * textScale;
        for (const auto& entry : view.backlog.entries) {
            float rx = 0.0f, ry = 0.0f;
            toPhys(LM::kRowX, entry.y, rx, ry);
            const float rowW = LM::kRowW * metrics.scaleFactor;
            const float rowH = entry.h * metrics.scaleFactor;
            if (entry.selected) {
                const SDL_FRect highlight{rx, ry, rowW, rowH};
                SDL_SetRenderDrawColor(m_sdlRenderer, menu.selectedFill.r, menu.selectedFill.g,
                                       menu.selectedFill.b, menu.selectedFill.a);
                SDL_RenderFillRect(m_sdlRenderer, &highlight);
                const SDL_FRect bar{rx, ry, menu.selectedBarWidth * metrics.scaleFactor, rowH};
                SDL_SetRenderDrawColor(m_sdlRenderer, menu.selectedBar.r, menu.selectedBar.g,
                                       menu.selectedBar.b, menu.selectedBar.a);
                if (bar.w > 0.0f) SDL_RenderFillRect(m_sdlRenderer, &bar);
                if (m_isOffscreen) {
                    SDL_RenderPresent(m_sdlRenderer);
                }
            }
            const float textX = rx + LM::kBacklogPadX * metrics.scaleFactor;
            // Pre-wrapped lines are drawn one by one inside a generous box so
            // the renderer never re-wraps them (width slack, one-line height).
            const float boxW = (LM::kRowW - LM::kBacklogPadX) * metrics.scaleFactor;
            const float boxH = lineH * 1.5f * metrics.scaleFactor;
            float lineY = ry + LM::kBacklogPadY * metrics.scaleFactor;
            if (!entry.speaker.empty()) {
                drawText(entry.speaker, textX, lineY, menu.historySpeakerPx * metrics.scaleFactor,
                         historySpeakerColor,
                         boxW * 0.7f, boxH);
            }
            if (entry.hasVoice) {
                // Small voice mark ("(ses)" / "(voice)") at the right edge of the first text row.
                drawText(view.backlog.voiceMark, rx + rowW - 74.0f * metrics.scaleFactor, lineY,
                         LM::kBacklogSmallPx * metrics.scaleFactor, dimColor,
                         64.0f * metrics.scaleFactor, boxH);
            }
            if (!entry.speaker.empty()) lineY += lineH * metrics.scaleFactor;
            for (const auto& line : entry.lines) {
                if (!line.empty()) {
                    drawText(line, textX, lineY, LM::kBacklogFontPx * metrics.scaleFactor,
                             textColor, boxW, boxH);
                }
                lineY += lineH * metrics.scaleFactor;
            }
        }
    }

    const int rows = backlogEntries ? 0 : static_cast<int>(view.rows.size());
    for (int i = 0; i < rows; ++i) {
        const auto& row = view.rows[i];
        // D05-2: pages with more than kComfortRows rows (Preferences) use the
        // compact pitch so the last row clears the hint line.
        const float rowTop = LM::rowTop(i, rows);
        float rx = 0.0f, ry = 0.0f;
        toPhys(LM::kRowX, rowTop, rx, ry);
        const float rowW = LM::kRowW * metrics.scaleFactor;
        const float rowH = LM::rowH(rows) * metrics.scaleFactor;
        const bool selected = (i == view.selected);
        if (selected) {
            const SDL_FRect highlight{rx, ry, rowW, rowH};
            SDL_SetRenderDrawColor(m_sdlRenderer, menu.selectedFill.r, menu.selectedFill.g,
                                   menu.selectedFill.b, menu.selectedFill.a);
            SDL_RenderFillRect(m_sdlRenderer, &highlight);
            const SDL_FRect bar{rx, ry, menu.selectedBarWidth * metrics.scaleFactor, rowH};
            SDL_SetRenderDrawColor(m_sdlRenderer, menu.selectedBar.r, menu.selectedBar.g,
                                   menu.selectedBar.b, menu.selectedBar.a);
            if (bar.w > 0.0f) SDL_RenderFillRect(m_sdlRenderer, &bar);
            if (m_isOffscreen) {
                SDL_RenderPresent(m_sdlRenderer);
            }
        }
        // D02: a row with a detail line lifts label/value to make room for it;
        // detail-less rows keep the exact pre-D02 geometry.
        const bool hasDetail = !row.detail.empty();
        // D05-2: label/value follow the text scale only as far as the row box
        // holds them (60% of the row height); a detail row stays at design size.
        const float rowPx = hasDetail ? menu.rowPx
                                      : std::min(menu.rowPx * accessScale, LM::rowH(rows) * 0.6f);
        const float valuePx = hasDetail ? menu.valuePx
                                        : std::min(menu.valuePx * accessScale, LM::rowH(rows) * 0.6f);
        const float fontPx = rowPx * metrics.scaleFactor;
        const float drawPx = fontPx / accessScale;  // what drawText is given
        const float valueDrawPx = valuePx * metrics.scaleFactor / accessScale;
        // The compact (12-row) pitch has 4 px less room under the glyphs.
        const float textY = ry + rowH * 0.5f - fontPx * 0.35f -
                            (hasDetail ? 9.0f * metrics.scaleFactor : 0.0f) -
                            (rows > LM::kComfortRows ? 4.0f * metrics.scaleFactor : 0.0f);
        const SDL_Color labelColor = !row.enabled ? disabledColor : (selected ? selectedTextColor : textColor);
        drawText(row.label, rx + 18.0f * metrics.scaleFactor, textY, drawPx,
                 labelColor, rowW * 0.55f, rowH);
        if (!row.value.empty()) {
            drawText(row.value, rx + rowW * 0.55f, textY, valueDrawPx, dimColor, rowW * 0.4f, rowH);
        }
        if (hasDetail) {
            drawText(row.detail, rx + 18.0f * metrics.scaleFactor,
                     ry + rowH - 22.0f * metrics.scaleFactor, menu.detailPx * metrics.scaleFactor / accessScale,
                     dimColor, rowW - 36.0f * metrics.scaleFactor, 20.0f * metrics.scaleFactor);
        }
    }

    float hx = 0.0f, hy = 0.0f;
    toPhys(LM::kRowX, LM::kPanelY + LM::kPanelH - 44.0f, hx, hy);
    drawText(view.hint, hx, hy, menu.hintPx * metrics.scaleFactor / accessScale, hintColor,
             LM::kRowW * metrics.scaleFactor, 30.0f * metrics.scaleFactor);
}

// Softly rounded rectangle: one whole-pixel row per strip (sub-pixel rects
// vanish in the software renderer on small surfaces); only the top and bottom
// few rows are inset by a small corner arc. A full half-height capsule would
// show hard 1px stair steps without anti-aliasing. (D04-2 badges; D07-2 toast.)
void Window::fillCapsule(SDL_Renderer* renderer, const SDL_FRect& r, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    const int rows = std::max(1, static_cast<int>(std::lround(r.h)));
    const float corner = std::min(static_cast<float>(rows) * 0.2f, 6.0f);
    const float top = std::floor(r.y);
    const float width = std::max(1.0f, std::round(r.w));
    const float x0 = std::round(r.x);
    for (int row = 0; row < rows; ++row) {
        const float edge = std::min(static_cast<float>(row) + 0.5f,
                                    static_cast<float>(rows - row) - 0.5f);
        float inset = 0.0f;
        if (edge < corner) {
            const float up = corner - edge;
            inset = std::round(corner - std::sqrt(std::max(0.0f, corner * corner - up * up)));
        }
        const SDL_FRect strip{x0 + inset, top + static_cast<float>(row),
                              std::max(1.0f, width - 2.0f * inset), 1.0f};
        SDL_RenderFillRect(renderer, &strip);
    }
}

void Window::renderPlayerBadges(bool autoBadge, bool skipBadge) {
    if (!m_sdlRenderer || (!autoBadge && !skipBadge)) return;
    // D04-2: AUTO / SKIP status capsules, top-right of the 1920x1080 canvas.
    // Semi-transparent so the scene stays readable; palette = the pause menu's
    // (panel navy / accent cyan / light text). SKIP sits left of AUTO.
    // T1: colours, size, gap and margins come from the UI theme (defaults ==
    // the former constants 116x44, gap 12, right 40, top 28, 24 px text).
    const Rowl::State::UiBadgeTheme& theme = uiTheme().badges;
    const float kBadgeW = theme.width;
    const float kBadgeH = theme.height;
    const float kBadgeGap = theme.gap;
    const float kMarginRight = theme.marginRight;
    const float kMarginTop = theme.marginTop;
    const ViewportMetrics metrics =
        AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);
    if (metrics.scaleFactor <= 0.0f) return;

    struct Badge {
        const char* text;
        SDL_Color fill;
        SDL_Color textColor;
        float left;
    };
    const auto sdl = [](const Rowl::State::UiColor& c) { return SDL_Color{c.r, c.g, c.b, c.a}; };
    Badge badges[2];
    int count = 0;
    float left = 1920.0f - kMarginRight - kBadgeW;
    if (autoBadge) {
        badges[count++] = {"AUTO", sdl(theme.autoFill), sdl(theme.autoText), left};
        left -= kBadgeW + kBadgeGap;
    }
    if (skipBadge) {
        badges[count++] = {"SKIP", sdl(theme.skipFill), sdl(theme.skipText), left};
    }

    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < count; ++i) {
        float px = 0.0f, py = 0.0f;
        AspectGuardian::virtualToPhysical(badges[i].left, kMarginTop, metrics, px, py);
        const SDL_FRect rect{px, py, kBadgeW * metrics.scaleFactor, kBadgeH * metrics.scaleFactor};
        fillCapsule(m_sdlRenderer, rect, badges[i].fill);
    }
    // Offscreen software path: flush queued rects before direct text writes.
    if (m_isOffscreen) {
        SDL_RenderPresent(m_sdlRenderer);
    }
    FontRenderer* const menuFont = getMenuFontRenderer();
    if (!menuFont || !menuFont->isLoaded()) return;
    const float fontPx = theme.px * metrics.scaleFactor;
    for (int i = 0; i < count; ++i) {
        float px = 0.0f, py = 0.0f;
        AspectGuardian::virtualToPhysical(badges[i].left, kMarginTop, metrics, px, py);
        const float w = kBadgeW * metrics.scaleFactor;
        const float h = kBadgeH * metrics.scaleFactor;
        // The text box is laid out from its top edge: cap-height centre sits
        // ~0.69 font-px above the box centre (measured against the font).
        const float textY = py + h * 0.5f - fontPx * 0.69f;
        if (m_offscreenSurface) {
            menuFont->renderText(m_offscreenSurface, badges[i].text, px, textY, fontPx,
                                 badges[i].textColor, w, h, "Center");
        } else {
            drawTrueTypeTextOnGpu(m_sdlRenderer, menuFont, nullptr, badges[i].text,
                                  px, textY, fontPx, badges[i].textColor, w, h, "Center");
        }
    }
}

}  // namespace Rowl::Render
