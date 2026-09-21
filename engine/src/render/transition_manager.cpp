#include "rowl/render/transition_manager.hpp"
#include "rowl/text/hex_color.hpp"
#include "rowl/core/logger.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_set>

namespace Rowl::Render {

constexpr float kMinDuration = 0.01f;
constexpr float kMaxDuration = 60.0f;

// A3-tur7 (hygiene): transition-hex yorum-çelişkisi kapatıldı — window.cpp'deki
// warnTaggedOnce çekirdeği bu dosyaya taşınmaz (çapraz-bağımlılık yok, bilinçli
// ikizlilik); tek-etiketli dosya-yerel emsal, aynı 32-cap deseniyle.
constexpr size_t kTransitionHexWarnCap = 32;

void warnTransitionHexOnce(const std::string& hex) {
    static std::mutex mutex;
    static std::unordered_set<std::string> warned;
    std::lock_guard<std::mutex> lock(mutex);
    if (warned.size() >= kTransitionHexWarnCap || !warned.insert(hex).second) return;
    ROWL_LOG_WARN("Invalid transition hex color '" + hex +
                  "'; using black fallback (logged once per value)");
}

static void parseHexColor(const std::string& hex, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& a) {
    // Faz 4.5 Dilim 2: tek birlesik cozucu (rowl/text/hex_color.hpp).
    // Gecis yedegi tarihsel siyahtir (0,0,0,255); bozuk girdi yedege duser
    // ve basarisizlik gozlenebilir (deger-basi tek WARN, spam yok).
    bool ok = false;
    const Rowl::Text::HexColor parsed = Rowl::Text::parseHexColor(
        hex, Rowl::Text::HexColor{0, 0, 0, 255}, &ok);
    if (!ok) warnTransitionHexOnce(hex);
    r = parsed.r;
    g = parsed.g;
    b = parsed.b;
    a = parsed.a;
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

// D6-#147: tür eşlemesi tek noktada — kapılar (isKnownKind,
// shouldCoalesceRetrigger) ve başlatma aynı tabloyu okur, kayma olmaz.
static TransitionType typeForKind(const std::string& kind) {
    if (kind == "crossfade" || kind == "fade") return TransitionType::CrossFade;
    if (kind == "fade_black" || kind == "black") return TransitionType::FadeToBlack;
    if (kind == "fade_white" || kind == "white") return TransitionType::FadeToWhite;
    if (kind == "fade_color" || kind == "color") return TransitionType::FadeToColor;
    if (kind == "wipe_left") return TransitionType::WipeLeft;
    if (kind == "wipe_right") return TransitionType::WipeRight;
    return TransitionType::None;
}

// D6-#147: erken aynı-tür yeniden tetikleme (%90 ilerleme altı) yoksayılır;
// geçiş baştan başlamaz, snapshot tazelenmez.
constexpr float kRetriggerCoalesceProgress = 0.9f;

bool TransitionManager::isKnownKind(const std::string& kind) {
    return typeForKind(kind) != TransitionType::None;
}

bool TransitionManager::isUsableDuration(float durationSeconds) {
    return std::isfinite(durationSeconds) && durationSeconds > 0.0f;
}

bool TransitionManager::canStartTransition(const std::string& kind,
                                           float durationSeconds) const {
    return isKnownKind(kind) && isUsableDuration(durationSeconds);
}

bool TransitionManager::shouldCoalesceRetrigger(const std::string& kind) const {
    if (m_type == TransitionType::None) return false;
    return typeForKind(kind) == m_type && m_progress < kRetriggerCoalesceProgress;
}

void TransitionManager::startTransitionFromKind(const std::string& kind,
                                               float durationSeconds,
                                               const std::string& colorHex) {
    const TransitionType type = typeForKind(kind);
    if (type == TransitionType::None) {
        stopTransition();
        return;
    }
    if (shouldCoalesceRetrigger(kind)) return;
    if (type == TransitionType::CrossFade) {
        startTransition(TransitionType::CrossFade, durationSeconds);
    } else if (type == TransitionType::FadeToBlack) {
        startTransition(TransitionType::FadeToBlack, durationSeconds, 0, 0, 0, 255);
    } else if (type == TransitionType::FadeToWhite) {
        startTransition(TransitionType::FadeToWhite, durationSeconds, 255, 255, 255, 255);
    } else if (type == TransitionType::FadeToColor) {
        uint8_t r = 0, g = 0, b = 0, a = 255;
        parseHexColor(colorHex, r, g, b, a);
        startTransition(TransitionType::FadeToColor, durationSeconds, r, g, b, a);
    } else if (type == TransitionType::WipeLeft) {
        startTransition(TransitionType::WipeLeft, durationSeconds);
    } else if (type == TransitionType::WipeRight) {
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
    // D6-#147 kanıt sayacı: her readback girişimi sayılır (Window kapıları
    // geçersiz girdiyi buraya hiç ulaştırmaz).
    ++m_snapshotCaptureCount;
    // #162: stage-then-commit. Eski kod önce cleanupSnapshot() çağırıp sonra
    // fallible readback'e giriyordu: renderer null / readback fail / texture
    // fail durumunda hem false dönüyor hem de o ana kadarki canlı kares.
    // metni imha ediyordu. Artık taze doku bir yerelde kurulur, SADECE başarıda
    // commitlenir; başarısızlık eski snapshot'a dokunmaz.
    if (!renderer) return false;

    SDL_Surface* src = surface;
    SDL_Surface* tempSurface = nullptr;
    if (!src) {
        tempSurface = SDL_RenderReadPixels(renderer, nullptr);
        src = tempSurface;
    }

    SDL_Texture* freshTexture = nullptr;
    int freshWidth = 0;
    int freshHeight = 0;
    if (src) {
        freshTexture = SDL_CreateTextureFromSurface(renderer, src);
        if (freshTexture) {
            freshWidth = src->w;
            freshHeight = src->h;
        }
    }

    if (tempSurface) {
        SDL_DestroySurface(tempSurface);
    }

    if (!freshTexture) return false;
    cleanupSnapshot();
    m_snapshotTexture = freshTexture;
    m_snapshotWidth = freshWidth;
    m_snapshotHeight = freshHeight;
    return true;
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
