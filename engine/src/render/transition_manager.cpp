#include "rowl/render/transition_manager.hpp"
#include "rowl/text/hex_color.hpp"
#include "rowl/core/logger.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
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
    // E2a repertuvari (yalniz-eklemeli; mevcut esleme aynen korunur).
    if (kind == "dissolve") return TransitionType::Dissolve;
    if (kind == "push_left" || kind == "push") return TransitionType::PushLeft;
    if (kind == "push_right") return TransitionType::PushRight;
    if (kind == "push_up") return TransitionType::PushUp;
    if (kind == "push_down") return TransitionType::PushDown;
    if (kind == "iris_in" || kind == "iris") return TransitionType::IrisIn;
    if (kind == "iris_out") return TransitionType::IrisOut;
    return TransitionType::None;
}

// E2a: kesif listesi typeForKind'in aynasidir; yeni tur eklenince buraya da
// satir duser (aksi halde C API listesi ile baslatilabilir turler kayar).
std::string TransitionManager::supportedKindsJson() {
    return R"(["crossfade","fade_black","fade_white","fade_color","wipe_left","wipe_right","dissolve","push_left","push_right","push_up","push_down","iris_in","iris_out"])";
}

// D6-#147 (+artık genellemesi): erken yeniden tetikleme (%90 ilerleme altı,
// türden bağımsız) yoksayılır; geçiş baştan başlamaz, snapshot tazelenmez.
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

