#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/sdl_subsystem_lease.hpp"
#include "rowl/vfs/vfs.hpp"
#include <SDL3/SDL.h>
#include <vorbis/vorbisfile.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <climits>
#include <cstring>
#include <istream>
#include <vector>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_set>

namespace Rowl::Audio {

namespace {

constexpr uintmax_t kMaxEncodedAudioBytes = 64ULL * 1024 * 1024;
constexpr Uint32 kMaxDecodedAudioBytes = 64U * 1024 * 1024;

// A5-tur1: per-frame/per-pump SDL hata log'ları için değer-başı warn-once
// (A3-tur7 transition deseni: cap-32, spam yok). m_lastError'e DOKUNMAZ:
// m_lastError "son tamamlanmış API çağrısı" snapshot'ıdır; update-thread
// path'leri onu ezmemelidir.
constexpr size_t kAudioWarnOnceCap = 32;

void warnAudioOnce(const std::string& message) {
    static std::mutex mutex;
    static std::unordered_set<std::string> warned;
    std::lock_guard<std::mutex> lock(mutex);
    if (warned.size() >= kAudioWarnOnceCap || !warned.insert(message).second) return;
    ROWL_LOG_WARN("[AudioEngine] " + message + " (logged once per value)");
}

size_t vorbisRead(void* pointer, size_t size, size_t count, void* datasource) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream || size == 0 || count == 0) return 0;
    const auto requested = std::min<uint64_t>(
        static_cast<uint64_t>(size) * count, kMaxEncodedAudioBytes);
    stream->read(static_cast<char*>(pointer), static_cast<std::streamsize>(requested));
    return static_cast<size_t>(stream->gcount()) / size;
}

int vorbisSeek(void* datasource, ogg_int64_t offset, int whence) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream) return -1;
    std::ios_base::seekdir direction;
    switch (whence) {
        case SEEK_SET: direction = std::ios::beg; break;
        case SEEK_CUR: direction = std::ios::cur; break;
        case SEEK_END: direction = std::ios::end; break;
        default: return -1;
    }
    stream->clear();
    stream->seekg(static_cast<std::streamoff>(offset), direction);
    return stream->fail() ? -1 : 0;
}

int vorbisClose(void*) { return 0; } // VFS retains ownership of the stream.

long vorbisTell(void* datasource) {
    auto* stream = static_cast<std::istream*>(datasource);
    if (!stream) return -1;
    const auto position = stream->tellg();
    return position < 0 || position > LONG_MAX ? -1 : static_cast<long>(position);
}

bool hasOggExtension(const std::string& path) {
    if (path.size() < 4) return false;
    return std::tolower(static_cast<unsigned char>(path[path.size() - 4])) == '.' &&
           std::tolower(static_cast<unsigned char>(path[path.size() - 3])) == 'o' &&
           std::tolower(static_cast<unsigned char>(path[path.size() - 2])) == 'g' &&
           std::tolower(static_cast<unsigned char>(path[path.size() - 1])) == 'g';
}

bool decodeOggVorbis(std::istream& stream, SDL_AudioSpec& spec, std::vector<uint8_t>& pcm,
                     std::string& error) {
    OggVorbis_File decoder{};
    const ov_callbacks callbacks{vorbisRead, vorbisSeek, vorbisClose, vorbisTell};
    if (ov_open_callbacks(&stream, &decoder, nullptr, 0, callbacks) < 0) {
        error = "Ogg/Vorbis stream could not be opened";
        return false;
    }
    const vorbis_info* info = ov_info(&decoder, -1);
    if (!info || info->channels <= 0 || info->channels > 8 || info->rate <= 0) {
        ov_clear(&decoder);
        error = "Ogg/Vorbis stream has an unsupported audio format";
        return false;
    }
    spec = {};
    spec.format = SDL_AUDIO_S16;
    spec.channels = static_cast<Uint8>(info->channels);
    spec.freq = static_cast<int>(info->rate);
    std::array<char, 32 * 1024> chunk{};
    int bitstream = 0;
    while (true) {
        const long bytes = ov_read(&decoder, chunk.data(), static_cast<int>(chunk.size()), 0, 2, 1, &bitstream);
        if (bytes == 0) break;
        if (bytes < 0 || pcm.size() > kMaxDecodedAudioBytes - static_cast<size_t>(bytes)) {
            ov_clear(&decoder);
            error = bytes < 0 ? "Ogg/Vorbis stream is corrupt" : "Decoded Ogg/Vorbis audio exceeds the maximum accepted size";
            return false;
        }
        pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + bytes);
    }
    ov_clear(&decoder);
    if (pcm.empty()) { error = "Ogg/Vorbis stream contains no PCM samples"; return false; }
    return true;
}

// #77 test-only mandal: son applyDspToFloatPcm çıkışının max|örnek|
// değeri. Gövde-içi olduğu için üç çağırıcıyı (playAudio RAM,
// decodeAssetToFloatPcm, pumpBgmStream) ek kablosuz kapsar. Tek float
// yazma — DSP davranışına dokunmaz. NaN-yapışkan semantik: `>` tek
// başına CaveReverb koldaki seyrek NaN'i (gecikme hattı 5512 frame ≫
// 64-örnek fixture) sonlu clamp komşuları arasında kaybederdi; tek bir
// NaN çıkışı mandalı NaN yapar, böylece sanitize-silme Telephone VE
// CaveReverb dallarında da DÜŞER (kırmızı-kanıt matrisi "0/NaN" kolu).
float g_testLastDspPeak = 0.0f;

void recordDspPeak(const float* samples, size_t sampleCount) {
    for (size_t i = 0; i < sampleCount; ++i) {
        const float a = std::fabs(samples[i]);
        if (std::isnan(a) || a > g_testLastDspPeak) g_testLastDspPeak = a;
    }
}

void applyDspToFloatPcm(float* samples, size_t sampleCount, int channels,
                        int sampleRate, DSPFilterType filter) {
    // Deterministik başlangıç: testler-arası sızıntı yok.
    g_testLastDspPeak = 0.0f;
    if (!samples || sampleCount == 0 || channels <= 0 || sampleRate <= 0) {
        return;
    }
    if (filter == DSPFilterType::Normal) {
        // Passthrough dalı: çıkış = giriş; mandal yine yazılır (kalıcılık
        // gözlemi temiz-Normal çalışın canlı sinyal taşıdığını kanıtlar).
        recordDspPeak(samples, sampleCount);
        return;
    }

    // D5 (#77): güvenilmeyen PCM tek noktada sanitize edilir. IEEE-float
    // WAV'deki tek NaN/Inf örnek, Telephone dalında lowPass durumunu,
    // CaveReverb dalında gecikme hattını kalıcı zehirlerdi (clamp NaN'i
    // geçirir; Underwater dalında clamp hiç yoktu). Filtreye girmeden
    // önce sonlu-olmayan örnekler sessizliğe çekilir.
    for (size_t i = 0; i < sampleCount; ++i) {
        if (!std::isfinite(samples[i])) samples[i] = 0.0f;
    }

    const size_t channelCount = static_cast<size_t>(channels);
    std::vector<float> lowPass(channelCount, 0.0f);
    std::vector<float> highPass(channelCount, 0.0f);
    constexpr float kTelephoneLowPassAlpha = 0.36f;
    constexpr float kUnderwaterLowPassAlpha = 0.10f;

    for (size_t sample = 0; sample < sampleCount; ++sample) {
        const size_t channel = sample % channelCount;
        const float input = samples[sample];
        switch (filter) {
            case DSPFilterType::Telephone: {
                // Cascaded high/low-pass filtering keeps only the speech band.
                lowPass[channel] += kTelephoneLowPassAlpha * (input - lowPass[channel]);
                const float hp = lowPass[channel] - highPass[channel];
                highPass[channel] = lowPass[channel];
                samples[sample] = std::clamp(hp * 2.1f, -1.0f, 1.0f);
                break;
            }
            case DSPFilterType::UnderwaterLowPass:
                lowPass[channel] += kUnderwaterLowPassAlpha * (input - lowPass[channel]);
                // D5 (#77): diğer dallardaki [-1,1] clamp'i burada da —
                // sanitize'e rağmen dal çıktısı sınırlı kalır.
                samples[sample] = std::clamp(lowPass[channel], -1.0f, 1.0f);
                break;
            case DSPFilterType::CaveReverb: {
                // A short feedback tap is intentionally bounded below one second,
                // so an untrusted clip can never allocate an unbounded delay line.
                const size_t delayFrames = static_cast<size_t>(std::max(1, sampleRate / 8));
                const size_t delaySamples = delayFrames * channelCount;
                const float delayed = sample >= delaySamples ? samples[sample - delaySamples] : 0.0f;
                samples[sample] = std::clamp(input + delayed * 0.28f, -1.0f, 1.0f);
                break;
            }
            case DSPFilterType::Normal:
                break;
        }
    }

    // #77 test-only mandal yazma noktası (gövde sonu, tek float yazma).
    recordDspPeak(samples, sampleCount);
}

} // namespace

// #77 test-only accessor (üretim kodu kullanmaz).
float AudioEngine::testLastDspPeak() const {
    return g_testLastDspPeak;
}

// Bulgu #81 test-only gözlem (üretim kodu kullanmaz; salt okuma).
size_t AudioEngine::testQueuedBytes(AudioChannelType channel) const {
    const SDL_AudioStream* stream = nullptr;
    switch (channel) {
        case AudioChannelType::Bgm: stream = m_bgmStream; break;
        case AudioChannelType::Voice: stream = m_voiceStream; break;
        case AudioChannelType::Ambience: stream = m_ambienceStream; break;
        case AudioChannelType::Ui: stream = m_uiStream; break;
        case AudioChannelType::Sfx:
            stream = m_sfxPoolStreams.empty() ? nullptr : m_sfxPoolStreams[0];
            break;
    }
    if (!stream) return 0;
    const int available =
        SDL_GetAudioStreamAvailable(const_cast<SDL_AudioStream*>(stream));
    return (available > 0) ? static_cast<size_t>(available) : 0;
}

AudioEngine::AudioEngine(Rowl::VFS::VFSManager* vfs)
    : m_vfs(vfs) {
    if (!m_vfs) {
        m_ownedVfs = std::make_shared<Rowl::VFS::VFSManager>();
        m_vfs = m_ownedVfs.get();
    }
}

AudioEngine::~AudioEngine() {
    if (m_initialized) {
        shutdown();
    }
}

void AudioEngine::setVfs(Rowl::VFS::VFSManager* vfs) {
    if (vfs) {
        m_ownedVfs.reset();
        m_vfs = vfs;
    } else {
        m_ownedVfs = std::make_shared<Rowl::VFS::VFSManager>();
        m_vfs = m_ownedVfs.get();
    }
}

Rowl::VFS::VFSManager& AudioEngine::vfs() const {
    return *m_vfs;
}

bool AudioEngine::initialize() {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing Dual-Path SDL3 Audio Engine Subsystem...");
    m_masterVolume = 1.0f;
    m_bgmVolume = 1.0f;
    m_voiceVolume = 1.0f;
    m_sfxVolume = 1.0f;
    m_bgmGain = 1.0f;
    m_duckingFactor = 0.5f;
    m_activeFilter = DSPFilterType::Normal;
    m_isDuckingActive = false;
    m_currentBgmPath = "";
    m_lastError.clear();
    m_isBgmPlaying = false;
    m_isVoicePlaying = false;
    m_deviceAvailable = false;
    m_outputSuspended = false;
    m_bgmTransitionActive = false;
    m_bgmTransitionElapsedSeconds = 0.0f;
    m_bgmTransitionDurationSeconds = 0.0f;
    m_telemetryBgm = {};
    m_telemetryVoice = {};
    m_telemetrySfx = {};
    m_telemetryMaster = {};
    m_spectrumBands.fill(0.0f);
    m_bgmSampleOffset = 0;
    // Faz 5 Dilim 2: havuz + mixer + eğri + bedB + crossfade + pump sıfırlanır.
    m_sfxPool = SfxVoicePool{};
    destroySfxPoolStreams();
    m_mixer = StreamMixer{};
    m_fadeCurve = FadeCurve::Linear;
    m_voiceBlipCount = 0;
    m_lastVoiceBlipPitch = 1.0f;
    // Faz 5 Dilim 1 ekleri: hacim matrisi varsayılanları + stream durumu.
    m_ambienceVolume = 1.0f;
    m_uiVolume = 1.0f;
    m_bgmStreamSource.reset();
    m_bgmRingWriteFrames = 0;
    m_bgmStreamPcmPos = 0;
    m_isBgmStreamed = false;
    m_bgmStreamEos = false;
    m_bgmStreamFilter = DSPFilterType::Normal;
    m_bgmRingChannels = 2;
    m_bgmStreamRateHz = 0;
    m_ambienceData.clear();
    m_ambienceSampleOffset = 0;
    m_isAmbiencePlaying = false;
    m_currentAmbiencePath.clear();
    // Faz 5 Dilim 2: BedB + crossfade + pump sayacı sıfırlanır.
    m_ambienceVolumeB = 1.0f;
    m_ambienceDataB.clear();
    m_ambienceSampleOffsetB = 0;
    m_isAmbiencePlayingB = false;
    m_currentAmbiencePathB.clear();
    cancelAmbienceCrossfade();
    m_pumpCount = 0;
    m_pumpLastUs = 0;
    m_pumpMaxUs = 0;
    m_pumpWindow.fill(0);
    m_pumpWindowPos = 0;
    m_uiData.clear();
    m_uiSampleOffset = 0;
    m_isUiPlaying = false;
    m_currentUiPath.clear();
    m_telemetryAmbience = {};
    m_telemetryUi = {};
    m_streamInfo = StreamInfo{};
    m_streamChannel = 0;
    m_streamChannelFresh = false;

    // Initialize SDL3 Audio subsystem
    if (Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_AUDIO)) {
        m_audioLeaseHeld = true;
        // A5-tur1: tekil akışın hangisinin açılamadığı kayda geçer (toplu
        // null-check öncesinde ilk hata korunur; davranış değişmez).
        auto openDeviceStreamChecked = [&](const char* streamName) {
            SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
            if (!stream && m_lastError.empty()) {
                m_lastError = std::string("Audio stream could not be opened (") +
                              streamName + "): " + SDL_GetError();
            }
            return stream;
        };
        // Open default audio device stream for BGM
        m_bgmStream = openDeviceStreamChecked("BGM");
        // A second BGM stream lets a new, already-decoded track be queued
        // before the current track is touched. This is what makes transition
        // failures transactional and enables a real crossfade.
        m_transitionBgmStream = openDeviceStreamChecked("transition-BGM");
        // Voice has its own gain path so narration controls never affect SFX.
        m_voiceStream = openDeviceStreamChecked("voice");
        // Faz 5 Dilim 2: SFX havuz akışları (slot başına bir akış).
        ensureSfxPoolStreams();
        // Faz 5 Dilim 1: Ambience loop RAM için kendi akışı (karışım yok,
        // yalnızca bağımsız gain + loop besleme).
        // Faz 5 Dilim 2: BedB için ikinci ambience akışı.
        m_ambienceStream = openDeviceStreamChecked("ambience");
        m_ambienceStreamB = openDeviceStreamChecked("ambience-B");
        // Faz 5 Dilim 2: Ui one-shot ayrı tekil akış (havuz dışı kalır).
        m_uiStream = openDeviceStreamChecked("UI");

        if (m_bgmStream && m_transitionBgmStream && m_voiceStream && m_ambienceStream && m_ambienceStreamB && m_uiStream && !m_sfxPoolStreams.empty()) {
            bool sfxReady = true;
            for (SDL_AudioStream* stream : m_sfxPoolStreams) sfxReady = sfxReady && (stream != nullptr);
            if (!sfxReady) {
                ROWL_LOG_WARN("[AudioEngine] SFX pool streams could not be opened: " + std::string(SDL_GetError()) +
                              " — running in silent fallback mode.");
                if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
                if (m_transitionBgmStream) { SDL_DestroyAudioStream(m_transitionBgmStream); m_transitionBgmStream = nullptr; }
                if (m_voiceStream) { SDL_DestroyAudioStream(m_voiceStream); m_voiceStream = nullptr; }
                destroySfxPoolStreams();
                if (m_ambienceStream) { SDL_DestroyAudioStream(m_ambienceStream); m_ambienceStream = nullptr; }
                if (m_ambienceStreamB) { SDL_DestroyAudioStream(m_ambienceStreamB); m_ambienceStreamB = nullptr; }
                if (m_uiStream) { SDL_DestroyAudioStream(m_uiStream); m_uiStream = nullptr; }
            } else {
                m_deviceAvailable = true;
                applyChannelGains();
                ROWL_LOG_INFO("[AudioEngine] Physical audio device initialized successfully (BGM, Voice & SFX streams active).");
            }
        } else {
            ROWL_LOG_WARN("[AudioEngine] Audio streams could not be opened: " + std::string(SDL_GetError()) +
                          " — running in silent fallback mode.");
            if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
            if (m_transitionBgmStream) { SDL_DestroyAudioStream(m_transitionBgmStream); m_transitionBgmStream = nullptr; }
            if (m_voiceStream) { SDL_DestroyAudioStream(m_voiceStream); m_voiceStream = nullptr; }
            destroySfxPoolStreams();
            if (m_ambienceStream) { SDL_DestroyAudioStream(m_ambienceStream); m_ambienceStream = nullptr; }
            if (m_ambienceStreamB) { SDL_DestroyAudioStream(m_ambienceStreamB); m_ambienceStreamB = nullptr; }
            if (m_uiStream) { SDL_DestroyAudioStream(m_uiStream); m_uiStream = nullptr; }
        }
    } else {
        ROWL_LOG_WARN("[AudioEngine] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: " + std::string(SDL_GetError()) +
                      " — running in silent fallback mode.");
    }

    m_initialized = true;
    ROWL_LOG_INFO("Audio Engine Subsystem Initialized Successfully (Hardware Available: " +
                  std::string(m_deviceAvailable ? "YES" : "NO") + ").");
    return true;
}

