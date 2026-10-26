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
#include <array>
#include <cmath>
#include <vector>

namespace Rowl::Render {

void Window::releasePauseThumbnail() {
    if (m_pauseThumbTexture) {
        SDL_DestroyTexture(m_pauseThumbTexture);
        m_pauseThumbTexture = nullptr;
    }
    m_pauseThumbKey.clear();
    m_pauseThumbFailedKey.clear();
    for (auto& [key, thumb] : m_cardThumbs) {  // T2b
        if (thumb.texture) SDL_DestroyTexture(thumb.texture);
    }
    m_cardThumbs.clear();
    m_cardThumbFailed.clear();
}

// T2b: decoded once per (slot, saved_at) key; at most kMaxCardThumbs textures stay (least recently used out).
SDL_Texture* Window::slotCardThumbnail(const std::string& key, const std::string& png) {
    constexpr size_t kMaxCardThumbs = 40;
    if (key.empty() || png.empty()) return nullptr;
    if (const auto hit = m_cardThumbs.find(key); hit != m_cardThumbs.end()) {
        hit->second.lastUsed = ++m_cardThumbClock;
        return hit->second.texture;
    }
    if (m_cardThumbFailed.contains(key)) return nullptr;
    int w = 0, h = 0, channels = 0, infoW = 0, infoH = 0, infoC = 0;
    const auto* bytes = reinterpret_cast<const unsigned char*>(png.data());
    const int count = static_cast<int>(png.size());
    SDL_Texture* texture = nullptr;
    if (stbi_info_from_memory(bytes, count, &infoW, &infoH, &infoC) && infoW > 0 && infoH > 0 && infoW <= 4096 &&
        infoH <= 4096) {
        if (unsigned char* data = stbi_load_from_memory(bytes, count, &w, &h, &channels, 4)) {
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, data, w * 4)) {
                texture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
                SDL_DestroySurface(surface);
            }
            stbi_image_free(data);
        }
    }
    if (!texture) {
        if (m_cardThumbFailed.size() >= 128) m_cardThumbFailed.clear();
        m_cardThumbFailed.insert(key);
        return nullptr;
    }
    while (m_cardThumbs.size() >= kMaxCardThumbs) {
        auto oldest = m_cardThumbs.begin();
        for (auto it = m_cardThumbs.begin(); it != m_cardThumbs.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        }
        SDL_DestroyTexture(oldest->second.texture);
        m_cardThumbs.erase(oldest);
    }
    m_cardThumbs[key] = CardThumb{texture, ++m_cardThumbClock};
    return texture;
}

namespace {

using Rowl::State::UiSkin;
using Rowl::State::UiSkinSet;
using Rowl::State::UiSkinState;

}  // namespace

