/**
 * test_d05_blip_queue_class_probe.cpp — D05 blip-kuyruk-sınıf RED kilidi.
 *
 * Bağımsız ikili: rowl_d05_blip_queue_class_probe
 * (ctest -R d05_blip_queue_class_probe, TIMEOUT 120).
 * D02 konvansiyonu: yalnızca public C API + ROWL_API Locked-yardımcıları
 * (paylaşılan RowlEngineCore'a bağlanır; üretim topolojisiyle aynı).
 * rowl_tests gövdesine gömülmez ki kırmızı-yeşil döngüsü tüm süiti
 * koşmadan saniyeler içinde kanıtlansın.
 *
 * Bacak 1 — yanlış-sınıf kilidi (TSan'sız KIRMIZI verir):
 *   audio_blip_error_channel.cpp:33 (recordAssetBlipQueueFailureLocked) ve
 *   :45 (recordSynthBlipQueueFailureLocked) kuyruk-fail'ini Decode
 *   damgalıyor; M4 sözleşmesi (hpp:123-129) kuyruk = Device(1)/IoError-7
 *   diyor. Prob, kuyruk-fail yazım çekirdeklerini doğrudan enjekte eder
 *   (D02'nin bu yardımcıları ROWL_API yapma gerekçesi: "Locked-yardımcılar
 *   Bacak-1 yapısal assertion'larından görünür olmalıdır"; blip-yolunda
 *   deterministik SDL-kuyruk-fail kancası yoktur, hook testFailNextQueue
 *   yalnız playAudio yolunu tüketir — o yol K4-7 ile zaten Device-pinlidir).
 *   Öngörü (pre-fix): gözlenen sınıf Decode(2) → "D05-CLASS RED" + exit 1.
 *   Fix (iki satır Decode→Device; hakemlik M4 lehine) sonrası Device(1) →
 *   exit 0.
 *
 * Bacak 2 — negatif pinler (her iki halde de YEŞİL kalmalı):
 *   recordBlipOggDecodeFailureLocked Decode kalır (K4-10 testi — corrupt
 *   asset → AudioDecodeError 10 — bu yoldan beslendiği için yeşil kalır);
 *   recordSfxPoolStreamOpenFailureLocked Device kalır (değişmez);
 *   recordSynthBlipQueueSuccessLocked kirli snapshot'ı temizler (None) +
 *   sayacı artırır (başarı-yolu değişmez).
 *
 * Bacak 3 — C-API uçtan-uca pin (her iki halde de YEŞİL):
 *   RowlEngine_PlayVoiceBlip("") synth-fallback'ı başarıdır (kod 0, boş
 *   hata) — yeniden-damgalama başarı yolunu kirletmemelidir.
 *
 * KIRMIZI-YEŞİL SÖZLEŞMESİ: TSan'sız exit 1 (RED) / exit 0 (YEŞİL).
 * Tek-thread'lidir; TSan sessiz kalmalıdır. Kırmızıda commit YOK.
 * Prob setter kilidini assert ETMEZ (D02-hakemlik/W3-W4 errata yasağı).
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_blip_error_channel.hpp"

namespace {

using ErrClass = Rowl::Audio::AudioEngine::AudioErrorClass;

void classFail(const std::string& message) {
    rowlLockFail("d05-blip-queue-class-probe", message);
}

void classRequire(bool condition, const std::string& message) {
    if (!condition) classFail(message);
}

const char* className(ErrClass cls) {
    switch (cls) {
        case ErrClass::None: return "None(0)";
        case ErrClass::Device: return "Device(1)";
        case ErrClass::Decode: return "Decode(2)";
        default: return "?";
    }
}

}  // namespace

int main() {
    TEST_SECTION("D05 blip-kuyruk-sinf probu (RED-2: Decode->Device)");

    // ── Bacak 1: kuyruk-fail yazım çekirdekleri Device damgalamalıdır ──
    {
        std::string lastError;
        ErrClass errorClass = ErrClass::None;
        Rowl::Audio::recordAssetBlipQueueFailureLocked(lastError,
                                                       errorClass);
        std::cout << "  asset-kuyruk-fail: sinif=" << className(errorClass)
                  << " msg='" << lastError << "'" << std::endl;
        if (errorClass != ErrClass::Device) {
            std::cout << "D05-CLASS RED: asset blip kuyruk-fail sinifi "
                      << className(errorClass) << " (beklenen Device(1))"
                      << std::endl;
            return 1;
        }
        classRequire(!lastError.empty(),
                     "bacak1: asset kuyruk-fail mesaji bos");
        TEST_PASS("Bacak1 — asset blip kuyruk-fail Device-sinifi");
    }
    {
        std::string lastError;
        ErrClass errorClass = ErrClass::None;
        Rowl::Audio::recordSynthBlipQueueFailureLocked(lastError,
                                                       errorClass);
        std::cout << "  synth-kuyruk-fail: sinif=" << className(errorClass)
                  << " msg='" << lastError << "'" << std::endl;
        if (errorClass != ErrClass::Device) {
            std::cout << "D05-CLASS RED: synth blip kuyruk-fail sinifi "
                      << className(errorClass) << " (beklenen Device(1))"
                      << std::endl;
            return 1;
        }
        classRequire(!lastError.empty(),
                     "bacak1: synth kuyruk-fail mesaji bos");
        TEST_PASS("Bacak1 — synth blip kuyruk-fail Device-sinifi");
    }
    std::cout << "D05-CLASS GREEN: kuyruk-fail sinifi Device(1)" << std::endl;

    // ── Bacak 2: negatif pinler ──
    {
        // OGG-decode çekirdeği Decode KALIR (K4-10 yolu).
        std::string lastError;
        ErrClass errorClass = ErrClass::None;
        Rowl::Audio::recordBlipOggDecodeFailureLocked(lastError, errorClass,
                                                      "prova decode hatasi");
        classRequire(errorClass == ErrClass::Decode,
                     "bacak2: OGG-decode cekirdegi Decode degil (K4-10 riski)");
        classRequire(lastError == "prova decode hatasi",
                     "bacak2: OGG-decode mesaji ezilmedi/yazilmadi");
        TEST_PASS("Bacak2 — OGG-decode cekirdegi Decode-pinli (K4-10 yesil kalir)");
    }
    {
        // SFX pool-open fail'i Device KALIR (değişmez).
        std::string lastError;
        ErrClass errorClass = ErrClass::None;
        Rowl::Audio::recordSfxPoolStreamOpenFailureLocked(lastError,
                                                          errorClass);
        classRequire(errorClass == ErrClass::Device,
                     "bacak2: pool-open fail'i Device degil");
        TEST_PASS("Bacak2 — pool-open fail Device-pinli (degismez)");
    }
    {
        // Başarılı synth kuyruğu kirli snapshot'ı temizler + sayar.
        std::string lastError = "kirli";
        ErrClass errorClass = ErrClass::Decode;
        std::atomic<uint32_t> synthCount{0};
        Rowl::Audio::recordSynthBlipQueueSuccessLocked(lastError, errorClass,
                                                       synthCount);
        classRequire(lastError.empty() && errorClass == ErrClass::None,
                     "bacak2: synth-basari snapshot'i temizlemedi");
        classRequire(synthCount.load() == 1,
                     "bacak2: synth-basari sayaci artmadi");
        TEST_PASS("Bacak2 — synth-basari temizleme+sayac pini");
    }

    // ── Bacak 3: C-API uçtan-uca pin (synth-fallback başarı = kod 0) ──
    {
        RowlEngineHandle handle = RowlEngine_Create();
        if (!handle || RowlEngine_Init(handle, 320, 180, 0) != 1) {
            classFail("bacak3: C-API init failed");
        }
        RowlEngine_PlayVoiceBlip(handle, "", 1.0f, 0.9f, 1);
        classRequire(RowlEngine_GetLastResultCode(handle) == 0,
                     "bacak3: synth blip basarisi kod 0 degil, got: " +
                         std::to_string(RowlEngine_GetLastResultCode(handle)));
        classRequire(std::string(RowlEngine_GetLastAudioError(handle)).empty(),
                     "bacak3: synth blip basarisinda hata mesaji var");
        RowlEngine_Destroy(handle);
        TEST_PASS("Bacak3 — synth-fallback basari yolu kod-0 pini");
    }

    TEST_PASS("d05 blip-kuyruk-sinif kilidi yesil");
    return 0;
}
