/**
 * test_audio_volume_race_red_probe.cpp — D03 hacim-yarışı RED kilit probu.
 *
 * Bağımsız ikili: rowl_audio_volume_race_red_probe
 * (ctest -R audio_volume_race_red_probe, TIMEOUT 120).
 * D02 konvansiyonu: public C API + header-only StreamMixer + dummy driver
 * (SDL_AUDIODRIVER=dummy, SDL_VIDEODRIVER=dummy) + yerleşik test-köprüsü
 * (Rowl::Core::testEngineFromHandle — harness'ta deklare). Linkaj rowl_tests
 * gibidir (rowl_engine_objects: C++ ve C-API çağrıları tek kopyayı paylaşır),
 * çünkü AudioEngine üye-sembolleri paylaşılan RowlEngineCore'dan ihraç
 * edilmez. rowl_tests gövdesine gömülmez ki kırmızı-yeşil döngüsü tüm süiti
 * koşmadan saniyeler içinde kanıtlansın.
 *
 * Bacak 1 — StreamMixer hammer (saf header, motor Init'siz):
 *   thread-A: setUserVolume(Bgm/Master/Ui) + setBgmDuckGain +
 *     setAmbienceBedVolume döngüsü (yazım: stream_mixer.hpp:39-44);
 *   thread-B: userVolume/gainFor/gainForAmbienceBed/applyChain okuma
 *     döngüsü (okuma: stream_mixer.hpp:50-56, :68-71).
 *   TSan KIRMIZI: 'WARNING: ThreadSanitizer: data race' + bu satırlar.
 *
 * Bacak 2 — AudioEngine üye hammer'ı (dummy driver):
 *   RowlEngine_Create+Init sonrası test-köprüsü (testEngineFromHandle) ile
 *   çözülen AudioEngine üzerinde thread-A: setMasterVolume/setBgmVolume/
 *   setUiVolume/setAmbienceVolume; thread-B: getMasterVolume/getBgmVolume/
 *   getUiVolume/getAmbienceVolume. Doğrudan üye çağrısı ŞARTTIR: public C API
 *   handle thread-sahipliği (toEngineChecked ownerThread) yabancı-thread
 *   çağrısını touchesiz reddeder (sahte-0.0), yani ham C-API hammer'ı üyeye
 *   hiç dokunmaz ve yarışı maskeler. Üye-yolu gerçek üretim paylaşımıdır
 *   (setter ana-thread, okur ses-pump yolu).
 *   TSan KIRMIZI: yazım audio_engine.cpp:1227 (m_masterVolume=) /
 *     :1218 (m_bgmVolume=) vs okuma audio_engine.hpp:90 (getMasterVolume) /
 *     :89 (getBgmVolume).
 *
 * Bacak 3 — fonksiyonel parite çipası (TSan'sız YEŞİL, fix-sonrası
 * TSan-YEŞİL'de birebir aynı kalmalı):
 *   clamp (2.0 -> 1.0, -1.0 -> 0.0), non-finite ignore (NaN -> son değer
 *   korunur), duck semantiği (TriggerVoiceDucking GetBgmVolume'u değiştirmez;
 *   StreamMixer setBgmDuckGain gainFor(Bgm) == master*bgm*duck).
 *
 * KIRMIZI-YEŞİL SÖZLEŞMESİ: TSan'lı ctest exit 66 + race warning;
 * TSan'sız exit 0. Fix (in-place atomic<float> relaxed, yeni dosya yok)
 * sonrası TSan exit 0, Bacak-3 değişmez. Kırmızıda commit YOK, max 3 fix.
 */
#include "rowl_test_harness.hpp"

#include <cmath>
#include <limits>
#include <thread>
#include <vector>

namespace {

void volFail(const std::string& message) {
    rowlLockFail("d03-audio-volume-race-probe", message);
}

// TSan'ın göreceği gerçek yük/depolama trafiği: değerler biriktirilip
// yazdırılır (ölü-kod eleme yok).
constexpr int kHammerIters = 200000;

void hammerStreamMixer() {
    Rowl::Audio::StreamMixer mixer;
    float sinkA = 0.0f;
    float sinkB = 0.0f;

    std::thread writer([&]() {
        for (int i = 0; i < kHammerIters; ++i) {
            const float v = static_cast<float>((i % 100)) / 100.0f;
            mixer.setUserVolume(Rowl::Audio::StreamBusId::Bgm, v);
            mixer.setUserVolume(Rowl::Audio::StreamBusId::Master, 1.0f - v * 0.5f);
            mixer.setUserVolume(Rowl::Audio::StreamBusId::Ui, v * 0.7f);
            mixer.setBgmDuckGain(0.5f + v * 0.5f);
            mixer.setAmbienceBedVolume(1, v);
            sinkA += v;
        }
    });
    std::thread reader([&]() {
        std::vector<float> pcm(128, 0.5f);
        for (int i = 0; i < kHammerIters; ++i) {
            sinkB += mixer.userVolume(Rowl::Audio::StreamBusId::Bgm);
            sinkB += mixer.userVolume(Rowl::Audio::StreamBusId::Master);
            sinkB += mixer.gainFor(Rowl::Audio::StreamBusId::Bgm);
            sinkB += mixer.gainFor(Rowl::Audio::StreamBusId::Sfx);
            sinkB += mixer.gainForAmbienceBed(1);
            mixer.applyChain(pcm.data(), 64, Rowl::Audio::StreamBusId::Bgm);
        }
    });
    writer.join();
    reader.join();
    std::cout << "  bacak1 sink=" << (sinkA + sinkB) << std::endl;
}

void hammerAudioMembers(Rowl::Audio::AudioEngine* audio) {
    double sinkA = 0.0;
    double sinkB = 0.0;

    std::thread writer([&]() {
        for (int i = 0; i < kHammerIters; ++i) {
            const float v = static_cast<float>((i % 100)) / 100.0f;
            audio->setMasterVolume(v);
            audio->setBgmVolume(1.0f - v * 0.5f);
            audio->setUiVolume(v * 0.7f);
            audio->setAmbienceVolume(v * 0.3f);
            sinkA += v;
        }
    });
    std::thread reader([&]() {
        for (int i = 0; i < kHammerIters; ++i) {
            sinkB += audio->getMasterVolume();
            sinkB += audio->getBgmVolume();
            sinkB += audio->getUiVolume();
            sinkB += audio->getAmbienceVolume();
        }
    });
    writer.join();
    reader.join();
    std::cout << "  bacak2 sink=" << (sinkA + sinkB) << std::endl;
}

}  // namespace

