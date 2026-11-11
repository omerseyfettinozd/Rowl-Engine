// window_skin.cpp — T2: skins (image + colours) for every engine-drawn piece of
// player UI, the theme's cursor and the dialogue continue indicator.
//
// A skin is drawn by three small steps (fill, image, border) so a surface can
// put its content between them (gallery thumbnails sit between the cell fill
// and the cell frame). `resolveSkin` decides what applies: WITH an image the
// surface's own colours are not drawn unless the skin names them; WITHOUT one
// (or when the image cannot be loaded) the surface keeps its T1 colours, so a
// project without images renders exactly as before.

#include "thirdparty/stb_image.h"
#include "rowl/render/window.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/render/aspect_guardian.hpp"
#include "rowl/render/ui_draw.hpp"
#include "rowl/text/text_shaper.hpp"
#include "rowl/vfs/vfs.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Rowl::Render {

namespace {

constexpr int kMaxImageDimension = 8192;
constexpr uint64_t kMaxImagePixels = 16ULL * 1024 * 1024;
constexpr int kMaxCursorSource = 512;
constexpr int kMaxCursorEdge = 128;
constexpr int kMaxTiles = 4096;

SDL_Color toSdl(const Rowl::State::UiColor& c) { return SDL_Color{c.r, c.g, c.b, c.a}; }

std::vector<std::string> imageCandidates(const std::string& path) {
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    const size_t slash = norm.find_last_of('/');
    const std::string bare = slash == std::string::npos ? norm : norm.substr(slash + 1);
    // Same probing order as Window::loadTexture.
    return {norm, bare, "images/" + bare, "images/" + norm, "Assets/images/" + bare, "Assets/images/" + norm,
            "Assets/" + bare, "Assets/" + norm};
}

float roundedInset(int row, int rows, float radius) {
    const float edge = std::min(static_cast<float>(row) + 0.5f, static_cast<float>(rows - row) - 0.5f);
    if (radius <= 0.0f || edge >= radius) return 0.0f;
    const float up = radius - edge;
    return std::round(radius - std::sqrt(std::max(0.0f, radius * radius - up * up)));
}

}  // namespace

void drawRoundedBox(SDL_Renderer* renderer, const SDL_FRect& r, float radius, SDL_Color fill,
                    SDL_Color border, int rings, bool drawFill) {
    const int rows = std::max(1, static_cast<int>(std::lround(r.h)));
    const float width = std::max(1.0f, std::round(r.w));
    const float x0 = std::round(r.x);
    const float top = std::floor(r.y);
    radius = std::clamp(radius, 0.0f, std::min(width, static_cast<float>(rows)) * 0.5f);
    const auto strip = [&](float left, float right, float row) {
        if (right - left < 1.0f) return;
        const SDL_FRect span{left, top + row, right - left, 1.0f};
        SDL_RenderFillRect(renderer, &span);
    };
    if (drawFill) {
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
        for (int row = 0; row < rows; ++row) {
            const float inset = roundedInset(row, rows, radius);
            strip(x0 + inset, x0 + width - inset, static_cast<float>(row));
        }
    }
    if (rings <= 0) return;
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    const int innerRows = rows - 2 * rings;
    const float innerRadius = std::max(0.0f, radius - static_cast<float>(rings));
    for (int row = 0; row < rows; ++row) {
        const float outer = roundedInset(row, rows, radius);
        const float left = x0 + outer;
        const float right = x0 + width - outer;
        const int innerRow = row - rings;
        if (innerRow >= 0 && innerRow < innerRows && width > 2.0f * rings) {
            const float inner = static_cast<float>(rings) + roundedInset(innerRow, innerRows, innerRadius);
            strip(left, std::max(left, x0 + inner), static_cast<float>(row));
            strip(std::min(right, x0 + width - inner), right, static_cast<float>(row));
        } else {
            strip(left, right, static_cast<float>(row));
        }
    }
}

// ── diagnostics and image probing ───────────────────────────────────────────

void Window::noteSkinDiagnostic(const std::string& line) {
    for (const auto& known : m_skinDiagnostics) {
        if (known == line) return;
    }
    if (m_skinDiagnostics.size() >= Rowl::State::kUiThemeMaxDiagnostics) return;
    m_skinDiagnostics.push_back(line);
    ROWL_LOG_WARN("[UiTheme] " + line);
}

