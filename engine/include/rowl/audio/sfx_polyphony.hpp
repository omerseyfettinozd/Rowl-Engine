/**
 * rowl/audio/sfx_polyphony.hpp
 *
 * Faz 5 Dilim 2 — SFX eşzamanlı ses havuzu (header-only, SDL'siz saf
 * durum). Fiziksel akışlar AudioEngine'de tutulur; bu sınıf yalnızca
 * slot/çalma durumu + steal-oldest politikasını taşır.
 *
 * Politika: varsayılan derinlik 8, üst sınır 16, aralık [1,16] clamp.
 * Boş slot varsa en düşük indeksli boş slot; yoksa en küçük sıra
 * numaralı (en eski) ses çalınır (steal-oldest). Derinlik 1 iken tek
 * slot davranışı bugünkü tek-stream semantiğiyle aynıdır (yeni ses
 * eskisini değiştirir).
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Rowl::Audio {

struct SfxVoice {
    std::vector<uint8_t> pcm; // float PCM baytları
    size_t sampleOffset = 0;  // float indeksi
    bool playing = false;
    uint64_t sequence = 0; // atama sırası (steal-oldest anahtarı)
    std::string assetPath; // teşhis/izlenebilirlik
    // Reopen sonrası geri-kuyruk için float format (queue anında kayda geçer).
    int channels = 2;
    int sampleRate = 48000;
};

class SfxVoicePool {
public:
    static constexpr size_t kDefaultDepth = 8;
    static constexpr size_t kMaxDepth = 16;
    static constexpr size_t kMinDepth = 1;

    SfxVoicePool() { m_voices.resize(kDefaultDepth); }

    static size_t clampDepth(int depth) {
        if (depth < static_cast<int>(kMinDepth)) return kMinDepth;
        if (depth > static_cast<int>(kMaxDepth)) return kMaxDepth;
        return static_cast<size_t>(depth);
    }

    void setDepth(int depth) {
        const size_t want = clampDepth(depth);
        if (want == m_voices.size()) return;
        if (want < m_voices.size()) {
            // Daraltma: sondaki slotlar düşer (çalan sesleri keser).
            m_voices.resize(want);
        } else {
            m_voices.resize(want);
        }
    }

    size_t depth() const { return m_voices.size(); }
    static constexpr size_t maxDepth() { return kMaxDepth; }

    /// Yeni sesi kuyruğa sokar, kullanılan slot indeksini döndürür.
    size_t play(const uint8_t* data, size_t byteCount,
                const std::string& assetPath) {
        return playInto(pickSlot(), data, byteCount, assetPath);
    }

    /// Kuyruk-başarısızlığı atomikliği için: slot önceden seçilir
    /// (akış temizliği), PCM yalnızca cihaz kuyruğu BAŞARILIYSA yazılır.
    size_t pickSlot() const {
        size_t slot = m_voices.size();
        for (size_t i = 0; i < m_voices.size(); ++i) {
            if (!m_voices[i].playing) {
                slot = i;
                break;
            }
        }
        if (slot >= m_voices.size()) {
            // Tümü dolu: en eski sequence çalınır (steal-oldest).
            slot = 0;
            for (size_t i = 1; i < m_voices.size(); ++i) {
                if (m_voices[i].sequence < m_voices[slot].sequence) slot = i;
            }
        }
        return slot;
    }

    size_t playInto(size_t slot, const uint8_t* data, size_t byteCount,
                    const std::string& assetPath,
                    int channels = 2, int sampleRate = 48000) {
        if (slot >= m_voices.size()) slot = pickSlot();
        if (slot >= m_voices.size()) return 0;
        SfxVoice& voice = m_voices[slot];
        voice.pcm.assign(data, data + byteCount);
        voice.sampleOffset = 0;
        voice.playing = true;
        voice.sequence = ++m_sequenceCounter;
        voice.assetPath = assetPath;
        voice.channels = (channels >= 1 && channels <= 8) ? channels : 2;
        voice.sampleRate = (sampleRate > 0) ? sampleRate : 48000;
        return slot;
    }

    void stopAll() {
        for (auto& voice : m_voices) {
            voice.playing = false;
            voice.sampleOffset = 0;
            voice.pcm.clear();
            voice.assetPath.clear();
        }
    }

    size_t activeCount() const {
        size_t count = 0;
        for (const auto& voice : m_voices) {
            if (voice.playing && !voice.pcm.empty()) ++count;
        }
        return count;
    }

    std::vector<SfxVoice>& voices() { return m_voices; }
    const std::vector<SfxVoice>& voices() const { return m_voices; }

private:
    std::vector<SfxVoice> m_voices;
    uint64_t m_sequenceCounter = 0;
};

} // namespace Rowl::Audio
