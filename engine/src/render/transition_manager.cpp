#include "rowl/render/transition_manager.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cmath>

namespace Rowl::Render {

constexpr float kMinDuration = 0.01f;
constexpr float kMaxDuration = 60.0f;

static void parseHexColor(const std::string& hex, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    std::string clean = hex;
    if (!clean.empty() && clean[0] == '#') clean = clean.substr(1);
    if (clean.size() == 3) {
        // #RGB -> #RRGGBB
        uint32_t val = static_cast<uint32_t>(std::strtoul(clean.c_str(), nullptr, 16));
        uint8_t r4 = static_cast<uint8_t>((val >> 8) & 0xF);
        uint8_t g4 = static_cast<uint8_t>((val >> 4) & 0xF);
        uint8_t b4 = static_cast<uint8_t>(val & 0xF);
        r = static_cast<uint8_t>((r4 << 4) | r4);
        g = static_cast<uint8_t>((g4 << 4) | g4);
        b = static_cast<uint8_t>((b4 << 4) | b4);
        a = 255;
    } else if (clean.size() == 4) {
        // #RGBA -> #RRGGBBAA
        uint32_t val = static_cast<uint32_t>(std::strtoul(clean.c_str(), nullptr, 16));
        uint8_t r4 = static_cast<uint8_t>((val >> 12) & 0xF);
        uint8_t g4 = static_cast<uint8_t>((val >> 8) & 0xF);
        uint8_t b4 = static_cast<uint8_t>((val >> 4) & 0xF);
        uint8_t a4 = static_cast<uint8_t>(val & 0xF);
        r = static_cast<uint8_t>((r4 << 4) | r4);
        g = static_cast<uint8_t>((g4 << 4) | g4);
        b = static_cast<uint8_t>((b4 << 4) | b4);
        a = static_cast<uint8_t>((a4 << 4) | a4);
    } else if (clean.size() == 6) {
        uint32_t val = static_cast<uint32_t>(std::strtoul(clean.c_str(), nullptr, 16));
        r = static_cast<uint8_t>((val >> 16) & 0xFF);
        g = static_cast<uint8_t>((val >> 8) & 0xFF);
        b = static_cast<uint8_t>(val & 0xFF);
        a = 255;
    } else if (clean.size() == 8) {
        uint32_t val = static_cast<uint32_t>(std::strtoul(clean.c_str(), nullptr, 16));
        r = static_cast<uint8_t>((val >> 24) & 0xFF);
        g = static_cast<uint8_t>((val >> 16) & 0xFF);
        b = static_cast<uint8_t>((val >> 8) & 0xFF);
        a = static_cast<uint8_t>(val & 0xFF);
    } else {
        r = 0; g = 0; b = 0; a = 255;
    }
}

TransitionManager::TransitionManager() = default;

TransitionManager::~TransitionManager() {
    cleanupSnapshot();
}

void TransitionManager::startTransition(TransitionType type,
                                       float durationSeconds,
                                       uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0f) {
        stopTransition();
        return;
    }
    m_type = type;
    m_duration = std::clamp(durationSeconds, kMinDuration, kMaxDuration);
    m_elapsed = 0.0f;
    m_progress = 0.0f;
    m_colorR = r;
    m_colorG = g;
    m_colorB = b;
    m_colorA = a;
}

void TransitionManager::startTransitionFromKind(const std::string& kind,
                                               float durationSeconds,
                                               const std::string& colorHex) {
    if (kind == "crossfade" || kind == "fade") {
        startTransition(TransitionType::CrossFade, durationSeconds);
    } else if (kind == "fade_black" || kind == "black") {
        startTransition(TransitionType::FadeToBlack, durationSeconds, 0, 0, 0, 255);
    } else if (kind == "fade_white" || kind == "white") {
        startTransition(TransitionType::FadeToWhite, durationSeconds, 255, 255, 255, 255);
    } else if (kind == "fade_color" || kind == "color") {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        parseHexColor(colorHex, r, g, b, a);
        startTransition(TransitionType::FadeToColor, durationSeconds, r, g, b, a);
    } else if (kind == "wipe_left") {
        startTransition(TransitionType::WipeLeft, durationSeconds);
    } else if (kind == "wipe_right") {
        startTransition(TransitionType::WipeRight, durationSeconds);
    } else {
        stopTransition();
    }
}

void TransitionManager::stopTransition() {
    completeTransition();
}

void TransitionManager::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0.0f) return;
    if (m_type == TransitionType::None) return;

    m_elapsed += dt;
    m_progress = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);

    if (m_progress >= 1.0f) {
        completeTransition();
    }
}

