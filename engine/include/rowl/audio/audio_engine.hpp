#pragma once

#include <string>
#include <memory>
#include <unordered_map>

namespace Rowl::Audio {

enum class AudioChannelType {
    Bgm,
    Voice,
    Sfx
};

enum class DSPFilterType {
    Normal,
    CaveReverb,
    Telephone,
    UnderwaterLowPass
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void playAudio(const std::string& assetPath, AudioChannelType channel, DSPFilterType filter = DSPFilterType::Normal);

    void setBgmVolume(float volume);
    void applyDspFilter(DSPFilterType filter);
    void triggerVoiceDucking(bool isVoiceActive);
    void setDuckingFactor(float factor);  // Configurable voice ducking attenuation (0.0-1.0)

    float getBgmGain() const { return m_bgmGain; }
    DSPFilterType getActiveFilter() const { return m_activeFilter; }
    void shutdown();

private:
    float m_masterVolume = 1.0f;
    float m_bgmVolume = 1.0f;
    float m_bgmGain = 1.0f;
    float m_duckingFactor = 0.5f;  // Configurable ducking factor (default -6dB = 0.5)
    DSPFilterType m_activeFilter = DSPFilterType::Normal;
    bool m_isDuckingActive = false;
    bool m_initialized = false;
};

} // namespace Rowl::Audio
