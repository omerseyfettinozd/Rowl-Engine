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
#include <cstdio>
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
        // Faz 5 Dilim 1: ham kanal int'i korunur (0=Bgm,1=Voice,2=Sfx,
        // 3=Ambience,4=Ui; diğerleri Sfx'e düşer); snapshot kanal sadakati
        // için playAudioInt kullanılır.
        auto filter  = (filterType == 1)  ? Rowl::Audio::DSPFilterType::CaveReverb :
                       (filterType == 2)  ? Rowl::Audio::DSPFilterType::Telephone :
                       (filterType == 3)  ? Rowl::Audio::DSPFilterType::UnderwaterLowPass :
                                            Rowl::Audio::DSPFilterType::Normal;
        audio->playAudioInt(assetPath, channelType, filter);
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

// ── Faz 5 Dilim 1: streaming observability + volume matrix ──────────────

int RowlEngine_IsStreaming(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isStreaming() ? 1 : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_GetStreamInfoJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto* audio = engine->getAudio();
        if (!audio) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(audio->streamInfoJson(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

float RowlEngine_GetBgmVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->getBgmVolume() : 0.0f;
    }, 0.0f);
}

void RowlEngine_SetAmbienceVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto* audio = toEngine(handle)->getAudio()) audio->setAmbienceVolume(volume);
    });
}

float RowlEngine_GetAmbienceVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->getAmbienceVolume() : 0.0f;
    }, 0.0f);
}

void RowlEngine_SetUiVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto* audio = toEngine(handle)->getAudio()) audio->setUiVolume(volume);
    });
}

float RowlEngine_GetUiVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->getUiVolume() : 0.0f;
    }, 0.0f);
}

// ── Faz 5 Dilim 2: mixer / polyphony / eğriler / bed'ler / pump ─────────────

void RowlEngine_SetFadeCurve(RowlEngineHandle handle, int curve) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (!audio) return;
        // Geçersiz eğri yoksayılır (son geçerli değer korunur).
        if (curve == static_cast<int>(Rowl::Audio::FadeCurve::Linear)) {
            audio->setFadeCurve(Rowl::Audio::FadeCurve::Linear);
        } else if (curve == static_cast<int>(Rowl::Audio::FadeCurve::EqualPower)) {
            audio->setFadeCurve(Rowl::Audio::FadeCurve::EqualPower);
        }
    });
}

int RowlEngine_GetFadeCurve(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? static_cast<int>(audio->fadeCurve()) : 0;
    }, 0);
}

void RowlEngine_SetSfxPoolDepth(RowlEngineHandle handle, int depth) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto* audio = toEngine(handle)->getAudio()) audio->setSfxPoolDepth(depth);
    });
}

int RowlEngine_GetSfxPoolDepth(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? static_cast<int>(audio->sfxPoolDepth()) : 0;
    }, 0);
}

int RowlEngine_GetSfxActiveVoices(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? static_cast<int>(audio->sfxActiveVoices()) : 0;
    }, 0);
}

// MSVC, extern "C" blogu icindeki C++ donuslu helper'a izin vermez;
// helper C++ linkage ile acikca isaretlenir (GCC/Clang'de zaten oyleydi).
extern "C++" {
namespace {

std::string escapeJsonString(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char hex[7];
                    std::snprintf(hex, sizeof(hex), "\\u%04x", ch);
                    out += hex;
                } else {
                    out += ch;
                }
                break;
        }
    }
    return out;
}

} // namespace
} // extern "C++"

RowlEngine_ResultCode RowlEngine_GetSfxActivePaths(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto* audio = engine->getAudio();
        if (!audio) return ROWL_RESULT_INVALID_HANDLE;
        std::string json = "[";
        bool first = true;
        for (const auto& path : audio->sfxActivePaths()) {
            if (!first) json += ",";
            first = false;
            json += "\"" + escapeJsonString(path) + "\"";
        }
        json += "]";
        return copyUtf8ToCaller(json, buffer, bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

int RowlEngine_PlayAmbienceBed(RowlEngineHandle handle, const char* assetPath, int bed) {
    if (!isLiveHandle(handle)) return 0;
    if (!assetPath || !*assetPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Ambience asset path is null or empty",
                                  "play_ambience_bed", "");
                }
            }
        });
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return 0;
        auto* audio = engine->getAudio();
        if (!audio) return 0;
        const bool ok = audio->playAmbienceBed(bed, assetPath);
        if (!ok && !audio->getLastError().empty()) {
            if (auto ctx = engine->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::AudioDecodeError,
                              audio->getLastError(), "play_ambience_bed", assetPath);
            }
        }
        return ok ? 1 : 0;
    }, 0);
}

void RowlEngine_StopAmbienceBed(RowlEngineHandle handle, int bed) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto* audio = toEngine(handle)->getAudio()) audio->stopAmbienceBed(bed);
    });
}

void RowlEngine_SetAmbienceBedVolume(RowlEngineHandle handle, int bed, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        if (auto* audio = toEngine(handle)->getAudio()) audio->setAmbienceBedVolume(bed, volume);
    });
}

float RowlEngine_GetAmbienceBedVolume(RowlEngineHandle handle, int bed) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->ambienceBedVolume(bed) : 0.0f;
    }, 0.0f);
}

int RowlEngine_IsAmbienceBedPlaying(RowlEngineHandle handle, int bed) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isAmbienceBedPlaying(bed) ? 1 : 0;
    }, 0);
}

int RowlEngine_CrossfadeAmbienceTo(RowlEngineHandle handle, const char* assetPath,
                                  float durationSeconds, int curve) {
    if (!isLiveHandle(handle)) return 0;
    if (!assetPath || !*assetPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Ambience asset path is null or empty",
                                  "crossfade_ambience_to", "");
                }
            }
        });
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return 0;
        auto* audio = engine->getAudio();
        if (!audio) return 0;
        Rowl::Audio::FadeCurve fade = audio->fadeCurve();
        if (curve == static_cast<int>(Rowl::Audio::FadeCurve::Linear)) {
            fade = Rowl::Audio::FadeCurve::Linear;
        } else if (curve == static_cast<int>(Rowl::Audio::FadeCurve::EqualPower)) {
            fade = Rowl::Audio::FadeCurve::EqualPower;
        }
        const bool ok = audio->crossfadeAmbienceTo(assetPath, durationSeconds, fade);
        if (!ok && !audio->getLastError().empty()) {
            if (auto ctx = engine->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::AudioDecodeError,
                              audio->getLastError(), "crossfade_ambience_to", assetPath);
            }
        }
        return ok ? 1 : 0;
    }, 0);
}

int RowlEngine_IsAmbienceCrossfadeActive(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isAmbienceCrossfadeActive() ? 1 : 0;
    }, 0);
}

RowlEngine_ResultCode RowlEngine_GetBgmPumpStatsJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) {
    if (!isLiveHandle(handle)) return ROWL_RESULT_INVALID_HANDLE;
    return invokeNoexcept<RowlEngine_ResultCode>([&] {
        auto* engine = toEngine(handle);
        if (!engine) return ROWL_RESULT_INVALID_HANDLE;
        const auto* audio = engine->getAudio();
        if (!audio) return ROWL_RESULT_INVALID_HANDLE;
        return copyUtf8ToCaller(audio->bgmPumpStatsJson(), buffer,
                                bufferSize, outRequiredSize);
    }, ROWL_RESULT_UNKNOWN_ERROR);
}

uint64_t RowlEngine_GetBgmPumpAvgMicroseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio ? audio->bgmPumpAvgMicroseconds() : 0;
    }, 0);
}

} // extern "C"
