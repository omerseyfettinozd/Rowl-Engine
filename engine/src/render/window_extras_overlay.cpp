// window_extras_overlay.cpp — D07-2: drawing of the title screen's Ekstralar
// pages (the gallery's thumbnail grid and full-screen viewer) and of the
// achievement toast. The page structure (rows, hint, title) rides the generic
// pause overlay; this TU adds what rows cannot: images and the capsule.
//
// A locked gallery cell arrives WITHOUT an image path (the engine view never
// carries one), so nothing of a locked picture can reach the renderer.

#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/font_renderer.hpp"
#include "rowl/render/gpu_truetype_text.hpp"
#include "rowl/vfs/vfs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Rowl::Render {

namespace {

constexpr size_t kMaxGalleryThumbs = 24;       // >= one page (16 cells) plus a few
constexpr size_t kMaxGalleryThumbFailures = 128;
constexpr int kGalleryThumbMaxW = 356;         // 2x the 178 px cell: sharp on HiDPI
constexpr int kGalleryThumbMaxH = 200;
constexpr int kMaxThumbDecodesPerFrame = 4;    // a first page fills over a few frames
constexpr int kMaxSourceDimension = 8192;
constexpr uint64_t kMaxSourcePixels = 16ULL * 1024 * 1024;

const SDL_Color kAccent{56, 189, 248, 255};
const SDL_Color kLightText{241, 245, 249, 255};
const SDL_Color kDimText{148, 163, 184, 255};

// Area-average downscale of RGBA8 (no upscaling). Returns w*h*4 bytes.
std::vector<unsigned char> boxDownscale(const unsigned char* src, int sw, int sh, int dw, int dh) {
    std::vector<unsigned char> out(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u);
    for (int dy = 0; dy < dh; ++dy) {
        const int y0 = static_cast<int>((static_cast<int64_t>(dy) * sh) / dh);
        const int y1 = std::max(y0 + 1, static_cast<int>((static_cast<int64_t>(dy + 1) * sh) / dh));
        for (int dx = 0; dx < dw; ++dx) {
            const int x0 = static_cast<int>((static_cast<int64_t>(dx) * sw) / dw);
            const int x1 = std::max(x0 + 1, static_cast<int>((static_cast<int64_t>(dx + 1) * sw) / dw));
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1; ++y) {
                const unsigned char* row = src + (static_cast<size_t>(y) * sw + x0) * 4u;
                for (int x = x0; x < x1; ++x, row += 4) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    sum[3] += row[3];
                }
            }
            const uint32_t count = static_cast<uint32_t>((y1 - y0) * (x1 - x0));
            unsigned char* dst = out.data() + (static_cast<size_t>(dy) * dw + dx) * 4u;
            for (int c = 0; c < 4; ++c) dst[c] = static_cast<unsigned char>((sum[c] + count / 2) / count);
        }
    }
    return out;
}

}  // namespace

void Window::releaseGalleryThumbnails() {
    for (auto& [path, thumb] : m_galleryThumbs) {
        if (thumb.texture) SDL_DestroyTexture(thumb.texture);
    }
    m_galleryThumbs.clear();
    m_galleryThumbFailed.clear();
    m_galleryThumbClock = 0;
}

