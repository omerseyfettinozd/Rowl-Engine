#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/sdl_subsystem_lease.hpp"
#include "rowl/vfs/vfs.hpp"
#include <SDL3/SDL.h>
#include <vorbis/vorbisfile.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstring>
#include <istream>
#include <vector>
#include <cctype>

namespace Rowl::Audio {

namespace {

constexpr uintmax_t kMaxEncodedAudioBytes = 64ULL * 1024 * 1024;
constexpr Uint32 kMaxDecodedAudioBytes = 64U * 1024 * 1024;

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

void applyDspToFloatPcm(float* samples, size_t sampleCount, int channels,
                        int sampleRate, DSPFilterType filter) {
    if (!samples || sampleCount == 0 || channels <= 0 || sampleRate <= 0 ||
        filter == DSPFilterType::Normal) {
        return;
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
                samples[sample] = lowPass[channel];
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
}

} // namespace

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
    m_sfxSampleOffset = 0;
    m_isSfxPlaying = false;
    m_lastSfxData.clear();
    m_voiceBlipCount = 0;
    m_lastVoiceBlipPitch = 1.0f;

    // Initialize SDL3 Audio subsystem
    if (Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_AUDIO)) {
        m_audioLeaseHeld = true;
        // Open default audio device stream for BGM
        m_bgmStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
        // A second BGM stream lets a new, already-decoded track be queued
        // before the current track is touched. This is what makes transition
        // failures transactional and enables a real crossfade.
        m_transitionBgmStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
        // Voice has its own gain path so narration controls never affect SFX.
        m_voiceStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
        // Open a third stream for short sound effects.
        m_sfxStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);

