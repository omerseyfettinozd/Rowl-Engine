#pragma once

// audio_blip_error_channel.hpp — D02: playVoiceBlip / ensureSfxPoolStreamsLocked
// içindeki m_lastError yazım çekirdeklerinin davranışsız taşınmış karşılıkları.
//
// Locked-sözleşme: bu yordamların HİÇBİRİ kilit ALMAZ; çağıran m_stateMutex'i
// tutar (playVoiceBlip gövdesi / ensureSfxPoolStreams() sarmalayıcı kilidi).
// Gövde ham m_lastError / m_lastErrorClass erişimi kullanır — kilit alan
// setLastError / setLastErrorIfEmpty / clearLastError helper'ları buradan
// ÇAĞRILAMAZ (non-recursive m_stateMutex özyinelemeli kilitlenir).
//
// Yeni export YOK, `RowlEngine_` öneki YOK (motor-içi semboller).

#include <atomic>
#include <cstdint>
#include <string>

#include "rowl/audio/audio_engine.hpp"
#include "rowl/c_api.h"  // W8-c: Locked-yardimcilar exportlu DEGILDIR
// (-fvisibility=hidden; baslikta ROWL_API yok). Kullanicilar: motor ici +
// rowl_engine_objects'a baglanan problar (Bacak 1); paylasilan
// RowlEngineCore'a baglanan TU'lar erisemez. RowlEngine_ C ABI'sine sembol
// EKLEMEZ (ABI kapisi yalniz bu oneki sayar).

namespace Rowl::Audio {

// 2224-2225 çekirdeği: OGG-decode-fail KOŞULSUZ yazar (önceki hatayı ezer).
// Çevreleyen `else if (!blipOggError.empty())` koşulu çağrıda kalır.
// Kilit almaz; çağıran tutar.
void recordBlipOggDecodeFailureLocked(std::string& lastError,
                                               AudioEngine::AudioErrorClass& errorClass,
                                               const std::string& blipOggError);

// 2264-2269: asset-blip SDL kuyruk-fail'i ilk-hatayı KORUR (empty-guard) +
// her durumda WARN. Kilit almaz; çağıran tutar.
void recordAssetBlipQueueFailureLocked(std::string& lastError,
                                                AudioEngine::AudioErrorClass& errorClass);

// 2330-2335: synth-blip kuyruk-fail'i aynı koruma sözleşmesi + WARN.
// Kilit almaz; çağıran tutar.
void recordSynthBlipQueueFailureLocked(std::string& lastError,
                                                AudioEngine::AudioErrorClass& errorClass);

// 2337-2341 else-kolu BÜTÜNDÜR (clear + None-reset + sayaç ayrılamaz):
// başarılı synth kuyruğu kirli snapshot'ı temizler ve sayacı artırır.
// Kilit almaz; çağıran tutar.
void recordSynthBlipQueueSuccessLocked(std::string& lastError,
                                                AudioEngine::AudioErrorClass& errorClass,
                                                std::atomic<uint32_t>& synthBlipCount);

// 2704-2709: SFX pool-open fail'i Device-sınıfıdır (Decode ile karışmaz) +
// WARN. Kilit almaz; çağıran tutar.
void recordSfxPoolStreamOpenFailureLocked(std::string& lastError,
                                                   AudioEngine::AudioErrorClass& errorClass);

}  // namespace Rowl::Audio