SDL_Texture* Window::galleryThumbnail(const std::string& filename) {
    if (filename.empty() || !m_sdlRenderer) return nullptr;
    std::string normPath = filename;
    std::replace(normPath.begin(), normPath.end(), '\\', '/');

    if (const auto hit = m_galleryThumbs.find(normPath); hit != m_galleryThumbs.end()) {
        hit->second.lastUsed = ++m_galleryThumbClock;
        return hit->second.texture;
    }
    if (m_galleryThumbFailed.contains(normPath)) return nullptr;
    if (m_galleryThumbDecodesThisFrame >= kMaxThumbDecodesPerFrame) return nullptr;  // later frame
    ++m_galleryThumbDecodesThisFrame;

    // Same VFS probing order as loadTexture: project assets only, never the CWD.
    const size_t lastSlash = normPath.find_last_of('/');
    const std::string bareName = lastSlash == std::string::npos ? normPath : normPath.substr(lastSlash + 1);
    const std::vector<std::string> candidates = {
        normPath,           bareName,
        "images/" + bareName, "images/" + normPath,
        "Assets/images/" + bareName, "Assets/images/" + normPath,
        "Assets/" + bareName, "Assets/" + normPath};

    unsigned char* pixels = nullptr;
    int width = 0, height = 0, channels = 0;
    for (const auto& candidate : candidates) {
        const auto bytes = vfs().readBytes(candidate);
        if (bytes.empty()) continue;
        int infoW = 0, infoH = 0, infoC = 0;
        if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &infoW, &infoH, &infoC)) continue;
        if (infoW <= 0 || infoH <= 0 || infoW > kMaxSourceDimension || infoH > kMaxSourceDimension ||
            static_cast<uint64_t>(infoW) * static_cast<uint64_t>(infoH) > kMaxSourcePixels) {
            ROWL_LOG_WARN("Rejected gallery image with unsafe dimensions: " + filename);
            continue;
        }
        pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (pixels) break;
    }

    SDL_Texture* texture = nullptr;
    if (pixels) {
        const float fit = std::min({1.0f, static_cast<float>(kGalleryThumbMaxW) / static_cast<float>(width),
                                    static_cast<float>(kGalleryThumbMaxH) / static_cast<float>(height)});
        const int dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(width) * fit)));
        const int dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(height) * fit)));
        std::vector<unsigned char> small;
        const unsigned char* data = pixels;
        int tw = width, th = height;
        if (dw != width || dh != height) {
            small = boxDownscale(pixels, width, height, dw, dh);
            data = small.data();
            tw = dw;
            th = dh;
        }
        // The surface wraps `data`; the texture copies it, so both can go right after.
        if (SDL_Surface* surface = SDL_CreateSurfaceFrom(tw, th, SDL_PIXELFORMAT_RGBA32,
                                                         const_cast<unsigned char*>(data), tw * 4)) {
            texture = SDL_CreateTextureFromSurface(m_sdlRenderer, surface);
            SDL_DestroySurface(surface);
        }
        stbi_image_free(pixels);
    }
    if (!texture) {
        if (m_galleryThumbFailed.size() >= kMaxGalleryThumbFailures) m_galleryThumbFailed.clear();
        m_galleryThumbFailed.insert(normPath);
        return nullptr;
    }
    while (m_galleryThumbs.size() >= kMaxGalleryThumbs) {
        auto oldest = m_galleryThumbs.begin();
        for (auto it = m_galleryThumbs.begin(); it != m_galleryThumbs.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        }
        // SDL flushes queued commands that still use a texture before destroying it.
        SDL_DestroyTexture(oldest->second.texture);
        m_galleryThumbs.erase(oldest);
    }
    m_galleryThumbs[normPath] = GalleryThumb{texture, ++m_galleryThumbClock};
    return texture;
}

void Window::drawOverlayText(const std::string& text, float x, float y, float px, SDL_Color color,
                             float w, float h, const char* align) {
    if (text.empty() || !m_fontRenderer || !m_fontRenderer->isLoaded()) return;
    if (m_offscreenSurface) {
        m_fontRenderer->renderText(m_offscreenSurface, text, x, y, px, color, w, h, align);
    } else {
        drawTrueTypeTextOnGpu(m_sdlRenderer, m_fontRenderer.get(), nullptr, text, x, y, px, color, w, h, align);
    }
}

// Cuts `text` (UTF-8 safe, whole codepoints) until it fits maxWidth, "..." appended.
std::string Window::fitOverlayText(const std::string& text, float px, float maxWidth) {
    if (!m_fontRenderer || !m_fontRenderer->isLoaded() || text.empty()) return text;
    if (m_fontRenderer->measureTextWidth(text, px) <= maxWidth) return text;
    std::string cut = text;
    while (!cut.empty()) {
        size_t end = cut.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(cut[end]) & 0xC0) == 0x80) --end;
        cut.resize(end);
        const std::string candidate = cut + "...";
        if (m_fontRenderer->measureTextWidth(candidate, px) <= maxWidth) return candidate;
    }
    return "...";
}