int main() {
    TEST_SECTION("Audio volume race probe (D03: StreamMixer + C-API hacim kilidi)");

    // ── Bacak 1: StreamMixer header hammer (TSan altında KIRMIZI verir).
    hammerStreamMixer();
    TEST_PASS("Bacak1 — StreamMixer hammer tamamlandi");

    // ── Bacak 2: AudioEngine üye hammer'ı (dummy driver; TSan altında KIRMIZI).
    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) volFail("RowlEngine_Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        volFail("RowlEngine_Init(320x180) must succeed under dummy drivers");
    Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(h);
    if (engine == nullptr) volFail("testEngineFromHandle returned null");
    Rowl::Audio::AudioEngine* audio = engine->getAudio();
    if (audio == nullptr) volFail("Engine::getAudio returned null");
    hammerAudioMembers(audio);

    // ── Bacak 3: fonksiyonel parite çipası (her konfigürasyonda YEŞİL).
    // Clamp: [0,1] dışına taşan yazım sınıra yapışır.
    RowlEngine_SetMasterVolume(h, 2.0f);
    if (RowlEngine_GetMasterVolume(h) != 1.0f)
        volFail("Bacak3 clamp: SetMasterVolume(2.0) must read back 1.0");
    RowlEngine_SetBgmVolume(h, -1.0f);
    if (RowlEngine_GetBgmVolume(h) != 0.0f)
        volFail("Bacak3 clamp: SetBgmVolume(-1.0) must read back 0.0");
    RowlEngine_SetUiVolume(h, 2.0f);
    if (RowlEngine_GetUiVolume(h) != 1.0f)
        volFail("Bacak3 clamp: SetUiVolume(2.0) must read back 1.0");
    RowlEngine_SetAmbienceVolume(h, -5.0f);
    if (RowlEngine_GetAmbienceVolume(h) != 0.0f)
        volFail("Bacak3 clamp: SetAmbienceVolume(-5.0) must read back 0.0");

    // Non-finite ignore: NaN yazımı son geçerli değeri korur.
    const float quiet = std::numeric_limits<float>::quiet_NaN();
    RowlEngine_SetMasterVolume(h, 0.4f);
    RowlEngine_SetMasterVolume(h, quiet);
    if (RowlEngine_GetMasterVolume(h) != 0.4f)
        volFail("Bacak3 non-finite: NaN must preserve last Master value 0.4");
    RowlEngine_SetBgmVolume(h, 0.6f);
    RowlEngine_SetBgmVolume(h, quiet);
    if (RowlEngine_GetBgmVolume(h) != 0.6f)
        volFail("Bacak3 non-finite: NaN must preserve last Bgm value 0.6");

    // Duck semantiği (C API): TriggerVoiceDucking kullanıcı hacim üyesini
    // değiştirmez (m_bgmVolume sabit kalır; duck yalnızca m_bgmGain/mixer
    // yoluna uygulanır). Gözlem: GetBgmVolume duck açık/kapalı aynı döner.
    RowlEngine_SetBgmVolume(h, 0.8f);
    const float fullGain = RowlEngine_GetBgmVolume(h);
    RowlEngine_TriggerVoiceDucking(h, 1);
    const float duckedGain = RowlEngine_GetBgmVolume(h);
    RowlEngine_TriggerVoiceDucking(h, 0);
    const float restoredGain = RowlEngine_GetBgmVolume(h);
    std::cout << "  duck: full=" << fullGain << " ducked=" << duckedGain
              << " restored=" << restoredGain << std::endl;
    if (duckedGain != fullGain)
        volFail("Bacak3 duck: ducking must not alter GetBgmVolume");
    if (restoredGain != fullGain)
        volFail("Bacak3 duck: unducking must not alter GetBgmVolume");

    // Duck semantiği (StreamMixer): gainFor(Bgm) == master*bgm*duck.
    {
        Rowl::Audio::StreamMixer m;
        m.setUserVolume(Rowl::Audio::StreamBusId::Master, 0.8f);
        m.setUserVolume(Rowl::Audio::StreamBusId::Bgm, 0.5f);
        m.setBgmDuckGain(0.5f);
        const float got = m.gainFor(Rowl::Audio::StreamBusId::Bgm);
        const float want = 0.8f * 0.5f * 0.5f;
        if (std::fabs(got - want) > 1e-6f)
            volFail("Bacak3 mixer-duck: gainFor(Bgm) must equal master*bgm*duck");
        m.setBgmDuckGain(1.0f);
        const float unducked = m.gainFor(Rowl::Audio::StreamBusId::Bgm);
        if (std::fabs(unducked - 0.8f * 0.5f) > 1e-6f)
            volFail("Bacak3 mixer-duck: duck=1.0 must yield master*bgm");
    }

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("d03 audio volume race + parite kilidi yesil");
    return 0;
}
