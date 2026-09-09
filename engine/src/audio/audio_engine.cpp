#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
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

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    if (m_initialized) {
        shutdown();
    }
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
    m_bgmTransitionActive = false;
    m_bgmTransitionElapsedSeconds = 0.0f;
    m_bgmTransitionDurationSeconds = 0.0f;

    // Initialize SDL3 Audio subsystem
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
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
        if (Rowl::VFS::VFSManager::instance().exists(candidate)) {
            if (hasOggExtension(candidate)) {
                auto stream = Rowl::VFS::VFSManager::instance().openReadStream(candidate);
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
            bytes = Rowl::VFS::VFSManager::instance().readBytes(candidate);
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
            }
            SDL_ResumeAudioStreamDevice(targetStream);
            ROWL_LOG_INFO("[AudioEngine] Playback started: " + assetPath + " (" + std::to_string(floatLength) + " PCM bytes)");
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
    ROWL_LOG_INFO("[AudioEngine] BGM stopped.");
}

void AudioEngine::stopAll() {
    stopBgm();
    if (m_sfxStream) {
        SDL_ClearAudioStream(m_sfxStream);
        SDL_PauseAudioStreamDevice(m_sfxStream);
    }
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
    if (!m_initialized || !m_deviceAvailable) {
        return;
    }
    if (m_bgmStream && !m_bgmData.empty() && m_bgmLoop) {
        int available = SDL_GetAudioStreamAvailable(m_bgmStream);
        if (available <= 0) {
            SDL_PutAudioStreamData(m_bgmStream, m_bgmData.data(), static_cast<int>(m_bgmData.size()));
            SDL_ResumeAudioStreamDevice(m_bgmStream);
        }
    }
    if (m_transitionBgmStream && !m_transitionBgmData.empty() && m_bgmLoop) {
        if (SDL_GetAudioStreamAvailable(m_transitionBgmStream) <= 0) {
            SDL_PutAudioStreamData(m_transitionBgmStream, m_transitionBgmData.data(), static_cast<int>(m_transitionBgmData.size()));
            SDL_ResumeAudioStreamDevice(m_transitionBgmStream);
        }
    }
    updateBgmTransition(std::isfinite(deltaSeconds) ? deltaSeconds : 0.0f);

    if (m_voiceStream && m_isVoicePlaying && SDL_GetAudioStreamQueued(m_voiceStream) <= 0) {
        m_isVoicePlaying = false;
        triggerVoiceDucking(false);
    }
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
    if (m_deviceAvailable) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        m_deviceAvailable = false;
    }

    m_initialized = false;
    ROWL_LOG_INFO("Audio Engine Subsystem Shutdown Complete.");
}

void AudioEngine::applyChannelGains() {
    if (m_bgmStream) SDL_SetAudioStreamGain(m_bgmStream, m_masterVolume * m_bgmGain);
    if (m_transitionBgmStream) SDL_SetAudioStreamGain(m_transitionBgmStream, 0.0f);
    if (m_voiceStream) SDL_SetAudioStreamGain(m_voiceStream, m_masterVolume * m_voiceVolume);
    if (m_sfxStream) SDL_SetAudioStreamGain(m_sfxStream, m_masterVolume * m_sfxVolume);
}

} // namespace Rowl::Audio
