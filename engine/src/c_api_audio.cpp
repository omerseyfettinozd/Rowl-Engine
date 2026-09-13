/**
 * c_api_audio.cpp
 *
 * C-API audio surface: playback control, volumes, telemetry, voice blips.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "rowl/audio/audio_engine.hpp"
#include "cstring"
extern "C" {
/* ── Audio control & voice blips ──────────────────────────────────────────────── */

void RowlEngine_PlayAudio(RowlEngineHandle handle,
                          const char* assetPath,
                          int channelType,
                          int filterType) {
    if (!isLiveHandle(handle)) return;
    if (!assetPath || !*assetPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Audio asset path is null or empty",
                                  "play_audio", "");
                }
            }
        });
        return;
    }
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        if (!engine) return;
        auto* audio = engine->getAudio();
        if (!audio) return;
        auto channel = (channelType == 0) ? Rowl::Audio::AudioChannelType::Bgm :
                       (channelType == 1) ? Rowl::Audio::AudioChannelType::Voice :
                                            Rowl::Audio::AudioChannelType::Sfx;
        auto filter  = (filterType == 1)  ? Rowl::Audio::DSPFilterType::CaveReverb :
                       (filterType == 2)  ? Rowl::Audio::DSPFilterType::Telephone :
                       (filterType == 3)  ? Rowl::Audio::DSPFilterType::UnderwaterLowPass :
                                            Rowl::Audio::DSPFilterType::Normal;
        audio->playAudio(assetPath, channel, filter);
        if (!audio->getLastError().empty()) {
            if (auto ctx = engine->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::AudioDecodeError,
                              audio->getLastError(), "play_audio", assetPath);
            }
        }
    });
}

void RowlEngine_StopBgm(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (audio) audio->stopBgm();
    });
}

void RowlEngine_SetBgmVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (audio) audio->setBgmVolume(volume);
    });
}

void RowlEngine_SetMasterVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto* audio = toEngine(handle)->getAudio()) audio->setMasterVolume(volume); });
}

void RowlEngine_SetVoiceVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto* audio = toEngine(handle)->getAudio()) audio->setVoiceVolume(volume); });
}

void RowlEngine_SetSfxVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto* audio = toEngine(handle)->getAudio()) audio->setSfxVolume(volume); });
}

void RowlEngine_SetTextSpeedMultiplier(RowlEngineHandle handle, float multiplier) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setTextSpeedMultiplier(multiplier); });
}

void RowlEngine_SetAutoAdvanceDelayOffset(RowlEngineHandle handle, float seconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setAutoAdvanceDelayOffset(seconds); });
}

float RowlEngine_GetMasterVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { const auto* audio = toEngine(handle)->getAudio(); return audio ? audio->getMasterVolume() : 0.0f; }, 0.0f);
}

float RowlEngine_GetVoiceVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { const auto* audio = toEngine(handle)->getAudio(); return audio ? audio->getVoiceVolume() : 0.0f; }, 0.0f);
}

float RowlEngine_GetSfxVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { const auto* audio = toEngine(handle)->getAudio(); return audio ? audio->getSfxVolume() : 0.0f; }, 0.0f);
}

void RowlEngine_TriggerVoiceDucking(RowlEngineHandle handle, int isVoiceActive) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (audio) audio->triggerVoiceDucking(isVoiceActive != 0);
    });
}

int RowlEngine_IsBgmPlaying(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isBgmPlaying() ? 1 : 0;
    }, 0);
}

int RowlEngine_IsVoicePlaying(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isVoicePlaying() ? 1 : 0;
    }, 0);
}

int RowlEngine_GetActiveDspFilter(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        if (!audio) return 0;
        switch (audio->getActiveFilter()) {
            case Rowl::Audio::DSPFilterType::CaveReverb: return 1;
            case Rowl::Audio::DSPFilterType::Telephone: return 2;
            case Rowl::Audio::DSPFilterType::UnderwaterLowPass: return 3;
            case Rowl::Audio::DSPFilterType::Normal: return 0;
        }
        return 0;
    }, 0);
}