void Window::forgetTexture(const std::string& path) {
    if (path.empty()) return;
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    if (const auto it = m_textureCache.find(norm); it != m_textureCache.end()) {
        SDL_Texture* texture = it->second;
        destroyCachedTexture(texture);
    }
    m_missingTextureCache.erase(norm);
    m_budgetRejectedTextureCache.erase(norm);
}

void Window::probeThemeImages() {
    m_skinDiagnostics.clear();
    for (const auto& [label, path] : Rowl::State::uiThemeImagePaths(m_theme)) {
        bool found = false;
        std::string why = "was not found in the project";
        for (const auto& candidate : imageCandidates(path)) {
            const auto bytes = vfs().readBytes(candidate);
            if (bytes.empty()) continue;
            int w = 0, h = 0, c = 0;
            if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c)) {
                why = "is not a readable image";
                continue;
            }
            if (w <= 0 || h <= 0 || w > kMaxImageDimension || h > kMaxImageDimension ||
                static_cast<uint64_t>(w) * static_cast<uint64_t>(h) > kMaxImagePixels) {
                why = "is too large (" + std::to_string(w) + "x" + std::to_string(h) + "; most 8192 px a side)";
                continue;
            }
            found = true;
            break;
        }
        if (!found) {
            const bool cursor = label == "cursor.image";
            noteSkinDiagnostic(label + ": image '" + path + "' " + why +
                               (cursor ? "; the system cursor stays" : "; drawn with colours instead"));
        }
    }
}

// ── painting ────────────────────────────────────────────────────────────────

Window::SkinPaint Window::resolveSkin(const Rowl::State::UiSkin& skin,
                                      const std::optional<Rowl::State::UiColor>& baseFill,
                                      const std::optional<Rowl::State::UiColor>& baseBorder,
                                      float baseBorderPx, float scale) {
    SkinPaint paint;
    paint.skin = &skin;
    if (!skin.image.empty()) paint.texture = loadTexture(skin.image);
    const bool dressed = paint.texture != nullptr;
    if (skin.fill) {
        paint.fillOn = true;
        paint.fill = toSdl(*skin.fill);
    } else if (!dressed && baseFill) {
        paint.fillOn = true;
        paint.fill = toSdl(*baseFill);
    }
    float borderPx = baseBorderPx;
    if (skin.border) {
        paint.borderOn = true;
        paint.border = toSdl(*skin.border);
        if (!skin.borderWidth && borderPx < 1.0f) borderPx = 1.0f;
    } else if (!dressed && baseBorder) {
        paint.borderOn = true;
        paint.border = toSdl(*baseBorder);
    }
    if (skin.borderWidth) borderPx = *skin.borderWidth;
    paint.rings = static_cast<int>(std::lround(borderPx));
    if (paint.rings <= 0) paint.borderOn = false;
    paint.radius = skin.cornerRadius ? *skin.cornerRadius * scale : 0.0f;
    return paint;
}

bool Window::skinDraws(const Rowl::State::UiSkin& skin) {
    if (skin.fill || skin.border) return true;
    return !skin.image.empty() && loadTexture(skin.image) != nullptr;
}

void Window::paintSkinFill(const SDL_FRect& rect, const SkinPaint& paint) {
    if (!paint.fillOn || !m_sdlRenderer) return;
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    if (paint.radius > 0.0f) {
        drawRoundedBox(m_sdlRenderer, rect, paint.radius, paint.fill, paint.fill, 0, true);
    } else {
        SDL_SetRenderDrawColor(m_sdlRenderer, paint.fill.r, paint.fill.g, paint.fill.b, paint.fill.a);
        SDL_RenderFillRect(m_sdlRenderer, &rect);
    }
}

void Window::paintSkinBorder(const SDL_FRect& rect, const SkinPaint& paint) {
    if (!paint.borderOn || !m_sdlRenderer) return;
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    if (paint.radius > 0.0f) {
        drawRoundedBox(m_sdlRenderer, rect, paint.radius, paint.border, paint.border, paint.rings, false);
    } else {
        SDL_SetRenderDrawColor(m_sdlRenderer, paint.border.r, paint.border.g, paint.border.b, paint.border.a);
        strokeRect(m_sdlRenderer, rect, paint.rings);
    }
}