// ── Faz 5 Dilim 2: playAudio decode bloğunun birebir çıkarımı ─────────────
// Kısa-ses full-decode yolu byte-identical korunur; eski satır-içi kod ile
// bu yordam aynı baytları üretir (VFS aday sırası, OGG/WAV dalları, cap
// kontrolleri, float dönüşümü, DSP, Ui gain bake aynen). channelIsBgm
// true iken BGM hata yollarındaki closeBgmStream+resetStreamInfoNoBgm
// davranışı da aynen korunur.
bool AudioEngine::decodeAssetToFloatPcm(const std::string& assetPath,
                                        DSPFilterType filter, bool applyUiGain,
                                        bool channelIsBgm,
                                        SDL_AudioSpec& specOut,
                                        std::vector<uint8_t>& floatPcmOut) {
    std::vector<uint8_t> bytes;
    SDL_AudioSpec spec;
    Uint8* audioBuf = nullptr;
    Uint32 audioLen = 0;
    bool loaded = false;

    // Audio assets resolve only through the selected project's VFS. The VFS
    // itself applies path-isolation and encoded-size limits before SDL sees
    // any bytes.
    std::vector<std::string> vfsCandidates = {
        assetPath,
        "Assets/" + assetPath,
        "Assets/audio/" + assetPath,
        "audio/" + assetPath
    };
    for (const auto& candidate : vfsCandidates) {
        if (vfs().exists(candidate)) {
            if (hasOggExtension(candidate)) {
                auto stream = vfs().openReadStream(candidate);
                if (stream && decodeOggVorbis(*stream, spec, bytes, m_lastError)) {
                    audioBuf = static_cast<Uint8*>(SDL_malloc(bytes.size()));
                    if (!audioBuf) {
                        m_lastError = "Unable to allocate decoded Ogg/Vorbis PCM";
                        if (channelIsBgm) {
                            closeBgmStream();
                            resetStreamInfoNoBgm();
                        }
                        return false;
                    }
                    std::memcpy(audioBuf, bytes.data(), bytes.size());
                    audioLen = static_cast<Uint32>(bytes.size());
                    loaded = true;
                    break;
                }
                if (m_lastError.empty()) m_lastError = "Ogg/Vorbis stream could not be decoded";
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError + ": " + assetPath);
                // Stale stream karari yalnizca Bgm kanalinda korunmaz (#78):
                // non-BGM miss baska kanalin stream/snapshot state'ine
                // dokunmaz.
                if (channelIsBgm) {
                    closeBgmStream();
                    resetStreamInfoNoBgm();
                }
                return false;
            }
            bytes = vfs().readBytes(candidate);
            if (!bytes.empty()) {
                if (bytes.size() > kMaxEncodedAudioBytes) {
                    // A5-tur1: 64 MiB reddi caller'a ulaşır (önce log-only idi,
                    // C API PlayAudio kördü).
                    m_lastError = "Audio file exceeds the maximum accepted size: " + assetPath;
                    ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
                    if (channelIsBgm) {
                        closeBgmStream();
                        resetStreamInfoNoBgm();
                    }
                    return false;
                }
                SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
                if (io) {
                    loaded = SDL_LoadWAV_IO(io, true, &spec, &audioBuf, &audioLen);
                    if (loaded) break;
                }
            }
        }
    }

    if (loaded && audioBuf && audioLen > 0) {
        if (audioLen > kMaxDecodedAudioBytes || audioLen > static_cast<Uint32>(INT_MAX)) {
            // A5-tur1: decode-cap reddi caller'a ulaşır (önce log-only idi).
            m_lastError = "Decoded audio exceeds the maximum accepted size: " + assetPath;
            ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            SDL_free(audioBuf);
            if (channelIsBgm) {
                closeBgmStream();
                resetStreamInfoNoBgm();
            }
            return false;
        }
        SDL_AudioSpec floatSpec{};
        floatSpec.format = SDL_AUDIO_F32;
        floatSpec.channels = spec.channels;
        floatSpec.freq = spec.freq;
        Uint8* floatBuffer = nullptr;
        int floatLength = 0;
        if (!SDL_ConvertAudioSamples(&spec, audioBuf, static_cast<int>(audioLen),
                                     &floatSpec, &floatBuffer, &floatLength) ||
            !floatBuffer || floatLength <= 0) {
            m_lastError = "Unable to convert decoded audio to float PCM: " + std::string(SDL_GetError());
            ROWL_LOG_ERROR("[AudioEngine] " + m_lastError);
            SDL_free(audioBuf);
            if (channelIsBgm) {
                closeBgmStream();
                resetStreamInfoNoBgm();
            }
            return false;
        }
        auto* samples = reinterpret_cast<float*>(floatBuffer);
        applyDspToFloatPcm(samples, static_cast<size_t>(floatLength) / sizeof(float),
                           floatSpec.channels, floatSpec.freq, filter);
        // Faz 5 Dilim 1: Ui one-shot kazancı örneklere işlenir; fiziksel
        // akış kazancı (master*sfx zinciri) aynen kalır.
        if (applyUiGain) {
            const size_t uiSamples = static_cast<size_t>(floatLength) / sizeof(float);
            for (size_t i = 0; i < uiSamples; ++i) samples[i] *= m_uiVolume;
        }
        specOut = floatSpec;
        floatPcmOut.assign(floatBuffer, floatBuffer + floatLength);
        SDL_free(floatBuffer);
        SDL_free(audioBuf);
        return true;
    }
    m_lastError = "Audio file could not be decoded (supported: WAV, OGG/Vorbis): " + assetPath;
    ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
    // Stale stream karari yalnizca Bgm kanalinda korunmaz (#78).
    if (channelIsBgm) {
        closeBgmStream();
        resetStreamInfoNoBgm();
    }
    return false;
}

void AudioEngine::playBgm(const std::string& assetPath, BgmTransitionKind transition, float durationSeconds) {
    if (!std::isfinite(durationSeconds)) durationSeconds = 0.0f;
    m_requestedBgmTransition = transition;
    m_requestedBgmTransitionDurationSeconds = std::clamp(durationSeconds, 0.0f, 60.0f);
    playAudio(assetPath, AudioChannelType::Bgm);
    // playAudio consumes this request synchronously; never leak it into a
    // later direct BGM call made by the legacy C API.
    m_requestedBgmTransition = BgmTransitionKind::Instant;
    m_requestedBgmTransitionDurationSeconds = 0.0f;
}