void Window::drawPauseThumbnail(const Rowl::Core::PauseMenuView& view,
                                const ViewportMetrics& metrics) {
    const Rowl::Core::MenuLayout& LM = view.layout;  // T2b: the layout the engine hit-tests with
    const Rowl::State::UiTheme& theme = uiTheme();
    const bool slotPage = view.page == "save" || view.page == "load";
    const bool hasPng = view.thumbnailPng && !view.thumbnailPng->empty() && !view.thumbnailKey.empty();
    if ((!hasPng && !slotPage) || LM.thumbHidden) return;
    float tx = 0.0f, ty = 0.0f;
    AspectGuardian::virtualToPhysical(LM.thumbX, LM.thumbY, metrics, tx, ty);
    const SDL_FRect dst{tx, ty, LM.thumbW * metrics.scaleFactor, LM.thumbH * metrics.scaleFactor};
    const Rowl::State::UiSkin& frame = theme.slots.thumbFrameSkin.normal;
    bool drew = false;
    bool legacyBorder = false;
    if (hasPng) {
        // D02: decoded once per (slot, saved_at) and kept as a single texture.
        if (m_pauseThumbKey != view.thumbnailKey && m_pauseThumbFailedKey != view.thumbnailKey) {
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
            }
        }
        if (m_pauseThumbKey == view.thumbnailKey && m_pauseThumbTexture) {
            SDL_RenderTexture(m_sdlRenderer, m_pauseThumbTexture, nullptr, &dst);
            drew = true;
            legacyBorder = !skinDraws(frame);
        }
    }
    if (!drew && slotPage) {
        // T2: nothing to show for this slot - the Save page's "new save" picture for an empty
        // slot, else the generic placeholder.
        const bool emptySlot = view.selected >= 0 && view.selected < static_cast<int>(view.rows.size()) &&
                               view.rows[static_cast<size_t>(view.selected)].slotState == 1;
        const Rowl::State::UiSkin& newImage = theme.slots.newImage;
        const Rowl::State::UiSkin& placeholder = theme.slots.thumbPlaceholder;
        const Rowl::State::UiSkin& pick = (view.page == "save" && emptySlot && !newImage.image.empty())
            ? newImage : placeholder;
        drew = paintSkinImageOnly(dst, pick, metrics.scaleFactor);
    }
    if (drew && skinDraws(frame)) {
        // The frame image is drawn around the thumbnail box; its padding (virtual px) grows it outward.
        const auto pad = frame.padding.value_or(std::array<float, 4>{0, 0, 0, 0});
        const float s = metrics.scaleFactor;
        const SDL_FRect around{dst.x - pad[0] * s, dst.y - pad[1] * s, dst.w + (pad[0] + pad[2]) * s,
                               dst.h + (pad[1] + pad[3]) * s};
        paintSkin(around, frame, std::nullopt, theme.menu.thumbBorder, 1.0f, s);
    } else if (legacyBorder) {
        const auto& thumbBorder = theme.menu.thumbBorder;
        SDL_SetRenderDrawColor(m_sdlRenderer, thumbBorder.r, thumbBorder.g, thumbBorder.b, thumbBorder.a);
        SDL_RenderRect(m_sdlRenderer, &dst);
    }
}

// T2: the title screen's own backdrop. Covers the whole window (letterbox included) so the
// story frame behind the title is no longer visible; returns true when it did.
bool Window::drawTitleBackdrop(const Rowl::Core::PauseMenuView&, const ViewportMetrics& metrics) {
    const Rowl::State::UiTitleScreenTheme& title = uiTheme().titleScreen;
    SDL_Texture* texture = title.background.image.empty() ? nullptr : loadTexture(title.background.image);
    if (texture == nullptr && !title.hideStoryFrame) return false;
    const SDL_FRect full{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)};
    const Rowl::State::UiColor ground = title.background.fill.value_or(Rowl::State::UiColor{0, 0, 0, 255});
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(m_sdlRenderer, ground.r, ground.g, ground.b, 255);
    SDL_RenderFillRect(m_sdlRenderer, &full);
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    if (texture != nullptr) paintSkin(full, title.background, std::nullopt, std::nullopt, 0.0f, metrics.scaleFactor);
    return true;
}

void Window::drawTitleLogo(const ViewportMetrics& metrics) {
    const Rowl::State::UiTitleScreenTheme& title = uiTheme().titleScreen;
    if (title.logo.image.empty()) return;
    float lx = 0.0f, ly = 0.0f;
    AspectGuardian::virtualToPhysical(title.logoX, title.logoY, metrics, lx, ly);
    const SDL_FRect rect{lx, ly, title.logoW * metrics.scaleFactor, title.logoH * metrics.scaleFactor};
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    paintSkinImageOnly(rect, title.logo, metrics.scaleFactor);
}