        if (m_bgmStream && m_transitionBgmStream && m_voiceStream && m_sfxStream) {
            m_deviceAvailable = true;
            applyChannelGains();
            ROWL_LOG_INFO("[AudioEngine] Physical audio device initialized successfully (BGM, Voice & SFX streams active).");
        } else {
            ROWL_LOG_WARN("[AudioEngine] Audio streams could not be opened: " + std::string(SDL_GetError()) +
                          " — running in silent fallback mode.");
            if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
            if (m_transitionBgmStream) { SDL_DestroyAudioStream(m_transitionBgmStream); m_transitionBgmStream = nullptr; }
            if (m_voiceStream) { SDL_DestroyAudioStream(m_voiceStream); m_voiceStream = nullptr; }
            if (m_sfxStream) { SDL_DestroyAudioStream(m_sfxStream); m_sfxStream = nullptr; }
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
    if (!m_initialized || assetPath.empty()) return;

    m_lastError.clear();

    std::string channelName = (channel == AudioChannelType::Bgm) ? "BGM (Streaming)" :
                              (channel == AudioChannelType::Voice) ? "Voice" : "SFX (Memory Pool)";

    ROWL_LOG_INFO("Audio Play -> Asset: '" + assetPath + "' on Channel: " + channelName);

    if (!m_deviceAvailable) {
        // Keep intended BGM state in headless/silent environments. This lets
        // scene transitions remain deterministic even when no device exists.
        if (channel == AudioChannelType::Bgm) {
            m_currentBgmPath = assetPath;
            m_isBgmPlaying = true;
            m_bgmTransitionActive = false;
        }
        if (channel == AudioChannelType::Voice) {
            m_isVoicePlaying = true;
            triggerVoiceDucking(true);
        }
        if (filter != DSPFilterType::Normal) applyDspFilter(filter);
        ROWL_LOG_INFO("[AudioEngine] Audio play registered (silent fallback): " + assetPath);
        return;
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
                    if (!audioBuf) { m_lastError = "Unable to allocate decoded Ogg/Vorbis PCM"; return; }
                    std::memcpy(audioBuf, bytes.data(), bytes.size());
                    audioLen = static_cast<Uint32>(bytes.size());
                    loaded = true;
                    break;
                }
                if (m_lastError.empty()) m_lastError = "Ogg/Vorbis stream could not be decoded";
                ROWL_LOG_WARN("[AudioEngine] " + m_lastError + ": " + assetPath);
                return;
            }
            bytes = vfs().readBytes(candidate);
            if (!bytes.empty()) {
                if (bytes.size() > kMaxEncodedAudioBytes) {
                    ROWL_LOG_WARN("Audio file exceeds the maximum accepted size: " + assetPath);
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
            ROWL_LOG_WARN("Decoded audio exceeds the maximum accepted size: " + assetPath);
            SDL_free(audioBuf);
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
            return;
        }
        auto* samples = reinterpret_cast<float*>(floatBuffer);
        applyDspToFloatPcm(samples, static_cast<size_t>(floatLength) / sizeof(float),
                           floatSpec.channels, floatSpec.freq, filter);

        const bool transitionRequested = channel == AudioChannelType::Bgm &&
            m_isBgmPlaying && !m_currentBgmPath.empty() &&
            m_requestedBgmTransition != BgmTransitionKind::Instant &&
            m_requestedBgmTransitionDurationSeconds > 0.0f;
        SDL_AudioStream* targetStream = (channel == AudioChannelType::Bgm)
            ? (transitionRequested ? m_transitionBgmStream : m_bgmStream) :
                                       (channel == AudioChannelType::Voice) ? m_voiceStream : m_sfxStream;
        if (targetStream) {
            if (channel == AudioChannelType::Bgm && !transitionRequested) {
                SDL_ClearAudioStream(m_bgmStream);
            }
            if (transitionRequested) {
                SDL_ClearAudioStream(m_transitionBgmStream);
            }
            if (!SDL_SetAudioStreamFormat(targetStream, &floatSpec, nullptr) ||
                !SDL_PutAudioStreamData(targetStream, floatBuffer, floatLength)) {
                ROWL_LOG_ERROR("[AudioEngine] Failed to queue decoded audio: " + std::string(SDL_GetError()));
                m_lastError = "Unable to queue decoded audio: " + std::string(SDL_GetError());
                SDL_free(floatBuffer);
                SDL_free(audioBuf);
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
                m_currentBgmPath = assetPath;
                m_isBgmPlaying = true;
                m_bgmSampleOffset = 0;
            } else if (channel == AudioChannelType::Sfx) {
                m_lastSfxData.assign(floatBuffer, floatBuffer + floatLength);
                m_isSfxPlaying = true;
                m_sfxSampleOffset = 0;
            }
            if (!m_outputSuspended) SDL_ResumeAudioStreamDevice(targetStream);
            ROWL_LOG_INFO("[AudioEngine] Playback started: " + assetPath + " (" + std::to_string(floatLength) + " PCM bytes)");
        } else {
            // Headless / fallback playback without physical stream
            if (channel == AudioChannelType::Bgm) {
                m_bgmData.assign(floatBuffer, floatBuffer + floatLength);
                m_currentBgmPath = assetPath;
                m_isBgmPlaying = true;
                m_bgmSampleOffset = 0;
            } else if (channel == AudioChannelType::Sfx) {
                m_lastSfxData.assign(floatBuffer, floatBuffer + floatLength);
                m_isSfxPlaying = true;
                m_sfxSampleOffset = 0;
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
    }
}

void AudioEngine::stopBgm() {
    if (m_bgmStream) {
        SDL_ClearAudioStream(m_bgmStream);
        SDL_PauseAudioStreamDevice(m_bgmStream);
    }
    if (m_transitionBgmStream) {
        SDL_ClearAudioStream(m_transitionBgmStream);
        SDL_PauseAudioStreamDevice(m_transitionBgmStream);
    }
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
    if (m_sfxStream) {
        SDL_ClearAudioStream(m_sfxStream);
        SDL_PauseAudioStreamDevice(m_sfxStream);
    }
    m_lastSfxData.clear();
    m_sfxSampleOffset = 0;
    m_isSfxPlaying = false;
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
    applyChannelGains();
}

void AudioEngine::setMasterVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    applyChannelGains();
}

void AudioEngine::setVoiceVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_voiceVolume = std::clamp(volume, 0.0f, 1.0f);
    applyChannelGains();
}

