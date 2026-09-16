/**
 * rowl/audio/fade_curves.hpp
 *
 * Faz 5 Dilim 2 — fade/crossfade eğri seçimi (header-only, saf matematik).
 *
 * Linear (varsayılan): AudioEngine::updateBgmTransition içindeki mevcut
 * formüllerle bit-identical sonuç verir (Crossfade: out=1-p, in=p;
 * Fade: out=max(0,1-2p), in=max(0,2p-1)).
 *
 * EqualPower: outgoing=cos(p*PI/2), incoming=sin(p*PI/2); uçlar
 * snap'lenir (p<=0 -> 1/0, p>=1 -> 0/1) çünkü cosf(PI/2) float'ta tam
 * sıfır değildir. p=0.5'te iki kol da sqrt(2)/2 verir.
 *
 * Eğri seçimi BGM transition + ambience crossfade'de kullanılır;
 * varsayılan Linear olduğundan mevcut golden'lar kırılmaz.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace Rowl::Audio {

enum class FadeCurve {
    Linear = 0,
    EqualPower = 1,
};

inline constexpr float kFadeHalfPi = 1.57079632679489661923f;

inline float fadeCurveClampProgress(float progress) {
    if (!(progress >= 0.0f) || !(progress <= 1.0f)) {
        if (!(progress > 0.0f)) return 0.0f;
        return 1.0f;
    }
    return progress;
}

/// Crossfade outgoing kolu (BGM Crossfade + ambience crossfade).
inline float fadeCurveOutgoing(FadeCurve curve, float progress) {
    const float p = fadeCurveClampProgress(progress);
    if (curve == FadeCurve::EqualPower) {
        if (p <= 0.0f) return 1.0f;
        if (p >= 1.0f) return 0.0f;
        return std::cos(p * kFadeHalfPi);
    }
    return 1.0f - p;
}

/// Crossfade incoming kolu.
inline float fadeCurveIncoming(FadeCurve curve, float progress) {
    const float p = fadeCurveClampProgress(progress);
    if (curve == FadeCurve::EqualPower) {
        if (p <= 0.0f) return 0.0f;
        if (p >= 1.0f) return 1.0f;
        return std::sin(p * kFadeHalfPi);
    }
    return p;
}

/// BgmTransitionKind::Fade outgoing yarısı: ilk yarıda tam kazançtan
/// sessizliğe, ikinci yarıda sessizlik (mevcut formülle bit-identical).
inline float fadeKindOutgoing(FadeCurve curve, float progress) {
    const float p = fadeCurveClampProgress(progress);
    if (curve == FadeCurve::EqualPower) {
        const float q = std::clamp(p * 2.0f, 0.0f, 1.0f);
        if (q <= 0.0f) return 1.0f;
        if (q >= 1.0f) return 0.0f;
        return std::cos(q * kFadeHalfPi);
    }
    return std::max(0.0f, 1.0f - p * 2.0f);
}

/// BgmTransitionKind::Fade incoming yarısı.
inline float fadeKindIncoming(FadeCurve curve, float progress) {
    const float p = fadeCurveClampProgress(progress);
    if (curve == FadeCurve::EqualPower) {
        const float q = std::clamp(p * 2.0f - 1.0f, 0.0f, 1.0f);
        if (q <= 0.0f) return 0.0f;
        if (q >= 1.0f) return 1.0f;
        return std::sin(q * kFadeHalfPi);
    }
    return std::max(0.0f, p * 2.0f - 1.0f);
}

inline bool isValidFadeCurveInt(int curve) {
    return curve == static_cast<int>(FadeCurve::Linear) ||
           curve == static_cast<int>(FadeCurve::EqualPower);
}

inline FadeCurve fadeCurveFromInt(int curve, FadeCurve fallback) {
    if (curve == static_cast<int>(FadeCurve::EqualPower)) return FadeCurve::EqualPower;
    if (curve == static_cast<int>(FadeCurve::Linear)) return FadeCurve::Linear;
    return fallback;
}

} // namespace Rowl::Audio
