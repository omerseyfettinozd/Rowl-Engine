/**
 * test_d05_telemetry_latency_probe.cpp — D05 telemetri-gecikme RED kilidi.
 *
 * HAKEMLİK REVİZYONU (ana-döngü kararı, bağlayıcı): RED-1 TSan-kırmızı
 * çerçevesi İPTALdir. Pre-fix okuyucular m_stateMutex tutar, TSan hem önce
 * hem sonra yeşil kalır — o çerçeve RED-kilidi olamaz. RED-1 bunun yerine
 * GECİKME/contention kilididir: bu prob blip-hammer altında okuyucu
 * p99/max gecikmesini ölçer; pre-fix eşik-üstü = KIRMIZI (exit 1), post-fix
 * (telemetri atomic/snapshot, D03 emsali, okuyucu kilitsiz) eşik-altı =
 * YEŞİL (exit 0). TSan yalnızca güvenlik ağıdır (yeni yarış YOK).
 *
 * Bağımsız ikili: rowl_d05_telemetry_latency_probe
 * (ctest -R d05_telemetry_latency_probe, TIMEOUT 120).
 * D03/D04 konvansiyonu: header + test-köprüsü YOK, doğrudan AudioEngine
 * (VFSManager, test_audio_lock.cpp deseni) + dummy driver
 * (SDL_AUDIODRIVER=dummy, SDL_VIDEODRIVER=dummy). Linkaj D03/D04 gibidir
 * (rowl_engine_objects: tek kopya), çünkü AudioEngine üye-sembolleri
 * paylaşılan RowlEngineCore'dan ihraç edilmez. rowl_tests gövdesine
 * gömülmez ki kırmızı-yeşil döngüsü tüm süiti koşmadan kanıtlansın.
 *
 * Bacak 1 — gecikme kilidi (TSan'sız KIRMIZI verir):
 *   thread-H: playVoiceBlip("", pitch, vol) döngüsü (synth kolu; blip
 *     gövdesi audio_engine.cpp:2232'de başlar, :2238'de m_stateMutex'i
 *     alır ve SDL-kuyruk adımları boyunca (:2352/:2356 asset kolu,
 *     :2413-2420 synth kolu) tutar; synth-üretim :2383-2406).
 *   thread-R (ana thread): getChannelPeak + getChannelRms +
 *     getSpectrumBands üçlüsünün çağrı-başına mikrosaniyesi (steady_clock,
 *     N=2000 örnek; p99 + max raporlanır).
 *   Öngörü (pre-fix): okuyucular m_stateMutex lock_guard'lı
 *     (pre-fix aralığı; güncel kilitsiz okuyucular :2191-2230) olduğundan
 *     hammer'ın kritik-bölümü arkasında bloklanır;
 *     p99 eşik-üstüdür → "D05-LATENCY RED" + exit 1.
 *   Fix (telemetri üyeleri atomic<float>, okuyucular load-relaxed kilitsiz;
 *     yazanlar m_stateMutex altında serileşmeye devam eder, kilit-sırası
 *     m_stateMutex->m_streamMutex tek-yönü bozulmaz) sonrası p99
 *     eşik-altıdır → exit 0.
 *   Eşik: p99 > 5.0µs = RED. Zemini: synth-gövde (~1920 örnek
 *     sin+exp üretimi + SDL dummy kuyruk adımları) kilidi onlarca µs tutar;
 *     kilitsiz okuyucu ~6 relaxed-load (<0.2µs) maliyetindedir. p99, nadir
 *     zamanlayıcı-preemption'larına karşı max'tan sağlamdır (max yalnızca
 *     bilgi için yazdırılır).
 *
 * Bacak 2 — fonksiyonel parite çipası (her konfigürasyonda YEŞİL):
 *   blip sonrası update(); peak/rms [0,1] aralığında + sonlu, spectrum
 *   bantları sonlu; blip sayacı ilerler. Fix tek-thread semantiği
 *   değiştiremez. Sayaç ilerlemesi el-sıkışmalıdır (w8-g): Bacak-1 öncesi
 *   ve Bacak-2'de deadline-sınırlı (2sn), 1ms-uykulu bekleyiş; el-sıkışmasız
 *   anlık okuma paralel yükte pul üretir (hammer ~100µs örnekleme
 *   penceresinde hiç zamanlanamayabilir).
 *
 * KIRMIZI-YEŞİL SÖZLEŞMESİ: TSan'sız exit 1 (RED) / exit 0 (YEŞİL);
 * TSan altında da aynı (TSan sessiz kalmalıdır — contention kilidi
 * TSan-kırmızısı üretmez). Kırmızıda commit YOK, max 3 fix turu.
 * Prob setter kilidini assert ETMEZ (D02-hakemlik/W3-W4 errata yasağı).
 */
