#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "rowl/audio/audio_streaming.hpp"
#include "rowl/audio/fade_curves.hpp"
#include "rowl/audio/ogg_stream_source.hpp"
#include "rowl/audio/sfx_polyphony.hpp"
#include "rowl/audio/stream_mixer.hpp"

struct SDL_AudioStream;
struct SDL_AudioSpec; // yalnızca referansla kullanılır (tanım SDL3 başlığında)

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
    // Hedef #74: snapshot semantiği — çağrı anındaki değerin kopyası döner
    // (referans YOK; host thread okurken writer ezemez).
    std::string getLastError() const;

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
    // Bulgu #81: Ui durumu ambience ile simetriktir (bayat/çelişkili durum
    // kilidi bu erişimciler üzerinden gözlenir). Salt okuma; davranışsız.
    bool isUiPlaying() const { return m_isUiPlaying; }
    const std::string& getCurrentUiPath() const { return m_currentUiPath; }

    // ── Faz 5 Dilim 2: mixer / polyphony / eğriler / bed'ler / pump ──
    // StreamMixer applyChannelGains'in tek kazanç kaynağıdır (salt okuma).
    const StreamMixer& mixer() const { return m_mixer; }
    void setFadeCurve(FadeCurve curve) { m_fadeCurve = curve; }
    FadeCurve fadeCurve() const { return m_fadeCurve; }
    // SFX havuzu: derinlik [1,16], varsayılan 8; derinlik 1 = eski davranış.
    void setSfxPoolDepth(int depth);
    size_t sfxPoolDepth() const { return m_sfxPool.depth(); }
    size_t sfxActiveVoices() const { return m_sfxPool.activeCount(); }
    std::vector<std::string> sfxActivePaths() const;
    // Ambience bed'leri: 0 = BedA (miras tek-bed yolu), 1 = BedB (yeni).
    // Geçersiz bed: bool=false / no-op / 0.0f / "" (fail-closed).
    bool playAmbienceBed(int bed, const std::string& assetPath);
    void stopAmbienceBed(int bed);
    void setAmbienceBedVolume(int bed, float volume);
    float ambienceBedVolume(int bed) const;
    bool isAmbienceBedPlaying(int bed) const;
    std::string ambienceBedPath(int bed) const;
    // Bed'ler arası crossfade (süre<=0 anlık geçiş = bugünkü davranış).
    bool crossfadeAmbienceTo(const std::string& assetPath,
                             float durationSeconds, FadeCurve curve);
    bool isAmbienceCrossfadeActive() const { return m_ambCrossActive; }
    // Bed efektif kazancı: master*bedVol (* crossfade ölçeği).
    float ambienceBedGain(int bed) const;
    // pumpBgmStream maliyet gözlemlenebilirliği (fail kapısı YOK):
    // son-64 pump penceresi + sayaç. Suspend altında pump çalışmaz,
    // örnek de üretilmez.
    uint64_t bgmPumpSampleCount() const { return m_pumpCount; }
    uint64_t bgmPumpLastMicroseconds() const { return m_pumpLastUs; }
    uint64_t bgmPumpAvgMicroseconds() const;
    uint64_t bgmPumpMaxMicroseconds() const { return m_pumpMaxUs; }
    std::string bgmPumpStatsJson() const;

    // Real-Time Audio Telemetry & VU Metering
    float getChannelPeak(int channelType, int channelIndex = 0) const;
    float getChannelRms(int channelType, int channelIndex = 0) const;
    void getSpectrumBands(float* outBands, int bandCount) const;

    // Typewriter Character Voice Blips & Audio Effects (Milestone 25)
    void playVoiceBlip(const std::string& assetPath = "", float pitch = 1.0f, float volume = 1.0f, AudioChannelType channel = AudioChannelType::Voice);
    uint32_t getVoiceBlipCount() const { return m_voiceBlipCount; }
    // A5-tur3: synth-fallback ayırt edilebilirliği — synth dalında üretilen
    // blip'ler ayrıca sayılır (synth <= voice her zaman).
    uint32_t getSynthBlipCount() const { return m_synthBlipCount; }
    // A5-tur3: kuyruğa konamayan chunk'lar (update/pump Put-fail).
    uint64_t getDropCount() const { return m_dropCount; }
    void resetVoiceBlipCount() { m_voiceBlipCount = 0; m_synthBlipCount = 0; }
    float getLastVoiceBlipPitch() const;

    // #77 test-only latch: son applyDspToFloatPcm çıkışının max|örnek|
    // değeri (NaN-yapışkan: tek bir NaN çıkış mandalı NaN yapar, böylece
    // Telephone durum-zehiri VE CaveReverb gecikme-hattı zehiri aynı
    // mandalla gözlemlenir). Davranışsız gözlemdir; üretim kodu bunu
    // asla kullanmamalıdır (emsal: Rowl::Core::testEngineFromHandle).
    float testLastDspPeak() const;

    // Bulgu #81 test-only kancalar (davranışsız gözlem + deterministik
    // hata-enjeksiyonu; üretim kodu bunları asla kullanmamalıdır,
    // emsal: testLastDspPeak):
    //  - testFailNextQueue: bir sonraki playAudio kuyruk denemesini SDL'ye
    //    dokunmadan başarısız sayar (fail yolu birebir aynı çalışır; bayrak
    //    tüketilir). Gerçek SetFormat/Put hatasını deterministik kurmak
    //    mümkün olmadığı için kilit bu kancayla yazılır.
    //  - testFailCommitPut: testFailNextQueue emsali ikinci kanca; commit
    //    asamasindaki kuyruk-dususunu ayni erken-noktada SDL'ye dokunmadan
    //    basarisiz sayar (prova/Clear hic calismaz; bayrak tuketilir).
    //    Clear-sonrasi commit-Put dusus senaryosunun atomiklik iddiasini
    //    kilitler (queued-bytes==before). Uretimde yalniz hook + tuketim
    //    noktasi vardir, davranis degisikligi yoktur.
    //  - testQueuedBytes: kanal akışındaki kuyruklu bayt
    //    (SDL_GetAudioStreamAvailable; akış yoksa/hatada 0). SFX'te slot 0
    //    izlenir (derinlik 1 ile hedef deterministiktir).
    void testFailNextQueue() { m_testFailQueueNext = true; }
    void testFailCommitPut() { m_testFailCommitPutNext = true; }
    size_t testQueuedBytes(AudioChannelType channel) const;
    // Bulgu #82 test-only kanca (davranissiz state anahtari; uretim kodu bunu
    // asla kullanmamalidir, emsal: testFailNextQueue): cihaz outage'unu
    // deterministik kurar. Donus uretim yoluyla (reopenDeviceStreams)
    // gerceklesir; kanca donusu simulate etmez, yalniz outage'u acar.
    void testSetDeviceAvailable(bool available) { m_deviceAvailable = available; }

    void shutdown();

