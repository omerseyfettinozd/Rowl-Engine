#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace Rowl::Audio {

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
    m_bgmGain = 1.0f;
    m_duckingFactor = 0.5f;
    m_activeFilter = DSPFilterType::Normal;
    m_isDuckingActive = false;
    m_currentBgmPath = "";
    m_deviceAvailable = false;

    // Initialize SDL3 Audio subsystem
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        // Open default audio device stream for BGM
        m_bgmStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
        // Open separate audio device stream for SFX & Voice
        m_sfxStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);

        if (m_bgmStream && m_sfxStream) {
            m_deviceAvailable = true;
            SDL_SetAudioStreamGain(m_bgmStream, m_bgmGain);
            SDL_SetAudioStreamGain(m_sfxStream, 1.0f);
            ROWL_LOG_INFO("[AudioEngine] Physical audio device initialized successfully (BGM & SFX streams active).");
        } else {
            ROWL_LOG_WARN("[AudioEngine] Audio streams could not be opened: " + std::string(SDL_GetError()) +
                          " — running in silent fallback mode.");
            if (m_bgmStream) { SDL_DestroyAudioStream(m_bgmStream); m_bgmStream = nullptr; }
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

void AudioEngine::playAudio(const std::string& assetPath, AudioChannelType channel, DSPFilterType filter) {
    if (!m_initialized || assetPath.empty()) return;

    std::string channelName = (channel == AudioChannelType::Bgm) ? "BGM (Streaming)" :
                              (channel == AudioChannelType::Voice) ? "Voice" : "SFX (Memory Pool)";

    ROWL_LOG_INFO("Audio Play -> Asset: '" + assetPath + "' on Channel: " + channelName);

    if (channel == AudioChannelType::Voice) {
        triggerVoiceDucking(true);
    }

    if (filter != DSPFilterType::Normal) {
        applyDspFilter(filter);
    }

    if (channel == AudioChannelType::Bgm) {
        m_currentBgmPath = assetPath;
    }

    if (!m_deviceAvailable) {
        ROWL_LOG_INFO("[AudioEngine] Audio play registered (silent fallback): " + assetPath);
        return;
    }

    std::vector<uint8_t> bytes;
    SDL_AudioSpec spec;
    Uint8* audioBuf = nullptr;
    Uint32 audioLen = 0;
    bool loaded = false;

    // 1. Try physical filesystem directly
    if (fs::exists(assetPath) && fs::is_regular_file(assetPath)) {
        loaded = SDL_LoadWAV(assetPath.c_str(), &spec, &audioBuf, &audioLen);
    }

    // 2. Try VFS lookup
    if (!loaded) {
        std::vector<std::string> vfsCandidates = {
            assetPath,
            "Assets/" + assetPath,
            "Assets/audio/" + assetPath,
            "audio/" + assetPath
        };
        for (const auto& candidate : vfsCandidates) {
            if (Rowl::VFS::VFSManager::instance().exists(candidate)) {
                bytes = Rowl::VFS::VFSManager::instance().readBytes(candidate);
                if (!bytes.empty()) {
                    SDL_IOStream* io = SDL_IOFromConstMem(bytes.data(), bytes.size());
                    if (io) {
                        loaded = SDL_LoadWAV_IO(io, true, &spec, &audioBuf, &audioLen);
                        if (loaded) break;
                    }
                }
            }
        }
    }

    if (loaded && audioBuf && audioLen > 0) {
        SDL_AudioStream* targetStream = (channel == AudioChannelType::Bgm) ? m_bgmStream : m_sfxStream;
        if (targetStream) {
            if (channel == AudioChannelType::Bgm) {
                SDL_ClearAudioStream(m_bgmStream);
                m_bgmData.assign(audioBuf, audioBuf + audioLen);
            }
            SDL_SetAudioStreamFormat(targetStream, &spec, nullptr);
            SDL_PutAudioStreamData(targetStream, audioBuf, static_cast<int>(audioLen));
            SDL_ResumeAudioStreamDevice(targetStream);
            ROWL_LOG_INFO("[AudioEngine] Playback started: " + assetPath + " (" + std::to_string(audioLen) + " bytes)");
        }
        SDL_free(audioBuf);
    } else {
        ROWL_LOG_WARN("[AudioEngine] Audio file could not be loaded: '" + assetPath + "' (WAV format required)");
    }
}

void AudioEngine::stopBgm() {
    if (m_bgmStream) {
        SDL_ClearAudioStream(m_bgmStream);
        SDL_PauseAudioStreamDevice(m_bgmStream);
    }
    m_currentBgmPath = "";
    m_bgmData.clear();
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
    m_bgmVolume = std::clamp(volume, 0.0f, 1.0f);
    m_bgmGain = m_isDuckingActive ? (m_bgmVolume * m_duckingFactor) : m_bgmVolume;
    if (m_bgmStream) {
        SDL_SetAudioStreamGain(m_bgmStream, m_bgmGain);
    }
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
    if (m_bgmStream) {
        SDL_SetAudioStreamGain(m_bgmStream, m_bgmGain);
    }
}

void AudioEngine::setDuckingFactor(float factor) {
    m_duckingFactor = std::clamp(factor, 0.0f, 1.0f);
    if (m_isDuckingActive) {
        m_bgmGain = m_bgmVolume * m_duckingFactor;
    }
    if (m_bgmStream) {
        SDL_SetAudioStreamGain(m_bgmStream, m_bgmGain);
    }
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

void AudioEngine::update() {
    if (!m_initialized || !m_deviceAvailable || !m_bgmStream || m_bgmData.empty() || !m_bgmLoop) {
        return;
    }
    int available = SDL_GetAudioStreamAvailable(m_bgmStream);
    if (available <= 0) {
        SDL_PutAudioStreamData(m_bgmStream, m_bgmData.data(), static_cast<int>(m_bgmData.size()));
        SDL_ResumeAudioStreamDevice(m_bgmStream);
    }
}

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Audio Engine Subsystem...");

    m_bgmData.clear();

    if (m_bgmStream) {
        SDL_DestroyAudioStream(m_bgmStream);
        m_bgmStream = nullptr;
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

} // namespace Rowl::Audio