#include "rowl_test_harness.hpp"

#include <algorithm>
#include <thread>
#include <vector>

namespace {

void latencyFail(const std::string& message) {
    rowlLockFail("d05-telemetry-latency-probe", message);
}

// Zamanlanan okuyucu üçlüsü: RED-1 kapsamındaki üç kilitsiz okuyucunun
// tamamı (:2191 getChannelPeak, :2208 getChannelRms, :2224
// getSpectrumBands) her örnekte çağrılır. (w8-e: satır-numaraları güncel
// zemine taşındı; eşik/mantık değişmedi.)
inline void timedTelemetryRead(Rowl::Audio::AudioEngine* audio, float* bands) {
    volatile float sink = 0.0f;
    sink += audio->getChannelPeak(1, 0);
    sink += audio->getChannelRms(1, 0);
    audio->getSpectrumBands(bands, 4);
    sink += bands[0] + bands[1] + bands[2] + bands[3];
    (void)sink;
}

constexpr int kLatencySamples = 2000;
// RED eşiği (mikrosaniye): p99 bunun üstündeyse okuyucu blip-kilidi
// arkasında bloklanıyor demektir.
constexpr double kRedP99Us = 5.0;

// Bacak-2 determinizm el-sıkışması (w8-g): hammer sayacının `baseline`
// değerini geçtiğini deadline-sınırlı, CPU-yakmayan bekleyişle doğrula.
// Her yoklamada 1ms uyunur (sınırsız busy-spin YOK); sayaç atomic olduğundan
// bekleyiş hammer'ın m_stateMutex'iyle yarışmaz. Başarıda true, 2sn
// bütçe aşımında false döner.
constexpr auto kBlipProgressBudget = std::chrono::seconds(2);
constexpr auto kBlipProgressPollStep = std::chrono::milliseconds(1);

bool waitBlipProgress(Rowl::Audio::AudioEngine* audio, uint32_t baseline) {
    const auto deadline = std::chrono::steady_clock::now() + kBlipProgressBudget;
    for (;;) {
        if (audio->getVoiceBlipCount() > baseline) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(kBlipProgressPollStep);
    }
}

}  // namespace

