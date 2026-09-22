/**
 * c_api_audio_error_stamp.cpp — D05 M4 damga-bağlama TU'su.
 *
 * Hata + sınıf TEK snapshot'tan bağlanır (bayat-sınıf yok): RowlEngine_
 * çağrı yolundaki iki-ayrı-okuma (getLastError + getLastErrorClass) arasına
 * başka bir thread'in yazımı girerse kod ile mesaj farklı anlara ait olurdu.
 * lastErrorStamped() tek kilit altında ikisini birlikte alır; bu TU da kodu
 * aynı damgadan türetir (struct sıfırdan değil — M4 damga-bağlama).
 *
 * Kapsam: yalnız RowlEngine_PlayVoiceBlip bağlaması bu TU'yu kullanır (dar
 * kesit; c_api_audio.cpp:97-153 setter/guard yüzeyi DEĞİŞMEZ).
 * Yeni RowlEngine_ export YOK (additive ABI; iç sembol Rowl::D05).
 * Davranış paritesi: mesaj boşsa başarı, doluysa Device→IoError(7) /
 * diğerleri→AudioDecodeError(10) — audioErrorCodeFor ile birebir aynı eşleme.
 */

#include "c_api_internal.hpp"
#include "rowl/audio/audio_engine.hpp"

namespace Rowl::D05 {

Rowl::Core::RuntimeErrorCode audioErrorCodeForStamped(
    const Rowl::Audio::AudioEngine* audio, std::string& messageOut) {
    if (!audio) {
        messageOut.clear();
        return Rowl::Core::RuntimeErrorCode::AudioDecodeError;
    }
    const Rowl::Audio::AudioEngine::AudioErrorStamp stamp =
        audio->lastErrorStamped();
    messageOut = stamp.message;
    if (stamp.errorClass ==
        Rowl::Audio::AudioEngine::AudioErrorClass::Device) {
        return Rowl::Core::RuntimeErrorCode::IoError;
    }
    return Rowl::Core::RuntimeErrorCode::AudioDecodeError;
}

}  // namespace Rowl::D05