void Window::drawExtrasGraphics(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics) {
    using LM = Rowl::Core::PauseMenuLayout;
    const auto& extras = view.extras;
    m_galleryThumbDecodesThisFrame = 0;
    if (!extras.active || extras.viewing) return;
    const float s = metrics.scaleFactor;
    if (extras.page == "achievements") {
        // Two-line rows: a plate per row, the selected one in the menu highlight
        // colour with the accent bar; the plate covers both text lines.
        SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
        for (const auto& item : extras.items) {
            float px = 0.0f, py = 0.0f;
            AspectGuardian::virtualToPhysical(Rowl::Core::PauseMenuLayout::kRowX, item.y, metrics, px, py);
            const SDL_FRect plate{px, py, Rowl::Core::PauseMenuLayout::kRowW * s, item.h * s};
            if (item.selected) SDL_SetRenderDrawColor(m_sdlRenderer, 14, 116, 144, 255);
            else SDL_SetRenderDrawColor(m_sdlRenderer, 22, 25, 42, 255);
            SDL_RenderFillRect(m_sdlRenderer, &plate);
            if (item.selected) {
                const SDL_FRect bar{px, py, 6.0f * s, item.h * s};
                SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
                SDL_RenderFillRect(m_sdlRenderer, &bar);
            }
        }
        return;
    }
    if (extras.page != "gallery") return;
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    for (const auto& cell : extras.cells) {
        float px = 0.0f, py = 0.0f;
        AspectGuardian::virtualToPhysical(cell.x, cell.y, metrics, px, py);
        const SDL_FRect thumb{px, py, cell.w * s, LM::kGalleryThumbH * s};
        if (cell.selected) {
            // Highlight plate behind the whole cell (thumbnail + caption), like a menu row.
            const float pad = 4.0f * s;
            const SDL_FRect plate{px - pad, py - pad, cell.w * s + 2.0f * pad, cell.h * s + 2.0f * pad};
            SDL_SetRenderDrawColor(m_sdlRenderer, 14, 116, 144, 255);
            SDL_RenderFillRect(m_sdlRenderer, &plate);
        }
        if (!cell.thumbnail.empty()) {  // the engine view gives a path to unlocked cells only
            SDL_SetRenderDrawColor(m_sdlRenderer, 8, 10, 18, 255);
            SDL_RenderFillRect(m_sdlRenderer, &thumb);
            if (SDL_Texture* texture = galleryThumbnail(cell.thumbnail)) {
                float tw = 0.0f, th = 0.0f;
                if (SDL_GetTextureSize(texture, &tw, &th) && tw > 0.0f && th > 0.0f) {
                    const float fit = std::min(thumb.w / tw, thumb.h / th);
                    const SDL_FRect dst{thumb.x + (thumb.w - tw * fit) * 0.5f,
                                        thumb.y + (thumb.h - th * fit) * 0.5f, tw * fit, th * fit};
                    SDL_RenderTexture(m_sdlRenderer, texture, nullptr, &dst);
                }
            }
        } else {
            // Locked: a flat dark box, never any part of the picture.
            SDL_SetRenderDrawColor(m_sdlRenderer, 22, 25, 40, 255);
            SDL_RenderFillRect(m_sdlRenderer, &thumb);
        }
        const SDL_Color border = cell.selected ? kAccent
                               : cell.unlocked ? SDL_Color{71, 85, 105, 255}
                                               : SDL_Color{46, 56, 78, 255};
        SDL_SetRenderDrawColor(m_sdlRenderer, border.r, border.g, border.b, border.a);
        SDL_RenderRect(m_sdlRenderer, &thumb);
    }
}