const char* RowlEngine_GetLastAudioError(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        buffer = audio ? audio->getLastError() : "";
        return buffer.c_str();
    }, "");
}

const char* RowlEngine_GetLastAudioErrorWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetLastAudioError(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

int RowlEngine_IsAudioDeviceAvailable(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return (audio && audio->isAudioDeviceAvailable()) ? 1 : 0;
    }, 0);
}

int RowlEngine_IsAudioOutputSuspended(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return (audio && audio->isOutputSuspended()) ? 1 : 0;
    }, 0);
}

float RowlEngine_GetAudioChannelPeak(RowlEngineHandle handle, int channelType, int channelIndex) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->getChannelPeak(channelType, channelIndex) : 0.0f;
    }, 0.0f);
}

float RowlEngine_GetAudioChannelRms(RowlEngineHandle handle, int channelType, int channelIndex) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->getChannelRms(channelType, channelIndex) : 0.0f;
    }, 0.0f);
}

void RowlEngine_GetAudioSpectrum(RowlEngineHandle handle, float* outBands, int bandCount) {
    if (!isLiveHandle(handle) || !outBands || bandCount <= 0) return;
    invokeNoexcept([&] {
        const auto* audio = toEngine(handle)->getAudio();
        if (audio) {
            audio->getSpectrumBands(outBands, bandCount);
        } else {
            for (int i = 0; i < bandCount; ++i) outBands[i] = 0.0f;
        }
    });
}

void RowlEngine_PlayVoiceBlip(RowlEngineHandle handle, const char* soundPath, float pitch, float volume, int channelType) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->playVoiceBlip(soundPath ? soundPath : "", pitch, volume, channelType);
    });
}

void RowlEngine_SetDialogueVoiceBlip(RowlEngineHandle handle, const char* soundPath, float basePitch, float pitchVariance, int cadence, int skipPunctuation, int channelType) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->setDialogueVoiceBlip(soundPath ? soundPath : "", basePitch, pitchVariance, cadence, skipPunctuation != 0, channelType);
    });
}

const char* RowlEngine_GetDialogueVoiceBlipSound(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        buffer = toEngine(handle)->getDialogueVoiceBlipSound();
        return buffer.c_str();
    }, "");
}

const char* RowlEngine_GetDialogueVoiceBlipSoundWithLength(RowlEngineHandle handle, uint32_t* outLen) {
    const char* value = RowlEngine_GetDialogueVoiceBlipSound(handle);
    if (outLen) *outLen = static_cast<uint32_t>(std::strlen(value));
    return value;
}

float RowlEngine_GetDialogueVoiceBlipPitch(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1.0f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getDialogueVoiceBlipPitch();
    }, 1.0f);
}

float RowlEngine_GetDialogueVoiceBlipVariance(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.08f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getDialogueVoiceBlipVariance();
    }, 0.08f);
}

int RowlEngine_GetDialogueVoiceBlipCadence(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->getDialogueVoiceBlipCadence();
    }, 1);
}

int RowlEngine_GetDialogueVoiceBlipSkipPunctuation(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->getDialogueVoiceBlipSkipPunctuation() ? 1 : 0;
    }, 1);
}

int RowlEngine_GetDialogueVoiceBlipChannel(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 1;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->getDialogueVoiceBlipChannel();
    }, 1);
}

float RowlEngine_GetDialogueVoiceBlipVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.85f;
    return invokeNoexcept<float>([&] {
        return toEngine(handle)->getDialogueVoiceBlipVolume();
    }, 0.85f);
}

void RowlEngine_SetDialogueVoiceBlipVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->setDialogueVoiceBlipVolume(volume);
    });
}

uint32_t RowlEngine_GetVoiceBlipCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint32_t>([&] {
        return toEngine(handle)->getVoiceBlipCount();
    }, 0);
}

void RowlEngine_ResetVoiceBlipCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        toEngine(handle)->resetVoiceBlipCount();
    });
}

} // extern "C"