void AudioEngine::playAudio(const std::string& assetPath, AudioChannelType channel, DSPFilterType filter) {
    // Bilinçli muafiyet: bu guard hiçbir state'e dokunmayan saf no-op'tur
    // (m_lastError.clear() bile çalışmaz); snapshot son tamamlanmış play
    // çağrısına aittir, o yüzden reset YOKTUR.
    if (!m_initialized || assetPath.empty()) return;

    m_lastError.clear();

    // ── Faz 5 Dilim 1: snapshot kanalı + Bgm header probe (RAM bloğu öncesi) ──
    // Karar operatörü long_audio_contract'tır (strict `>`); burada formül
    // YOKTUR. Kısa-ses RAM decode bloğu aşağıda satır satır aynen korunur.
    int snapshotChannel = (channel == AudioChannelType::Bgm) ? 0 :
                          (channel == AudioChannelType::Voice) ? 1 :
                          (channel == AudioChannelType::Sfx) ? 2 :
                          (channel == AudioChannelType::Ambience) ? 3 : 4;
    if (m_streamChannelFresh) {
        snapshotChannel = m_streamChannel;
        m_streamChannelFresh = false;
    }
    m_streamChannel = snapshotChannel;
    StreamInfo probedInfo;
    bool probedForBgm = false;
    std::string probeCandidate;
    if (channel == AudioChannelType::Bgm) {
        std::vector<uint8_t> probeBytes;
        if (findBgmStreamCandidate(assetPath, probeCandidate, probeBytes)) {
            probedInfo = makeStreamInfoFromHeader(probeBytes.data(),
                                                  probeBytes.size(),
                                                  snapshotChannel, assetPath);
            probedForBgm = true;
        }
    }

    std::string channelName = (channel == AudioChannelType::Bgm) ? "BGM (Streaming)" :
                              (channel == AudioChannelType::Voice) ? "Voice" :
                              (channel == AudioChannelType::Ambience) ? "Ambience (Loop)" :
                              (channel == AudioChannelType::Ui) ? "Ui (One-Shot)" : "SFX (Memory Pool)";

    ROWL_LOG_INFO("Audio Play -> Asset: '" + assetPath + "' on Channel: " + channelName);

    if (!m_deviceAvailable) {
        // Keep intended BGM state in headless/silent environments. This lets
        // scene transitions remain deterministic even when no device exists.
        if (channel == AudioChannelType::Bgm) {
            // Sessiz yedek akış açmaz: önceki kaynak kapatılır, karar
            // memory/unknown olarak kayda geçer (fail-closed).
            closeBgmStream();
            m_currentBgmPath = assetPath;
            m_isBgmPlaying = true;
            m_bgmTransitionActive = false;
            if (probedForBgm) {
                m_streamInfo = probedInfo;
                if (m_streamInfo.mode == StreamMode::Stream) {
                    m_streamInfo.mode = StreamMode::Memory;
                    // Sessiz yedekte RAM'e indirgenen over-threshold kararı:
                    // reason da memory ile tutarlı yazılır (bilinen header).
                    m_streamInfo.reason = "under_threshold";
                }
                m_streamInfo.bufferedSeconds = 0.0;
            } else {
                // Probe yoksa eski snapshot korunamaz (IsStreaming/JSON
                // diverge olur): karar unknown olarak kayda geçer.
                resetStreamInfoNoBgm();
            }
        }
        if (channel == AudioChannelType::Ambience) {
            m_currentAmbiencePath = assetPath;
            m_isAmbiencePlaying = true;
        }
        if (channel == AudioChannelType::Voice) {
            m_isVoicePlaying = true;
            triggerVoiceDucking(true);
        }
        if (filter != DSPFilterType::Normal) applyDspFilter(filter);
        ROWL_LOG_INFO("[AudioEngine] Audio play registered (silent fallback): " + assetPath);
        return;
    }

    // ── Faz 5 Dilim 1: streaming taahhüdü (yalnız Bgm + OGG + over-threshold).
    // Başarıda RAM bloğuna girmeden döner; akış açılamazsa closeBgmStream
    // + resetStreamInfoNoBgm ile fail-closed kapanır (hata kayda geçer).
    if (channel == AudioChannelType::Bgm && probedForBgm &&
        probedInfo.mode == StreamMode::Stream) {
        if (hasOggExtension(probeCandidate)) {
            if (openBgmStream(probeCandidate, assetPath, snapshotChannel,
                              filter)) {
                return;
            }
            if (m_lastError.empty()) {
                m_lastError = "Ogg/Vorbis stream could not be opened: " + assetPath;
            }
            ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            closeBgmStream();
            resetStreamInfoNoBgm();
            return;
        }
        // Over-threshold ama akışlanamayan konteyner (WAV): mevcut RAM yolu
        // aynen denenir (decode cap fail-closed reddeder).
    }

    std::vector<uint8_t> bytes;
    SDL_AudioSpec spec;
    Uint8* audioBuf = nullptr;
    Uint32 audioLen = 0;
    bool loaded = false;

    // Audio assets resolve only through the selected project's VFS. The VFS
    // itself applies path-isolation and encoded-size limits before SDL sees
    // any bytes.
    std::vector<std::string> vfsCandidates = {
        assetPath,
        "Assets/" + assetPath,
        "Assets/audio/" + assetPath,
        "audio/" + assetPath
    };
    for (const auto& candidate : vfsCandidates) {
        if (vfs().exists(candidate)) {
            if (hasOggExtension(candidate)) {
                auto stream = vfs().openReadStream(candidate);
                if (stream && decodeOggVorbis(*stream, spec, bytes, m_lastError)) {
                    audioBuf = static_cast<Uint8*>(SDL_malloc(bytes.size()));
                    if (!audioBuf) {
                        m_lastError = "Unable to allocate decoded Ogg/Vorbis PCM";
                        if (channel == AudioChannelType::Bgm) {
                            closeBgmStream();
                            resetStreamInfoNoBgm();
                        }
                        return;
                    }
                    std::memcpy(audioBuf, bytes.data(), bytes.size());
                    audioLen = static_cast<Uint32>(bytes.size());
                    loaded = true;
                    break;
                }
                if (m_lastError.empty()) m_lastError = "Ogg/Vorbis stream could not be decoded";
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError + ": " + assetPath);
                // Stale stream karari yalnizca Bgm kanalinda korunmaz (#78):
                // non-BGM miss baska kanalin stream/snapshot state'ine
                // dokunmaz.
                if (channel == AudioChannelType::Bgm) {
                    closeBgmStream();
                    resetStreamInfoNoBgm();
                }
                return;
            }
            bytes = vfs().readBytes(candidate);
            if (!bytes.empty()) {
                if (bytes.size() > kMaxEncodedAudioBytes) {
                    // A5-tur1: 64 MiB reddi caller'a ulaşır (önce log-only idi,
                    // C API PlayAudio kördü).
                    m_lastError = "Audio file exceeds the maximum accepted size: " + assetPath;
                    ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
                    if (channel == AudioChannelType::Bgm) {
                        closeBgmStream();
                        resetStreamInfoNoBgm();
                    }
                    return;
                }
                SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
                if (io) {
                    loaded = SDL_LoadWAV_IO(io, true, &spec, &audioBuf, &audioLen);
                    if (loaded) break;
                }
            }
        }
    }

    if (loaded && audioBuf && audioLen > 0) {
        if (audioLen > kMaxDecodedAudioBytes || audioLen > static_cast<Uint32>(INT_MAX)) {
            // A5-tur1: decode-cap reddi caller'a ulaşır (önce log-only idi).
            m_lastError = "Decoded audio exceeds the maximum accepted size: " + assetPath;
            ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            SDL_free(audioBuf);
            if (channel == AudioChannelType::Bgm) {
                closeBgmStream();
                resetStreamInfoNoBgm();
            }
            return;
        }
        SDL_AudioSpec floatSpec{};
        floatSpec.format = SDL_AUDIO_F32;
        floatSpec.channels = spec.channels;
        floatSpec.freq = spec.freq;
        Uint8* floatBuffer = nullptr;
        int floatLength = 0;
        if (!SDL_ConvertAudioSamples(&spec, audioBuf, static_cast<int>(audioLen),
                                     &floatSpec, &floatBuffer, &floatLength) ||
            !floatBuffer || floatLength <= 0) {
            m_lastError = "Unable to convert decoded audio to float PCM: " + std::string(SDL_GetError());
            ROWL_LOG_ERROR("[AudioEngine] " + m_lastError);
            SDL_free(audioBuf);
            if (channel == AudioChannelType::Bgm) {
                closeBgmStream();
                resetStreamInfoNoBgm();
            }
            return;
        }
        auto* samples = reinterpret_cast<float*>(floatBuffer);
        applyDspToFloatPcm(samples, static_cast<size_t>(floatLength) / sizeof(float),
                           floatSpec.channels, floatSpec.freq, filter);
        // Faz 5 Dilim 1: Ui one-shot kazancı örneklere işlenir; fiziksel
        // akış kazancı (master*sfx zinciri) aynen kalır.
        if (channel == AudioChannelType::Ui) {
            const size_t uiSamples = static_cast<size_t>(floatLength) / sizeof(float);
            for (size_t i = 0; i < uiSamples; ++i) samples[i] *= m_uiVolume;
        }

        const bool transitionRequested = channel == AudioChannelType::Bgm &&
            m_isBgmPlaying && !m_currentBgmPath.empty() &&
            m_requestedBgmTransition != BgmTransitionKind::Instant &&
            m_requestedBgmTransitionDurationSeconds > 0.0f;
        // Faz 5 Dilim 2: SFX havuz slotu decode SONRASI seçilir (slotun
        // fiziksel akışı hedef olur); PCM cihaza BAŞARILI kuyruklanırsa
        // slota yazılır (kuyruk-başarısızlığı atomikliği korunur).
        // Ui ayrı tekil akışa kuyruğa girer.
        // Derinlik 1 iken slot 0'ın davranışı eski tek-stream ile aynıdır.
        size_t sfxSlot = 0;
        if (channel == AudioChannelType::Sfx) {
            ensureSfxPoolStreams();
            sfxSlot = m_sfxPool.pickSlot();
        }
        SDL_AudioStream* sfxTargetStream =
            (channel == AudioChannelType::Sfx && sfxSlot < m_sfxPoolStreams.size())
                ? m_sfxPoolStreams[sfxSlot]
                : nullptr;
        SDL_AudioStream* targetStream = (channel == AudioChannelType::Bgm)
            ? (transitionRequested ? m_transitionBgmStream : m_bgmStream) :
                                       (channel == AudioChannelType::Voice) ? m_voiceStream :
                                       (channel == AudioChannelType::Ambience) ? m_ambienceStream :
                                       (channel == AudioChannelType::Ui) ? m_uiStream : sfxTargetStream;
        if (targetStream) {
            if (transitionRequested) {
                if (!SDL_ClearAudioStream(m_transitionBgmStream)) {
                    ROWL_LOG_WARN("[AudioEngine] Failed to clear transition BGM audio stream: " +
                                  std::string(SDL_GetError()));
                }
            }
            // Bulgu #81: yıkıcı pre-clear kaldırıldı — Clear, prova-Put
            // BAŞARISINA gate'lendi (fail-atomicity). Eski sıra
            // (Clear → SetFormat/Put) kuyruk-hatasında çalmakta olan sesi
            // yok edip bayrakları bayat bırakıyordu (ambience/UI/SFX'te
            // kuyruk boş + eski PCM iddiası; non-transition BGM'de sağlıklı
            // BGM'in imhası). Yeni sıra: SetFormat → prova-Put →
            // (başarıda) Clear-artığı + commit-Put + state yazımı.
            // SDL_SetAudioStreamFormat kuyruğu flush etmez (belge: eski veri
            // eski formatıyla korunur), o yüzden SetFormat-hatası da eski
            // kuyruğa dokunmaz. Transition scratch akışı yukarıda aynen
            // temizlenir (eski BGM Put başarısına kadar korunur — doğru
            // desen, dokunulmadı).
            const bool needsReplace =
                !transitionRequested &&
                (channel == AudioChannelType::Bgm || channel == AudioChannelType::Ambience ||
                 channel == AudioChannelType::Ui || channel == AudioChannelType::Sfx);
            bool queueOk = false;
            if (m_testFailQueueNext) {
                // Bulgu #81 test kancası: prova öncesi deterministik
                // kuyruk-hatası (SDL çağrılmaz; fail yolu birebir aynı
                // çalışır, bayrak tüketilir).
                m_testFailQueueNext = false;
            } else if (SDL_SetAudioStreamFormat(targetStream, &floatSpec, nullptr)) {
                if (!needsReplace) {
                    // Voice + transition-scratch: doğrudan ek-kuyruk
                    // (yıkıcı adım yoktur; eski davranış aynen).
                    queueOk = SDL_PutAudioStreamData(targetStream, floatBuffer, floatLength);
                } else if (SDL_PutAudioStreamData(targetStream, floatBuffer, floatLength)) {
                    // Prova BAŞARILI: eski kuyruk yıkılır, aynı yük taze
                    // kuyruğa commit-Put ile yazılır (replace semantiği
                    // korunur; hata eski kuyruğa dokunmaz).
                    // A5-tur1: Clear fail'i kayda geçer ama queue belirleyicidir
                    // (m_lastError'e yazılmaz — başarılı Put kirlenmemelidir).
                    if (!SDL_ClearAudioStream(targetStream)) {
                        ROWL_LOG_WARN("[AudioEngine] Failed to clear audio stream: " +
                                      std::string(SDL_GetError()));
                    }
                    queueOk = SDL_PutAudioStreamData(targetStream, floatBuffer, floatLength);
                }
            }
            if (!queueOk) {
                ROWL_LOG_ERROR("[AudioEngine] Failed to queue decoded audio: " + std::string(SDL_GetError()));
                m_lastError = "Unable to queue decoded audio: " + std::string(SDL_GetError());
                SDL_free(floatBuffer);
                SDL_free(audioBuf);
                if (channel == AudioChannelType::Bgm) {
                    closeBgmStream();
                    resetStreamInfoNoBgm();
                }
                return;
            }
            // Device-facing effects are transactional with decode/queue: a
            // missing voice must not leave BGM ducked, and a failed filtered
            // SFX must not alter the active DSP state.
            if (channel == AudioChannelType::Voice) {
                m_isVoicePlaying = true;
                triggerVoiceDucking(true);
            }
            applyDspFilter(filter);
            if (channel == AudioChannelType::Bgm) {
                if (transitionRequested) {
                    // The old stream remains entirely intact until this queue
                    // succeeds, so decode/format/device errors cannot stop it.
                    m_transitionBgmData.assign(floatBuffer, floatBuffer + floatLength);
                    m_activeBgmTransition = m_requestedBgmTransition;
                    m_bgmTransitionDurationSeconds = m_requestedBgmTransitionDurationSeconds;
                    m_bgmTransitionElapsedSeconds = 0.0f;
                    m_bgmTransitionActive = true;
                } else {
                    m_bgmData.assign(floatBuffer, floatBuffer + floatLength);
                    m_bgmTransitionActive = false;
                }
                // Faz 5 Dilim 1: RAM taahhüdü snapshot'ı (önceki stream
                // kaynağı kapanır; over-threshold WAV köşesinde karar
                // memory'e indirgenir, buffered sıfırlanır).
                closeBgmStream();
                if (probedForBgm) {
                    m_streamInfo = probedInfo;
                    if (m_streamInfo.mode == StreamMode::Stream) {
                        m_streamInfo.mode = StreamMode::Memory;
                        // Over-threshold WAV köşesinde RAM'e indirgenen karar:
                        // reason da memory ile tutarlı yazılır.
                        m_streamInfo.reason = "under_threshold";
                    }
                    m_streamInfo.bufferedSeconds = 0.0;
                }
                m_currentBgmPath = assetPath;
                m_isBgmPlaying = true;
                m_bgmSampleOffset = 0;
            } else if (channel == AudioChannelType::Ambience) {
                // Faz 5 Dilim 1: Ambience loop RAM (stream-ready iskelet;
                // bu dilimde tam decode + loop besleme).
                m_ambienceData.assign(floatBuffer, floatBuffer + floatLength);
                m_isAmbiencePlaying = true;
                m_ambienceSampleOffset = 0;
                m_currentAmbiencePath = assetPath;
            } else if (channel == AudioChannelType::Sfx) {
                // Faz 5 Dilim 2: PCM havuz slotuna yazılır (cihaz kuyruğu
                // yukarıda başarılı; derinlik 1 = eski tek-ses davranışı).
                m_sfxPool.playInto(sfxSlot,
                                   reinterpret_cast<const uint8_t*>(floatBuffer),
                                   static_cast<size_t>(floatLength), assetPath,
                                   floatSpec.channels, floatSpec.freq);
            } else if (channel == AudioChannelType::Ui) {
                // Faz 5 Dilim 1: Ui one-shot (ayrı tekil akış RAM).
                m_uiData.assign(floatBuffer, floatBuffer + floatLength);
                m_isUiPlaying = true;
                m_uiSampleOffset = 0;
                m_currentUiPath = assetPath;
            }
            if (!m_outputSuspended) {
                // A5-tur1: resume fail'inde queue başarılı olsa da ses çıkmaz —
                // gerçek başarısızlıktır, caller'a ulaşır.
                if (!SDL_ResumeAudioStreamDevice(targetStream)) {
                    m_lastError = "Unable to resume audio stream: " + std::string(SDL_GetError());
                    ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
                }
            }
            ROWL_LOG_INFO("[AudioEngine] Playback started: " + assetPath + " (" + std::to_string(floatLength) + " PCM bytes)");
        } else {
            // Headless / fallback playback without physical stream
            if (channel == AudioChannelType::Bgm) {
                m_bgmData.assign(floatBuffer, floatBuffer + floatLength);
                m_currentBgmPath = assetPath;
                m_isBgmPlaying = true;
                m_bgmSampleOffset = 0;
                // Faz 5 Dilim 1: akışsız yedekte RAM snapshot'ı.
                closeBgmStream();
                if (probedForBgm) {
                    m_streamInfo = probedInfo;
                    if (m_streamInfo.mode == StreamMode::Stream) {
                        m_streamInfo.mode = StreamMode::Memory;
                        // Akışsız yedekte RAM'e indirgenen karar: reason da
                        // memory ile tutarlı yazılır (bilinen header).
                        m_streamInfo.reason = "under_threshold";
                    }
                    m_streamInfo.bufferedSeconds = 0.0;
                }
            } else if (channel == AudioChannelType::Ambience) {
                m_ambienceData.assign(floatBuffer, floatBuffer + floatLength);
                m_currentAmbiencePath = assetPath;
                m_isAmbiencePlaying = true;
                m_ambienceSampleOffset = 0;
            } else if (channel == AudioChannelType::Ui) {
                m_uiData.assign(floatBuffer, floatBuffer + floatLength);
                m_currentUiPath = assetPath;
                m_isUiPlaying = true;
                m_uiSampleOffset = 0;
            } else if (channel == AudioChannelType::Sfx) {
                // Faz 5 Dilim 2: akışsız yedekte de havuz durumu kayda geçer
                // (format geri-kuyruk için saklanır).
                m_sfxPool.playInto(m_sfxPool.pickSlot(),
                                   reinterpret_cast<const uint8_t*>(floatBuffer),
                                   static_cast<size_t>(floatLength), assetPath,
                                   floatSpec.channels, floatSpec.freq);
            } else if (channel == AudioChannelType::Voice) {
                m_isVoicePlaying = true;
                triggerVoiceDucking(true);
            }
            applyDspFilter(filter);
        }
        SDL_free(floatBuffer);
        SDL_free(audioBuf);
    } else {
        m_lastError = "Audio file could not be decoded (supported: WAV, OGG/Vorbis): " + assetPath;
        ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
        // Stale stream karari yalnizca Bgm kanalinda korunmaz (#78).
        if (channel == AudioChannelType::Bgm) {
            closeBgmStream();
            resetStreamInfoNoBgm();
        }
    }
}

