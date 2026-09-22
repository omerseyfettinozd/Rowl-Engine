// audio_blip_error_channel.cpp — D02: blip hata-kanalı yazım çekirdekleri.
//
// playVoiceBlip / ensureSfxPoolStreamsLocked içinden davranışsız taşınmıştır:
// koşul, sıra, sınıf sabiti (Decode/Device/None), log satırı ve sayaç artışı
// birebirdir. Bu TU kilit ALMAZ; çağıran m_stateMutex'i tutar (Locked-sözleşme).
// Yeni export YOK, `RowlEngine_` sembolü YOK.

#include "rowl/audio/audio_blip_error_channel.hpp"

#include "rowl/core/logger.hpp"

#include <SDL3/SDL.h>

namespace Rowl::Audio {

void recordBlipOggDecodeFailureLocked(std::string& lastError,
                                      AudioEngine::AudioErrorClass& errorClass,
                                      const std::string& blipOggError) {
    // M4: kilitli-yazım (kilit üstte tutulur; bu yordam kilit
    // almaz; blip decode fail'i daima Decode-sınıfıdır;
    // stream-yokluğu yazım yapmaz).
    lastError = blipOggError;
    errorClass = AudioEngine::AudioErrorClass::Decode;
}

void recordAssetBlipQueueFailureLocked(std::string& lastError,
                                       AudioEngine::AudioErrorClass& errorClass) {
    // M4: kilitli-yazım (kilit üstte tutulur; blip kuyruk
    // fail'i Device-sınıfıdır — kuyruk = Device(1)/IoError-7,
    // hpp:123-129; D05 RED-2 hakemliği M4 lehine).
    if (lastError.empty()) {
        lastError = "Voice blip asset could not be queued: " +
            std::string(SDL_GetError());
        errorClass = AudioEngine::AudioErrorClass::Device;
    }
    ROWL_LOG_WARN("[AudioEngine] " + lastError);
}

void recordSynthBlipQueueFailureLocked(std::string& lastError,
                                       AudioEngine::AudioErrorClass& errorClass) {
    // M4: kilitli-yazım/temizleme (kilit üstte tutulur; synth kuyruk
    // fail'i Device — D05 RED-2).
    if (lastError.empty()) {
        lastError = "Synth voice blip could not be queued: " +
            std::string(SDL_GetError());
        errorClass = AudioEngine::AudioErrorClass::Device;
    }
    ROWL_LOG_WARN("[AudioEngine] " + lastError);
}

void recordSynthBlipQueueSuccessLocked(std::string& lastError,
                                       AudioEngine::AudioErrorClass& errorClass,
                                       std::atomic<uint32_t>& synthBlipCount) {
    lastError.clear();
    errorClass = AudioEngine::AudioErrorClass::None;
    // A5-tur3: synth-fallback ayırt edilebilirliği — başarılı synth
    // kuyruğu ayrıca sayılır (synth <= voice).
    ++synthBlipCount;
}

void recordSfxPoolStreamOpenFailureLocked(std::string& lastError,
                                          AudioEngine::AudioErrorClass& errorClass) {
    // Kilitli yazım: bu yordam kilit almaz; çağıran kilidi tutar
    // (özyinelemeli m_stateMutex kilitlenmesi olmaz).
    if (lastError.empty()) {
        lastError = "SFX pool audio stream could not be opened: " +
            std::string(SDL_GetError());
        errorClass = AudioEngine::AudioErrorClass::Device;
    }
    ROWL_LOG_WARN("[AudioEngine] " + lastError);
}

}  // namespace Rowl::Audio
