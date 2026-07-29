#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/logger.hpp"
#include <algorithm>  // for std::clamp

namespace Rowl::Audio {

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    if (m_initialized) {
        shutdown();
    }
}

bool AudioEngine::initialize() {
    if (m_initialized) return true;

    ROWL_LOG_INFO("Initializing Dual-Path Audio Engine Subsystem...");
    m_masterVolume = 1.0f;
    m_bgmVolume = 1.0f;
    m_bgmGain = 1.0f;
    m_duckingFactor = 0.5f;  // -6dB default, configurable
    m_activeFilter = DSPFilterType::Normal;
    m_isDuckingActive = false;

    m_initialized = true;
    ROWL_LOG_INFO("Audio Engine Subsystem Initialized Successfully.");
    return true;
}

void AudioEngine::playAudio(const std::string& assetPath, AudioChannelType channel, DSPFilterType filter) {
    if (!m_initialized) return;

    std::string channelName = (channel == AudioChannelType::Bgm) ? "BGM (Streaming)" :
                              (channel == AudioChannelType::Voice) ? "Voice" : "SFX (Memory Pool)";

    ROWL_LOG_INFO("Audio Play -> Asset: '" + assetPath + "' on Channel: " + channelName);

    if (channel == AudioChannelType::Voice) {
        triggerVoiceDucking(true);
    }

    if (filter != DSPFilterType::Normal) {
        applyDspFilter(filter);
    }
    
    // TODO: Actual audio playback implementation with miniaudio/SDL3_audio
    // For now: stub logging
    ROWL_LOG_INFO("[AudioEngine] Playback started (stub) for: " + assetPath);
}

void AudioEngine::setBgmVolume(float volume) {
    m_bgmVolume = volume;
    // Recalculate gain based on current ducking state
    m_bgmGain = m_isDuckingActive ? (m_bgmVolume * 0.5f) : m_bgmVolume; // -6 dB ducking = 0.5 multiplier
}

void AudioEngine::triggerVoiceDucking(bool isVoiceActive) {
    m_isDuckingActive = isVoiceActive;
    if (isVoiceActive) {
        m_bgmGain = m_bgmVolume * m_duckingFactor;  // Configurable ducking factor
        ROWL_LOG_INFO("Voice Ducking Triggered -> BGM Attenuated by " + std::to_string(m_duckingFactor * 100.0f) + "% (Gain: " + std::to_string(m_bgmGain) + ")");
    } else {
        m_bgmGain = m_bgmVolume;
        ROWL_LOG_INFO("Voice Finished -> BGM Restored to Full Volume (Gain: " + std::to_string(m_bgmGain) + ")");
    }
}

void AudioEngine::setDuckingFactor(float factor) {
    m_duckingFactor = std::clamp(factor, 0.0f, 1.0f);
    if (m_isDuckingActive) {
        m_bgmGain = m_bgmVolume * m_duckingFactor;
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

void AudioEngine::shutdown() {
    if (!m_initialized) return;

    ROWL_LOG_INFO("Shutting down Audio Engine Subsystem...");
    m_initialized = false;
}

} // namespace Rowl::Audio