void Window::paintSkinImage(const SDL_FRect& rect, const SkinPaint& paint, float scale) {
    SDL_Texture* texture = paint.texture;
    if (!texture || !paint.skin || !m_sdlRenderer || rect.w < 1.0f || rect.h < 1.0f) return;
    const Rowl::State::UiSkin& skin = *paint.skin;
    float tw = 0.0f, th = 0.0f;
    if (!SDL_GetTextureSize(texture, &tw, &th) || tw <= 0.0f || th <= 0.0f) return;

    const float alpha = std::clamp(skin.opacity, 0.0f, 1.0f) * static_cast<float>(skin.tint.a);
    SDL_SetTextureColorMod(texture, skin.tint.r, skin.tint.g, skin.tint.b);
    SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(std::lround(alpha)));
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(texture, skin.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);

    const float unit = std::max(0.01f, skin.sliceScale * scale);  // physical px per image px
    const auto blit = [&](const SDL_FRect& src, const SDL_FRect& dst) {
        if (src.w <= 0.0f || src.h <= 0.0f || dst.w <= 0.0f || dst.h <= 0.0f) return;
        SDL_RenderTexture(m_sdlRenderer, texture, &src, &dst);
    };
    const auto stretch = [&] { SDL_RenderTexture(m_sdlRenderer, texture, nullptr, &rect); };

    switch (skin.mode) {
        case Rowl::State::UiSkinMode::Stretch:
            stretch();
            break;
        case Rowl::State::UiSkinMode::Fit: {
            const float fit = std::min(rect.w / tw, rect.h / th);
            const SDL_FRect dst{rect.x + (rect.w - tw * fit) * 0.5f, rect.y + (rect.h - th * fit) * 0.5f, tw * fit,
                                th * fit};
            SDL_RenderTexture(m_sdlRenderer, texture, nullptr, &dst);
            break;
        }
        case Rowl::State::UiSkinMode::Cover: {
            const float cover = std::max(rect.w / tw, rect.h / th);
            const float sw = rect.w / cover, sh = rect.h / cover;
            blit(SDL_FRect{(tw - sw) * 0.5f, (th - sh) * 0.5f, sw, sh}, rect);
            break;
        }
        case Rowl::State::UiSkinMode::Center: {
            const float dw = tw * unit, dh = th * unit;
            SDL_FRect src{0.0f, 0.0f, tw, th};
            SDL_FRect dst{rect.x + (rect.w - dw) * 0.5f, rect.y + (rect.h - dh) * 0.5f, dw, dh};
            if (dw > rect.w) {
                src.w = rect.w / unit;
                src.x = (tw - src.w) * 0.5f;
                dst.x = rect.x;
                dst.w = rect.w;
            }
            if (dh > rect.h) {
                src.h = rect.h / unit;
                src.y = (th - src.h) * 0.5f;
                dst.y = rect.y;
                dst.h = rect.h;
            }
            blit(src, dst);
            break;
        }
        case Rowl::State::UiSkinMode::Tile: {
            const float stepX = std::max(1.0f, tw * unit), stepY = std::max(1.0f, th * unit);
            const int nx = static_cast<int>(std::ceil(rect.w / stepX));
            const int ny = static_cast<int>(std::ceil(rect.h / stepY));
            if (static_cast<int64_t>(nx) * ny > kMaxTiles) {  // a pathological tile size: do not flood the queue
                stretch();
                break;
            }
            for (int iy = 0; iy < ny; ++iy) {
                for (int ix = 0; ix < nx; ++ix) {
                    const float x = rect.x + static_cast<float>(ix) * stepX;
                    const float y = rect.y + static_cast<float>(iy) * stepY;
                    const float w = std::min(stepX, rect.x + rect.w - x);
                    const float h = std::min(stepY, rect.y + rect.h - y);
                    blit(SDL_FRect{0.0f, 0.0f, w / unit, h / unit}, SDL_FRect{x, y, w, h});
                }
            }
            break;
        }
        case Rowl::State::UiSkinMode::NineSlice: {
            float l = std::min(skin.slice[0], tw), t = std::min(skin.slice[1], th);
            float r = std::min(skin.slice[2], tw - l), b = std::min(skin.slice[3], th - t);
            if (l + r + t + b <= 0.0f) {
                stretch();
                break;
            }
            float dl = l * unit, dr = r * unit, dt = t * unit, db = b * unit;
            if (dl + dr > rect.w) {  // the frame is smaller than its own corners: shrink both sides alike
                const float f = rect.w / (dl + dr);
                dl *= f;
                dr *= f;
            }
            if (dt + db > rect.h) {
                const float f = rect.h / (dt + db);
                dt *= f;
                db *= f;
            }
            const float xs[4] = {std::round(rect.x), std::round(rect.x + dl), std::round(rect.x + rect.w - dr),
                                 std::round(rect.x + rect.w)};
            const float ys[4] = {std::round(rect.y), std::round(rect.y + dt), std::round(rect.y + rect.h - db),
                                 std::round(rect.y + rect.h)};
            const float sx[4] = {0.0f, l, tw - r, tw};
            const float sy[4] = {0.0f, t, th - b, th};
            for (int row = 0; row < 3; ++row) {
                for (int col = 0; col < 3; ++col) {
                    blit(SDL_FRect{sx[col], sy[row], sx[col + 1] - sx[col], sy[row + 1] - sy[row]},
                         SDL_FRect{xs[col], ys[row], xs[col + 1] - xs[col], ys[row + 1] - ys[row]});
                }
            }
            break;
        }
    }

    // The texture cache is shared with the scene: leave every texture the way we found it.
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
}

