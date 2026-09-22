/**
 * rowl/audio/stream_mixer.hpp
 *
 * Faz 5 Dilim 1 — 6-bus gain zinciri (header-only iskelet).
 * Faz 5 Dilim 2 — AKTİF: AudioEngine::applyChannelGains'in tek kazanç
 * kaynağıdır (matematik birebir: master*bus, duck yalnız BGM).
 *
 * mixer_buses.hpp üzerindeki 4 bus'ı Ambience + Ui ile genişletir;
 * AudioEngine'in mevcut hacim üyelerini BOZMAZ (motor bu dilimde kendi
 * applyChannelGains yolunu kullanmaya devam eder). applyChain, bir bus'a
 * ait interleaved float PCM'e ortak gain zincirini uygular.
 */

#pragma once

#include "rowl/audio/mixer_buses.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace Rowl::Audio {

enum class StreamBusId {
    Master,
    Bgm,
    Voice,
    Sfx,
    Ambience,
    Ui,
};

class StreamMixer {
public:
    void setUserVolume(StreamBusId bus, float volume) {
        if (!std::isfinite(volume)) return;
        volume = std::clamp(volume, 0.0f, 1.0f);
        switch (bus) {
            case StreamBusId::Master: m_master.store(volume, std::memory_order_relaxed); break;
            case StreamBusId::Bgm: m_bgm.store(volume, std::memory_order_relaxed); break;
            case StreamBusId::Voice: m_voice.store(volume, std::memory_order_relaxed); break;
            case StreamBusId::Sfx: m_sfx.store(volume, std::memory_order_relaxed); break;
            case StreamBusId::Ambience: m_ambience.store(volume, std::memory_order_relaxed); break;
            case StreamBusId::Ui: m_ui.store(volume, std::memory_order_relaxed); break;
        }
    }

    float userVolume(StreamBusId bus) const {
        switch (bus) {
            case StreamBusId::Master: return m_master.load(std::memory_order_relaxed);
            case StreamBusId::Bgm: return m_bgm.load(std::memory_order_relaxed);
            case StreamBusId::Voice: return m_voice.load(std::memory_order_relaxed);
            case StreamBusId::Sfx: return m_sfx.load(std::memory_order_relaxed);
            case StreamBusId::Ambience: return m_ambience.load(std::memory_order_relaxed);
            case StreamBusId::Ui: return m_ui.load(std::memory_order_relaxed);
        }
        return 1.0f;
    }

    void setBgmDuckGain(float gain) {
        if (!std::isfinite(gain)) return;
        m_duckGain.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    /// Bus efektif kazancı: master * bus (* duck yalnızca Bgm'de).
    /// Motorun applyChannelGains zinciriyle birebir aynı matematik.
    float gainFor(StreamBusId bus) const {
        const float busVolume = userVolume(bus);
        if (bus == StreamBusId::Master) return m_master.load(std::memory_order_relaxed);
        if (bus == StreamBusId::Bgm) return m_master.load(std::memory_order_relaxed) * busVolume * m_duckGain.load(std::memory_order_relaxed);
        return m_master.load(std::memory_order_relaxed) * busVolume;
    }

    // ── Faz 5 Dilim 2: ambience ikinci bed (BedA=0 miras, BedB=1 yeni).
    // setUserVolume(Ambience)/userVolume(Ambience) BedA ile eşlenir;
    // BedB bağımsız tutulur. İki bed için de formül master*bed'dir.
    void setAmbienceBedVolume(int bed, float volume) {
        if (!std::isfinite(volume)) return;
        volume = std::clamp(volume, 0.0f, 1.0f);
        if (bed == 0) {
            m_ambience.store(volume, std::memory_order_relaxed);
        } else if (bed == 1) {
            m_ambienceBedB.store(volume, std::memory_order_relaxed);
        }
    }

    float ambienceBedVolume(int bed) const {
        if (bed == 0) return m_ambience.load(std::memory_order_relaxed);
        if (bed == 1) return m_ambienceBedB.load(std::memory_order_relaxed);
        return 1.0f;
    }

    float gainForAmbienceBed(int bed) const {
        return m_master.load(std::memory_order_relaxed) * ambienceBedVolume(bed);
    }

    /// Ortak gain zincirini interleaved float PCM'e uygular (yerinde).
    void applyChain(float* samples, size_t frameCount, StreamBusId bus) const {
        if (!samples || frameCount == 0 || bus == StreamBusId::Master) return;
        const float gain = gainFor(bus);
        const size_t channels = channelCountFor(bus);
        const size_t total = frameCount * channels;
        for (size_t i = 0; i < total; ++i) samples[i] *= gain;
    }

    void setChannelCount(StreamBusId bus, size_t channels) {
        if (channels == 0 || channels > 8) return;
        switch (bus) {
            case StreamBusId::Bgm: m_bgmChannels.store(channels, std::memory_order_relaxed); break;
            case StreamBusId::Voice: m_voiceChannels.store(channels, std::memory_order_relaxed); break;
            case StreamBusId::Sfx: m_sfxChannels.store(channels, std::memory_order_relaxed); break;
            case StreamBusId::Ambience: m_ambienceChannels.store(channels, std::memory_order_relaxed); break;
            case StreamBusId::Ui: m_uiChannels.store(channels, std::memory_order_relaxed); break;
            case StreamBusId::Master: break;
        }
    }

private:
    size_t channelCountFor(StreamBusId bus) const {
        switch (bus) {
            case StreamBusId::Bgm: return m_bgmChannels.load(std::memory_order_relaxed);
            case StreamBusId::Voice: return m_voiceChannels.load(std::memory_order_relaxed);
            case StreamBusId::Sfx: return m_sfxChannels.load(std::memory_order_relaxed);
            case StreamBusId::Ambience: return m_ambienceChannels.load(std::memory_order_relaxed);
            case StreamBusId::Ui: return m_uiChannels.load(std::memory_order_relaxed);
            case StreamBusId::Master: return 2;
        }
        return 2;
    }

    // D03: hacim + kanal sayaçları ses-pump ve C-API thread'leri arasında
    // paylaşılır (yırtık-okuma + TSan kilidi; her bus bağımsız tek-word,
    // bileşik invariant yok → memory_order_relaxed yeterli).
    std::atomic<float> m_master = 1.0f;
    std::atomic<float> m_bgm = 1.0f;
    std::atomic<float> m_voice = 1.0f;
    std::atomic<float> m_sfx = 1.0f;
    std::atomic<float> m_ambience = 1.0f;
    std::atomic<float> m_ambienceBedB = 1.0f; // Faz 5 Dilim 2: ikinci ambience bed'i
    std::atomic<float> m_ui = 1.0f;
    std::atomic<float> m_duckGain = 1.0f;
    std::atomic<size_t> m_bgmChannels = 2;
    std::atomic<size_t> m_voiceChannels = 2;
    std::atomic<size_t> m_sfxChannels = 2;
    std::atomic<size_t> m_ambienceChannels = 2;
    std::atomic<size_t> m_uiChannels = 2;
};

} // namespace Rowl::Audio
