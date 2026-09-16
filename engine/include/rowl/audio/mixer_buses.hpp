/**
 * rowl/audio/mixer_buses.hpp
 *
 * Faz 5 Dilim 1 — mixer bus İSKELETİ (header-only, bu dilimde karıştırma
 * YOKTUR). applyChannelGains matematiği birebir taşınır; SDL çağrıları
 * değişmez. Motor bu iskeleti henüz KULLANMAZ; tek kaynak
 * AudioEngine::applyChannelGains olarak kalır.
 */

#pragma once

#include <algorithm>
#include <cmath>

namespace Rowl::Audio {

enum class BusId {
    Bgm,
    Voice,
    Sfx,
    Master,
};

struct BusState {
    /// Kullanıcı hacmi [0,1]; non-finite ve aralık dışı girdi son geçerli
    /// değeri korur (fail-closed, motor setter'larıyla aynı sözleşme).
    float userVolume = 1.0f;
    /// Yalnızca BGM duck kazancı (voice aktifken bgm * duck).
    float duckGain = 1.0f;
};

class MixerBuses {
public:
    void setUserVolume(BusId bus, float volume) {
        if (!std::isfinite(volume)) return;
        busState(bus).userVolume = std::clamp(volume, 0.0f, 1.0f);
    }

    float userVolume(BusId bus) const { return busState(bus).userVolume; }

    void setMasterVolume(float volume) { setUserVolume(BusId::Master, volume); }
    void setBgmVolume(float volume) { setUserVolume(BusId::Bgm, volume); }
    void setVoiceVolume(float volume) { setUserVolume(BusId::Voice, volume); }
    void setSfxVolume(float volume) { setUserVolume(BusId::Sfx, volume); }

    /// Voice ducking anlık kazancı (eğriler sonraki dilim).
    void setBgmDuckGain(float gain) {
        if (!std::isfinite(gain)) return;
        m_bgm.duckGain = std::clamp(gain, 0.0f, 1.0f);
    }

    /// master * bgm * duck — AudioEngine::applyChannelGains ile birebir.
    float effectiveBgmGain() const {
        return m_master.userVolume * m_bgm.userVolume * m_bgm.duckGain;
    }
    float effectiveVoiceGain() const {
        return m_master.userVolume * m_voice.userVolume;
    }
    float effectiveSfxGain() const {
        return m_master.userVolume * m_sfx.userVolume;
    }

private:
    BusState& busState(BusId bus) {
        switch (bus) {
            case BusId::Bgm: return m_bgm;
            case BusId::Voice: return m_voice;
            case BusId::Sfx: return m_sfx;
            case BusId::Master: return m_master;
        }
        return m_master;
    }
    const BusState& busState(BusId bus) const {
        switch (bus) {
            case BusId::Bgm: return m_bgm;
            case BusId::Voice: return m_voice;
            case BusId::Sfx: return m_sfx;
            case BusId::Master: return m_master;
        }
        return m_master;
    }

    BusState m_bgm;
    BusState m_voice;
    BusState m_sfx;
    BusState m_master;
};

} // namespace Rowl::Audio