void Window::renderPauseMenuOverlay(const Rowl::Core::PauseMenuView& view) {
    if (!m_sdlRenderer || !view.open) return;
    using PL = Rowl::Core::PauseMenuLayout;  // fixed design sizes (text px, line pitch)
    const Rowl::Core::MenuLayout& LM = view.layout;  // T2b: the page's layout - one source with the hit-tests
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

    const Rowl::State::UiTheme& theme = uiTheme();
    const Rowl::State::UiMenuTheme& menu = theme.menu;
    const int screenIndex = Rowl::State::uiScreenIndex(view.page);
    static const Rowl::State::UiScreenTheme kNoScreen;
    const Rowl::State::UiScreenTheme& screen =
        screenIndex >= 0 ? theme.screens[static_cast<size_t>(screenIndex)] : kNoScreen;
    FontRenderer* const menuFont = getMenuFontRenderer();  // T1: theme menu font or the project font
    const float s = metrics.scaleFactor;

    // T2: the title's own backdrop hides the story frame behind every page opened from the title.
    const bool backdrop = view.titleActive && drawTitleBackdrop(view, metrics);

    // Dim the frozen story frame.
    Rowl::State::UiColor dim = menu.dim;
    if (screen.dim) dim = *screen.dim;
    else if (backdrop) dim = theme.titleScreen.dim.value_or(Rowl::State::UiColor{0, 0, 0, 0});
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_sdlRenderer, dim.r, dim.g, dim.b, dim.a);
    const SDL_FRect full{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)};
    SDL_RenderFillRect(m_sdlRenderer, &full);
    if (skinDraws(menu.overlaySkin.normal)) {
        paintSkin(full, menu.overlaySkin.normal, std::nullopt, std::nullopt, 0.0f, s);
    }

    // Panel + accent border (or the page's skin, or no panel at all).
    float panelX = 0.0f, panelY = 0.0f;
    toPhys(LM.panelX, LM.panelY, panelX, panelY);
    const SDL_FRect panel{panelX, panelY, LM.panelW * metrics.scaleFactor,
                          LM.panelH * metrics.scaleFactor};
    const UiSkinSet& panelSet = panelSkinFor(view.page);
    if (!panelSet.none) {
        paintSkin(panel, panelSet.normal, menu.panelFill, menu.panelBorder, menu.panelBorderWidth, s);
    }
    if (view.page == "title") drawTitleLogo(metrics);
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
                        float w, float h, const char* align = "Left") {
        if (m_offscreenSurface) {
            menuFont->renderText(m_offscreenSurface, text, x, y, px, color, w, h, align);
        } else {
            drawTrueTypeTextOnGpu(m_sdlRenderer, menuFont, nullptr, text, x, y, px,
                                  color, w, h, align);
        }
    };

    const auto sdl = [](const Rowl::State::UiColor& c) { return SDL_Color{c.r, c.g, c.b, c.a}; };
    const SDL_Color titleColor = sdl(screen.titleColor.value_or(menu.title));
    const SDL_Color textColor = sdl(menu.text);
    const SDL_Color dimColor = sdl(menu.dimText);
    const SDL_Color counterColor = sdl(screen.counterColor.value_or(menu.dimText));
    const float counterPx = screen.counterPx.value_or(24.0f);
    const SDL_Color selectedTextColor = sdl(menu.selectedText);
    const SDL_Color disabledColor = sdl(menu.disabledText);
    const SDL_Color hintColor = sdl(menu.hint);
    const SDL_Color historySpeakerColor = sdl(menu.historySpeaker);
    // D05-2: the renderer multiplies every font size by the accessibility text
    // scale. The fixed-pitch menu boxes (title, hint) must not outgrow their
    // rows, so those texts are drawn at their design size (the base is divided
    // by the scale); a row's label/value may grow with the scale up to what its
    // row box holds (see rowPx below). The backlog list keeps scaling: the engine
    // laid it out with the same factor. T2b: a slot row's detail line scales too -
    // the engine grew the row with the same factor (MenuLayout::textScale).
    const float accessScale = std::max(1.0f, menuFont->textScale());

    // ── T2 pass 1: everything that is not text (row skins, highlights, sliders) ──
    // D03-2: backlog page — engine-laid-out entries (wrapped lines, y/h in the
    // virtual canvas) instead of the fixed-pitch row list. An empty history
    // falls through to the generic loop with its single dim "history is empty" row.
    // T2b: slot cards replace the row list the same way.
    const bool backlogEntries = view.backlog.active && !view.backlog.entries.empty();
    const bool slotCards = !view.cards.empty();
    const int rows = (backlogEntries || slotCards) ? 0 : static_cast<int>(view.rows.size());
    const bool slotPage = view.page == "save" || view.page == "load";
    const UiSkinSet& pageRows = rowSkinFor(view.page);
    const auto setDraws = [&](const UiSkinSet& set) {
        if (skinDraws(set.normal)) return true;
        for (const auto& variant : set.variant) {
            if (variant && skinDraws(*variant)) return true;
        }
        return false;
    };
    const UiSkinSet& rowSet = (slotPage && setDraws(theme.slots.rowSkin)) ? theme.slots.rowSkin : pageRows;
    const bool sliderStyle = theme.preferences.valueStyle == "slider";

    // Paints one row's background: the state's skin (or the normal one), else the legacy highlight.
    // Returns the skin that dressed it (for its text inset), or null.
    const auto paintRowBackground = [&](const SDL_FRect& r, bool selected, bool enabled, int slotState) -> const UiSkin* {
        UiSkinState state = UiSkinState::Normal;
        if (selected) state = UiSkinState::Selected;
        else if (slotState == 3) state = UiSkinState::Corrupt;
        else if (slotState == 1) state = UiSkinState::Empty;
        else if (!enabled) state = UiSkinState::Disabled;
        const UiSkin* variant = state == UiSkinState::Normal ? nullptr : rowSet.find(state);
        const bool variantShows = variant != nullptr && skinDraws(*variant);
        const UiSkin& base = variantShows ? *variant : rowSet.normal;
        const bool baseDraws = skinDraws(base);
        if (baseDraws) paintSkin(r, base, std::nullopt, std::nullopt, 0.0f, s);
        if (selected && !variantShows) {
            // A row dressed by its normal skin keeps that picture: only the bar marks the selection.
            if (!baseDraws) {
                SDL_SetRenderDrawColor(m_sdlRenderer, menu.selectedFill.r, menu.selectedFill.g,
                                       menu.selectedFill.b, menu.selectedFill.a);
                SDL_RenderFillRect(m_sdlRenderer, &r);
            }
            const SDL_FRect bar{r.x, r.y, menu.selectedBarWidth * s, r.h};
            SDL_SetRenderDrawColor(m_sdlRenderer, menu.selectedBar.r, menu.selectedBar.g,
                                   menu.selectedBar.b, menu.selectedBar.a);
            if (bar.w > 0.0f) SDL_RenderFillRect(m_sdlRenderer, &bar);
        }
        return baseDraws ? &base : nullptr;
    };

    std::vector<float> rowInsetLeft(static_cast<size_t>(rows), LM.rowPadX);
    if (backlogEntries) {
        for (const auto& entry : view.backlog.entries) {
            float rx = 0.0f, ry = 0.0f;
            toPhys(LM.rowX, entry.y, rx, ry);
            const SDL_FRect box{rx, ry, LM.rowW * s, entry.h * s};
            paintRowBackground(box, entry.selected, true, 0);
        }
    }
    // T2b: slot cards - the card plate (row skin by slot state), the slot's picture and its frame.
    const float cardPad = 10.0f;
    auto cardThumbRect = [&](const Rowl::Core::PauseSlotCardView& card) {
        float cx = 0.0f, cy = 0.0f;
        toPhys(card.x + cardPad, card.y + cardPad, cx, cy);
        const float h = (card.h - 2.0f * cardPad) * s;
        return SDL_FRect{cx, cy, h * 16.0f / 9.0f, h};
    };
    for (const auto& card : view.cards) {
        float cx = 0.0f, cy = 0.0f;
        toPhys(card.x, card.y, cx, cy);
        const SDL_FRect box{cx, cy, card.w * s, card.h * s};
        paintRowBackground(box, card.selected, true, card.slotState);
        const SDL_FRect thumb = cardThumbRect(card);
        SDL_Texture* texture = card.thumbnailPng ? slotCardThumbnail(card.thumbnailKey, *card.thumbnailPng) : nullptr;
        bool drew = false;
        if (texture != nullptr) {
            SDL_RenderTexture(m_sdlRenderer, texture, nullptr, &thumb);
            drew = true;
        } else {
            const bool emptySlot = card.slotState == 1;
            const Rowl::State::UiSkin& newImage = theme.slots.newImage;
            const Rowl::State::UiSkin& pick = (view.page == "save" && emptySlot && !newImage.image.empty())
                ? newImage : theme.slots.thumbPlaceholder;
            drew = paintSkinImageOnly(thumb, pick, s);
            if (!drew) {  // nothing to show: a quiet plate keeps the picture box readable
                SDL_SetRenderDrawColor(m_sdlRenderer, menu.dimText.r, menu.dimText.g, menu.dimText.b, 40);
                SDL_RenderFillRect(m_sdlRenderer, &thumb);
            }
        }
        const Rowl::State::UiSkin& frame = theme.slots.thumbFrameSkin.normal;
        if (skinDraws(frame)) {
            const auto pad = frame.padding.value_or(std::array<float, 4>{0, 0, 0, 0});
            const SDL_FRect around{thumb.x - pad[0] * s, thumb.y - pad[1] * s, thumb.w + (pad[0] + pad[2]) * s,
                                   thumb.h + (pad[1] + pad[3]) * s};
            paintSkin(around, frame, std::nullopt, menu.thumbBorder, 1.0f, s);
        } else if (texture != nullptr) {
            SDL_SetRenderDrawColor(m_sdlRenderer, menu.thumbBorder.r, menu.thumbBorder.g, menu.thumbBorder.b,
                                   menu.thumbBorder.a);
            SDL_RenderRect(m_sdlRenderer, &thumb);
        }
    }
    // T2b: the "< Sayfa 1 / 3 >" strip of a paged slot list.
    if (view.pager.active) {
        float px = 0.0f, py = 0.0f;
        toPhys(view.pager.x, view.pager.y, px, py);
        paintRowBackground(SDL_FRect{px, py, view.pager.w * s, view.pager.h * s}, false, true, 0);
    }
    for (int i = 0; i < rows; ++i) {
        const auto& row = view.rows[static_cast<size_t>(i)];
        // D05-2: pages with more than comfortRows rows (Preferences) use the
        // compact pitch so the last row clears the hint line.
        float rx = 0.0f, ry = 0.0f;
        toPhys(LM.rowX, LM.rowTop(i, rows), rx, ry);
        const SDL_FRect box{rx, ry, LM.rowW * s, LM.rowHOf(rows) * s};
        const UiSkin* dressed = paintRowBackground(box, i == view.selected, row.enabled, row.slotState);
        if (dressed != nullptr && dressed->padding) rowInsetLeft[static_cast<size_t>(i)] = (*dressed->padding)[0];
        else if (rowSet.normal.padding) rowInsetLeft[static_cast<size_t>(i)] = (*rowSet.normal.padding)[0];
        if (sliderStyle && row.fraction >= 0.0f) {
            // T2: slider instead of the "100%" text. Track, filled part and knob are skins; without
            // images they are plain bars in the menu colours.
            const auto& prefs = theme.preferences;
            const float trackH = prefs.trackHeight * s;
            const float trackW = (LM.sliderX1() - LM.sliderX0()) * s;
            float tx0 = 0.0f, ty0 = 0.0f;
            toPhys(LM.sliderX0(), 0.0f, tx0, ty0);
            const float cy = ry + box.h * 0.5f;
            const SDL_FRect track{tx0, cy - trackH * 0.5f, trackW, trackH};
            Rowl::State::UiColor trackBase = menu.dimText;
            trackBase.a = 90;
            paintSkin(track, prefs.trackSkin.normal, trackBase, std::nullopt, 0.0f, s);
            const SDL_FRect filled{track.x, track.y, std::max(0.0f, trackW * row.fraction), trackH};
            if (filled.w >= 1.0f) paintSkin(filled, prefs.fillSkin.normal, menu.selectedBar, std::nullopt, 0.0f, s);
            const float knob = prefs.knobSize * s;
            const SDL_FRect knobRect{track.x + trackW * row.fraction - knob * 0.5f, cy - knob * 0.5f, knob, knob};
            const UiSkin& knobSkin = row.dragging ? prefs.knobSkin.pick(UiSkinState::Pressed) : prefs.knobSkin.normal;
            paintSkin(knobRect, knobSkin, menu.text, std::nullopt, 0.0f, s);
        }
    }
    if (m_isOffscreen) SDL_RenderPresent(m_sdlRenderer);

    // ── pass 2: text ──
    const auto alignName = [](Rowl::Core::MenuAlign a) { return Rowl::Core::menuAlignName(a); };
    float tx = 0.0f, ty = 0.0f;
    toPhys(LM.titleX, LM.titleY, tx, ty);
    if (!screen.titleHidden) {
        drawText(view.title, tx, ty, screen.titlePx.value_or(menu.titlePx) * metrics.scaleFactor / accessScale,
                 titleColor, LM.titleW * metrics.scaleFactor, LM.titleH * metrics.scaleFactor,
                 alignName(LM.titleAlign));
    }

    drawExtrasText(view, metrics);  // D07-2: counter + gallery captions

    if (view.backlog.active) {
        float cx = 0.0f, cy = 0.0f;
        toPhys(LM.counterX, LM.counterY, cx, cy);
        drawText(view.backlog.counter, cx, cy, counterPx * metrics.scaleFactor, counterColor,
                 LM.counterW * metrics.scaleFactor, 40.0f * metrics.scaleFactor);
    }
    if (backlogEntries) {
        // The font's accessibility scale grows glyphs and line pitch alike
        // (the engine laid the list out with the same factor).
        const float textScale = std::max(1.0f, menuFont->textScale());
        const float lineH = PL::kBacklogLineH * textScale;
        const float entryInset = rowSet.normal.padding ? (*rowSet.normal.padding)[0] : LM.backlogPadX;
        for (const auto& entry : view.backlog.entries) {
            float rx = 0.0f, ry = 0.0f;
            toPhys(LM.rowX, entry.y, rx, ry);
            const float rowW = LM.rowW * metrics.scaleFactor;
            const float textX = rx + entryInset * metrics.scaleFactor;
            // Pre-wrapped lines are drawn one by one inside a generous box so
            // the renderer never re-wraps them (width slack, one-line height).
            const float boxW = (LM.rowW - entryInset) * metrics.scaleFactor;
            const float boxH = lineH * 1.5f * metrics.scaleFactor;
            float lineY = ry + PL::kBacklogPadY * metrics.scaleFactor;
            if (!entry.speaker.empty()) {
                drawText(entry.speaker, textX, lineY, menu.historySpeakerPx * metrics.scaleFactor,
                         historySpeakerColor,
                         boxW * 0.7f, boxH);
            }
            if (entry.hasVoice) {
                // Small voice mark ("(ses)" / "(voice)") at the right edge of the first text row.
                drawText(view.backlog.voiceMark, rx + rowW - 74.0f * metrics.scaleFactor, lineY,
                         PL::kBacklogSmallPx * metrics.scaleFactor, dimColor,
                         64.0f * metrics.scaleFactor, boxH);
            }
            if (!entry.speaker.empty()) lineY += lineH * metrics.scaleFactor;
            for (const auto& line : entry.lines) {
                if (!line.empty()) {
                    drawText(line, textX, lineY, PL::kBacklogFontPx * metrics.scaleFactor,
                             textColor, boxW, boxH);
                }
                lineY += lineH * metrics.scaleFactor;
            }
        }
    }

    // T2b: slot cards - label + state on the first line, the date and the chapter / summary below,
    // each cut to the width the card leaves (ellipsis) so a long title never leaves the card.
    for (const auto& card : view.cards) {
        float cx = 0.0f, cy = 0.0f;
        toPhys(card.x, card.y, cx, cy);
        const SDL_FRect thumb = cardThumbRect(card);
        const float textX = thumb.x + thumb.w + 12.0f * s;
        const float textW = cx + card.w * s - textX - cardPad * s;
        if (textW < 8.0f * s) continue;
        const bool dimmed = card.slotState != 2;
        const SDL_Color labelColor = card.selected ? selectedTextColor : (dimmed ? disabledColor : textColor);
        // drawText sizes are design sizes (the renderer multiplies them by the text scale); the
        // line pitch below uses the size that ends up on screen.
        const float labelPx = menu.rowPx * s;
        const float smallPx = menu.detailPx * s;
        const float labelShown = menu.rowPx * accessScale * s;
        const float smallShown = menu.detailPx * accessScale * s;
        const float lineGap = 4.0f * s;
        float y = cy + cardPad * s;
        drawText(fitOverlayText(card.label, labelPx, textW), textX, y, labelPx, labelColor, textW, labelShown * 1.5f);
        y += labelShown + lineGap;
        if (!card.value.empty() && card.slotState != 2) {
            drawText(fitOverlayText(card.value, smallPx, textW), textX, y, smallPx, dimColor, textW, smallShown * 1.6f);
        }
        if (!card.date.empty()) {
            drawText(fitOverlayText(card.date, smallPx, textW), textX, y, smallPx, dimColor, textW, smallShown * 1.6f);
            y += smallShown * 1.3f + lineGap;
        }
        if (!card.summary.empty()) {
            drawText(fitOverlayText(card.summary, smallPx, textW), textX, y, smallPx, dimColor, textW,
                     smallShown * 1.6f);
        }
    }
    if (view.pager.active) {
        float px = 0.0f, py = 0.0f;
        toPhys(view.pager.x, view.pager.y, px, py);
        const float pagerPx = menu.rowPx * 0.85f * s;
        const float textY = py + view.pager.h * s * 0.5f - pagerPx * 0.35f;
        drawText(view.pager.label, px, textY, pagerPx / accessScale, dimColor, view.pager.w * s, view.pager.h * s,
                 "Center");
    }

    for (int i = 0; i < rows; ++i) {
        const auto& row = view.rows[static_cast<size_t>(i)];
        const float rowTop = LM.rowTop(i, rows);
        float rx = 0.0f, ry = 0.0f;
        toPhys(LM.rowX, rowTop, rx, ry);
        const float rowW = LM.rowW * metrics.scaleFactor;
        const float rowH = LM.rowHOf(rows) * metrics.scaleFactor;
        const bool selected = (i == view.selected);
        const float inset = rowInsetLeft[static_cast<size_t>(i)] * metrics.scaleFactor;
        // D02: a row with a detail line lifts label/value to make room for it;
        // detail-less rows keep the exact pre-D02 geometry.
        const bool hasDetail = !row.detail.empty();
        // D05-2: label/value follow the text scale only as far as the row box
        // holds them (60% of the row height). T2b: a detail row follows it too - the
        // engine grew that row by the same factor (B-12), so label, value and the
        // detail line all keep their proportions at 125% / 150%.
        const float rowPx = std::min(menu.rowPx * accessScale, LM.rowHOf(rows) * 0.6f);
        const float valuePx = std::min(menu.valuePx * accessScale, LM.rowHOf(rows) * 0.6f);
        const float fontPx = rowPx * metrics.scaleFactor;
        const float drawPx = fontPx / accessScale;  // what drawText is given
        const float valueDrawPx = valuePx * metrics.scaleFactor / accessScale;
        // The compact (12-row) pitch has 4 px less room under the glyphs.
        const float detailLift = hasDetail ? 14.0f * accessScale * metrics.scaleFactor : 0.0f;
        const float textY = ry + rowH * 0.5f - fontPx * 0.35f - detailLift -
                            (rows > LM.comfortRows ? 4.0f * metrics.scaleFactor : 0.0f);
        const SDL_Color labelColor = !row.enabled ? disabledColor : (selected ? selectedTextColor : textColor);
        // Label column. Left keeps the pre-T2b box (x = inset, width = column); a centred / right
        // label sits in the column minus both insets.
        const bool labelLeft = LM.labelAlign == Rowl::Core::MenuAlign::Left;
        const float labelW = labelLeft ? rowW * LM.labelWidth : rowW * LM.labelWidth - 2.0f * inset;
        drawText(row.label, rx + inset, textY, drawPx, labelColor, labelW, rowH, alignName(LM.labelAlign));
        if (!row.value.empty()) {
            if (sliderStyle && row.fraction >= 0.0f) {
                if (theme.preferences.showValue) {
                    float vx = 0.0f, vy = 0.0f;
                    toPhys(LM.sliderValuePos(), 0.0f, vx, vy);
                    drawText(row.value, vx, textY, valueDrawPx, dimColor, 100.0f * metrics.scaleFactor, rowH);
                }
            } else {
                // B-11: a value wider than its column (the Continue row's "Slot 3  2026-10-29 01:59" at 150%)
                // takes the room the label leaves instead of being cut: the box grows to the left up to
                // the label's end; only then is the text shortened with an ellipsis.
                float valueBoxX = rx + rowW * LM.valueX;
                float valueBoxW = rowW * LM.valueWidth;
                std::string valueText = row.value;
                const char* valueAlign = alignName(LM.valueAlign);
                const float needed = menuFont->measureTextWidth(valueText, valueDrawPx);
                if (needed > valueBoxW) {
                    const float right = valueBoxX + valueBoxW;
                    const float labelMeasured = menuFont->measureTextWidth(row.label, drawPx);
                    const float labelBoxX = rx + inset;
                    const float labelTextEnd = LM.labelAlign == Rowl::Core::MenuAlign::Left ? labelBoxX + labelMeasured
                                             : LM.labelAlign == Rowl::Core::MenuAlign::Center ? labelBoxX + (labelW + labelMeasured) * 0.5f
                                                                                              : labelBoxX + labelW;
                    const float labelEnd = labelTextEnd + 16.0f * metrics.scaleFactor;
                    const float room = std::max(valueBoxW, right - std::max(labelEnd, rx));
                    valueBoxX = right - room;
                    valueBoxW = room;
                    valueAlign = "Right";
                    if (needed > room) valueText = fitOverlayText(valueText, valueDrawPx, room);
                }
                drawText(valueText, valueBoxX, textY, valueDrawPx, dimColor, valueBoxW, rowH, valueAlign);
            }
        }
        if (hasDetail) {
            // B-12: the detail line is cut to the row (ellipsis) so a long chapter title never runs into the
            // next row or past the edge, and it scales with the text size like the label.
            const float detailPx = menu.detailPx * metrics.scaleFactor;  // the renderer scales it with the text
            const float detailW = rowW - 2.0f * inset;
            drawText(fitOverlayText(row.detail, detailPx, detailW), rx + inset,
                     ry + rowH - 21.0f * accessScale * metrics.scaleFactor, detailPx,
                     dimColor, detailW, 20.0f * accessScale * metrics.scaleFactor);
        }
    }

    if (!screen.hintHidden) {
        float hx = 0.0f, hy = 0.0f;
        toPhys(LM.hintX, LM.hintY, hx, hy);
        drawText(view.hint, hx, hy, menu.hintPx * metrics.scaleFactor / accessScale, hintColor,
                 LM.hintW * metrics.scaleFactor, 30.0f * metrics.scaleFactor, alignName(LM.hintAlign));
    }
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
        const Rowl::State::UiSkinSet* skin;
    };
    const auto sdl = [](const Rowl::State::UiColor& c) { return SDL_Color{c.r, c.g, c.b, c.a}; };
    // T2: the corner the capsules sit in (default top_right == the former place). They stack
    // away from the screen edge: SKIP left of AUTO on the right, right of it on the left.
    const bool bottom = theme.anchor.rfind("bottom", 0) == 0;
    const bool leftSide = theme.anchor.size() >= 4 && theme.anchor.compare(theme.anchor.size() - 4, 4, "left") == 0;
    const float badgeTop = bottom ? 1080.0f - kMarginTop - kBadgeH : kMarginTop;
    Badge badges[2];
    int count = 0;
    float left = leftSide ? kMarginRight : 1920.0f - kMarginRight - kBadgeW;
    const float step = leftSide ? kBadgeW + kBadgeGap : -(kBadgeW + kBadgeGap);
    if (autoBadge) {
        badges[count++] = {"AUTO", sdl(theme.autoFill), sdl(theme.autoText), left, &theme.autoSkin};
        left += step;
    }
    if (skipBadge) {
        badges[count++] = {"SKIP", sdl(theme.skipFill), sdl(theme.skipText), left, &theme.skipSkin};
    }

    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < count; ++i) {
        float px = 0.0f, py = 0.0f;
        AspectGuardian::virtualToPhysical(badges[i].left, badgeTop, metrics, px, py);
        const SDL_FRect rect{px, py, kBadgeW * metrics.scaleFactor, kBadgeH * metrics.scaleFactor};
        if (skinDraws(badges[i].skin->normal)) {
            paintSkin(rect, badges[i].skin->normal,
                      Rowl::State::UiColor{badges[i].fill.r, badges[i].fill.g, badges[i].fill.b, badges[i].fill.a},
                      std::nullopt, 0.0f, metrics.scaleFactor);
        } else {
            fillCapsule(m_sdlRenderer, rect, badges[i].fill);
        }
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
        AspectGuardian::virtualToPhysical(badges[i].left, badgeTop, metrics, px, py);
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