void AudioEngine::setSfxVolume(float volume) {
    if (!std::isfinite(volume)) return;
    m_sfxVolume = std::clamp(volume, 0.0f, 1.0f);
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
        if (m_bgmStream && !m_bgmData.empty() && m_bgmLoop) {
            int available = SDL_GetAudioStreamAvailable(m_bgmStream);
            if (available <= 0) {
                SDL_PutAudioStreamData(m_bgmStream, m_bgmData.data(), static_cast<int>(m_bgmData.size()));
                if (!m_outputSuspended) SDL_ResumeAudioStreamDevice(m_bgmStream);
            }
        }
        if (m_transitionBgmStream && !m_transitionBgmData.empty() && m_bgmLoop) {
            if (SDL_GetAudioStreamAvailable(m_transitionBgmStream) <= 0) {
                SDL_PutAudioStreamData(m_transitionBgmStream, m_transitionBgmData.data(), static_cast<int>(m_transitionBgmData.size()));
                if (!m_outputSuspended) SDL_ResumeAudioStreamDevice(m_transitionBgmStream);
            }
        }
        updateBgmTransition(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f);

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
    float outgoing = 1.0f - progress;
    float incoming = progress;
    if (m_activeBgmTransition == BgmTransitionKind::Fade) {
        outgoing = std::max(0.0f, 1.0f - progress * 2.0f);
        incoming = std::max(0.0f, progress * 2.0f - 1.0f);
    }
    const float baseGain = m_masterVolume * (m_isDuckingActive ? m_bgmVolume * m_duckingFactor : m_bgmVolume);
    SDL_SetAudioStreamGain(m_bgmStream, baseGain * outgoing);
    SDL_SetAudioStreamGain(m_transitionBgmStream, baseGain * incoming);
    if (progress < 1.0f) return;

    SDL_ClearAudioStream(m_bgmStream);
    SDL_PauseAudioStreamDevice(m_bgmStream);
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
    m_lastSfxData.clear();
    m_telemetryBgm = {};
    m_telemetryVoice = {};
    m_telemetrySfx = {};
    m_telemetryMaster = {};
    m_spectrumBands.fill(0.0f);
    m_bgmSampleOffset = 0;
    m_sfxSampleOffset = 0;
    m_isSfxPlaying = false;

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
    if (m_sfxStream) {
        SDL_DestroyAudioStream(m_sfxStream);
        m_sfxStream = nullptr;
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
    // Playback intent (BGM path/loop buffer, volumes, filter, ducking) lives
    // in member state, so only the device-bound streams are rebuilt. The BGM
    // loop feed in update() re-queues m_bgmData into a fresh stream on its
    // own, which is also what resumes playback after a device loss.
    if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
    if (m_transitionBgmStream) { SDL_DestroyAudioStream(m_transitionBgmStream); m_transitionBgmStream = nullptr; }
    if (m_voiceStream) { SDL_DestroyAudioStream(m_voiceStream); m_voiceStream = nullptr; }
    if (m_sfxStream) { SDL_DestroyAudioStream(m_sfxStream); m_sfxStream = nullptr; }
    m_deviceAvailable = false;

    if (!m_audioLeaseHeld && !Rowl::Platform::SdlSubsystemLease::acquire(SDL_INIT_AUDIO)) {
        m_lastError = "Audio subsystem unavailable while reopening device streams";
        ROWL_LOG_WARN("[AudioEngine] " + m_lastError);
        return false;
    }
    m_audioLeaseHeld = true;

    m_bgmStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
    m_transitionBgmStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
    m_voiceStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
    m_sfxStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
    if (m_bgmStream && m_transitionBgmStream && m_voiceStream && m_sfxStream) {
        m_deviceAvailable = true;
        applyChannelGains();
        applyDspFilter(m_activeFilter);
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
    if (m_sfxStream) { SDL_DestroyAudioStream(m_sfxStream); m_sfxStream = nullptr; }
    return false;
}

void AudioEngine::setOutputSuspended(bool suspended, bool force) {
    if (!m_initialized) return;
    if (!force && suspended == m_outputSuspended) return;
    m_outputSuspended = suspended;
    if (!m_deviceAvailable) return;
    SDL_AudioStream* streams[] = {m_bgmStream, m_transitionBgmStream, m_voiceStream, m_sfxStream};
    for (SDL_AudioStream* stream : streams) {
        if (!stream) continue;
        if (suspended) {
            SDL_PauseAudioStreamDevice(stream);
        } else {
            SDL_ResumeAudioStreamDevice(stream);
        }
    }
    ROWL_LOG_INFO(std::string("[AudioEngine] Output ") + (suspended ? "suspended." : "resumed."));
}

void AudioEngine::applyChannelGains() {
    if (m_bgmStream) SDL_SetAudioStreamGain(m_bgmStream, m_masterVolume * m_bgmGain);
    if (m_transitionBgmStream) SDL_SetAudioStreamGain(m_transitionBgmStream, 0.0f);
    if (m_voiceStream) SDL_SetAudioStreamGain(m_voiceStream, m_masterVolume * m_voiceVolume);
    if (m_sfxStream) SDL_SetAudioStreamGain(m_sfxStream, m_masterVolume * m_sfxVolume);
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

    if (m_isBgmPlaying && !m_bgmData.empty()) {
        const float* samples = reinterpret_cast<const float*>(m_bgmData.data());
        const size_t totalFloats = m_bgmData.size() / sizeof(float);
        if (totalFloats >= 2) {
            const size_t windowSize = std::min<size_t>(1024, totalFloats);
            size_t start = m_bgmSampleOffset % totalFloats;
            float maxL = 0.0f, maxR = 0.0f;
            float sumSqL = 0.0f, sumSqR = 0.0f;
            size_t frames = 0;

            for (size_t i = 0; i < windowSize && (start + i + 1) < totalFloats; i += 2) {
                float sL = std::abs(samples[start + i]);
                float sR = std::abs(samples[start + i + 1]);
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

    // 2. SFX Telemetry
    float targetSfxL = 0.0f;
    float targetSfxR = 0.0f;
    float targetSfxRmsL = 0.0f;
    float targetSfxRmsR = 0.0f;

    if (m_isSfxPlaying && !m_lastSfxData.empty()) {
        const float* samples = reinterpret_cast<const float*>(m_lastSfxData.data());
        const size_t totalFloats = m_lastSfxData.size() / sizeof(float);
        if (totalFloats >= 2 && m_sfxSampleOffset < totalFloats) {
            const size_t windowSize = std::min<size_t>(1024, totalFloats - m_sfxSampleOffset);
            size_t start = m_sfxSampleOffset;
            float maxL = 0.0f, maxR = 0.0f;
            float sumSqL = 0.0f, sumSqR = 0.0f;
            size_t frames = 0;

            for (size_t i = 0; i < windowSize && (start + i + 1) < totalFloats; i += 2) {
                float sL = std::abs(samples[start + i]);
                float sR = std::abs(samples[start + i + 1]);
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
                sumSqL += sL * sL;
                sumSqR += sR * sR;
                frames++;
            }
            if (frames > 0) {
                const float gain = m_masterVolume * m_sfxVolume;
                targetSfxL = std::clamp(maxL * gain, 0.0f, 1.0f);
                targetSfxR = std::clamp(maxR * gain, 0.0f, 1.0f);
                targetSfxRmsL = std::clamp(std::sqrt(sumSqL / static_cast<float>(frames)) * gain, 0.0f, 1.0f);
                targetSfxRmsR = std::clamp(std::sqrt(sumSqR / static_cast<float>(frames)) * gain, 0.0f, 1.0f);
            }
            m_sfxSampleOffset += static_cast<size_t>(dt * kSampleRate * 2);
            if (m_sfxSampleOffset >= totalFloats) {
                m_isSfxPlaying = false;
            }
        } else {
            m_isSfxPlaying = false;
        }
    }

    m_telemetrySfx.peakL = decayVal(m_telemetrySfx.peakL, targetSfxL);
    m_telemetrySfx.peakR = decayVal(m_telemetrySfx.peakR, targetSfxR);
    m_telemetrySfx.rmsL = decayVal(m_telemetrySfx.rmsL, targetSfxRmsL);
    m_telemetrySfx.rmsR = decayVal(m_telemetrySfx.rmsR, targetSfxRmsR);

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

    // 4. Master Telemetry (Combined peaks and RMS)
    m_telemetryMaster.peakL = std::clamp(std::max({m_telemetryBgm.peakL, m_telemetrySfx.peakL, m_telemetryVoice.peakL}), 0.0f, 1.0f);
    m_telemetryMaster.peakR = std::clamp(std::max({m_telemetryBgm.peakR, m_telemetrySfx.peakR, m_telemetryVoice.peakR}), 0.0f, 1.0f);
    m_telemetryMaster.rmsL = std::clamp(std::sqrt(m_telemetryBgm.rmsL * m_telemetryBgm.rmsL +
                                                  m_telemetrySfx.rmsL * m_telemetrySfx.rmsL +
                                                  m_telemetryVoice.rmsL * m_telemetryVoice.rmsL), 0.0f, 1.0f);
    m_telemetryMaster.rmsR = std::clamp(std::sqrt(m_telemetryBgm.rmsR * m_telemetryBgm.rmsR +
                                                  m_telemetrySfx.rmsR * m_telemetrySfx.rmsR +
                                                  m_telemetryVoice.rmsR * m_telemetryVoice.rmsR), 0.0f, 1.0f);

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
        case 3: default: tel = &m_telemetryMaster; break;
    }
    return (channelIndex == 1) ? tel->peakR : tel->peakL;
}

float AudioEngine::getChannelRms(int channelType, int channelIndex) const {
    const ChannelTelemetry* tel = nullptr;
    switch (channelType) {
        case 0: tel = &m_telemetryBgm; break;
        case 1: tel = &m_telemetryVoice; break;
        case 2: tel = &m_telemetrySfx; break;
        case 3: default: tel = &m_telemetryMaster; break;
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

    SDL_AudioStream* targetStream = (channel == AudioChannelType::Sfx) ? m_sfxStream : m_voiceStream;
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
                        SDL_ClearAudioStream(targetStream);
                        SDL_SetAudioStreamFormat(targetStream, &floatSpec, nullptr);
                        SDL_SetAudioStreamFrequencyRatio(targetStream, pitch);
                        SDL_PutAudioStreamData(targetStream, floatBuffer, floatLength);
                        SDL_ResumeAudioStreamDevice(targetStream);
                        if (channel == AudioChannelType::Voice) {
                            m_isVoicePlaying = true;
                        } else if (channel == AudioChannelType::Sfx) {
                            m_isSfxPlaying = true;
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
        SDL_SetAudioStreamFormat(targetStream, &blipSpec, nullptr);
        SDL_SetAudioStreamFrequencyRatio(targetStream, 1.0f);
        SDL_PutAudioStreamData(targetStream, blipPcm.data(), static_cast<int>(blipPcm.size() * sizeof(float)));
        SDL_ResumeAudioStreamDevice(targetStream);

        if (channel == AudioChannelType::Sfx) {
            m_lastSfxData.assign(reinterpret_cast<const uint8_t*>(blipPcm.data()),
                                 reinterpret_cast<const uint8_t*>(blipPcm.data() + blipPcm.size()));
            m_isSfxPlaying = true;
            m_sfxSampleOffset = 0;
        } else if (channel == AudioChannelType::Voice) {
            m_isVoicePlaying = true;
        }
    }
}

} // namespace Rowl::Audio
