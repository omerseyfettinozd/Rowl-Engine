#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <unordered_map>

struct SDL_AudioStream;

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
    void stopBgm();
    void stopAll();

    void setBgmVolume(float volume);
    void applyDspFilter(DSPFilterType filter);
    void triggerVoiceDucking(bool isVoiceActive);
    void setDuckingFactor(float factor);  // Configurable voice ducking attenuation (0.0-1.0)

    void update();
    bool isBgmLooping() const { return m_bgmLoop; }
    void setBgmLooping(bool loop) { m_bgmLoop = loop; }

    float getBgmGain() const { return m_bgmGain; }
    float getBgmVolume() const { return m_bgmVolume; }
    DSPFilterType getActiveFilter() const { return m_activeFilter; }
    bool isInitialized() const { return m_initialized; }
    bool isDuckingActive() const { return m_isDuckingActive; }
    bool isAudioDeviceAvailable() const { return m_deviceAvailable; }
    const std::string& getCurrentBgmPath() const { return m_currentBgmPath; }
    bool isBgmPlaying() const { return m_isBgmPlaying; }
    bool isVoicePlaying() const { return m_isVoicePlaying; }
    const std::string& getLastError() const { return m_lastError; }

    void shutdown();

private:
    float m_masterVolume = 1.0f;
    float m_bgmVolume = 1.0f;
    float m_bgmGain = 1.0f;
    float m_duckingFactor = 0.5f;  // Configurable ducking factor (default -6dB = 0.5)
    DSPFilterType m_activeFilter = DSPFilterType::Normal;
    bool m_isDuckingActive = false;
    bool m_initialized = false;
    bool m_deviceAvailable = false;
    bool m_bgmLoop = true;

    std::string m_currentBgmPath = "";
    std::string m_lastError;
    std::vector<uint8_t> m_bgmData;
    bool m_isBgmPlaying = false;
    bool m_isVoicePlaying = false;
    SDL_AudioStream* m_bgmStream = nullptr;
    SDL_AudioStream* m_sfxStream = nullptr;
};

} // namespace Rowl::Audio
