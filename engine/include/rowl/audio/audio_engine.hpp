#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

struct SDL_AudioStream;

namespace Rowl::VFS {
class VFSManager;
}

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

// Kept separate from component JSON so the audio engine has one bounded,
// validated runtime contract irrespective of the host that requested it.
enum class BgmTransitionKind {
    Instant,
    Fade,
    Crossfade
};

class AudioEngine {
public:
    explicit AudioEngine(Rowl::VFS::VFSManager* vfs = nullptr);
    ~AudioEngine();

    void setVfs(Rowl::VFS::VFSManager* vfs);
    Rowl::VFS::VFSManager* getVfs() const { return m_vfs; }

    bool initialize();
    void playAudio(const std::string& assetPath, AudioChannelType channel, DSPFilterType filter = DSPFilterType::Normal);
    void playBgm(const std::string& assetPath, BgmTransitionKind transition, float durationSeconds);
    void stopBgm();
    void stopAll();

    void setBgmVolume(float volume);
    void setMasterVolume(float volume);
    void setVoiceVolume(float volume);
    void setSfxVolume(float volume);
    void applyDspFilter(DSPFilterType filter);
    void triggerVoiceDucking(bool isVoiceActive);
    void setDuckingFactor(float factor);  // Configurable voice ducking attenuation (0.0-1.0)

    void update(float deltaSeconds = 1.0f / 60.0f);
    bool isBgmLooping() const { return m_bgmLoop; }
    void setBgmLooping(bool loop) { m_bgmLoop = loop; }

    float getBgmGain() const { return m_bgmGain; }
    float getBgmVolume() const { return m_bgmVolume; }
    float getMasterVolume() const { return m_masterVolume; }
    float getVoiceVolume() const { return m_voiceVolume; }
    float getSfxVolume() const { return m_sfxVolume; }
    DSPFilterType getActiveFilter() const { return m_activeFilter; }
    bool isInitialized() const { return m_initialized; }
    bool isDuckingActive() const { return m_isDuckingActive; }
    bool isAudioDeviceAvailable() const { return m_deviceAvailable; }
    const std::string& getCurrentBgmPath() const { return m_currentBgmPath; }
    bool isBgmPlaying() const { return m_isBgmPlaying; }
    bool isVoicePlaying() const { return m_isVoicePlaying; }
    bool isBgmTransitionActive() const { return m_bgmTransitionActive; }
    const std::string& getLastError() const { return m_lastError; }

    // Real-Time Audio Telemetry & VU Metering
    float getChannelPeak(int channelType, int channelIndex = 0) const;
    float getChannelRms(int channelType, int channelIndex = 0) const;
    void getSpectrumBands(float* outBands, int bandCount) const;

    void shutdown();

private:
    float m_masterVolume = 1.0f;
    float m_bgmVolume = 1.0f;
    float m_voiceVolume = 1.0f;
    float m_sfxVolume = 1.0f;
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
    std::vector<uint8_t> m_transitionBgmData;
    bool m_isBgmPlaying = false;
    bool m_isVoicePlaying = false;
    SDL_AudioStream* m_bgmStream = nullptr;
    SDL_AudioStream* m_transitionBgmStream = nullptr;
    SDL_AudioStream* m_voiceStream = nullptr;
    SDL_AudioStream* m_sfxStream = nullptr;
    BgmTransitionKind m_requestedBgmTransition = BgmTransitionKind::Instant;
    float m_requestedBgmTransitionDurationSeconds = 0.0f;
    bool m_bgmTransitionActive = false;
    BgmTransitionKind m_activeBgmTransition = BgmTransitionKind::Instant;
    float m_bgmTransitionElapsedSeconds = 0.0f;
    float m_bgmTransitionDurationSeconds = 0.0f;
    void applyChannelGains();
    void updateBgmTransition(float deltaSeconds);
    void updateTelemetry(float deltaSeconds);

    struct ChannelTelemetry {
        float peakL = 0.0f;
        float peakR = 0.0f;
        float rmsL = 0.0f;
        float rmsR = 0.0f;
    };
    ChannelTelemetry m_telemetryBgm;
    ChannelTelemetry m_telemetryVoice;
    ChannelTelemetry m_telemetrySfx;
    ChannelTelemetry m_telemetryMaster;
    std::array<float, 4> m_spectrumBands{0.0f, 0.0f, 0.0f, 0.0f};
    size_t m_bgmSampleOffset = 0;
    std::vector<uint8_t> m_lastSfxData;
    size_t m_sfxSampleOffset = 0;
    bool m_isSfxPlaying = false;

    Rowl::VFS::VFSManager* m_vfs = nullptr;
    std::shared_ptr<Rowl::VFS::VFSManager> m_ownedVfs;
    bool m_audioLeaseHeld = false;
    Rowl::VFS::VFSManager& vfs() const;
};

} // namespace Rowl::Audio