bool Window::paintSkin(const SDL_FRect& rect, const Rowl::State::UiSkin& skin,
                       const std::optional<Rowl::State::UiColor>& baseFill,
                       const std::optional<Rowl::State::UiColor>& baseBorder, float baseBorderPx, float scale) {
    const SkinPaint paint = resolveSkin(skin, baseFill, baseBorder, baseBorderPx, scale);
    paintSkinFill(rect, paint);
    paintSkinImage(rect, paint, scale);
    paintSkinBorder(rect, paint);
    return paint.texture != nullptr;
}

bool Window::paintSkinImageOnly(const SDL_FRect& rect, const Rowl::State::UiSkin& skin, float scale) {
    if (skin.image.empty()) return false;
    return paintSkin(rect, skin, std::nullopt, std::nullopt, 0.0f, scale);
}

const Rowl::State::UiSkinSet& Window::panelSkinFor(const std::string& page) const {
    const Rowl::State::UiTheme& theme = uiTheme();
    const int index = Rowl::State::uiScreenIndex(page);
    if (index >= 0 && theme.screens[static_cast<size_t>(index)].panel) {
        return *theme.screens[static_cast<size_t>(index)].panel;
    }
    return theme.menu.panelSkin;
}

const Rowl::State::UiSkinSet& Window::rowSkinFor(const std::string& page) const {
    const Rowl::State::UiTheme& theme = uiTheme();
    const int index = Rowl::State::uiScreenIndex(page);
    if (index >= 0 && theme.screens[static_cast<size_t>(index)].row) {
        return *theme.screens[static_cast<size_t>(index)].row;
    }
    return theme.menu.rowSkin;
}

// ── the theme's mouse cursor ────────────────────────────────────────────────

