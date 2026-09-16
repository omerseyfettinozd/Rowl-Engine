#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "rowl/audio/audio_streaming.hpp"
#include "rowl/audio/ogg_stream_source.hpp"

struct SDL_AudioStream;

namespace Rowl::VFS {
class VFSManager;
}

namespace Rowl::Audio {

enum class AudioChannelType {
    Bgm,
    Voice,
    Sfx,
    // Faz 5 Dilim 1 ekleri (mevcut 0/1/2 değerleri aynen korunur):
    // 3 = Ambience (loop RAM), 4 = Ui (one-shot, fiziksel sfxStream).
    Ambience,
    Ui
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
    // Faz 5 Dilim 1: ham kanal int'iyle giriş (0=Bgm,1=Voice,2=Sfx,
    // 3=Ambience loop, 4=Ui one-shot; diğerleri mevcut else-Sfx dalına
    // düşer). StreamInfo snapshot kanalını da kaydeder.
    void playAudioInt(const std::string& assetPath, int channelInt,
                      DSPFilterType filter = DSPFilterType::Normal);
    void stopBgm();
    void stopAll();

    void setBgmVolume(float volume);
    void setMasterVolume(float volume);
    void setVoiceVolume(float volume);
    void setSfxVolume(float volume);
    // Faz 5 Dilim 1 — volume matrisi tamamlamaları ([0,1] clamp +
    // non-finite ignore, son geçerli değer korunur).
    void setAmbienceVolume(float volume);
    void setUiVolume(float volume);
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
    // Faz 5 Dilim 1 ekleri.
    float getAmbienceVolume() const { return m_ambienceVolume; }
    float getUiVolume() const { return m_uiVolume; }
    DSPFilterType getActiveFilter() const { return m_activeFilter; }
    bool isInitialized() const { return m_initialized; }
    bool isDuckingActive() const { return m_isDuckingActive; }
    bool isAudioDeviceAvailable() const { return m_deviceAvailable; }

    // Audio-device hotplug recovery. Call with an SDL audio-device event type
    // (SDL_EVENT_AUDIO_DEVICE_ADDED/REMOVED/FORMAT_CHANGED). Removal or format
    // changes rebuild the output streams while preserving playback intent
    // (BGM path/loop buffer, gains, filter, ducking); addition retries a
    // previously failed device open. Safe to call with no device present.
    void handleDeviceEvent(uint32_t sdlEventType);
    bool reopenDeviceStreams();

    // Window visibility policy. While suspended, all output streams stay
    // paused; playback intent (BGM position/state, gains) is preserved and
    // resumes automatically. Orthogonal to device availability.
    // Edge-triggered: repeat calls with the same state are no-ops so the
    // per-frame call from Engine::step costs nothing once settled.
    void setOutputSuspended(bool suspended, bool force = false);
    bool isOutputSuspended() const { return m_outputSuspended; }
    const std::string& getCurrentBgmPath() const { return m_currentBgmPath; }
    bool isBgmPlaying() const { return m_isBgmPlaying; }
    bool isVoicePlaying() const { return m_isVoicePlaying; }
    bool isBgmTransitionActive() const { return m_bgmTransitionActive; }
    const std::string& getLastError() const { return m_lastError; }

    // ── Faz 5 Dilim 1: OGG streaming çekirdek gözlemlenebilirliği ──
    // 1 = o anki BGM kararı stream, 0 = memory / unknown / yok (fail-closed).
    bool isStreaming() const { return m_isBgmStreamed; }
    // Ring refill: update() içinden senkron çağrılır (thread YOKTUR).
    void pumpBgmStream();
    // Kaynağı kapatır, ring indekslerini sıfırlar (intent korunur).
    void closeBgmStream();
    // Saf yönlendirme operatörü: assessLongAudio ile aynı strict `>`
    // semantiği; unknown/fail-closed girdilerde false (sessiz).
    static bool shouldStreamRoute(double durationSeconds,
                                  double thresholdSeconds);
    // Tek karar kaynağından üretilmiş StreamInfo JSON'u (caller-buffer
    // modeliyle C API üzerinden makine-tüketilebilir).
    std::string streamInfoJson() const;
    // Ring'de kuyruklu çözülmüş saniye (oynatmayı etkilemez).
    double bgmStreamBufferedSeconds() const;
    bool isAmbiencePlaying() const { return m_isAmbiencePlaying; }
    const std::string& getCurrentAmbiencePath() const { return m_currentAmbiencePath; }