void Window::drawExtrasText(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics) {
    using LM = Rowl::Core::PauseMenuLayout;
    const auto& extras = view.extras;
    if (!extras.active || extras.page == "root" || !m_fontRenderer || !m_fontRenderer->isLoaded()) return;
    const float s = metrics.scaleFactor;
    const float accessScale = std::max(1.0f, m_fontRenderer->textScale());
    float cx = 0.0f, cy = 0.0f;
    AspectGuardian::virtualToPhysical(LM::kRowX + LM::kRowW - 150.0f, 178.0f, metrics, cx, cy);
    drawOverlayText(extras.counter, cx, cy, 24.0f * s / accessScale, kDimText, 150.0f * s, 40.0f * s, "Left");
    if (extras.page == "achievements") {
        const float rowW = LM::kRowW * s;
        for (const auto& item : extras.items) {
            float px = 0.0f, py = 0.0f;
            AspectGuardian::virtualToPhysical(LM::kRowX, item.y, metrics, px, py);
            const float padX = LM::kAchPadX * s;
            const float titlePx = LM::kAchTitlePx * s / accessScale;
            const float descPx = LM::kAchDescPx * s / accessScale;
            const float datePx = LM::kAchDatePx * s / accessScale;
            // The date sits at the right edge on the title line; the title gets
            // the width it leaves.
            float dateW = 0.0f;
            if (!item.at.empty()) dateW = m_fontRenderer->measureTextWidth(item.at, datePx);
            const float titleMaxW = rowW - 2.0f * padX - (dateW > 0.0f ? dateW + 16.0f * s : 0.0f);
            const SDL_Color titleColour = item.selected ? kAccent : (item.unlocked ? kLightText : kDimText);
            drawOverlayText(fitOverlayText(item.title, titlePx, titleMaxW), px + padX,
                            py + LM::kAchTitleTop * s, titlePx, titleColour, titleMaxW + 4.0f * s,
                            LM::kAchTitlePx * 1.6f * s, "Left");
            if (dateW > 0.0f) {
                drawOverlayText(item.at, px + rowW - padX - dateW,
                                py + (LM::kAchTitleTop + 3.0f) * s, datePx, kDimText, dateW + 8.0f * s,
                                LM::kAchDatePx * 1.6f * s, "Left");
            }
            const float descMaxW = rowW - 2.0f * padX;
            drawOverlayText(fitOverlayText(item.detail, descPx, descMaxW), px + padX,
                            py + LM::kAchDescTop * s, descPx, item.unlocked ? SDL_Color{203, 213, 225, 255} : kDimText,
                            descMaxW + 4.0f * s, LM::kAchDescPx * 1.6f * s, "Left");
        }
        return;
    }
    if (extras.page != "gallery") return;
    // Everything drawn so far went through the renderer queue: land it before
    // the direct surface writes below (offscreen path).
    if (m_isOffscreen) SDL_RenderPresent(m_sdlRenderer);
    for (const auto& cell : extras.cells) {
        float px = 0.0f, py = 0.0f;
        AspectGuardian::virtualToPhysical(cell.x, cell.y, metrics, px, py);
        if (!cell.unlocked) {
            const float markPx = 44.0f * s / accessScale;
            const float boxH = LM::kGalleryThumbH * s;
            drawOverlayText("?", px, py + boxH * 0.5f - markPx * 0.69f * accessScale, markPx, kDimText,
                            cell.w * s, boxH, "Center");
        }
        const float captionPx = 16.0f * s / accessScale;
        const std::string caption = fitOverlayText(cell.title, captionPx, cell.w * s - 4.0f * s);
        drawOverlayText(caption, px + 2.0f * s, py + (LM::kGalleryThumbH + 7.0f) * s, captionPx,
                        cell.selected ? kLightText : (cell.unlocked ? kLightText : kDimText),
                        cell.w * s, LM::kGalleryCaptionH * s, "Left");
    }
}

void Window::renderGalleryViewer(const Rowl::Core::PauseMenuView& view, const ViewportMetrics& metrics) {
    const float s = metrics.scaleFactor;
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(m_sdlRenderer, 0, 0, 0, 240);
    const SDL_FRect full{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height)};
    SDL_RenderFillRect(m_sdlRenderer, &full);

    // Aspect-preserving fit, centred, in the virtual area above the caption.
    constexpr float kAreaX = 20.0f, kAreaY = 20.0f, kAreaW = 1880.0f, kAreaH = 930.0f;
    if (SDL_Texture* texture = loadTexture(view.extras.viewImage)) {
        float tw = 0.0f, th = 0.0f;
        if (SDL_GetTextureSize(texture, &tw, &th) && tw > 0.0f && th > 0.0f) {
            const float fit = std::min(kAreaW / tw, kAreaH / th);
            const float w = tw * fit, h = th * fit;
            float px = 0.0f, py = 0.0f;
            AspectGuardian::virtualToPhysical(kAreaX + (kAreaW - w) * 0.5f, kAreaY + (kAreaH - h) * 0.5f,
                                              metrics, px, py);
            const SDL_FRect dst{px, py, w * s, h * s};
            SDL_RenderTexture(m_sdlRenderer, texture, nullptr, &dst);
            SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 255);
            SDL_RenderRect(m_sdlRenderer, &dst);
        }
    }
    // The viewer repaints the whole frame every time (see renderPauseMenuOverlay).
    invalidateFrameCache();
    if (m_isOffscreen) SDL_RenderPresent(m_sdlRenderer);
    if (!m_fontRenderer || !m_fontRenderer->isLoaded()) return;
    const float accessScale = std::max(1.0f, m_fontRenderer->textScale());
    float tx = 0.0f, ty = 0.0f;
    AspectGuardian::virtualToPhysical(0.0f, 968.0f, metrics, tx, ty);
    drawOverlayText(view.extras.viewTitle, tx, ty, 30.0f * s / accessScale, kLightText, 1920.0f * s, 44.0f * s,
                    "Center");
    AspectGuardian::virtualToPhysical(0.0f, 1030.0f, metrics, tx, ty);
    drawOverlayText(view.hint, tx, ty, 19.0f * s / accessScale, kDimText, 1920.0f * s, 30.0f * s, "Center");
}

