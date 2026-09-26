/**
 * rowl/scene/character_fx.hpp
 *
 * E2b — karakter tween + konusan-vurgusu + dudak-senkronu + expression-harmani
 * (header-only, saf matematik + durum; SDL/render bagimliligi YOK).
 *
 * Kapsam disi birakilanlar (bilerek):
 *  - window.cpp / renderVisualNovelFrame imzasi ve frame-hash/reuse kilidi
 *    (C0/D2): FX yalnizca Engine'in sundugu CharacterRenderData degerlerini
 *    donusturur; Window yeni alan gormez, hash'e yeni alan girmez.
 *  - editor/ (C3): saf native; C# tarafi yalnizca yeni C API'leri tuketir.
 *  - aux preset deposu (g_characterStates): harman, Engine sahnesindeki
 *    CANLI karakter listesi uzerinde calisir; preset kaliciligi C#'dadir.
 *
 * Sozlesme:
 *  - Tum ilerleme dt>0 ile olur; dt<=0 (idle Step(0), NaN/negatif) saati
 *    oynatmaz (camera tween #160 emsali).
 *  - Tween hedefleri sonlu olmalidir; opaklik [0,1]'e clamp'lenir
 *    (setSlotOpacity emsali). Gecersiz girdi Engine'de reddedilir, durum
 *    degismez (fail-closed).
 *  - Sprite-bekcisi: tween baslarken o indeksteki sprite kaydedilir; sahne
 *    degisip sprite farklilassa girdi sessizce bayat sayilir ve budanir
 *    (rollback/snapshot kancasi gerekmez — kendini temizler).
 *  - Bitmis tween hedefte CIMBELENIR (progress=1 pinlenir); sahne guncelle-
 *    mesiyle geri sicrama olmaz. Temizlik: yeni tween / cancel / sprite
 *    uyusmazligi.
 *  - Kapali iken (varsayilan) cikti tane-tane girisin kopyasidir: legacy
 *    pikseller bayt-birebir korunur.
 */

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace Rowl::Scene {

// Tween egrisi (C API int eslemesi: 0..4). CameraEasing ile ayni aile ama
// render include'u cekmemek icin burada bagimsiz tanimlidir.
enum class CharacterFxEase : int {
    Linear = 0,
    EaseInQuad = 1,
    EaseOutQuad = 2,
    EaseInOutCubic = 3,
    SmoothStep = 4,
};

inline bool characterFxEaseFromInt(int value, CharacterFxEase& out) noexcept {
    switch (value) {
        case 0: out = CharacterFxEase::Linear; return true;
        case 1: out = CharacterFxEase::EaseInQuad; return true;
        case 2: out = CharacterFxEase::EaseOutQuad; return true;
        case 3: out = CharacterFxEase::EaseInOutCubic; return true;
        case 4: out = CharacterFxEase::SmoothStep; return true;
        default: return false;
    }
}

inline float applyCharacterFxEase(CharacterFxEase ease, float t) noexcept {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    switch (ease) {
        case CharacterFxEase::Linear: return clamped;
        case CharacterFxEase::EaseInQuad: return clamped * clamped;
        case CharacterFxEase::EaseOutQuad: return 1.0f - (1.0f - clamped) * (1.0f - clamped);
        case CharacterFxEase::EaseInOutCubic: {
            if (clamped < 0.5f) return 4.0f * clamped * clamped * clamped;
            const float u = 2.0f * clamped - 2.0f;
            return 1.0f + u * u * u / 2.0f;
        }
        case CharacterFxEase::SmoothStep:
            return clamped * clamped * (3.0f - 2.0f * clamped);
    }
    return clamped;
}

// Duz rect + opaklik (x, y, w, h, opaklik).
struct CharacterFxRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float opacity = 1.0f;
};

inline CharacterFxRect lerpCharacterFxRect(const CharacterFxRect& from,
                                           const CharacterFxRect& to,
                                           float t) noexcept {
    CharacterFxRect out;
    out.x = from.x + (to.x - from.x) * t;
    out.y = from.y + (to.y - from.y) * t;
    out.w = from.w + (to.w - from.w) * t;
    out.h = from.h + (to.h - from.h) * t;
    out.opacity = from.opacity + (to.opacity - from.opacity) * t;
    return out;
}

struct CharacterFxTween {
    CharacterFxRect from;
    CharacterFxRect to;
    float durationSeconds = 0.0f;
    float elapsedSeconds = 0.0f;
    CharacterFxEase easing = CharacterFxEase::Linear;
    std::string spriteGuard;

    float progress() const noexcept {
        if (!(durationSeconds > 0.0f)) return 1.0f;
        return std::clamp(elapsedSeconds / durationSeconds, 0.0f, 1.0f);
    }
    bool done() const noexcept { return progress() >= 1.0f; }
    CharacterFxRect sample() const noexcept {
        return lerpCharacterFxRect(from, to, applyCharacterFxEase(easing, progress()));
    }
};

// Indeks-anahtarli tween izi (sirali map: snapshot JSON deterministik).
class CharacterTweenTrack {
public:
    CharacterTweenTrack() = default;