int main() {
    TEST_SECTION("D05 telemetri-gecikme probu (RED-1: contention kilidi)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    if (!audio.initialize() || !audio.isInitialized()) {
        latencyFail("audio init failed");
    }
    if (!audio.isAudioDeviceAvailable()) {
        // D04 emsali: kilit yalnızca cihazlı blip-yolunda (SDL-kuyruk
        // adımları) yazılır; cihazsız kurulumda SKIP.
        std::cout << "  SKIP d05 latency probe (cihaz yok; blip SDL-kuyruk adimlari calismaz)"
                  << std::endl;
        return 0;
    }

    // Isınma: synth-blip + update (akışlar ve telemetri deflect'i kurulu).
    audio.playVoiceBlip("", 1.0f, 0.9f, Rowl::Audio::AudioChannelType::Voice);
    audio.update();
    const uint32_t blips0 = audio.getVoiceBlipCount();
    if (blips0 == 0) latencyFail("kurulum: blip sayaci ilerlemedi");

    // ── Bacak 1: blip-hammer altında okuyucu gecikmesi ──
    std::atomic<bool> stopHammer{false};
    std::thread hammer([&]() {
        float pitch = 1.0f;
        while (!stopHammer.load(std::memory_order_relaxed)) {
            audio.playVoiceBlip("", pitch, 0.9f,
                                Rowl::Audio::AudioChannelType::Voice);
            pitch += 0.001f;
            if (pitch > 1.2f) pitch = 0.9f;
        }
    });

    // El-sıkışma (w8-g): örneklemeye başlamadan önce hammer'ın en az bir
    // blip işlediğini deadline-sınırlı bekleyişle doğrula. El-sıkışmasız
    // düzende kilitsiz okuyucu ~100µs'de 2000 örneği bitirir; paralel yükte
    // hammer thread'i bu pencerede hiç zamanlanamazsa (stop bayrağı ilk
    // yoklamadan önce görülür, 0 iterasyon) Bacak-2 pulu üretir. Bekleyiş
    // 1ms adımlarla uyur, hammer'ın kilidiyle yarışmaz; aynı zamanda
    // örnekleme boyunca hammer'ın aktif çekiçlediğini garantiler (RED-1
    // contention sinyalini zayıflatmaz, güçlendirir).
    if (!waitBlipProgress(&audio, blips0))
        latencyFail("handshake: 2sn icinde hammer blip sayaci ilerlemedi");

    std::vector<double> samples;
    samples.reserve(kLatencySamples);
    float bands[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < kLatencySamples; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        timedTelemetryRead(&audio, bands);
        const auto t1 = std::chrono::steady_clock::now();
        samples.push_back(
            std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    stopHammer.store(true, std::memory_order_relaxed);
    hammer.join();

    std::sort(samples.begin(), samples.end());
    const double p50 = samples[samples.size() / 2];
    const double p99 = samples[(samples.size() * 99) / 100];
    const double maxUs = samples.back();
    std::cout << "  latency-us: p50=" << p50 << " p99=" << p99
              << " max=" << maxUs << " (n=" << samples.size() << ")"
              << std::endl;
    TEST_PASS("Bacak1 — blip-hammer altinda 2000 okuyucu ornegi toplandi");

    if (p99 > kRedP99Us) {
        std::cout << "D05-LATENCY RED: okuyucu p99=" << p99
                  << "us > esik=" << kRedP99Us
                  << "us (blip kilidi arkasinda bloklanma)" << std::endl;
        return 1;
    }
    std::cout << "D05-LATENCY GREEN: okuyucu p99=" << p99
              << "us <= esik=" << kRedP99Us << "us (kilitsiz okuyucu)"
              << std::endl;
    TEST_PASS("Bacak1 — okuyucu p99 esik-alti (contention kilidi yesil)");

    // ── Bacak 2: fonksiyonel parite çipası ──
    audio.update();
    const float peak = audio.getChannelPeak(1, 0);
    const float rms = audio.getChannelRms(1, 0);
    float check[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    audio.getSpectrumBands(check, 4);
    if (!(peak >= 0.0f && peak <= 1.0f && std::isfinite(peak)))
        latencyFail("bacak2: peak aralik-disi/sonlu-degil");
    if (!(rms >= 0.0f && rms <= 1.0f && std::isfinite(rms)))
        latencyFail("bacak2: rms aralik-disi/sonlu-degil");
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(check[i]) || check[i] < 0.0f)
            latencyFail("bacak2: spectrum bandi bozuk");
    }
    // El-sıkışma Bacak-1 öncesi sayaç ilerlemesini zaten garantiledi;
    // buradaki deadline-sınırlı doğrulama anlık okuma pulunu dışlar
    // (hammer join'li, sayaç sabit; ilk yoklamada dönülür).
    if (!waitBlipProgress(&audio, blips0))
        latencyFail("bacak2: hammer blip sayaci ilerlemedi");
    TEST_PASS("Bacak2 — tek-thread telemetri semantigi parite (aralik + sonluluk + sayac)");

    audio.stopAll();
    audio.shutdown();

    TEST_PASS("d05 telemetri-gecikme kilidi yesil");
    return 0;
}