void Window::renderUnlockToast(const std::string& text) {
    if (!m_sdlRenderer || text.empty()) return;
    // Achievement toast: a semi-transparent capsule, top-centre of the 1920x1080
    // canvas ("Basarim: <baslik>"). Palette = the badges' (navy / accent cyan /
    // light text) so it reads as part of the same player chrome.
    constexpr float kCapsuleH = 56.0f;
    constexpr float kMarginTop = 28.0f;
    constexpr float kPadX = 30.0f;
    constexpr float kMaxTextW = 1300.0f;
    const ViewportMetrics metrics = AspectGuardian::calculateViewport(m_width, m_height, 1920, 1080);
    if (metrics.scaleFactor <= 0.0f) return;
    const float s = metrics.scaleFactor;
    const bool haveFont = m_fontRenderer && m_fontRenderer->isLoaded();
    const float accessScale = haveFont ? std::max(1.0f, m_fontRenderer->textScale()) : 1.0f;
    const float px = 26.0f * s / accessScale;  // what the font renderer is given

    // Without a font the capsule still shows (a fixed width), just without text.
    std::string shown = text;
    float textW = 520.0f * s;
    if (haveFont) {
        shown = fitOverlayText(text, px, kMaxTextW * s);
        textW = m_fontRenderer->measureTextWidth(shown, px);
    }
    const float capsuleW = std::min(textW + 2.0f * kPadX * s, (kMaxTextW + 2.0f * kPadX) * s);
    float cx = 0.0f, cy = 0.0f;
    AspectGuardian::virtualToPhysical(960.0f, kMarginTop, metrics, cx, cy);
    const SDL_FRect capsule{cx - capsuleW * 0.5f, cy, capsuleW, kCapsuleH * s};

    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    fillCapsule(m_sdlRenderer, capsule, SDL_Color{15, 15, 26, 215});
    // Thin accent rule along the bottom edge.
    const SDL_FRect rule{capsule.x + 14.0f * s, capsule.y + capsule.h - 4.0f * s, capsule.w - 28.0f * s, 2.0f * s};
    SDL_SetRenderDrawColor(m_sdlRenderer, 56, 189, 248, 190);
    SDL_RenderFillRect(m_sdlRenderer, &rule);
    // Offscreen software path: flush queued rects before direct text writes.
    if (m_isOffscreen) SDL_RenderPresent(m_sdlRenderer);
    if (!haveFont) return;

    // "Basarim:" in the accent colour, the title in light text.
    const size_t colon = shown.find(':');
    const std::string head = colon == std::string::npos ? std::string() : shown.substr(0, colon + 1);
    const std::string tail = colon == std::string::npos ? shown : shown.substr(colon + 1);
    const float headW = head.empty() ? 0.0f : m_fontRenderer->measureTextWidth(head, px);
    const float fontPx = px * accessScale;  // effective size, for the vertical centre
    const float textY = capsule.y + capsule.h * 0.5f - fontPx * 0.69f;
    const float left = capsule.x + (capsule.w - textW) * 0.5f;
    if (!head.empty()) drawOverlayText(head, left, textY, px, kAccent, headW + 8.0f * s, capsule.h, "Left");
    drawOverlayText(tail, left + headW, textY, px, kLightText, capsule.w - (left - capsule.x) - headW, capsule.h,
                    "Left");
}

}  // namespace Rowl::Render