void Window::applyThemeCursor() {
    SDL_Cursor* previous = m_themeCursor;
    m_themeCursor = nullptr;
    if (m_sdlWindow == nullptr) return;  // offscreen / embedded surfaces have no pointer of their own
    const Rowl::State::UiCursorTheme& cursor = m_theme.cursor;
    if (!cursor.image.empty()) {
        for (const auto& candidate : imageCandidates(cursor.image)) {
            const auto bytes = vfs().readBytes(candidate);
            if (bytes.empty()) continue;
            int w = 0, h = 0, c = 0;
            if (!stbi_info_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c) || w <= 0 ||
                h <= 0 || w > kMaxCursorSource || h > kMaxCursorSource) {
                continue;
            }
            unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &c, 4);
            if (pixels == nullptr) continue;
            const float edgeCap = static_cast<float>(kMaxCursorEdge) / static_cast<float>(std::max(w, h));
            const float factor = std::min(cursor.scale, edgeCap);
            const int dw = std::max(1, static_cast<int>(std::lround(static_cast<float>(w) * factor)));
            const int dh = std::max(1, static_cast<int>(std::lround(static_cast<float>(h) * factor)));
            std::vector<unsigned char> scaled;
            const unsigned char* data = pixels;
            if (dw != w || dh != h) {  // nearest: cursors are small and often pixel art
                scaled.resize(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 4u);
                for (int y = 0; y < dh; ++y) {
                    for (int x = 0; x < dw; ++x) {
                        const int sx = std::min(w - 1, x * w / dw), sy = std::min(h - 1, y * h / dh);
                        std::copy_n(pixels + (static_cast<size_t>(sy) * w + sx) * 4u, 4,
                                    scaled.data() + (static_cast<size_t>(y) * dw + x) * 4u);
                    }
                }
                data = scaled.data();
            }
            if (SDL_Surface* surface = SDL_CreateSurfaceFrom(dw, dh, SDL_PIXELFORMAT_RGBA32,
                                                             const_cast<unsigned char*>(data), dw * 4)) {
                m_themeCursor = SDL_CreateColorCursor(
                    surface, std::clamp(static_cast<int>(std::lround(cursor.hotspotX * factor)), 0, dw - 1),
                    std::clamp(static_cast<int>(std::lround(cursor.hotspotY * factor)), 0, dh - 1));
                SDL_DestroySurface(surface);
            }
            stbi_image_free(pixels);
            break;
        }
    }
    SDL_SetCursor(m_themeCursor != nullptr ? m_themeCursor : SDL_GetDefaultCursor());
    if (previous != nullptr) SDL_DestroyCursor(previous);
}

// ── continue indicator ──────────────────────────────────────────────────────

bool Window::dialogueTextComplete(const DialogueRenderData& dlg, float, float) {
    if (!(dlg.isPlaying && dlg.typewriterEnabled && dlg.textSpeed > 0)) return true;
    FontRenderer* const font = getDialogueFontRenderer();
    if (font == nullptr || !font->isLoaded()) return true;
    const auto shaped = font->shapeTextShared(dlg.dialogue, dlg.fontSize, 0.0f, dlg.language);
    if (!shaped) return true;
    return Rowl::Text::evaluateReveal(*shaped, dlg.elapsedTypewriterTime, dlg.textSpeed).visibleUnits >=
           shaped->revealUnits.size();
}

bool Window::continueIndicatorAnimating(const std::vector<DialogueRenderData>& dialogues,
                                        const std::vector<ChoiceButtonRenderData>& choices) const {
    const auto& indicator = uiTheme().story.continueIndicator;
    if (indicator.skin.image.empty() || indicator.animation == "none") return false;
    if (m_camera && m_camera->reducedMotion()) return false;
    for (const auto& choice : choices) {
        if (choice.enabled) return false;  // the story waits for an answer, not for a click
    }
    for (const auto& dlg : dialogues) {
        if (dlg.hasDialogueBox && !dlg.dialogue.empty()) return true;
    }
    return false;
}

void Window::drawContinueIndicator(const DialogueRenderData& dlg, const SDL_FRect& box, float scale) {
    const auto& indicator = uiTheme().story.continueIndicator;
    if (indicator.skin.image.empty() || dlg.dialogue.empty()) return;
    if (!dialogueTextComplete(dlg, box.w, scale)) return;
    const bool still = m_camera && m_camera->reducedMotion();
    float lift = 0.0f;
    float opacity = 1.0f;
    if (!still && indicator.animation != "none") {
        const double period = std::max(0.2, static_cast<double>(indicator.period));
        const double phase = std::fmod(m_uiClock, period) / period;
        if (indicator.animation == "bounce") {
            lift = indicator.amplitude * scale * static_cast<float>(std::fabs(std::sin(3.14159265358979 * phase)));
        } else if (indicator.animation == "blink") {
            opacity = phase < 0.5 ? 1.0f : 0.15f;
        }
    }
    Rowl::State::UiSkin skin = indicator.skin;
    skin.opacity *= opacity;
    const float w = indicator.width * scale, h = indicator.height * scale;
    const SDL_FRect rect{box.x + box.w + indicator.offsetX * scale - w, box.y + box.h + indicator.offsetY * scale - h - lift,
                         w, h};
    SDL_SetRenderDrawBlendMode(m_sdlRenderer, SDL_BLENDMODE_BLEND);
    paintSkinImageOnly(rect, skin, scale);
}

}  // namespace Rowl::Render