    void start(int index, std::string spriteGuard, const CharacterFxRect& from,
               const CharacterFxRect& to, float durationSeconds,
               CharacterFxEase easing) {
        CharacterFxTween tween;
        tween.from = from;
        tween.to = to;
        tween.to.opacity = std::clamp(to.opacity, 0.0f, 1.0f);
        tween.durationSeconds = durationSeconds;
        // duration<=0: anlik pin (ilerleme yok, hedefte durur).
        tween.elapsedSeconds = (durationSeconds > 0.0f) ? 0.0f : 1.0f;
        tween.easing = easing;
        tween.spriteGuard = std::move(spriteGuard);
        m_tweens[index] = std::move(tween);
    }

    bool cancel(int index) {
        return m_tweens.erase(index) > 0;
    }
    void cancelAll() noexcept { m_tweens.clear(); }

    // Canli sprite listesiyle uyusmayan girdileri budar (indeks-tasma veya
    // sprite degisimi). Sahne guncellemesi sonrasi cagrilir; idempotent.
    void purgeMismatched(const std::vector<std::string>& liveSprites) {
        for (auto it = m_tweens.begin(); it != m_tweens.end();) {
            const int index = it->first;
            if (index < 0 ||
                static_cast<std::size_t>(index) >= liveSprites.size() ||
                it->second.spriteGuard != liveSprites[static_cast<std::size_t>(index)]) {
                it = m_tweens.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Saati ilerletir (dt<=0 / non-finite: yalnizca budama, zaman donar).
    void advance(float dt, const std::vector<std::string>& liveSprites) {
        purgeMismatched(liveSprites);
        if (!(dt > 0.0f) || !std::isfinite(dt)) return;
        for (auto& [index, tween] : m_tweens) {
            if (tween.done()) continue;
            tween.elapsedSeconds += dt;
            if (tween.elapsedSeconds >= tween.durationSeconds) {
                tween.elapsedSeconds = tween.durationSeconds;
            }
        }
    }

    // Ornekleme: girdi varsa ve sprite tutuyorsa true + out dolar (ucus ya
    // da pinli). Yoksa/bayat ise false.
    bool sample(int index, const std::string& liveSprite,
                CharacterFxRect& out) const noexcept {
        const auto it = m_tweens.find(index);
        if (it == m_tweens.end()) return false;
        if (it->second.spriteGuard != liveSprite) return false;
        out = it->second.sample();
        return true;
    }

    bool entryProgress(int index, float& outProgress) const noexcept {
        const auto it = m_tweens.find(index);
        if (it == m_tweens.end()) return false;
        outProgress = it->second.progress();
        return true;
    }

    // Ucustaki (progress<1) girdi var mi? Pinli-bitmisler sayilmaz.
    bool isActive() const noexcept {
        for (const auto& [index, tween] : m_tweens) {
            if (!tween.done()) return true;
        }
        return false;
    }

    bool empty() const noexcept { return m_tweens.empty(); }
    std::size_t size() const noexcept { return m_tweens.size(); }

    const std::map<int, CharacterFxTween>& entries() const noexcept { return m_tweens; }

private:
    std::map<int, CharacterFxTween> m_tweens;
};

// Konusan-vurgusu: odaktaki karakter tam parlak, digerleri dim carpaniyla
// solar. Kapali (varsayilan) veya odaksizken carpan her indekste 1.0'dir.
struct SpeakerFocusState {
    bool enabled = false;
    int focusedIndex = -1;
    float dimOpacity = 0.5f;

    float multiplierFor(int index, std::size_t count) const noexcept {
        if (!enabled || focusedIndex < 0 ||
            static_cast<std::size_t>(focusedIndex) >= count ||
            index == focusedIndex) {
            return 1.0f;
        }
        return std::clamp(dimOpacity, 0.0f, 1.0f);
    }
};

// Basit dudak-senkronu: konusurken (odakli karakter + tamamlanmamis diyalog)
// dikey salinim. Saf fonksiyondur; saat Engine'dedir (pause'da donar).
struct LipSyncState {
    bool enabled = false;
    float amplitudePx = 4.0f;
    float frequencyHz = 8.0f;

    float bobOffset(double timeSeconds) const noexcept {
        if (!enabled) return 0.0f;
        constexpr double kTwoPi = 6.283185307179586;
        return amplitudePx *
               static_cast<float>(std::sin(kTwoPi * frequencyHz * timeSeconds));
    }
};

// Expression-harmani anligi (canli liste disinda tutulan eski kare).
struct FxCharSnapshot {
    std::string sprite;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float opacity = 1.0f;
};

struct ExpressionBlendState {
    bool active = false;
    float durationSeconds = 0.0f;
    float elapsedSeconds = 0.0f;
    std::vector<FxCharSnapshot> oldChars;

    float progress() const noexcept {
        if (!active || !(durationSeconds > 0.0f)) return 1.0f;
        return std::clamp(elapsedSeconds / durationSeconds, 0.0f, 1.0f);
    }
};

} // namespace Rowl::Scene
