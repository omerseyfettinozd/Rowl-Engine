/**
 * test_audio_ui_gain.cpp — Ui bağımsız-kazanç RED kilidi (daraltılmış
 * Bulgu #13: UI çift-kapılama / Sfx çapraz-kapısı).
 *
 * Kilitlenen davranış: Ui one-shot kazancı TEK kaynaktan gelir —
 * `StreamMixer::gainFor(StreamBusId::Ui)` (= master*ui, master dahili).
 * PCM'e bake YOKTUR, Sfx bus'ı UI'ye dokunmaz. Efektif UI kazancı tam
 * olarak bir Ui x master uygulamasıdır.
 *
 * Kırmızı-kanıt probları (ikisi de eski kodda düşer):
 *  (1) Ui=1, Sfx=0 iken çalınan UI sesi telemetride SIFIR olmamalı
 *      (eski kodda master*sfx = 0 sustururdu);
 *  (2) Ui=0.5 iken efektif kazanç ~0.5x olmalı, ~0.25 değil (çift-uygulama
 *      / ui-kare yok — ne bake+akış ne de bake+gainFor(Ui) kalmalı);
 *  (3) kuyruk SONRASI Ui slider hareketi telemetriye yansımalı (donmuş
 *      bake yok).
 *
 * Gözlem: C++ AudioEngine + dummy driver, UI telemetri bus'ı
 * `getChannelPeak(5)` (5 = Ui). Komşu kilitler gibi gerçek WAV + VFS
 * fixture kullanır; mevcut test beklentilerine dokunmaz.
 */
#include "rowl_test_harness.hpp"

namespace {

void appendU16UiGain(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendU32UiGain(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

// Küçük mono S16 WAV: decode yolunu gerçekten çalıştırır (ham tepe ~0.5).
std::vector<uint8_t> makeToneWavUiGain(uint32_t rateHz, float freqHz, float seconds) {
    const uint32_t frames = static_cast<uint32_t>(rateHz * seconds);
    const uint32_t dataBytes = frames * 2u;
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32UiGain(out, 36u + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32UiGain(out, 16u);
    appendU16UiGain(out, 1u);
    appendU16UiGain(out, 1u);
    appendU32UiGain(out, rateHz);
    appendU32UiGain(out, rateHz * 2u);
    appendU16UiGain(out, 2u);
    appendU16UiGain(out, 16u);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32UiGain(out, dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rateHz);
        const float s = std::sin(2.0f * 3.14159265358979323846f * freqHz * t) * 0.5f;
        const int16_t v = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        appendU16UiGain(out, static_cast<uint16_t>(v));
    }
    return out;
}

void writeFileUiGain(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

#define UIGAIN_EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "UiGain test failed: " << msg << std::endl; \
            exit(1); \
        } \
    } while (0)

} // namespace

void test_audio_ui_gain() {
    TEST_SECTION("Audio Ui independent gain (no Sfx cross-gate, single application)");

    const auto projectRoot =
        std::filesystem::temp_directory_path() / "rowl_audio_ui_gain";
    const auto assetDir = projectRoot / "Assets" / "audio";
    std::filesystem::create_directories(assetDir);
    writeFileUiGain(assetDir / "ui_tone.wav", makeToneWavUiGain(22050, 880.0f, 0.10f));
    const std::string uiTone = "audio/ui_tone.wav";

    // ── Prob 1: Ui=1, Sfx=0 iken UI duyulabilir olmalı ───────────────────
    {
        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(projectRoot.string());
        Rowl::Audio::AudioEngine audio(&vfs);
        UIGAIN_EXPECT(audio.initialize() && audio.isInitialized(), "ui engine init");
        audio.setMasterVolume(1.0f);
        audio.setSfxVolume(0.0f);
        audio.setUiVolume(1.0f);
        // Tek kaynak: mixer Ui bus'u master*ui'dir, Sfx dokunmaz.
        UIGAIN_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Ui) == 1.0f,
                      "mixer Ui bus must be master*ui (1.0)");
        audio.playAudio(uiTone, Rowl::Audio::AudioChannelType::Ui);
        UIGAIN_EXPECT(audio.isUiPlaying(), "ui intent must register");
        audio.update();
        const float peak = audio.getChannelPeak(5, 0);
        UIGAIN_EXPECT(peak > 0.35f,
                      "Ui=1/Sfx=0 UI sustu (Sfx capraz-kapisi): peak must be audible");
        audio.stopAll();
        audio.shutdown();
    }
    TEST_PASS("Ui audible while Sfx=0 (no Sfx cross-gate)");

    // ── Prob 2: Ui=0.5 tek-uygulama (~0.25, ~0.125 değil) ────────────────
    {
        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(projectRoot.string());
        Rowl::Audio::AudioEngine audio(&vfs);
        UIGAIN_EXPECT(audio.initialize() && audio.isInitialized(), "ui engine init");
        audio.setMasterVolume(1.0f);
        audio.setSfxVolume(0.5f);
        audio.setUiVolume(0.5f);
        UIGAIN_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Ui) == 0.5f,
                      "mixer Ui bus must be master*ui (0.5)");
        audio.playAudio(uiTone, Rowl::Audio::AudioChannelType::Ui);
        audio.update();
        const float peak = audio.getChannelPeak(5, 0);
        // Ham tepe ~0.5 x tek Ui kazancı 0.5 = ~0.25. Çift-uygulama
        // (bake+akış ya da bake+gainFor(Ui)) ~0.125 verirdi.
        UIGAIN_EXPECT(peak > 0.20f && peak < 0.30f,
                      "Ui=0.5 cift-uygulandi (ui-kare/bake): peak must be single-gain ~0.25");
        audio.stopAll();
        audio.shutdown();
    }
    TEST_PASS("Ui single application (no ui-squared, no bake)");

    // ── Prob 3: kuyruk-sonrası Ui slider canlı olmalı (donmuş bake yok) ──
    {
        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(projectRoot.string());
        Rowl::Audio::AudioEngine audio(&vfs);
        UIGAIN_EXPECT(audio.initialize() && audio.isInitialized(), "ui engine init");
        audio.setMasterVolume(1.0f);
        audio.setSfxVolume(1.0f);
        audio.setUiVolume(1.0f);
        audio.playAudio(uiTone, Rowl::Audio::AudioChannelType::Ui);
        // Kuyruk SONRASI slider: bake donsaydı bu hareket yoksayılırdı.
        audio.setUiVolume(0.25f);
        audio.update();
        const float peak = audio.getChannelPeak(5, 0);
        // Ham tepe ~0.5 x 0.25 = ~0.125. Donmuş bake ~0.5 verirdi.
        UIGAIN_EXPECT(peak > 0.08f && peak < 0.18f,
                      "kuyruk-sonrasi Ui slider yoksayildi (donmus bake): peak must follow live gain");
        audio.stopAll();
        audio.shutdown();
    }
    TEST_PASS("Ui queued one-shot follows later slider (no frozen bake)");

    std::filesystem::remove_all(projectRoot);
}