void AudioEngine::stopBgm() {
    // A5-tur1: stop snapshot'ı — önceki hata korunmaz, bu çağrının sonucu
    // kayda geçer (state sıfırlama aynen; davranış değişmez).
    m_lastError.clear();
    auto noteStopFailure = [&](const std::string& message) {
        if (m_lastError.empty()) m_lastError = message;
        ROWL_LOG_WARN("[AudioEngine] " + message);
    };
    if (m_bgmStream) {
        if (!SDL_ClearAudioStream(m_bgmStream)) {
            noteStopFailure("Unable to clear BGM audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
        if (!SDL_PauseAudioStreamDevice(m_bgmStream)) {
            noteStopFailure("Unable to pause BGM audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
    }
    if (m_transitionBgmStream) {
        if (!SDL_ClearAudioStream(m_transitionBgmStream)) {
            noteStopFailure("Unable to clear transition BGM audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
        if (!SDL_PauseAudioStreamDevice(m_transitionBgmStream)) {
            noteStopFailure("Unable to pause transition BGM audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
    }
    // Faz 5 Dilim 1: stream kolları (kaynak kapanır, snapshot no_bgm'e döner).
    closeBgmStream();
    resetStreamInfoNoBgm();
    m_currentBgmPath = "";
    m_bgmData.clear();
    m_transitionBgmData.clear();
    m_bgmTransitionActive = false;
    m_isBgmPlaying = false;
    m_bgmSampleOffset = 0;
    ROWL_LOG_INFO("[AudioEngine] BGM stopped.");
}

void AudioEngine::stopAll() {
    stopBgm();
    // A5-tur1: stopBgm snapshot'ı korunur — havuz/bed/UI fail'leri ilk-hatayı
    // ezmez (noteStopFailure guard'lıdır); state sıfırlama aynen.
    auto noteStopFailure = [&](const std::string& message) {
        if (m_lastError.empty()) m_lastError = message;
        ROWL_LOG_WARN("[AudioEngine] " + message);
    };
    // Faz 5 Dilim 2: havuzdaki TÜM sesler + BedB + Ui durdurulur.
    for (SDL_AudioStream* stream : m_sfxPoolStreams) {
        if (stream) {
            if (!SDL_ClearAudioStream(stream)) {
                noteStopFailure("Unable to clear SFX pool audio stream while stopping: " +
                                std::string(SDL_GetError()));
            }
            if (!SDL_PauseAudioStreamDevice(stream)) {
                noteStopFailure("Unable to pause SFX pool audio stream while stopping: " +
                                std::string(SDL_GetError()));
            }
        }
    }
    m_sfxPool.stopAll();
    if (m_ambienceStream) {
        if (!SDL_ClearAudioStream(m_ambienceStream)) {
            noteStopFailure("Unable to clear ambience audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
        if (!SDL_PauseAudioStreamDevice(m_ambienceStream)) {
            noteStopFailure("Unable to pause ambience audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
    }
    if (m_ambienceStreamB) {
        if (!SDL_ClearAudioStream(m_ambienceStreamB)) {
            noteStopFailure("Unable to clear ambience-B audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
        if (!SDL_PauseAudioStreamDevice(m_ambienceStreamB)) {
            noteStopFailure("Unable to pause ambience-B audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
    }
    if (m_uiStream) {
        if (!SDL_ClearAudioStream(m_uiStream)) {
            noteStopFailure("Unable to clear UI audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
        if (!SDL_PauseAudioStreamDevice(m_uiStream)) {
            noteStopFailure("Unable to pause UI audio stream while stopping: " +
                            std::string(SDL_GetError()));
        }
    }
    cancelAmbienceCrossfade();
    m_ambienceData.clear();
    m_ambienceSampleOffset = 0;
    m_isAmbiencePlaying = false;
    m_currentAmbiencePath.clear();
    // Faz 5 Dilim 2: BedB durumu da sıfırlanır.
    m_ambienceDataB.clear();
    m_ambienceSampleOffsetB = 0;
    m_isAmbiencePlayingB = false;
    m_currentAmbiencePathB.clear();
    m_uiData.clear();
    m_uiSampleOffset = 0;
    m_isUiPlaying = false;
    m_currentUiPath.clear();
    m_isVoicePlaying = false;
    triggerVoiceDucking(false);
    ROWL_LOG_INFO("[AudioEngine] All audio channels stopped.");
}

void AudioEngine::setBgmVolume(float volume) {
    if (!std::isfinite(volume)) {
        ROWL_LOG_WARN("Ignoring non-finite BGM volume");
        return;
    }
    m_bgmVolume = std::clamp(volume, 0.0f, 1.0f);
    m_bgmGain = m_isDuckingActive ? (m_bgmVolume * m_duckingFactor) : m_bgmVolume;
    // Faz 5 Dilim 2: üye + mixer çift-yön senkron (tek kaynak okumada mixer).
    m_mixer.setUserVolume(StreamBusId::Bgm, m_bgmVolume);
    applyChannelGains();
}

void AudioEngine::setMasterVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    m_mixer.setUserVolume(StreamBusId::Master, m_masterVolume);
    applyChannelGains();
}

void AudioEngine::setVoiceVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_voiceVolume = std::clamp(volume, 0.0f, 1.0f);
    m_mixer.setUserVolume(StreamBusId::Voice, m_voiceVolume);
    applyChannelGains();
}

void AudioEngine::setSfxVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_sfxVolume = std::clamp(volume, 0.0f, 1.0f);
    m_mixer.setUserVolume(StreamBusId::Sfx, m_sfxVolume);
    applyChannelGains();
}

// Faz 5 Dilim 1 — volume matrisi tamamlamaları: [0,1] clamp +
// non-finite ignore, son geçerli değer korunur (fail-closed).
// Faz 5 Dilim 2: miras tek-bed yolu BedA'ya delege eder.
void AudioEngine::setAmbienceVolume(float volume) {
    setAmbienceBedVolume(0, volume);
}

void AudioEngine::setUiVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_uiVolume = std::clamp(volume, 0.0f, 1.0f);
    m_mixer.setUserVolume(StreamBusId::Ui, m_uiVolume);
    applyChannelGains();
}

void AudioEngine::triggerVoiceDucking(bool isVoiceActive) {
    m_isDuckingActive = isVoiceActive;
    if (isVoiceActive) {
        m_bgmGain = m_bgmVolume * m_duckingFactor;
        ROWL_LOG_INFO("Voice Ducking Triggered -> BGM Attenuated by " + std::to_string(m_duckingFactor * 100.0f) + "% (Gain: " + std::to_string(m_bgmGain) + ")");
    } else {
        m_bgmGain = m_bgmVolume;
        ROWL_LOG_INFO("Voice Finished -> BGM Restored to Full Volume (Gain: " + std::to_string(m_bgmGain) + ")");
    }
    // Faz 5 Dilim 2: duck mixer'e bağlanır (mixer gainFor(Bgm) ==
    // master*m_bgmGain birebir korunur).
    m_mixer.setBgmDuckGain(isVoiceActive ? m_duckingFactor : 1.0f);
    applyChannelGains();
}

void AudioEngine::setDuckingFactor(float factor) {
    if (!std::isfinite(factor)) {
        ROWL_LOG_WARN("Ignoring non-finite ducking factor");
        return;
    }
    m_duckingFactor = std::clamp(factor, 0.0f, 1.0f);
    if (m_isDuckingActive) {
        m_bgmGain = m_bgmVolume * m_duckingFactor;
        m_mixer.setBgmDuckGain(m_duckingFactor);
    }
    applyChannelGains();
}

void AudioEngine::applyDspFilter(DSPFilterType filter) {
    m_activeFilter = filter;
    std::string filterName = "Normal";

    switch (filter) {
        case DSPFilterType::CaveReverb: filterName = "Cave Reverb"; break;
        case DSPFilterType::Telephone: filterName = "Telephone (Band-pass 300Hz-3400Hz)"; break;
        case DSPFilterType::UnderwaterLowPass: filterName = "Underwater (Low-pass Cutoff 800Hz)"; break;
        default: filterName = "Normal Direct Pass-through"; break;
    }

    ROWL_LOG_INFO("DSP Filter Applied -> " + filterName);
}

void AudioEngine::update(float deltaSeconds) {
    if (!m_initialized) {
        return;
    }
    if (m_deviceAvailable) {
        // A5-tur1: loop-feed fail'leri warn-once ile kayda geçer.
        // m_lastError'e YAZILMAZ — o "son API çağrısı" snapshot'ıdır,
        // update-thread onu ezmemelidir.
        if (m_bgmStream && !m_bgmData.empty() && m_bgmLoop) {
            int available = SDL_GetAudioStreamAvailable(m_bgmStream);
            if (available <= 0) {
                if (!SDL_PutAudioStreamData(m_bgmStream, m_bgmData.data(), static_cast<int>(m_bgmData.size()))) {
                    warnAudioOnce("BGM loop re-queue failed: " + std::string(SDL_GetError()));
                    // A5-tur3: kuyruğa giremeyen chunk drop sayılır.
                    ++m_dropCount;
                } else if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(m_bgmStream)) {
                    warnAudioOnce("BGM loop stream resume failed: " + std::string(SDL_GetError()));
                }
            }
        }
        if (m_transitionBgmStream && !m_transitionBgmData.empty() && m_bgmLoop) {
            if (SDL_GetAudioStreamAvailable(m_transitionBgmStream) <= 0) {
                if (!SDL_PutAudioStreamData(m_transitionBgmStream, m_transitionBgmData.data(), static_cast<int>(m_transitionBgmData.size()))) {
                    warnAudioOnce("Transition BGM loop re-queue failed: " + std::string(SDL_GetError()));
                    ++m_dropCount;
                } else if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(m_transitionBgmStream)) {
                    warnAudioOnce("Transition BGM loop stream resume failed: " + std::string(SDL_GetError()));
                }
            }
        }
        // Faz 5 Dilim 1: stream refill update-thread'de senkron çalışır
        // (prefetch thread'i YOKTUR); Ambience loop RAM beslemesi.
        // Faz 5 Dilim 2: iki bed bağımsız beslenir + crossfade ilerler.
        pumpBgmStream();
        if (m_ambienceStream && !m_ambienceData.empty() && m_isAmbiencePlaying) {
            int ambAvailable = SDL_GetAudioStreamAvailable(m_ambienceStream);
            if (ambAvailable <= 0) {
                if (!SDL_PutAudioStreamData(m_ambienceStream, m_ambienceData.data(), static_cast<int>(m_ambienceData.size()))) {
                    warnAudioOnce("Ambience loop re-queue failed: " + std::string(SDL_GetError()));
                    ++m_dropCount;
                } else if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(m_ambienceStream)) {
                    warnAudioOnce("Ambience loop stream resume failed: " + std::string(SDL_GetError()));
                }
            }
        }
        if (m_ambienceStreamB && !m_ambienceDataB.empty() && m_isAmbiencePlayingB) {
            int ambAvailableB = SDL_GetAudioStreamAvailable(m_ambienceStreamB);
            if (ambAvailableB <= 0) {
                if (!SDL_PutAudioStreamData(m_ambienceStreamB, m_ambienceDataB.data(), static_cast<int>(m_ambienceDataB.size()))) {
                    warnAudioOnce("Ambience-B loop re-queue failed: " + std::string(SDL_GetError()));
                    ++m_dropCount;
                } else if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(m_ambienceStreamB)) {
                    warnAudioOnce("Ambience-B loop stream resume failed: " + std::string(SDL_GetError()));
                }
            }
        }
        updateBgmTransition(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f);
        updateAmbienceCrossfade(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f);

        if (m_voiceStream && m_isVoicePlaying && SDL_GetAudioStreamQueued(m_voiceStream) <= 0) {
            m_isVoicePlaying = false;
            triggerVoiceDucking(false);
        }
    }
    updateTelemetry(std::isfinite(deltaSeconds) ? deltaSeconds : (1.0f / 60.0f));
}

void AudioEngine::updateBgmTransition(float deltaSeconds) {
    if (!m_bgmTransitionActive || !m_bgmStream || !m_transitionBgmStream) return;
    m_bgmTransitionElapsedSeconds += std::max(0.0f, deltaSeconds);
    const float progress = std::clamp(m_bgmTransitionElapsedSeconds / m_bgmTransitionDurationSeconds, 0.0f, 1.0f);
    // Faz 5 Dilim 2: eğri seçimi. Linear kolu mevcut formüllerle
    // bit-identicaldir (varsayılan; golden'lar kırılmaz).
    float outgoing = 0.0f;
    float incoming = 0.0f;
    if (m_activeBgmTransition == BgmTransitionKind::Fade) {
        outgoing = fadeKindOutgoing(m_fadeCurve, progress);
        incoming = fadeKindIncoming(m_fadeCurve, progress);
    } else {
        outgoing = fadeCurveOutgoing(m_fadeCurve, progress);
        incoming = fadeCurveIncoming(m_fadeCurve, progress);
    }
    const float baseGain = m_mixer.gainFor(StreamBusId::Bgm);
    // A5-tur1: transition gain/clear/pause fail'leri kayda geçer (warn-only;
    // swap aynen devam eder — davranış değişmez, m_lastError'e yazılmaz).
    if (!SDL_SetAudioStreamGain(m_bgmStream, baseGain * outgoing)) {
        warnAudioOnce("BGM transition outgoing gain failed: " + std::string(SDL_GetError()));
    }
    if (!SDL_SetAudioStreamGain(m_transitionBgmStream, baseGain * incoming)) {
        warnAudioOnce("BGM transition incoming gain failed: " + std::string(SDL_GetError()));
    }
    if (progress < 1.0f) return;

    if (!SDL_ClearAudioStream(m_bgmStream)) {
        warnAudioOnce("BGM transition swap clear failed: " + std::string(SDL_GetError()));
    }
    if (!SDL_PauseAudioStreamDevice(m_bgmStream)) {
        warnAudioOnce("BGM transition swap pause failed: " + std::string(SDL_GetError()));
    }
    std::swap(m_bgmStream, m_transitionBgmStream);
    std::swap(m_bgmData, m_transitionBgmData);
    m_bgmTransitionActive = false;
    applyChannelGains();
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Audio Engine Subsystem...");

    m_bgmData.clear();
    m_transitionBgmData.clear();
    // Faz 5 Dilim 2: havuz + BedB + crossfade + pump sayacı temizlenir.
    m_sfxPool.stopAll();
    destroySfxPoolStreams();
    // Faz 5 Dilim 1 ekleri.
    closeBgmStream();
    resetStreamInfoNoBgm();
    m_ambienceData.clear();
    m_ambienceSampleOffset = 0;
    m_isAmbiencePlaying = false;
    m_currentAmbiencePath.clear();
    m_ambienceDataB.clear();
    m_ambienceSampleOffsetB = 0;
    m_isAmbiencePlayingB = false;
    m_currentAmbiencePathB.clear();
    cancelAmbienceCrossfade();
    m_pumpCount = 0;
    m_pumpLastUs = 0;
    m_pumpMaxUs = 0;
    m_pumpWindow.fill(0);
    m_pumpWindowPos = 0;
    m_uiData.clear();
    m_uiSampleOffset = 0;
    m_isUiPlaying = false;
    m_currentUiPath.clear();
    m_telemetryAmbience = {};
    m_telemetryUi = {};
    m_telemetryBgm = {};
    m_telemetryVoice = {};
    m_telemetrySfx = {};
    m_telemetryMaster = {};
    m_spectrumBands.fill(0.0f);
    m_bgmSampleOffset = 0;

    if (m_bgmStream) {
        SDL_DestroyAudioStream(m_bgmStream);
        m_bgmStream = nullptr;
    }
    if (m_transitionBgmStream) {
        SDL_DestroyAudioStream(m_transitionBgmStream);
        m_transitionBgmStream = nullptr;
    }
    if (m_voiceStream) {
        SDL_DestroyAudioStream(m_voiceStream);
        m_voiceStream = nullptr;
    }
    // Faz 5 Dilim 2: havuz akışları zaten destroySfxPoolStreams ile yıkıldı.
    if (m_ambienceStream) {
        SDL_DestroyAudioStream(m_ambienceStream);
        m_ambienceStream = nullptr;
    }
    if (m_ambienceStreamB) {
        SDL_DestroyAudioStream(m_ambienceStreamB);
        m_ambienceStreamB = nullptr;
    }
    if (m_uiStream) {
        SDL_DestroyAudioStream(m_uiStream);
        m_uiStream = nullptr;
    }
    if (m_audioLeaseHeld) {
        Rowl::Platform::SdlSubsystemLease::release(SDL_INIT_AUDIO);
        m_audioLeaseHeld = false;
    }
    m_deviceAvailable = false;

    m_initialized = false;
    ROWL_LOG_INFO("Audio Engine Subsystem Shutdown Complete.");
}

void AudioEngine::handleDeviceEvent(uint32_t sdlEventType) {
    if (!m_initialized) return;
    switch (sdlEventType) {
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
            ROWL_LOG_WARN("[AudioEngine] Audio device change detected; rebuilding output streams.");
            reopenDeviceStreams();
            break;
        case SDL_EVENT_AUDIO_DEVICE_ADDED:
            if (!m_deviceAvailable) {
                ROWL_LOG_INFO("[AudioEngine] Audio device added; retrying output stream open.");
                reopenDeviceStreams();
            }
            break;
        default:
            break;
    }
}

bool AudioEngine::reopenDeviceStreams() {
    if (!m_initialized) return false;
    // A5-tur1: reopen snapshot'ı — bu çağrının sonucu kayda geçer.
    m_lastError.clear();
    // Playback intent (BGM path/loop buffer, volumes, filter, ducking) lives
    // in member state, so only the device-bound streams are rebuilt. The BGM
    // loop feed in update() re-queues m_bgmData into a fresh stream on its
    // own, which is also what resumes playback after a device loss.
    if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
    if (m_transitionBgmStream) { SDL_DestroyAudioStream(m_transitionBgmStream); m_transitionBgmStream = nullptr; }
    if (m_voiceStream) { SDL_DestroyAudioStream(m_voiceStream); m_voiceStream = nullptr; }
    // Faz 5 Dilim 2: havuz + BedB + Ui akışları da yeniden kurulur
    // (havuz ses PCM'leri üyede durur; kalan baytlar geri kuyruğa girer).
    destroySfxPoolStreams();
    if (m_ambienceStream) { SDL_DestroyAudioStream(m_ambienceStream); m_ambienceStream = nullptr; }
    if (m_ambienceStreamB) { SDL_DestroyAudioStream(m_ambienceStreamB); m_ambienceStreamB = nullptr; }
    if (m_uiStream) { SDL_DestroyAudioStream(m_uiStream); m_uiStream = nullptr; }
    m_deviceAvailable = false;

    if (!m_audioLeaseHeld && !Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_AUDIO)) {
        m_lastError = "Audio subsystem unavailable while reopening device streams";
        ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
        return false;
    }
    m_audioLeaseHeld = true;

    // A5-tur1: kısmi açılışta hangi akışın öldüğü bilinir (ilk hata korunur).
    auto reopenStreamChecked = [&](const char* streamName) {
        SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
            SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
        if (!stream && m_lastError.empty()) {
            m_lastError = std::string("Audio stream could not be reopened (") +
                          streamName + "): " + SDL_GetError();
        }
        return stream;
    };
    m_bgmStream = reopenStreamChecked("BGM");
    m_transitionBgmStream = reopenStreamChecked("transition-BGM");
    m_voiceStream = reopenStreamChecked("voice");
    ensureSfxPoolStreams();
    m_ambienceStream = reopenStreamChecked("ambience");
    m_ambienceStreamB = reopenStreamChecked("ambience-B");
    m_uiStream = reopenStreamChecked("UI");
    bool sfxStreamsReady = !m_sfxPoolStreams.empty();
    for (SDL_AudioStream* stream : m_sfxPoolStreams) sfxStreamsReady = sfxStreamsReady && (stream != nullptr);
    if (m_bgmStream && m_transitionBgmStream && m_voiceStream && sfxStreamsReady && m_ambienceStream && m_ambienceStreamB && m_uiStream) {
        m_deviceAvailable = true;
        applyChannelGains();
        applyDspFilter(m_activeFilter);
        // Faz 5 Dilim 1: stream intent korunur — kaynak VFS düzeyinde açık
        // kalır; ring'deki çözülmüş pencere taze akışa geri kuyruğa girer
        // (granule konumu + path korunur, baştan başlama YOKTUR).
        if (m_isBgmStreamed && m_bgmStreamSource &&
            m_bgmStreamSource->isOpen() && m_bgmRingChannels > 0) {
            SDL_ClearAudioStream(m_bgmStream);
            const size_t streamCh = m_bgmRingChannels;
            const size_t ringFloats = kStreamRingCapacityFrames * streamCh;
            const uint64_t validFrames = std::min<uint64_t>(
                m_bgmStreamPcmPos, kStreamRingCapacityFrames);
            const size_t validFloats = static_cast<size_t>(validFrames) * streamCh;
            if (validFloats > 0 && m_bgmRing.size() >= ringFloats) {
                SDL_AudioSpec floatSpec{};
                floatSpec.format = SDL_AUDIO_F32;
                floatSpec.channels = static_cast<Uint8>(streamCh);
                floatSpec.freq = static_cast<int>(m_bgmStreamRateHz);
                std::vector<float> restore(validFloats);
                const uint64_t firstFrame =
                    m_bgmRingWriteFrames - validFrames;
                for (size_t i = 0; i < validFloats; ++i) {
                    restore[i] = m_bgmRing[static_cast<size_t>(
                        ((firstFrame * streamCh) + i) % ringFloats)];
                }
                if (SDL_SetAudioStreamFormat(m_bgmStream, &floatSpec, nullptr)) {
                    // A5-tur1: "rebuild başarılı, intent korundu" denirken geri
                    // kuyruklama düşmüş olabilir — kayda geçer.
                    if (!SDL_PutAudioStreamData(m_bgmStream, restore.data(),
                                               static_cast<int>(validFloats * sizeof(float))) &&
                        m_lastError.empty()) {
                        m_lastError = "BGM ring restore re-queue failed: " + std::string(SDL_GetError());
                    }
                }
            }
            if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(m_bgmStream) && m_lastError.empty()) {
                m_lastError = "Reopened BGM stream resume failed: " + std::string(SDL_GetError());
            }
        }
        // Faz 5 Dilim 2: havuz sesleri kalan baytlarıyla geri kuyruğa girer
        // (offset korunur, baştan başlama YOKTUR); bed'ler loop niyetiyle
        // tam PCM'leriyle geri kuyruğa girer (format queue anında saklanır).
        // Ui one-shot + Voice geçicidir: niyet bayrakları korunur, kuyruk
        // update() akışına bırakılır (Dilim 1 davranışı).
        {
            auto& voices = m_sfxPool.voices();
            for (size_t i = 0; i < voices.size() && i < m_sfxPoolStreams.size(); ++i) {
                SfxVoice& voice = voices[i];
                SDL_AudioStream* poolStream = m_sfxPoolStreams[i];
                if (!poolStream || !voice.playing || voice.pcm.empty()) continue;
                SDL_AudioSpec voiceSpec{};
                voiceSpec.format = SDL_AUDIO_F32;
                voiceSpec.channels = static_cast<Uint8>(std::clamp(voice.channels, 1, 8));
                voiceSpec.freq = (voice.sampleRate > 0) ? voice.sampleRate : 48000;
                const size_t totalFloats = voice.pcm.size() / sizeof(float);
                const size_t off = (totalFloats > 0) ? (voice.sampleOffset % totalFloats) : 0;
                const size_t remBytes = voice.pcm.size() - off * sizeof(float);
                if (remBytes == 0) continue;
                SDL_ClearAudioStream(poolStream);
                SDL_SetAudioStreamGain(poolStream, m_mixer.gainFor(StreamBusId::Sfx));
                if (SDL_SetAudioStreamFormat(poolStream, &voiceSpec, nullptr)) {
                    // A5-tur1: havuz geri-kuyruklama fail'i kayda geçer.
                    if (!SDL_PutAudioStreamData(poolStream, voice.pcm.data() + off * sizeof(float),
                                               static_cast<int>(remBytes)) &&
                        m_lastError.empty()) {
                        m_lastError = "SFX pool voice re-queue failed: " + std::string(SDL_GetError());
                    }
                }
            }
        }
        for (int bed = 0; bed < 2; ++bed) {
            SDL_AudioStream* bedStream = ambienceBedStream(bed);
            if (!bedStream) continue;
            const bool playing = (bed == 0) ? m_isAmbiencePlaying : m_isAmbiencePlayingB;
            const std::vector<uint8_t>& data = (bed == 0) ? m_ambienceData : m_ambienceDataB;
            if (!playing || data.empty()) continue;
            SDL_AudioSpec bedSpec{};
            bedSpec.format = SDL_AUDIO_F32;
            bedSpec.channels = static_cast<Uint8>(std::clamp(m_ambienceBedChannels[bed], 1, 8));
            bedSpec.freq = (m_ambienceBedRateHz[bed] > 0) ? m_ambienceBedRateHz[bed] : 48000;
            SDL_ClearAudioStream(bedStream);
            SDL_SetAudioStreamGain(bedStream, ambienceBedGain(bed));
            if (SDL_SetAudioStreamFormat(bedStream, &bedSpec, nullptr)) {
                // A5-tur1: bed geri-kuyruklama fail'i kayda geçer.
                if (!SDL_PutAudioStreamData(bedStream, data.data(), static_cast<int>(data.size())) &&
                    m_lastError.empty()) {
                    m_lastError = "Ambience bed re-queue failed: " + std::string(SDL_GetError());
                }
            }
        }
        if (m_outputSuspended) setOutputSuspended(true, true);
        ROWL_LOG_INFO("[AudioEngine] Output streams rebuilt after device change (BGM intent preserved).");
        return true;
    }
    ROWL_LOG_WARN("[AudioEngine] Output stream rebuild failed: " + std::string(SDL_GetError()) +
                  " — keeping silent fallback with playback intent.");
    m_lastError = "Audio output stream rebuild failed: " + std::string(SDL_GetError());
    if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
    if (m_transitionBgmStream) { SDL_DestroyAudioStream(m_transitionBgmStream); m_transitionBgmStream = nullptr; }
    if (m_voiceStream) { SDL_DestroyAudioStream(m_voiceStream); m_voiceStream = nullptr; }
    destroySfxPoolStreams();
    if (m_ambienceStream) { SDL_DestroyAudioStream(m_ambienceStream); m_ambienceStream = nullptr; }
    if (m_ambienceStreamB) { SDL_DestroyAudioStream(m_ambienceStreamB); m_ambienceStreamB = nullptr; }
    if (m_uiStream) { SDL_DestroyAudioStream(m_uiStream); m_uiStream = nullptr; }
    return false;
}

void AudioEngine::setOutputSuspended(bool suspended, bool force) {
    if (!m_initialized) return;
    if (!force && suspended == m_outputSuspended) return;
    m_outputSuspended = suspended;
    if (!m_deviceAvailable) return;
    // Faz 5 Dilim 2: havuzdaki TÜM sesler + BedB + Ui askıya alınır/devam eder.
    std::vector<SDL_AudioStream*> streams = {m_bgmStream, m_transitionBgmStream, m_voiceStream, m_ambienceStream, m_ambienceStreamB, m_uiStream};
    for (SDL_AudioStream* poolStream : m_sfxPoolStreams) streams.push_back(poolStream);
    for (SDL_AudioStream* stream : streams) {
        if (!stream) continue;
        // A5-tur1: suspend/resume ıskalanırsa flag ile cihaz diverge olur —
        // kayda geçer (ilk hata korunur; davranış değişmez).
        if (suspended) {
            if (!SDL_PauseAudioStreamDevice(stream) && m_lastError.empty()) {
                m_lastError = "Unable to suspend audio stream: " + std::string(SDL_GetError());
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            }
        } else {
            if (!SDL_ResumeAudioStreamDevice(stream) && m_lastError.empty()) {
                m_lastError = "Unable to resume audio stream: " + std::string(SDL_GetError());
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            }
        }
    }
    ROWL_LOG_INFO(std::string("[AudioEngine] Output ") + (suspended ? "suspended." : "resumed."));
}

void AudioEngine::applyChannelGains() {
    // Faz 5 Dilim 2: TEK kazanç kaynağı StreamMixer'dır (matematik birebir:
    // master*bus, duck yalnız BGM; bed başına master*bedVol).
    // A5-tur1: uygulanamayan gain kayda geçer (UI ile duyulan uyuşmazsa
    // teşhis vardır; değer aynen hesaplanır — matematik değişmez).
    auto setGainChecked = [&](SDL_AudioStream* stream, float gain, const char* busName) {
        if (stream && !SDL_SetAudioStreamGain(stream, gain)) {
            warnAudioOnce(std::string("Channel gain not applied (") + busName +
                          "): " + SDL_GetError());
        }
    };
    setGainChecked(m_bgmStream, m_mixer.gainFor(StreamBusId::Bgm), "bgm");
    setGainChecked(m_transitionBgmStream, 0.0f, "transition-bgm");
    setGainChecked(m_voiceStream, m_mixer.gainFor(StreamBusId::Voice), "voice");
    const float sfxGain = m_mixer.gainFor(StreamBusId::Sfx);
    for (SDL_AudioStream* stream : m_sfxPoolStreams) {
        setGainChecked(stream, sfxGain, "sfx-pool");
    }
    setGainChecked(m_uiStream, m_mixer.gainFor(StreamBusId::Sfx), "ui");
    setGainChecked(m_ambienceStream, ambienceBedGain(0), "ambience");
    setGainChecked(m_ambienceStreamB, ambienceBedGain(1), "ambience-B");
}

void AudioEngine::updateTelemetry(float deltaSeconds) {
    constexpr float kSampleRate = 44100.0f;
    constexpr float kDecayRate = 2.8f; // ~350ms smooth analog VU decay
    const float dt = (std::isfinite(deltaSeconds) && deltaSeconds > 0.0f) ? std::min(deltaSeconds, 0.1f) : (1.0f / 60.0f);

    auto decayVal = [dt, kDecayRate](float current, float target) {
        if (target >= current) return target;
        return std::max(target, current - kDecayRate * dt);
    };

    // 1. BGM Telemetry
    float targetBgmL = 0.0f;
    float targetBgmR = 0.0f;
    float targetBgmRmsL = 0.0f;
    float targetBgmRmsR = 0.0f;

    // Faz 5 Dilim 1: stream telemetrisi ring penceresinden okunur (RAM
    // bloğu aşağıda aynen korunur; stream aktifken RAM dalı atlanır).
    const bool bgmStreamActive =
        m_isBgmStreamed && m_isBgmPlaying && m_bgmRingWriteFrames > 0 &&
        m_bgmRingChannels > 0 && !m_bgmRing.empty();
    if (bgmStreamActive) {
        const size_t streamCh = m_bgmRingChannels;
        const size_t ringFloats = kStreamRingCapacityFrames * streamCh;
        const uint64_t validFrames = std::min<uint64_t>(
            m_bgmStreamPcmPos, kStreamRingCapacityFrames);
        const size_t windowFrames = std::min<size_t>(
            static_cast<size_t>(validFrames), 512);
        if (windowFrames > 0 && m_bgmRing.size() >= ringFloats) {
            float maxL = 0.0f, maxR = 0.0f;
            float sumSqL = 0.0f, sumSqR = 0.0f;
            for (size_t f = 0; f < windowFrames; ++f) {
                const uint64_t frameIdx =
                    m_bgmRingWriteFrames - windowFrames + f;
                const size_t base = static_cast<size_t>(
                    ((frameIdx * streamCh) % ringFloats));
                const float sL = std::abs(m_bgmRing[base]);
                const float sR = (streamCh > 1)
                    ? std::abs(m_bgmRing[(base + 1) % ringFloats]) : sL;
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
                sumSqL += sL * sL;
                sumSqR += sR * sR;
            }
            const float gain = m_masterVolume * m_bgmGain;
            targetBgmL = std::clamp(maxL * gain, 0.0f, 1.0f);
            targetBgmR = std::clamp(maxR * gain, 0.0f, 1.0f);
            targetBgmRmsL = std::clamp(
                std::sqrt(sumSqL / static_cast<float>(windowFrames)) * gain,
                0.0f, 1.0f);
            targetBgmRmsR = std::clamp(
                std::sqrt(sumSqR / static_cast<float>(windowFrames)) * gain,
                0.0f, 1.0f);
        }
    }

    if (!bgmStreamActive && m_isBgmPlaying && !m_bgmData.empty()) {
        const size_t totalFloats = m_bgmData.size() / sizeof(float);
        if (totalFloats >= 2) {
            const size_t windowSize = std::min<size_t>(1024, totalFloats);
            size_t start = m_bgmSampleOffset % totalFloats;
            float maxL = 0.0f, maxR = 0.0f;
            float sumSqL = 0.0f, sumSqR = 0.0f;
            size_t frames = 0;

            for (size_t i = 0; i < windowSize && (start + i + 1) < totalFloats; i += 2) {
                float rawL = 0.0f, rawR = 0.0f;
                std::memcpy(&rawL, m_bgmData.data() + (start + i) * sizeof(float), sizeof(float));
                std::memcpy(&rawR, m_bgmData.data() + (start + i + 1) * sizeof(float), sizeof(float));
                float sL = std::abs(rawL);
                float sR = std::abs(rawR);
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
                sumSqL += sL * sL;
                sumSqR += sR * sR;
                frames++;
            }
            if (frames > 0) {
                const float gain = m_masterVolume * m_bgmGain;
                targetBgmL = std::clamp(maxL * gain, 0.0f, 1.0f);
                targetBgmR = std::clamp(maxR * gain, 0.0f, 1.0f);
                targetBgmRmsL = std::clamp(std::sqrt(sumSqL / static_cast<float>(frames)) * gain, 0.0f, 1.0f);
                targetBgmRmsR = std::clamp(std::sqrt(sumSqR / static_cast<float>(frames)) * gain, 0.0f, 1.0f);
            }
            m_bgmSampleOffset = (m_bgmSampleOffset + static_cast<size_t>(dt * kSampleRate * 2)) % totalFloats;
        }
    }

    m_telemetryBgm.peakL = decayVal(m_telemetryBgm.peakL, targetBgmL);
    m_telemetryBgm.peakR = decayVal(m_telemetryBgm.peakR, targetBgmR);
    m_telemetryBgm.rmsL = decayVal(m_telemetryBgm.rmsL, targetBgmRmsL);
    m_telemetryBgm.rmsR = decayVal(m_telemetryBgm.rmsR, targetBgmRmsR);

    // 2. SFX Telemetry (Faz 5 Dilim 2: havuz TOPLAMINDAN okunur; tek ses
    // iken eski tek-stream formülüyle birebir aynıdır).
    float targetSfxL = 0.0f;
    float targetSfxR = 0.0f;
    float targetSfxRmsL = 0.0f;
    float targetSfxRmsR = 0.0f;

    {
        const float gain = m_masterVolume * m_sfxVolume;
        for (SfxVoice& voice : m_sfxPool.voices()) {
            if (!voice.playing || voice.pcm.empty()) continue;
            const size_t totalFloats = voice.pcm.size() / sizeof(float);
            if (totalFloats >= 2 && voice.sampleOffset < totalFloats) {
                const size_t windowSize = std::min<size_t>(1024, totalFloats - voice.sampleOffset);
                size_t start = voice.sampleOffset;
                float maxL = 0.0f, maxR = 0.0f;
                float sumSqL = 0.0f, sumSqR = 0.0f;
                size_t frames = 0;

                for (size_t i = 0; i < windowSize && (start + i + 1) < totalFloats; i += 2) {
                    float rawL = 0.0f, rawR = 0.0f;
                    std::memcpy(&rawL, voice.pcm.data() + (start + i) * sizeof(float), sizeof(float));
                    std::memcpy(&rawR, voice.pcm.data() + (start + i + 1) * sizeof(float), sizeof(float));
                    float sL = std::abs(rawL);
                    float sR = std::abs(rawR);
                    if (sL > maxL) maxL = sL;
                    if (sR > maxR) maxR = sR;
                    sumSqL += sL * sL;
                    sumSqR += sR * sR;
                    frames++;
                }
                if (frames > 0) {
                    targetSfxL += maxL * gain;
                    targetSfxR += maxR * gain;
                    targetSfxRmsL += std::sqrt(sumSqL / static_cast<float>(frames)) * gain;
                    targetSfxRmsR += std::sqrt(sumSqR / static_cast<float>(frames)) * gain;
                }
                voice.sampleOffset += static_cast<size_t>(dt * kSampleRate * 2);
                if (voice.sampleOffset >= totalFloats) {
                    voice.playing = false;
                }
            } else {
                voice.playing = false;
            }
        }
        targetSfxL = std::clamp(targetSfxL, 0.0f, 1.0f);
        targetSfxR = std::clamp(targetSfxR, 0.0f, 1.0f);
        targetSfxRmsL = std::clamp(targetSfxRmsL, 0.0f, 1.0f);
        targetSfxRmsR = std::clamp(targetSfxRmsR, 0.0f, 1.0f);
    }

    m_telemetrySfx.peakL = decayVal(m_telemetrySfx.peakL, targetSfxL);
    m_telemetrySfx.peakR = decayVal(m_telemetrySfx.peakR, targetSfxR);
    m_telemetrySfx.rmsL = decayVal(m_telemetrySfx.rmsL, targetSfxRmsL);
    m_telemetrySfx.rmsR = decayVal(m_telemetrySfx.rmsR, targetSfxRmsR);

    // 2b. Ambience Telemetry (Faz 5 Dilim 2: iki bed TOPLAMI; tek bed +
    // crossfade'siz durumda eski formülle birebir aynıdır).
    float targetAmbL = 0.0f;
    float targetAmbR = 0.0f;
    float targetAmbRmsL = 0.0f;
    float targetAmbRmsR = 0.0f;

    for (int bed = 0; bed < 2; ++bed) {
        const bool playing = (bed == 0) ? m_isAmbiencePlaying : m_isAmbiencePlayingB;
        const std::vector<uint8_t>& data = (bed == 0) ? m_ambienceData : m_ambienceDataB;
        size_t& offset = (bed == 0) ? m_ambienceSampleOffset : m_ambienceSampleOffsetB;
        if (!playing || data.empty()) continue;
        const size_t totalFloats = data.size() / sizeof(float);
        if (totalFloats >= 2) {
            const size_t windowSize = std::min<size_t>(1024, totalFloats);
            size_t start = offset % totalFloats;
            float maxL = 0.0f, maxR = 0.0f;
            float sumSqL = 0.0f, sumSqR = 0.0f;
            size_t frames = 0;

            for (size_t i = 0; i < windowSize && (start + i + 1) < totalFloats; i += 2) {
                float rawL = 0.0f, rawR = 0.0f;
                std::memcpy(&rawL, data.data() + (start + i) * sizeof(float), sizeof(float));
                std::memcpy(&rawR, data.data() + (start + i + 1) * sizeof(float), sizeof(float));
                float sL = std::abs(rawL);
                float sR = std::abs(rawR);
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
                sumSqL += sL * sL;
                sumSqR += sR * sR;
                frames++;
            }
            if (frames > 0) {
                const float gain = ambienceBedGain(bed);
                targetAmbL += maxL * gain;
                targetAmbR += maxR * gain;
                targetAmbRmsL += std::sqrt(sumSqL / static_cast<float>(frames)) * gain;
                targetAmbRmsR += std::sqrt(sumSqR / static_cast<float>(frames)) * gain;
            }
            offset = (offset + static_cast<size_t>(dt * kSampleRate * 2)) % totalFloats;
        }
    }
    targetAmbL = std::clamp(targetAmbL, 0.0f, 1.0f);
    targetAmbR = std::clamp(targetAmbR, 0.0f, 1.0f);
    targetAmbRmsL = std::clamp(targetAmbRmsL, 0.0f, 1.0f);
    targetAmbRmsR = std::clamp(targetAmbRmsR, 0.0f, 1.0f);

    m_telemetryAmbience.peakL = decayVal(m_telemetryAmbience.peakL, targetAmbL);
    m_telemetryAmbience.peakR = decayVal(m_telemetryAmbience.peakR, targetAmbR);
    m_telemetryAmbience.rmsL = decayVal(m_telemetryAmbience.rmsL, targetAmbRmsL);
    m_telemetryAmbience.rmsR = decayVal(m_telemetryAmbience.rmsR, targetAmbRmsR);

    // 2c. Ui Telemetry (one-shot drain; SFX deseniyle aynı pencere)
    float targetUiL = 0.0f;
    float targetUiR = 0.0f;
    float targetUiRmsL = 0.0f;
    float targetUiRmsR = 0.0f;

    if (m_isUiPlaying && !m_uiData.empty()) {
        const size_t totalFloats = m_uiData.size() / sizeof(float);
        if (totalFloats >= 2 && m_uiSampleOffset < totalFloats) {
            const size_t windowSize = std::min<size_t>(1024, totalFloats - m_uiSampleOffset);
            size_t start = m_uiSampleOffset;
            float maxL = 0.0f, maxR = 0.0f;
            float sumSqL = 0.0f, sumSqR = 0.0f;
            size_t frames = 0;

            for (size_t i = 0; i < windowSize && (start + i + 1) < totalFloats; i += 2) {
                float rawL = 0.0f, rawR = 0.0f;
                std::memcpy(&rawL, m_uiData.data() + (start + i) * sizeof(float), sizeof(float));
                std::memcpy(&rawR, m_uiData.data() + (start + i + 1) * sizeof(float), sizeof(float));
                float sL = std::abs(rawL);
                float sR = std::abs(rawR);
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
                sumSqL += sL * sL;
                sumSqR += sR * sR;
                frames++;
            }
            if (frames > 0) {
                const float gain = m_masterVolume * m_sfxVolume;
                targetUiL = std::clamp(maxL * gain, 0.0f, 1.0f);
                targetUiR = std::clamp(maxR * gain, 0.0f, 1.0f);
                targetUiRmsL = std::clamp(std::sqrt(sumSqL / static_cast<float>(frames)) * gain, 0.0f, 1.0f);
                targetUiRmsR = std::clamp(std::sqrt(sumSqR / static_cast<float>(frames)) * gain, 0.0f, 1.0f);
            }
            m_uiSampleOffset += static_cast<size_t>(dt * kSampleRate * 2);
            if (m_uiSampleOffset >= totalFloats) {
                m_isUiPlaying = false;
            }
        } else {
            m_isUiPlaying = false;
        }
    }

    m_telemetryUi.peakL = decayVal(m_telemetryUi.peakL, targetUiL);
    m_telemetryUi.peakR = decayVal(m_telemetryUi.peakR, targetUiR);
    m_telemetryUi.rmsL = decayVal(m_telemetryUi.rmsL, targetUiRmsL);
    m_telemetryUi.rmsR = decayVal(m_telemetryUi.rmsR, targetUiRmsR);

    // 3. Voice Telemetry
    float targetVoiceL = 0.0f;
    float targetVoiceR = 0.0f;
    if (m_isVoicePlaying) {
        const float gain = m_masterVolume * m_voiceVolume;
        targetVoiceL = gain * 0.85f;
        targetVoiceR = gain * 0.85f;
    }
    m_telemetryVoice.peakL = decayVal(m_telemetryVoice.peakL, targetVoiceL);
    m_telemetryVoice.peakR = decayVal(m_telemetryVoice.peakR, targetVoiceR);
    m_telemetryVoice.rmsL = decayVal(m_telemetryVoice.rmsL, targetVoiceL * 0.7f);
    m_telemetryVoice.rmsR = decayVal(m_telemetryVoice.rmsR, targetVoiceR * 0.7f);

    // 4. Master Telemetry (Combined peaks and RMS; Faz 5 Dilim 1: 4/5
    // Ambience/Ui de karışıma dahildir, 3=Master numarası korunur).
    m_telemetryMaster.peakL = std::clamp(std::max({m_telemetryBgm.peakL, m_telemetrySfx.peakL, m_telemetryVoice.peakL, m_telemetryAmbience.peakL, m_telemetryUi.peakL}), 0.0f, 1.0f);
    m_telemetryMaster.peakR = std::clamp(std::max({m_telemetryBgm.peakR, m_telemetrySfx.peakR, m_telemetryVoice.peakR, m_telemetryAmbience.peakR, m_telemetryUi.peakR}), 0.0f, 1.0f);
    m_telemetryMaster.rmsL = std::clamp(std::sqrt(m_telemetryBgm.rmsL * m_telemetryBgm.rmsL +
                                                  m_telemetrySfx.rmsL * m_telemetrySfx.rmsL +
                                                  m_telemetryVoice.rmsL * m_telemetryVoice.rmsL +
                                                  m_telemetryAmbience.rmsL * m_telemetryAmbience.rmsL +
                                                  m_telemetryUi.rmsL * m_telemetryUi.rmsL), 0.0f, 1.0f);
    m_telemetryMaster.rmsR = std::clamp(std::sqrt(m_telemetryBgm.rmsR * m_telemetryBgm.rmsR +
                                                  m_telemetrySfx.rmsR * m_telemetrySfx.rmsR +
                                                  m_telemetryVoice.rmsR * m_telemetryVoice.rmsR +
                                                  m_telemetryAmbience.rmsR * m_telemetryAmbience.rmsR +
                                                  m_telemetryUi.rmsR * m_telemetryUi.rmsR), 0.0f, 1.0f);

    // 5. 4-Band Spectrum Estimation
    const float masterEnergy = (m_telemetryMaster.peakL + m_telemetryMaster.peakR) * 0.5f;
    float targetBands[4] = {
        masterEnergy * 0.95f,
        masterEnergy * 0.80f,
        masterEnergy * 0.65f,
        masterEnergy * 0.50f
    };
    if (m_activeFilter == DSPFilterType::UnderwaterLowPass) {
        targetBands[0] *= 1.2f; targetBands[1] *= 0.5f; targetBands[2] *= 0.1f; targetBands[3] *= 0.02f;
    } else if (m_activeFilter == DSPFilterType::Telephone) {
        targetBands[0] *= 0.1f; targetBands[1] *= 1.1f; targetBands[2] *= 1.0f; targetBands[3] *= 0.15f;
    }
    for (size_t b = 0; b < 4; ++b) {
        m_spectrumBands[b] = decayVal(m_spectrumBands[b], std::clamp(targetBands[b], 0.0f, 1.0f));
    }
}

float AudioEngine::getChannelPeak(int channelType, int channelIndex) const {
    const ChannelTelemetry* tel = nullptr;
    switch (channelType) {
        case 0: tel = &m_telemetryBgm; break;
        case 1: tel = &m_telemetryVoice; break;
        case 2: tel = &m_telemetrySfx; break;
        case 3: tel = &m_telemetryMaster; break;
        case 4: tel = &m_telemetryAmbience; break;
        case 5: tel = &m_telemetryUi; break;
        default: tel = &m_telemetryMaster; break;
    }
    return (channelIndex == 1) ? tel->peakR : tel->peakL;
}

float AudioEngine::getChannelRms(int channelType, int channelIndex) const {
    const ChannelTelemetry* tel = nullptr;
    switch (channelType) {
        case 0: tel = &m_telemetryBgm; break;
        case 1: tel = &m_telemetryVoice; break;
        case 2: tel = &m_telemetrySfx; break;
        case 3: tel = &m_telemetryMaster; break;
        case 4: tel = &m_telemetryAmbience; break;
        case 5: tel = &m_telemetryUi; break;
        default: tel = &m_telemetryMaster; break;
    }
    return (channelIndex == 1) ? tel->rmsR : tel->rmsL;
}

void AudioEngine::getSpectrumBands(float* outBands, int bandCount) const {
    if (!outBands || bandCount <= 0) return;
    for (int i = 0; i < bandCount; ++i) {
        outBands[i] = (i < 4) ? m_spectrumBands[i] : 0.0f;
    }
}

void AudioEngine::playVoiceBlip(const std::string& assetPath, float pitch, float volume, AudioChannelType channel) {
    if (!m_initialized) return;

    if (!std::isfinite(pitch) || pitch <= 0.01f) pitch = 1.0f;
    if (!std::isfinite(volume)) volume = 0.85f;
    volume = std::clamp(volume, 0.0f, 1.0f);
    pitch = std::clamp(pitch, 0.25f, 4.0f);

    m_voiceBlipCount++;
    m_lastVoiceBlipPitch = pitch;

    // Immediately deflect channel telemetry so VU meters and telemetry readers reflect the blip
    float effectiveVol = volume * m_masterVolume * (channel == AudioChannelType::Sfx ? m_sfxVolume : m_voiceVolume);
    ChannelTelemetry& tel = (channel == AudioChannelType::Sfx) ? m_telemetrySfx : m_telemetryVoice;
    tel.peakL = std::max(tel.peakL, effectiveVol);
    tel.peakR = std::max(tel.peakR, effectiveVol);
    tel.rmsL = std::max(tel.rmsL, effectiveVol * 0.707f);
    tel.rmsR = std::max(tel.rmsR, effectiveVol * 0.707f);

    m_telemetryMaster.peakL = std::clamp(std::max(m_telemetryMaster.peakL, tel.peakL), 0.0f, 1.0f);
    m_telemetryMaster.peakR = std::clamp(std::max(m_telemetryMaster.peakR, tel.peakR), 0.0f, 1.0f);
    m_telemetryMaster.rmsL = std::clamp(std::max(m_telemetryMaster.rmsL, tel.rmsL), 0.0f, 1.0f);
    m_telemetryMaster.rmsR = std::clamp(std::max(m_telemetryMaster.rmsR, tel.rmsR), 0.0f, 1.0f);

    if (!m_deviceAvailable) {
        return;
    }

    // Faz 5 Dilim 2: SFX blip havuz slotuna gider (slotun fiziksel akışı
    // hedef olur; derinlik 1 = eski tek-stream davranışı).
    size_t blipSfxSlot = 0;
    SDL_AudioStream* targetStream = m_voiceStream;
    if (channel == AudioChannelType::Sfx) {
        ensureSfxPoolStreams();
        blipSfxSlot = m_sfxPool.pickSlot();
        targetStream = (blipSfxSlot < m_sfxPoolStreams.size())
            ? m_sfxPoolStreams[blipSfxSlot]
            : nullptr;
    }
    if (!targetStream) return;

    // 1. Try loading custom audio asset if provided
    bool assetPlayed = false;
    if (!assetPath.empty()) {
        std::vector<std::string> vfsCandidates = {
            assetPath,
            "Assets/" + assetPath,
            "Assets/audio/" + assetPath,
            "audio/" + assetPath
        };
        for (const auto& candidate : vfsCandidates) {
            if (vfs().exists(candidate)) {
                std::vector<uint8_t> bytes;
                SDL_AudioSpec spec{};
                Uint8* audioBuf = nullptr;
                Uint32 audioLen = 0;
                bool loaded = false;

                if (hasOggExtension(candidate)) {
                    auto stream = vfs().openReadStream(candidate);
                    if (stream && decodeOggVorbis(*stream, spec, bytes, m_lastError)) {
                        audioBuf = static_cast<Uint8*>(SDL_malloc(bytes.size()));
                        if (audioBuf) {
                            std::memcpy(audioBuf, bytes.data(), bytes.size());
                            audioLen = static_cast<Uint32>(bytes.size());
                            loaded = true;
                        }
                    }
                } else {
                    bytes = vfs().readBytes(candidate);
                    if (!bytes.empty() && bytes.size() <= kMaxEncodedAudioBytes) {
                        SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
                        if (io) {
                            loaded = SDL_LoadWAV_IO(io, true, &spec, &audioBuf, &audioLen);
                        }
                    }
                }

                if (loaded && audioBuf && audioLen > 0) {
                    SDL_AudioSpec floatSpec{};
                    floatSpec.format = SDL_AUDIO_F32;
                    floatSpec.channels = spec.channels;
                    floatSpec.freq = spec.freq;
                    Uint8* floatBuffer = nullptr;
                    int floatLength = 0;
                    if (SDL_ConvertAudioSamples(&spec, audioBuf, static_cast<int>(audioLen),
                                                &floatSpec, &floatBuffer, &floatLength) &&
                        floatBuffer && floatLength > 0) {
                        float* samples = reinterpret_cast<float*>(floatBuffer);
                        size_t sampleCount = static_cast<size_t>(floatLength) / sizeof(float);
                        for (size_t s = 0; s < sampleCount; ++s) {
                            samples[s] *= volume;
                        }
                        // A5-tur1: kuyruk fail'i kayda geçer (assetPlayed aynen
                        // true kalır — synth'e düşmek davranış değiştirirdi).
                        if (!SDL_ClearAudioStream(targetStream)) {
                            ROWL_LOG_WARN("[AudioEngine] Failed to clear voice blip stream: " +
                                          std::string(SDL_GetError()));
                        }
                        if (!SDL_SetAudioStreamFormat(targetStream, &floatSpec, nullptr) ||
                            !SDL_SetAudioStreamFrequencyRatio(targetStream, pitch) ||
                            !SDL_PutAudioStreamData(targetStream, floatBuffer, floatLength) ||
                            !SDL_ResumeAudioStreamDevice(targetStream)) {
                            if (m_lastError.empty()) {
                                m_lastError = "Voice blip asset could not be queued: " +
                                              std::string(SDL_GetError());
                            }
                            ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
                        }
                        if (channel == AudioChannelType::Voice) {
                            m_isVoicePlaying = true;
                        } else if (channel == AudioChannelType::Sfx) {
                            // Faz 5 Dilim 2: blip PCM'i havuz slotuna yazılır
                            // (cihaz kuyruğu yukarıda başarılı).
                            m_sfxPool.playInto(blipSfxSlot, floatBuffer,
                                               static_cast<size_t>(floatLength),
                                               assetPath, spec.channels, spec.freq);
                        }
                        SDL_free(floatBuffer);
                        assetPlayed = true;
                    }
                    SDL_free(audioBuf);
                    break;
                }
            }
        }
    }

    // 2. If no asset played, generate procedural synthesis voice blip
    if (!assetPlayed) {
        constexpr int kBlipSampleRate = 48000;
        constexpr float kBlipDuration = 0.040f; // 40ms short expressive blip
        const size_t totalSamples = static_cast<size_t>(kBlipSampleRate * kBlipDuration);
        std::vector<float> blipPcm(totalSamples);
        const float baseFreq = 440.0f * pitch;
        constexpr float kAttackDuration = 0.005f; // 5ms attack
        const size_t attackSamples = static_cast<size_t>(kBlipSampleRate * kAttackDuration);

        for (size_t i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kBlipSampleRate);
            float envelope = 1.0f;
            if (i < attackSamples) {
                envelope = static_cast<float>(i) / static_cast<float>(attackSamples);
            } else {
                float decayProgress = static_cast<float>(i - attackSamples) / static_cast<float>(totalSamples - attackSamples);
                envelope = std::exp(-5.0f * decayProgress);
            }
            float phase = 2.0f * 3.14159265358979323846f * baseFreq * t;
            // Warm voice blip synthesis (sine fundamental + 2nd harmonic)
            float sample = (std::sin(phase) * 0.75f + std::sin(phase * 2.0f) * 0.25f) * envelope * volume;
            blipPcm[i] = std::clamp(sample, -1.0f, 1.0f);
        }

        SDL_AudioSpec blipSpec{};
        blipSpec.format = SDL_AUDIO_F32;
        blipSpec.channels = 1;
        blipSpec.freq = kBlipSampleRate;

        SDL_ClearAudioStream(targetStream);
        // A5-tur1: synth kuyruk fail'i kayda geçer; synth BAŞARILI kuyruğa
        // girerse asset-decode'dan kalma kirli m_lastError temizlenir
        // (başarı+kirlilik olmamalıdır).
        if (!SDL_SetAudioStreamFormat(targetStream, &blipSpec, nullptr) ||
            !SDL_SetAudioStreamFrequencyRatio(targetStream, 1.0f) ||
            !SDL_PutAudioStreamData(targetStream, blipPcm.data(), static_cast<int>(blipPcm.size() * sizeof(float))) ||
            !SDL_ResumeAudioStreamDevice(targetStream)) {
            if (m_lastError.empty()) {
                m_lastError = "Synth voice blip could not be queued: " +
                              std::string(SDL_GetError());
            }
            ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
        } else {
            m_lastError.clear();
            // A5-tur3: synth-fallback ayırt edilebilirliği — başarılı synth
            // kuyruğu ayrıca sayılır (synth <= voice).
            ++m_synthBlipCount;
        }

        if (channel == AudioChannelType::Sfx) {
            // Faz 5 Dilim 2: prosedürel blip de havuz slotuna yazılır
            // (mono 48kHz float; kuyruk-başarısızlığı atomikliği korunur).
            m_sfxPool.playInto(blipSfxSlot,
                               reinterpret_cast<const uint8_t*>(blipPcm.data()),
                               blipPcm.size() * sizeof(float), assetPath, 1,
                               kBlipSampleRate);
        } else if (channel == AudioChannelType::Voice) {
            m_isVoicePlaying = true;
        }
    }
}

// ── Faz 5 Dilim 1: OGG streaming çekirdek + gözlemlenebilirlik ──────────────

void AudioEngine::playAudioInt(const std::string& assetPath, int channelInt,
                               DSPFilterType filter) {
    m_streamChannel = channelInt;
    m_streamChannelFresh = true;
    AudioChannelType channel = AudioChannelType::Sfx;
    if (channelInt == 0) channel = AudioChannelType::Bgm;
    else if (channelInt == 1) channel = AudioChannelType::Voice;
    else if (channelInt == 3) channel = AudioChannelType::Ambience;
    else if (channelInt == 4) channel = AudioChannelType::Ui;
    playAudio(assetPath, channel, filter);
}

bool AudioEngine::shouldStreamRoute(double durationSeconds,
                                    double thresholdSeconds) {
    // assessLongAudio ile aynı strict `>` operatörü; sessizdir (warn YOK).
    if (!(durationSeconds >= 0.0) || !std::isfinite(durationSeconds)) {
        return false;
    }
    if (!(thresholdSeconds > 0.0) || !std::isfinite(thresholdSeconds)) {
        return false;
    }
    return durationSeconds > thresholdSeconds;
}

bool AudioEngine::findBgmStreamCandidate(
    const std::string& assetPath, std::string& candidateOut,
    std::vector<uint8_t>& headerBytesOut) {
    // RAM decode bloğundaki aday sırasıyla birebir aynıdır.
    const std::vector<std::string> vfsCandidates = {
        assetPath,
        "Assets/" + assetPath,
        "Assets/audio/" + assetPath,
        "audio/" + assetPath
    };
    for (const auto& candidate : vfsCandidates) {
        if (!vfs().exists(candidate)) continue;
        auto stream = vfs().openReadStream(candidate);
        if (!stream || !(*stream)) continue;
        // OGG granule kuyrukta: tüm dosya gerekir (cap'li). WAV fmt/data
        // başta: ilk 1 MiB probe için yeterlidir (clamp'li okuma, OOB yok).
        const bool isOgg = hasOggExtension(candidate);
        const size_t cap = isOgg
            ? static_cast<size_t>(kMaxEncodedAudioBytes) + 1
            : static_cast<size_t>(1024 * 1024);
        std::vector<uint8_t> buffer;
        buffer.reserve(std::min<size_t>(cap, 65536));
        std::array<char, 32768> chunk{};
        size_t total = 0;
        while (total < cap && stream->good()) {
            const size_t want = std::min<size_t>(chunk.size(), cap - total);
            stream->read(chunk.data(), static_cast<std::streamsize>(want));
            const std::streamsize got = stream->gcount();
            if (got <= 0) break;
            buffer.insert(buffer.end(), chunk.data(), chunk.data() + got);
            total += static_cast<size_t>(got);
        }
        if (buffer.empty()) continue;
        candidateOut = candidate;
        headerBytesOut = std::move(buffer);
        return true;
    }
    return false;
}

bool AudioEngine::openBgmStream(const std::string& candidate,
                                const std::string& assetPath,
                                int snapshotChannel, DSPFilterType filter) {
    closeBgmStream();
    auto source = std::make_unique<OggStreamSource>();
    std::string error;
    if (!source->open(vfs(), candidate, error)) {
        m_lastError = error.empty()
            ? "Ogg/Vorbis stream could not be opened: " + assetPath : error;
        return false;
    }
    const uint32_t rate = source->sampleRateHz();
    const uint32_t channels = source->channelCount();
    if (rate == 0 || channels == 0 || channels > 8) {
        m_lastError = "Ogg/Vorbis stream has an unsupported audio format: " + assetPath;
        return false;
    }
    // Ring 4x4096 frame sabit üst bant: heap'te bir kez ayrılır, akışlar
    // arasında yeniden kullanılır (kanal üst bandı 8).
    const size_t ringFloats = kStreamRingCapacityFrames * 8;
    if (m_bgmRing.size() < ringFloats) {
        m_bgmRing.assign(ringFloats, 0.0f);
    }
    if (m_bgmPumpScratch.size() < kStreamRingChunkFrames * 8) {
        m_bgmPumpScratch.assign(kStreamRingChunkFrames * 8, 0.0f);
    }
    m_bgmStreamSource = std::move(source);
    m_bgmRingChannels = channels;
    m_bgmStreamRateHz = rate;
    m_bgmRingWriteFrames = 0;
    m_bgmStreamPcmPos = 0;
    m_bgmStreamEos = false;
    m_bgmStreamFilter = filter;
    m_bgmTransitionActive = false;
    m_currentBgmPath = assetPath;
    m_isBgmPlaying = true;
    m_bgmSampleOffset = 0;
    m_isBgmStreamed = true;
    if (m_bgmStream) {
        // A5-tur1: hazırlık fail'leri kayda geçer (warn-only; return true
        // aynen — başarı+kirlilik olmamalıdır).
        if (!SDL_ClearAudioStream(m_bgmStream)) {
            ROWL_LOG_WARN("[AudioEngine] Failed to clear BGM stream for streaming playback: " +
                          std::string(SDL_GetError()));
        }
        SDL_AudioSpec floatSpec{};
        floatSpec.format = SDL_AUDIO_F32;
        floatSpec.channels = static_cast<Uint8>(channels);
        floatSpec.freq = static_cast<int>(rate);
        if (!SDL_SetAudioStreamFormat(m_bgmStream, &floatSpec, nullptr)) {
            ROWL_LOG_WARN("[AudioEngine] Failed to set BGM stream format for streaming playback: " +
                          std::string(SDL_GetError()));
        }
    }
    const double threshold =
        longAudioThresholdSeconds(rate, channels, 2);
    // ov_pcm_total bilinmiyorsa duration negatiftir; mode=Stream DEĞİL,
    // fail-closed Unknown + unknown_header. Tek paylaşılan predicate
    // shouldStreamRoute'dur: m_isBgmStreamed (IsStreaming) ile C# IsStream
    // (mode==stream && duration>threshold) aynı kararı verir, diverge olmaz.
    const double streamDuration = m_bgmStreamSource->durationSeconds();
    m_streamInfo.durationSeconds = streamDuration;
    m_streamInfo.thresholdSeconds = threshold;
    m_streamInfo.bufferedSeconds = 0.0;
    if (streamDuration < 0.0 || !shouldStreamRoute(streamDuration, threshold)) {
        m_streamInfo.mode = StreamMode::Unknown;
        m_streamInfo.reason = "unknown_header";
        m_isBgmStreamed = false;
    } else {
        m_streamInfo.mode = StreamMode::Stream;
        m_streamInfo.reason = "over_threshold";
        m_isBgmStreamed = true;
    }
    m_streamInfo.channel = snapshotChannel;
    m_streamInfo.asset = assetPath;
    applyChannelGains();
    pumpBgmStream();
    if (m_bgmStream && m_deviceAvailable && !m_outputSuspended) {
        // A5-tur1: warn-only (açılış başarılı sayılır; return true aynen).
        if (!SDL_ResumeAudioStreamDevice(m_bgmStream)) {
            ROWL_LOG_WARN("[AudioEngine] Failed to resume BGM stream for streaming playback: " +
                          std::string(SDL_GetError()));
        }
    }
    ROWL_LOG_INFO("[AudioEngine] Streaming playback started: " + assetPath);
    return true;
}

void AudioEngine::queueStreamChunkToDevice(const float* samples, size_t frames,
                                           uint32_t channels,
                                           uint32_t sampleRate) {
    (void)channels;
    (void)sampleRate;
    if (!samples || frames == 0 || !m_bgmStream || !m_deviceAvailable) return;
    // A5-tur1: pump-thread path'i — warn-once log-only (m_lastError snapshot
    // kontratı gereği update-thread'den ezilmez).
    if (!SDL_PutAudioStreamData(m_bgmStream, samples,
                           static_cast<int>(frames * channels * sizeof(float)))) {
        warnAudioOnce("BGM stream chunk re-queue failed: " + std::string(SDL_GetError()));
        ++m_dropCount;
    } else if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(m_bgmStream)) {
        warnAudioOnce("BGM stream chunk resume failed: " + std::string(SDL_GetError()));
    }
}

void AudioEngine::pumpBgmStream() {
    if (!m_initialized || !m_isBgmStreamed || !m_bgmStreamSource ||
        !m_bgmStreamSource->isOpen()) {
        return;
    }
    // Suspended iken konum korunur: decode ilerlemez, kuyruk tüketilmez.
    if (m_outputSuspended) return;
    // Faz 5 Dilim 2: maliyet gözlemlenebilirliği (fail kapısı YOKTUR).
    // Koruma/suspend dönüşleri örnek üretmez; gövdeye giren her çağrı
    // (erken-eos dönüşü dahil) pencereye bir örnek yazar.
    const auto pumpStart = std::chrono::steady_clock::now();
    auto recordElapsed = [&]() {
        const auto elapsed = std::chrono::steady_clock::now() - pumpStart;
        recordPumpSample(static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
    };
    const uint32_t rate = m_bgmStreamRateHz;
    const uint32_t channels = m_bgmRingChannels;
    if (rate == 0 || channels == 0 || channels > 8) return;
    if (m_bgmPumpScratch.size() < kStreamRingChunkFrames * 8) {
        m_bgmPumpScratch.assign(kStreamRingChunkFrames * 8, 0.0f);
    }
    if (m_bgmStreamEos && !m_bgmLoop) {
        if (!m_bgmStream ||
            SDL_GetAudioStreamAvailable(m_bgmStream) <= 0) {
            m_isBgmPlaying = false;
        }
        recordElapsed();
        return;
    }
    const bool haveDevice = (m_bgmStream != nullptr) && m_deviceAvailable;
    const int targetQueued =
        static_cast<int>(rate * channels * sizeof(float)) / 2; // ~0.5 sn
    int chunks = 0;
    int emptyWraps = 0;
    while (chunks < 4) {
        if (haveDevice && m_bgmStreamPcmPos > 0 &&
            SDL_GetAudioStreamAvailable(m_bgmStream) >= targetQueued) {
            break;
        }
        bool eos = false;
        const size_t got = m_bgmStreamSource->readFloatFrames(
            m_bgmPumpScratch.data(), kStreamRingChunkFrames, eos);
        if (got > 0) {
            applyDspToFloatPcm(m_bgmPumpScratch.data(), got * channels,
                               static_cast<int>(channels),
                               static_cast<int>(rate), m_bgmStreamFilter);
            const size_t ringFloats = kStreamRingCapacityFrames * channels;
            const size_t floats = got * channels;
            if (m_bgmRing.size() >= ringFloats) {
                for (size_t i = 0; i < floats; ++i) {
                    m_bgmRing[static_cast<size_t>(
                        ((m_bgmRingWriteFrames * channels) + i) % ringFloats)] =
                        m_bgmPumpScratch[i];
                }
            }
            queueStreamChunkToDevice(m_bgmPumpScratch.data(), got, channels,
                                     rate);
            m_bgmRingWriteFrames += got;
            m_bgmStreamPcmPos += got;
            ++chunks;
        }
        if (eos) {
            // Corrupt-mid-stream: ov_read<0 ara-akışta loop açık olsa bile
            // sonsuz seek(0) döngüsüne girme; fail-closed hata + stop/close
            // (decode-yolu corrupt davranışıyla tutarlı).
            const std::string& srcError = m_bgmStreamSource->lastError();
            const bool midStreamCorrupt =
                !srcError.empty() && srcError.find("corrupt") != std::string::npos;
            if (midStreamCorrupt) {
                m_lastError = srcError;
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
                stopBgm();
                break;
            }
            if (m_bgmLoop && m_bgmStreamSource->seekPcmFrame(0)) {
                // Loop-wrap: yeni turun konumu sıfırdan başlar; ring write
                // sayaçları ve pcm konumu sıfırlanır. bufferedSeconds tanımı
                // korunur: min(pcmPos,kapasite)/rate.
                m_bgmStreamPcmPos = 0;
                m_bgmRingWriteFrames = 0;
                if (got == 0 && ++emptyWraps >= 2) break;
                continue;
            }
            m_bgmStreamEos = true;
            break;
        }
        if (got == 0) break;
    }
    recordElapsed();
}

void AudioEngine::closeBgmStream() {
    if (m_bgmStreamSource) {
        m_bgmStreamSource->close();
        m_bgmStreamSource.reset();
    }
    m_isBgmStreamed = false;
    m_bgmStreamEos = false;
    m_bgmStreamPcmPos = 0;
    m_bgmRingWriteFrames = 0;
}

void AudioEngine::resetStreamInfoNoBgm() {
    m_streamInfo = StreamInfo{};
}

std::string AudioEngine::streamInfoJson() const {
    StreamInfo info = m_streamInfo;
    if (m_isBgmStreamed) {
        info.bufferedSeconds = bgmStreamBufferedSeconds();
    } else {
        info.bufferedSeconds = 0.0;
    }
    return streamInfoToJson(info);
}

double AudioEngine::bgmStreamBufferedSeconds() const {
    if (!m_isBgmStreamed || m_bgmStreamRateHz == 0) return 0.0;
    const uint64_t valid =
        std::min<uint64_t>(m_bgmStreamPcmPos, kStreamRingCapacityFrames);
    return static_cast<double>(valid) /
           static_cast<double>(m_bgmStreamRateHz);
}

// ── Faz 5 Dilim 2: mixer / polyphony / eğriler / bed'ler / pump ─────────────

void AudioEngine::setSfxPoolDepth(int depth) {
    const size_t want = SfxVoicePool::clampDepth(depth);
    if (want == m_sfxPool.depth()) return;
    // Daraltmada düşen slotların cihaz kuyrukları da yıkılır (sesi keser).
    destroySfxPoolStreams();
    m_sfxPool.setDepth(static_cast<int>(want));
    if (m_initialized && m_audioLeaseHeld) {
        ensureSfxPoolStreams();
        applyChannelGains();
    }
}

std::vector<std::string> AudioEngine::sfxActivePaths() const {
    std::vector<std::string> paths;
    for (const auto& voice : m_sfxPool.voices()) {
        if (voice.playing && !voice.pcm.empty()) paths.push_back(voice.assetPath);
    }
    return paths;
}

void AudioEngine::ensureSfxPoolStreams() {
    const size_t want = m_sfxPool.depth();
    if (m_sfxPoolStreams.size() != want) {
        destroySfxPoolStreams();
        m_sfxPoolStreams.assign(want, nullptr);
    }
    if (!m_audioLeaseHeld) return; // headless: havuz durumu korunur, akış yok
    const float gain = m_mixer.gainFor(StreamBusId::Sfx);
    for (size_t i = 0; i < want; ++i) {
        if (!m_sfxPoolStreams[i]) {
            // A5-tur1: havuz akış açılış fail'i kayda geçer (ilk hata korunur).
            m_sfxPoolStreams[i] = SDL_OpenAudioDeviceStream(
                SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
            if (!m_sfxPoolStreams[i]) {
                if (m_lastError.empty()) {
                    m_lastError = "SFX pool audio stream could not be opened: " +
                                  std::string(SDL_GetError());
                }
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            } else if (!SDL_SetAudioStreamGain(m_sfxPoolStreams[i], gain)) {
                warnAudioOnce("SFX pool stream gain not applied: " + std::string(SDL_GetError()));
            }
        }
    }
}

void AudioEngine::destroySfxPoolStreams() {
    for (SDL_AudioStream* stream : m_sfxPoolStreams) {
        if (stream) SDL_DestroyAudioStream(stream);
    }
    m_sfxPoolStreams.clear();
}

SDL_AudioStream* AudioEngine::ambienceBedStream(int bed) const {
    if (bed == 0) return m_ambienceStream;
    if (bed == 1) return m_ambienceStreamB;
    return nullptr;
}

void AudioEngine::clearAmbienceBed(int bed) {
    if (!isValidAmbienceBed(bed)) return;
    SDL_AudioStream* stream = ambienceBedStream(bed);
    if (stream) {
        // A5-tur1: warn-only (bed-clear update-thread'den de çağrılır;
        // m_lastError snapshot kontratı korunur; state sıfırlama aynen).
        if (!SDL_ClearAudioStream(stream)) {
            ROWL_LOG_WARN("[AudioEngine] Failed to clear ambience bed stream: " +
                          std::string(SDL_GetError()));
        }
        if (!SDL_PauseAudioStreamDevice(stream)) {
            ROWL_LOG_WARN("[AudioEngine] Failed to pause ambience bed stream: " +
                          std::string(SDL_GetError()));
        }
    }
    if (bed == 0) {
        m_ambienceData.clear();
        m_ambienceSampleOffset = 0;
        m_isAmbiencePlaying = false;
        m_currentAmbiencePath.clear();
    } else {
        m_ambienceDataB.clear();
        m_ambienceSampleOffsetB = 0;
        m_isAmbiencePlayingB = false;
        m_currentAmbiencePathB.clear();
    }
}

void AudioEngine::queueAmbienceBed(int bed, const SDL_AudioSpec& floatSpec,
                                  const uint8_t* floatBytes, size_t byteCount,
                                  const std::string& assetPath) {
    if (!isValidAmbienceBed(bed) || !floatBytes || byteCount == 0) return;
    // A5-tur1: queue snapshot'ı — decode başarılıysa buraya temiz gelinir;
    // önceki çağrıdan stale kalmamalıdır.
    m_lastError.clear();
    std::vector<uint8_t>& data = (bed == 0) ? m_ambienceData : m_ambienceDataB;
    size_t& offset = (bed == 0) ? m_ambienceSampleOffset : m_ambienceSampleOffsetB;
    bool& playing = (bed == 0) ? m_isAmbiencePlaying : m_isAmbiencePlayingB;
    std::string& path = (bed == 0) ? m_currentAmbiencePath : m_currentAmbiencePathB;
    data.assign(floatBytes, floatBytes + byteCount);
    offset = 0;
    playing = true;
    path = assetPath;
    m_ambienceBedChannels[bed] =
        (floatSpec.channels >= 1 && floatSpec.channels <= 8) ? floatSpec.channels : 2;
    m_ambienceBedRateHz[bed] = (floatSpec.freq > 0) ? floatSpec.freq : 48000;
    SDL_AudioStream* stream = ambienceBedStream(bed);
    if (stream && m_deviceAvailable) {
        // A5-tur1: kuyruk fail'i kayda geçer — playAmbienceBed true dönerken
        // hata görünmezdi (C API körlüğü). Davranış aynen (dönüş değişmez).
        if (!SDL_ClearAudioStream(stream)) {
            ROWL_LOG_WARN("[AudioEngine] Failed to clear ambience bed stream before queue: " +
                          std::string(SDL_GetError()));
        }
        if (SDL_SetAudioStreamFormat(stream, &floatSpec, nullptr) &&
            SDL_PutAudioStreamData(stream, floatBytes, static_cast<int>(byteCount))) {
            if (!m_outputSuspended && !SDL_ResumeAudioStreamDevice(stream) && m_lastError.empty()) {
                m_lastError = "Ambience bed stream resume failed: " + std::string(SDL_GetError());
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
            }
        } else if (m_lastError.empty()) {
            m_lastError = "Ambience bed audio could not be queued: " + std::string(SDL_GetError());
            ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
        }
    }
}

bool AudioEngine::playAmbienceBed(int bed, const std::string& assetPath) {
    if (!m_initialized || !isValidAmbienceBed(bed) || assetPath.empty()) return false;
    SDL_AudioSpec spec{};
    std::vector<uint8_t> pcm;
    if (!decodeAssetToFloatPcm(assetPath, DSPFilterType::Normal, false, false, spec, pcm)) {
        return false;
    }
    cancelAmbienceCrossfade();
    queueAmbienceBed(bed, spec, pcm.data(), pcm.size(), assetPath);
    applyChannelGains();
    return true;
}

void AudioEngine::stopAmbienceBed(int bed) {
    if (!isValidAmbienceBed(bed)) return;
    if (m_ambCrossActive && (bed == m_ambCrossFrom || bed == m_ambCrossTo)) {
        cancelAmbienceCrossfade();
    }
    clearAmbienceBed(bed);
    applyChannelGains();
}

void AudioEngine::setAmbienceBedVolume(int bed, float volume) {
    if (!isValidAmbienceBed(bed) || !std::isfinite(volume)) return;
    volume = std::clamp(volume, 0.0f, 1.0f);
    if (bed == 0) {
        // Miras tek-bed üyesi BedA ile çift-yön senkron tutulur.
        m_ambienceVolume = volume;
        m_mixer.setUserVolume(StreamBusId::Ambience, volume);
    } else {
        m_ambienceVolumeB = volume;
    }
    m_mixer.setAmbienceBedVolume(bed, volume);
    applyChannelGains();
}

float AudioEngine::ambienceBedVolume(int bed) const {
    if (!isValidAmbienceBed(bed)) return 0.0f;
    return (bed == 0) ? m_ambienceVolume : m_ambienceVolumeB;
}

bool AudioEngine::isAmbienceBedPlaying(int bed) const {
    if (!isValidAmbienceBed(bed)) return false;
    return (bed == 0) ? m_isAmbiencePlaying : m_isAmbiencePlayingB;
}

std::string AudioEngine::ambienceBedPath(int bed) const {
    if (!isValidAmbienceBed(bed)) return "";
    return (bed == 0) ? m_currentAmbiencePath : m_currentAmbiencePathB;
}

bool AudioEngine::crossfadeAmbienceTo(const std::string& assetPath,
                                     float durationSeconds, FadeCurve curve) {
    if (!m_initialized || assetPath.empty()) return false;
    if (!std::isfinite(durationSeconds) || durationSeconds < 0.0f) durationSeconds = 0.0f;
    durationSeconds = std::min(durationSeconds, 60.0f);
    SDL_AudioSpec spec{};
    std::vector<uint8_t> pcm;
    if (!decodeAssetToFloatPcm(assetPath, DSPFilterType::Normal, false, false, spec, pcm)) {
        return false;
    }
    const int from = m_isAmbiencePlaying ? 0 : (m_isAmbiencePlayingB ? 1 : 0);
    const int to = 1 - from;
    if (durationSeconds <= 0.0f) {
        // Anlık geçiş = bugünkü davranış: çalan bed yerinde değişir,
        // diğer bed susar (hiçbir şey çalmıyorsa BedA = miras yolu).
        cancelAmbienceCrossfade();
        clearAmbienceBed(to);
        queueAmbienceBed(from, spec, pcm.data(), pcm.size(), assetPath);
        applyChannelGains();
        return true;
    }
    queueAmbienceBed(to, spec, pcm.data(), pcm.size(), assetPath);
    m_ambCrossActive = true;
    m_ambCrossFrom = from;
    m_ambCrossTo = to;
    m_ambCrossElapsed = 0.0f;
    m_ambCrossDuration = durationSeconds;
    m_ambCrossCurve = curve;
    // İlk kare: from tam kazançta, to sessiz (eğri uçları snap'lenir).
    // A5-tur1: warn-only (dönüş true aynen; matematik değişmez).
    SDL_AudioStream* fromStream = ambienceBedStream(from);
    if (fromStream && !SDL_SetAudioStreamGain(fromStream, ambienceBedGain(from))) {
        ROWL_LOG_WARN("[AudioEngine] Ambience crossfade from-gain not applied: " +
                      std::string(SDL_GetError()));
    }
    SDL_AudioStream* toStream = ambienceBedStream(to);
    if (toStream && !SDL_SetAudioStreamGain(toStream, ambienceBedGain(to))) {
        ROWL_LOG_WARN("[AudioEngine] Ambience crossfade to-gain not applied: " +
                      std::string(SDL_GetError()));
    }
    return true;
}

void AudioEngine::cancelAmbienceCrossfade() {
    m_ambCrossActive = false;
    m_ambCrossFrom = 0;
    m_ambCrossTo = 1;
    m_ambCrossElapsed = 0.0f;
    m_ambCrossDuration = 0.0f;
}

void AudioEngine::updateAmbienceCrossfade(float deltaSeconds) {
    if (!m_ambCrossActive) return;
    m_ambCrossElapsed += std::max(0.0f, deltaSeconds);
    // A5-tur1: per-frame path — warn-once log-only (m_lastError'e yazılmaz).
    SDL_AudioStream* fromStream = ambienceBedStream(m_ambCrossFrom);
    if (fromStream && !SDL_SetAudioStreamGain(fromStream, ambienceBedGain(m_ambCrossFrom))) {
        warnAudioOnce("Ambience crossfade from-gain failed: " + std::string(SDL_GetError()));
    }
    SDL_AudioStream* toStream = ambienceBedStream(m_ambCrossTo);
    if (toStream && !SDL_SetAudioStreamGain(toStream, ambienceBedGain(m_ambCrossTo))) {
        warnAudioOnce("Ambience crossfade to-gain failed: " + std::string(SDL_GetError()));
    }
    const float progress = (m_ambCrossDuration > 0.0f)
        ? std::clamp(m_ambCrossElapsed / m_ambCrossDuration, 0.0f, 1.0f)
        : 1.0f;
    if (progress >= 1.0f) {
        clearAmbienceBed(m_ambCrossFrom);
        m_ambCrossActive = false;
        applyChannelGains();
    }
}

float AudioEngine::ambienceCrossScale(int bed, float progress) const {
    if (!m_ambCrossActive) return 1.0f;
    const float p = std::clamp(progress, 0.0f, 1.0f);
    if (bed == m_ambCrossFrom) return fadeCurveOutgoing(m_ambCrossCurve, p);
    if (bed == m_ambCrossTo) return fadeCurveIncoming(m_ambCrossCurve, p);
    return 1.0f;
}

float AudioEngine::ambienceBedGain(int bed) const {
    if (!isValidAmbienceBed(bed)) return 0.0f;
    const float base = m_mixer.gainForAmbienceBed(bed);
    if (!m_ambCrossActive) return base;
    const float progress = (m_ambCrossDuration > 0.0f)
        ? std::clamp(m_ambCrossElapsed / m_ambCrossDuration, 0.0f, 1.0f)
        : 1.0f;
    return base * ambienceCrossScale(bed, progress);
}

void AudioEngine::recordPumpSample(uint64_t microseconds) {
    m_pumpWindow[m_pumpWindowPos % m_pumpWindow.size()] = microseconds;
    ++m_pumpWindowPos;
    ++m_pumpCount;
    m_pumpLastUs = microseconds;
    if (microseconds > m_pumpMaxUs) m_pumpMaxUs = microseconds;
}

uint64_t AudioEngine::bgmPumpAvgMicroseconds() const {
    const size_t filled =
        static_cast<size_t>(std::min<uint64_t>(m_pumpCount, m_pumpWindow.size()));
    if (filled == 0) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < filled; ++i) sum += m_pumpWindow[i];
    return sum / static_cast<uint64_t>(filled);
}

std::string AudioEngine::bgmPumpStatsJson() const {
    return std::string("{\"count\":") + std::to_string(m_pumpCount) +
           ",\"last_us\":" + std::to_string(m_pumpLastUs) +
           ",\"avg_us\":" + std::to_string(bgmPumpAvgMicroseconds()) +
           ",\"max_us\":" + std::to_string(m_pumpMaxUs) + "}";
}

} // namespace Rowl::Audio