// D6-#147-artık genellemesi: aynı kural tür-değiştiren tetiklemeye de
// uygulanır — alterne-tür spam'i de snapshot sayacını artırmaz, sürmekte
// olan geçiş progress=1'e ulaşır. Tür parametresi sözleşme gereği durur
// (çağrı noktaları değişmez).
bool TransitionManager::shouldCoalesceRetrigger(const std::string& /*kind*/) const {
    if (m_type == TransitionType::None) return false;
    return m_progress < kRetriggerCoalesceProgress;
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
    // E2a: yeni turler renksizdir (snapshot + geometri); renk alani tasiyici
    // olarak durur, davranisa katilmaz. Gecersiz sure reddi startTransition
    // icindedir (stop) — kapilar (canStartTransition) zaten yalnizca gecerli
    // girdiyi gecirir.
    } else if (type == TransitionType::Dissolve) {
        startTransition(TransitionType::Dissolve, durationSeconds);
    } else if (type == TransitionType::PushLeft) {
        startTransition(TransitionType::PushLeft, durationSeconds);
    } else if (type == TransitionType::PushRight) {
        startTransition(TransitionType::PushRight, durationSeconds);
    } else if (type == TransitionType::PushUp) {
        startTransition(TransitionType::PushUp, durationSeconds);
    } else if (type == TransitionType::PushDown) {
        startTransition(TransitionType::PushDown, durationSeconds);
    } else if (type == TransitionType::IrisIn) {
        startTransition(TransitionType::IrisIn, durationSeconds);
    } else if (type == TransitionType::IrisOut) {
        startTransition(TransitionType::IrisOut, durationSeconds);
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
        // E2a repertuvari: snapshot salt-okunur, geometri progress-surumlu.
        // Snapshot yoksa dal cizmez (yeni sahne gorunur) — legacy dallarla
        // ayni fail-closed davranis.
        case TransitionType::Dissolve: {
            if (m_snapshotTexture && m_snapshotWidth > 0 && m_snapshotHeight > 0) {
                constexpr int kCols = 32;
                constexpr int kRows = 18;
                SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                for (int j = 0; j < kRows; ++j) {
                    for (int i = 0; i < kCols; ++i) {
                        const uint32_t h = static_cast<uint32_t>(i) * 73856093u
                                         ^ static_cast<uint32_t>(j) * 19349663u
                                         ^ 83492791u;
                        const float threshold =
                            static_cast<float>(h % 1000u) / 1000.0f;
                        if (threshold < m_progress) continue; // cozundu
                        SDL_FRect src = {
                            static_cast<float>(m_snapshotWidth) * i / kCols,
                            static_cast<float>(m_snapshotHeight) * j / kRows,
                            static_cast<float>(m_snapshotWidth) / kCols,
                            static_cast<float>(m_snapshotHeight) / kRows
                        };
                        SDL_FRect blk = {
                            dst.x + dst.w * i / kCols,
                            dst.y + dst.h * j / kRows,
                            dst.w / kCols,
                            dst.h / kRows
                        };
                        SDL_RenderTexture(renderer, m_snapshotTexture, &src, &blk);
                    }
                }
            }
            break;
        }
        case TransitionType::PushLeft:
        case TransitionType::PushRight:
        case TransitionType::PushUp:
        case TransitionType::PushDown: {
            if (m_snapshotTexture) {
                SDL_FRect pushDst = dst;
                if (m_type == TransitionType::PushLeft) {
                    pushDst.x -= dst.w * m_progress;
                } else if (m_type == TransitionType::PushRight) {
                    pushDst.x += dst.w * m_progress;
                } else if (m_type == TransitionType::PushUp) {
                    pushDst.y -= dst.h * m_progress;
                } else {
                    pushDst.y += dst.h * m_progress;
                }
                SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                SDL_RenderTexture(renderer, m_snapshotTexture, nullptr, &pushDst);
            }
            break;
        }
        case TransitionType::IrisOut: {
            if (m_snapshotTexture) {
                const float scale = 1.0f - m_progress;
                SDL_FRect irisDst = {
                    dst.x + dst.w * (1.0f - scale) * 0.5f,
                    dst.y + dst.h * (1.0f - scale) * 0.5f,
                    dst.w * scale,
                    dst.h * scale
                };
                SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                SDL_RenderTexture(renderer, m_snapshotTexture, nullptr, &irisDst);
            }
            break;
        }
        case TransitionType::IrisIn: {
            if (m_snapshotTexture && m_snapshotWidth > 0 && m_snapshotHeight > 0) {
                if (m_progress <= 0.0f) {
                    SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                    SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                    SDL_RenderTexture(renderer, m_snapshotTexture, nullptr, &dst);
                } else {
                    const float hx0 = dst.x + dst.w * (1.0f - m_progress) * 0.5f;
                    const float hx1 = dst.x + dst.w * (1.0f + m_progress) * 0.5f;
                    const float hy0 = dst.y + dst.h * (1.0f - m_progress) * 0.5f;
                    const float hy1 = dst.y + dst.h * (1.0f + m_progress) * 0.5f;
                    const float sx = static_cast<float>(m_snapshotWidth) / dst.w;
                    const float sy = static_cast<float>(m_snapshotHeight) / dst.h;
                    SDL_SetTextureBlendMode(m_snapshotTexture, SDL_BLENDMODE_NONE);
                    SDL_SetTextureAlphaMod(m_snapshotTexture, 255);
                    const struct Band { float x0, y0, x1, y1; } bands[4] = {
                        {dst.x, dst.y, hx0, dst.y + dst.h},
                        {hx1, dst.y, dst.x + dst.w, dst.y + dst.h},
                        {hx0, dst.y, hx1, hy0},
                        {hx0, hy1, hx1, dst.y + dst.h}
                    };
                    for (const auto& b : bands) {
                        if (b.x1 <= b.x0 || b.y1 <= b.y0) continue;
                        SDL_FRect src = {
                            (b.x0 - dst.x) * sx, (b.y0 - dst.y) * sy,
                            (b.x1 - b.x0) * sx, (b.y1 - b.y0) * sy
                        };
                        SDL_FRect bdst = {b.x0, b.y0, b.x1 - b.x0, b.y1 - b.y0};
                        SDL_RenderTexture(renderer, m_snapshotTexture, &src, &bdst);
                    }
                }
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