    // Real-Time Audio Telemetry & VU Metering
    float getChannelPeak(int channelType, int channelIndex = 0) const;
    float getChannelRms(int channelType, int channelIndex = 0) const;
    void getSpectrumBands(float* outBands, int bandCount) const;

    // Typewriter Character Voice Blips & Audio Effects (Milestone 25)
    void playVoiceBlip(const std::string& assetPath = "", float pitch = 1.0f, float volume = 1.0f, AudioChannelType channel = AudioChannelType::Voice);
    uint32_t getVoiceBlipCount() const { return m_voiceBlipCount; }
    void resetVoiceBlipCount() { m_voiceBlipCount = 0; }
    float getLastVoiceBlipPitch() const { return m_lastVoiceBlipPitch; }

    void shutdown();

private:
    uint32_t m_voiceBlipCount = 0;
    float m_lastVoiceBlipPitch = 1.0f;
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
    bool m_outputSuspended = false;
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

    // ── Faz 5 Dilim 1 ekleri (mevcut üye/imza/sıra/formül değişmez) ──
    std::unique_ptr<OggStreamSource> m_bgmStreamSource;
    // 4x4096 frame sabit üst bant, heap'te bir kez ayrılır (interleaved
    // float; kapasite kanal sayısından bağımsız üst bantla tutulur).
    std::vector<float> m_bgmRing;
    uint64_t m_bgmRingWriteFrames = 0; // üretici (decode frontier sayacı)
    uint64_t m_bgmRingReadFrames = 0;  // tüketici sayacı (telemetri penceresi)
    uint64_t m_bgmStreamPcmPos = 0;    // kaynaktan çözülen toplam frame
    bool m_isBgmStreamed = false;
    bool m_bgmStreamEos = false;
    DSPFilterType m_bgmStreamFilter = DSPFilterType::Normal;
    float m_ambienceVolume = 1.0f;
    float m_uiVolume = 1.0f;
    SDL_AudioStream* m_ambienceStream = nullptr;
    std::vector<uint8_t> m_ambienceData; // float PCM, loop RAM
    size_t m_ambienceSampleOffset = 0;
    bool m_isAmbiencePlaying = false;
    std::string m_currentAmbiencePath;
    std::vector<uint8_t> m_uiData; // float PCM, one-shot (fiziksel sfxStream)
    size_t m_uiSampleOffset = 0;
    bool m_isUiPlaying = false;
    ChannelTelemetry m_telemetryAmbience;
    ChannelTelemetry m_telemetryUi;
    StreamInfo m_streamInfo; // snapshot: tek karar kaynağı
    int m_streamChannel = 0; // PlayAudio'ya verilen son int
    bool m_streamChannelFresh = false; // playAudioInt'ten taze kanal
    uint32_t m_bgmRingChannels = 2;
    uint32_t m_bgmStreamRateHz = 0;
    std::vector<float> m_bgmPumpScratch; // sabit chunk-cap scratch
    void queueStreamChunkToDevice(const float* samples, size_t frames,
                                  uint32_t channels, uint32_t sampleRate);
    void resetStreamInfoNoBgm();
    bool findBgmStreamCandidate(const std::string& assetPath,
                                std::string& candidateOut,
                                std::vector<uint8_t>& headerBytesOut);
    bool openBgmStream(const std::string& candidate,
                       const std::string& assetPath, int snapshotChannel,
                       DSPFilterType filter);
};

} // namespace Rowl::Audio