private:
    // Hedef #74: typewriter-blip yazımları ile host telemetri/hata okumaları
    // arasındaki data-race kilidi. Sayaçlar atomiktir (N×M hammer kayıpsız);
    // m_lastError snapshot semantiğiyle m_stateMutex altında okunur/yazılır
    // (cpp'deki warn-once statik kilidi üye durumunu korumaz); blip gövdesi,
    // pitch ve telemetri deflect'i de aynı kilitle serileşir.
    std::atomic<uint32_t> m_voiceBlipCount{0};
    std::atomic<uint32_t> m_synthBlipCount{0};
    std::atomic<uint64_t> m_dropCount{0};
    // Blip yolunda yazılan bayrak da atomiktir (hammera katılır).
    std::atomic<bool> m_isVoicePlaying{false};
    mutable std::mutex m_stateMutex;
    void setLastError(const std::string& message);
    void setLastErrorIfEmpty(const std::string& message);
    void clearLastError();
    std::string lastErrorSnapshot() const;
    bool lastErrorEmpty() const;
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
    // Bulgu #82: outage pending-BGM niyeti. Sessiz-yedekte (!m_deviceAvailable)
    // BGM play niyeti yazar ama PCM verisini getiremez (bayat m_bgmData + yeni
    // m_currentBgmPath ayrismasi). Commit noktasi reopenDeviceStreams basari
    // yoludur (gercek-donus gecisinde tam playAudio taahhudu: routing karari
    // dahil memory-RAM kuyrugu + olu-stream kaynagi tek-noktada). Basarili her
    // BGM commit'i ve explicit stopBgm pending'i tuketir. Fail-closed: decode/
    // queue duserse predecessor + snapshot + niyet korunur, m_lastError dolar,
    // pending tutulur (retry bir sonraki donuste; #87 sozlesmesi).
    std::string m_pendingBgmPath = "";
    // Bulgu #81 test-only: testFailNextQueue bayrağı (bir sonraki kuyruk
    // denemesinde tüketilir) + testFailCommitPut bayrağı (commit asamasinin
    // ayni erken-noktada tuketilen ikinci kancasi).
    bool m_testFailQueueNext = false;
    bool m_testFailCommitPutNext = false;
    std::vector<uint8_t> m_bgmData;
    std::vector<uint8_t> m_transitionBgmData;
    bool m_isBgmPlaying = false;
    // m_isVoicePlaying atomik bayrak olarak yukarıda (Hedef #74 bloğunda).
    SDL_AudioStream* m_bgmStream = nullptr;
    SDL_AudioStream* m_transitionBgmStream = nullptr;
    SDL_AudioStream* m_voiceStream = nullptr;
    // Faz 5 Dilim 2: SFX tek-stream (m_sfxStream) yerine havuzu; her slotun
    // kendi fiziksel akışı vardır (derinlik 1 = eski tek-stream davranış).
    // Ui one-shot için ayrı tekil akış (havuz dışı; telemetri ayrıdır).
    std::vector<SDL_AudioStream*> m_sfxPoolStreams;
    SDL_AudioStream* m_uiStream = nullptr;
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
    // Faz 5 Dilim 2: SFX durumu havuzdadır (slot başına PCM + offset +
    // playing). m_lastSfxData/m_sfxSampleOffset/m_isSfxPlaying kaldırıldı.
    SfxVoicePool m_sfxPool;

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
    // A5-tur3: m_bgmRingReadFrames KALDIRILDI (ölü-sayaç: yazılıyordu ama
    // hiçbir okuyucusu yoktu; yanıltıcı telemetri barındırmamak için silindi).
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
    std::vector<uint8_t> m_uiData; // float PCM, one-shot (m_uiStream)
    size_t m_uiSampleOffset = 0;
    bool m_isUiPlaying = false;
    std::string m_currentUiPath; // Bulgu #81: ambience-path simetriği
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

    // ── Faz 5 Dilim 2 üyeleri (mevcut üye/imza/sıra/formül değişmez) ──
    StreamMixer m_mixer; // applyChannelGains'in TEK kazanç kaynağı
    FadeCurve m_fadeCurve = FadeCurve::Linear; // BGM transition + amb cross
    // Ambience BedB (BedA miras üyelerdedir).
    SDL_AudioStream* m_ambienceStreamB = nullptr;
    std::vector<uint8_t> m_ambienceDataB;
    size_t m_ambienceSampleOffsetB = 0;
    bool m_isAmbiencePlayingB = false;
    std::string m_currentAmbiencePathB;
    float m_ambienceVolumeB = 1.0f;
    // Bed float formatı (reopen sonrası geri-kuyruk için; queue anında kayda
    // geçer; varsayılan 2ch/48kHz yalnızca format hiç görülmediyse kullanılır).
    int m_ambienceBedChannels[2] = {2, 2};
    int m_ambienceBedRateHz[2] = {48000, 48000};
    // Bed'ler arası crossfade durumu.
    bool m_ambCrossActive = false;
    int m_ambCrossFrom = 0;
    int m_ambCrossTo = 1;
    float m_ambCrossElapsed = 0.0f;
    float m_ambCrossDuration = 0.0f;
    FadeCurve m_ambCrossCurve = FadeCurve::Linear;
    // pumpBgmStream maliyet penceresi (son 64 örnek + sayaç/maks).
    uint64_t m_pumpCount = 0;
    uint64_t m_pumpLastUs = 0;
    uint64_t m_pumpMaxUs = 0;
    std::array<uint64_t, 64> m_pumpWindow{};
    size_t m_pumpWindowPos = 0;
    void recordPumpSample(uint64_t microseconds);
    static bool isValidAmbienceBed(int bed) { return bed == 0 || bed == 1; }
    SDL_AudioStream* ambienceBedStream(int bed) const;
    void clearAmbienceBed(int bed);
    void cancelAmbienceCrossfade();
    void updateAmbienceCrossfade(float deltaSeconds);
    float ambienceCrossScale(int bed, float progress) const;
    void queueAmbienceBed(int bed, const SDL_AudioSpec& floatSpec,
                          const uint8_t* floatBytes, size_t byteCount,
                          const std::string& assetPath);
    void ensureSfxPoolStreams();
    void destroySfxPoolStreams();
    // playAudio decode bloğunun birebir çıkarımı (kısa-ses full-decode
    // byte-identical; eski satır-içi kod bu yordamı çağırır).
    bool decodeAssetToFloatPcm(const std::string& assetPath,
                               DSPFilterType filter, bool applyUiGain,
                               bool channelIsBgm,
                               SDL_AudioSpec& specOut,
                               std::vector<uint8_t>& floatPcmOut);
};

} // namespace Rowl::Audio