bool TransitionManager::captureSnapshot(SDL_Surface* surface, SDL_Renderer* renderer) {
    cleanupSnapshot();
    if (!renderer) return false;

    SDL_Surface* src = surface;
    SDL_Surface* tempSurface = nullptr;
    if (!src) {
        tempSurface = SDL_RenderReadPixels(renderer, nullptr);
        src = tempSurface;
    }

    if (!src) return false;

    m_snapshotTexture = SDL_CreateTextureFromSurface(renderer, src);
    if (m_snapshotTexture) {
        m_snapshotWidth = src->w;
        m_snapshotHeight = src->h;
    }

    if (tempSurface) {
        SDL_DestroySurface(tempSurface);
    }

    return m_snapshotTexture != nullptr;
}

void TransitionManager::renderTransition(SDL_Renderer* renderer, const ViewportMetrics& metrics) {
    if (!renderer || m_type == TransitionType::None || m_progress >= 1.0f) return;

    SDL_FRect dst = {
        static_cast<float>(metrics.x),
        static_cast<float>(metrics.y),
        static_cast<float>(metrics.width),
        static_cast<float>(metrics.height)
    };

    switch (m_type) {
        case TransitionType::CrossFade: {
            if (m_snapshotTexture) {
                Uint8 alpha = static_cast<Uint8>(std::clamp((1.0f - m_progress) * 255.0f, 0.0f, 255.0f));
                SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_BLEND);
                SDL_SetTextureAlphaMod(m_snapshotTexture, alpha);
                SDL_RenderTexture(renderer, m_snapshotTexture, nullptr, &dst);
            }
            break;
        }
        case TransitionType::FadeToBlack:
        case TransitionType::FadeToWhite:
        case TransitionType::FadeToColor: {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const float maxAlpha = static_cast<float>(m_colorA);
            if (m_progress < 0.5f) {
                // First half: Old scene fading into target color
                if (m_snapshotTexture) {
                    SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                    SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                    SDL_RenderTexture(renderer, m_snapshotTexture, nullptr, &dst);
                }
                float alphaFactor = m_progress / 0.5f;
                Uint8 a = static_cast<Uint8>(std::clamp(alphaFactor * maxAlpha, 0.0f, 255.0f));
                SDL_SetRenderDrawColor(renderer, m_colorR, m_colorG, m_colorB, a);
                SDL_RenderFillRect(renderer, &dst);
            } else {
                // Second half: New scene emerging from target color
                float alphaFactor = 1.0f - ((m_progress - 0.5f) / 0.5f);
                Uint8 a = static_cast<Uint8>(std::clamp(alphaFactor * maxAlpha, 0.0f, 255.0f));
                SDL_SetRenderDrawColor(renderer, m_colorR, m_colorG, m_colorB, a);
                SDL_RenderFillRect(renderer, &dst);
            }
            break;
        }
        case TransitionType::WipeLeft: {
            if (m_snapshotTexture && m_snapshotWidth > 0 && m_snapshotHeight > 0) {
                float remain = 1.0f - m_progress;
                SDL_FRect src = {
                    0.0f, 0.0f,
                    static_cast<float>(m_snapshotWidth) * remain,
                    static_cast<float>(m_snapshotHeight)
                };
                SDL_FRect wipeDst = {
                    dst.x, dst.y,
                    dst.w * remain, dst.h
                };
                SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                SDL_RenderTexture(renderer, m_snapshotTexture, &src, &wipeDst);
            }
            break;
        }
        case TransitionType::WipeRight: {
            if (m_snapshotTexture && m_snapshotWidth > 0 && m_snapshotHeight > 0) {
                float remain = 1.0f - m_progress;
                float offset = m_progress;
                SDL_FRect src = {
                    static_cast<float>(m_snapshotWidth) * offset, 0.0f,
                    static_cast<float>(m_snapshotWidth) * remain,
                    static_cast<float>(m_snapshotHeight)
                };
                SDL_FRect wipeDst = {
                    dst.x + dst.w * offset, dst.y,
                    dst.w * remain, dst.h
                };
                SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                SDL_RenderTexture(renderer, m_snapshotTexture, &src, &wipeDst);
            }
            break;
        }
        case TransitionType::None:
        default:
            break;
    }
}

void TransitionManager::completeTransition() {
    m_type = TransitionType::None;
    m_progress = 1.0f;
    cleanupSnapshot();
}

void TransitionManager::cleanupSnapshot() {
    if (m_snapshotTexture) {
        SDL_DestroyTexture(m_snapshotTexture);
        m_snapshotTexture = nullptr;
    }
    m_snapshotWidth = 0;
    m_snapshotHeight = 0;
}

void TransitionManager::reset() {
    stopTransition();
}

} // namespace Rowl::Render
