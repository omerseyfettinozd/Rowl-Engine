/**
 * test_audio_mixer.cpp — Faz 5 Dilim 2 native hat testleri.
 *
 * Kapsam: StreamMixer tek-kaynak paritesi, SFX polyphony havuzu
 * (derinlik/steal-oldest/clamp), fade/crossfade eğrileri (linear
 * bit-identical + equal-power uçlar), ambience bed'leri (çift bed +
 * crossfade tamamlama + tek-bed mirası), C API vektörleri (16384 biti,
 * null-handle fail-closed, caller-buffer), pumpBgmStream maliyet
 * gözlemlenebilirliği (fail kapısı YOK).
 */
#include "rowl_test_harness.hpp"
#include "rowl/audio/fade_curves.hpp"
#include "rowl/audio/sfx_polyphony.hpp"
#include "rowl/audio/stream_mixer.hpp"

namespace {

void appendU16Mix(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendU32Mix(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

// Küçük mono S16 WAV: decode yolunu gerçekten çalıştırır.
std::vector<uint8_t> makeToneWavMix(uint32_t rateHz, float freqHz, float seconds) {
    const uint32_t frames = static_cast<uint32_t>(rateHz * seconds);
    const uint32_t dataBytes = frames * 2u;
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32Mix(out, 36u + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32Mix(out, 16u);
    appendU16Mix(out, 1u);
    appendU16Mix(out, 1u);
    appendU32Mix(out, rateHz);
    appendU32Mix(out, rateHz * 2u);
    appendU16Mix(out, 2u);
    appendU16Mix(out, 16u);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32Mix(out, dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rateHz);
        const float s = std::sin(2.0f * 3.14159265358979323846f * freqHz * t) * 0.5f;
        const int16_t v = static_cast<int16_t>(std::clamp(s, -1.0f, 1.0f) * 32767.0f);
        appendU16Mix(out, static_cast<uint16_t>(v));
    }
    return out;
}

void writeFileMix(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

#define MIX_EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "Mixer test failed: " << msg << std::endl; \
            exit(1); \
        } \
    } while (0)

} // namespace

void test_audio_mixer() {
    TEST_SECTION("Audio Mixer / Polyphony / Curves / Beds / Pump (Faz 5 Dilim 2)");

    // ── 1. StreamMixer tek-kaynak paritesi ──────────────────────────────
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        MIX_EXPECT(audio.initialize() && audio.isInitialized(), "mixer init");

        // Varsayılanlar: tüm kazançlar 1.
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Bgm) == 1.0f, "default bgm gain");
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Voice) == 1.0f, "default voice gain");
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Sfx) == 1.0f, "default sfx gain");
        MIX_EXPECT(audio.mixer().gainForAmbienceBed(0) == 1.0f, "default bedA gain");
        MIX_EXPECT(audio.mixer().gainForAmbienceBed(1) == 1.0f, "default bedB gain");

        // master*bus matematiği (ikilik-tam değerlerle bit-seviyesi deterministik).
        audio.setMasterVolume(0.5f);
        audio.setBgmVolume(0.5f);
        audio.setVoiceVolume(0.25f);
        audio.setSfxVolume(0.5f);
        audio.setAmbienceBedVolume(0, 1.0f);
        audio.setAmbienceBedVolume(1, 0.5f);
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Voice) == 0.125f, "voice master*bus");
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Sfx) == 0.25f, "sfx master*bus");
        MIX_EXPECT(audio.mixer().gainForAmbienceBed(0) == 0.5f, "bedA master*bed");
        MIX_EXPECT(audio.mixer().gainForAmbienceBed(1) == 0.25f, "bedB master*bed");
        // Duck yalnız BGM'de: duck kapalıyken master*bgm.
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Bgm) == 0.25f, "bgm noduck master*bgm");
        audio.triggerVoiceDucking(true); // varsayılan faktör 0.5
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Bgm) == 0.125f, "bgm duck master*bgm*duck");
        // Üye ↔ mixer çift-yön senkron: mixer, üye formülüyle aynı sonucu verir.
        const float memberFormula =
            audio.getMasterVolume() * (audio.getBgmVolume() * 0.5f);
        MIX_EXPECT(std::abs(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Bgm) - memberFormula) < 1e-6f,
                   "mixer/member duck sync");
        audio.triggerVoiceDucking(false);
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Bgm) == 0.25f, "bgm duck restore");
        // Non-finite setter mixer'i bozmaz (son geçerli korunur).
        audio.setBgmVolume(std::numeric_limits<float>::quiet_NaN());
        MIX_EXPECT(audio.mixer().gainFor(Rowl::Audio::StreamBusId::Bgm) == 0.25f, "nan keeps mixer");
        audio.shutdown();
    }
    TEST_PASS("Mixer parity (single gain source, master*bus, duck only BGM)");

    // ── 2. SFX polyphony: havuz birimi + motor derinliği ────────────────
    {
        Rowl::Audio::SfxVoicePool pool;
        MIX_EXPECT(pool.depth() == 8, "default depth 8");
        MIX_EXPECT(Rowl::Audio::SfxVoicePool::maxDepth() == 16, "max depth 16");
        MIX_EXPECT(Rowl::Audio::SfxVoicePool::clampDepth(0) == 1, "clamp low");
        MIX_EXPECT(Rowl::Audio::SfxVoicePool::clampDepth(-100) == 1, "clamp negative");
        MIX_EXPECT(Rowl::Audio::SfxVoicePool::clampDepth(99) == 16, "clamp high");
        MIX_EXPECT(Rowl::Audio::SfxVoicePool::clampDepth(8) == 8, "clamp passthrough");

        // Steal-oldest: derinlik 2'de üçüncü ses en eskiyi çalar.
        pool.setDepth(2);
        const uint8_t d1[4] = {1, 2, 3, 4};
        const uint8_t d2[4] = {5, 6, 7, 8};
        const uint8_t d3[4] = {9, 10, 11, 12};
        MIX_EXPECT(pool.play(d1, 4, "a.wav") == 0, "slot a");
        MIX_EXPECT(pool.play(d2, 4, "b.wav") == 1, "slot b");
        MIX_EXPECT(pool.activeCount() == 2, "two active");
        MIX_EXPECT(pool.play(d3, 4, "c.wav") == 0, "steal oldest slot 0");
        MIX_EXPECT(pool.activeCount() == 2, "still two after steal");
        MIX_EXPECT(pool.voices()[0].assetPath == "c.wav", "slot0 stolen");
        MIX_EXPECT(pool.voices()[1].assetPath == "b.wav", "slot1 kept");

        // Derinlik 1 = bugünkü tek-ses davranışı (yeni ses eskisini değiştirir).
        pool.setDepth(1);
        pool.stopAll();
        pool.play(d1, 4, "a.wav");
        pool.play(d2, 4, "b.wav");
        MIX_EXPECT(pool.activeCount() == 1, "depth1 single active");
        MIX_EXPECT(pool.voices()[0].assetPath == "b.wav", "depth1 replace");
        pool.stopAll();
        MIX_EXPECT(pool.activeCount() == 0, "stopAll clears");
    }
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        MIX_EXPECT(audio.initialize(), "pool engine init");
        MIX_EXPECT(audio.sfxPoolDepth() == 8, "engine default depth");
        audio.setSfxPoolDepth(0);
        MIX_EXPECT(audio.sfxPoolDepth() == 1, "engine clamp low");
        audio.setSfxPoolDepth(99);
        MIX_EXPECT(audio.sfxPoolDepth() == 16, "engine clamp high");
        audio.setSfxPoolDepth(2);
        MIX_EXPECT(audio.sfxPoolDepth() == 2, "engine depth set");
        MIX_EXPECT(audio.sfxActiveVoices() == 0, "no active voices fresh");
        MIX_EXPECT(audio.sfxActivePaths().empty(), "no active paths fresh");
        audio.shutdown();
    }
    TEST_PASS("SFX polyphony (pool depth, steal-oldest, depth-1 legacy, clamp)");

    // ── 3. Fade/crossfade eğrileri ──────────────────────────────────────
    {
        using Rowl::Audio::FadeCurve;
        using Rowl::Audio::fadeCurveOutgoing;
        using Rowl::Audio::fadeCurveIncoming;
        using Rowl::Audio::fadeKindOutgoing;
        using Rowl::Audio::fadeKindIncoming;
        // Linear kolu mevcut formüllerle bit-identicaldir (aynı op sırası).
        for (float p : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            MIX_EXPECT(fadeCurveOutgoing(FadeCurve::Linear, p) == 1.0f - p, "linear out identical");
            MIX_EXPECT(fadeCurveIncoming(FadeCurve::Linear, p) == p, "linear in identical");
            MIX_EXPECT(fadeKindOutgoing(FadeCurve::Linear, p) == std::max(0.0f, 1.0f - p * 2.0f),
                       "fade-kind out identical");
            MIX_EXPECT(fadeKindIncoming(FadeCurve::Linear, p) == std::max(0.0f, p * 2.0f - 1.0f),
                       "fade-kind in identical");
        }
        // Equal-power uçları: p=0 → 1/0, p=1 → 0/1 (snap), p=0.5 → √2/2.
        MIX_EXPECT(fadeCurveOutgoing(FadeCurve::EqualPower, 0.0f) == 1.0f, "eqpow out p0");
        MIX_EXPECT(fadeCurveIncoming(FadeCurve::EqualPower, 0.0f) == 0.0f, "eqpow in p0");
        MIX_EXPECT(fadeCurveOutgoing(FadeCurve::EqualPower, 1.0f) == 0.0f, "eqpow out p1");
        MIX_EXPECT(fadeCurveIncoming(FadeCurve::EqualPower, 1.0f) == 1.0f, "eqpow in p1");
        constexpr float kHalfRoot2 = 0.70710678f;
        MIX_EXPECT(std::abs(fadeCurveOutgoing(FadeCurve::EqualPower, 0.5f) - kHalfRoot2) < 1e-6f,
                   "eqpow out p05");
        MIX_EXPECT(std::abs(fadeCurveIncoming(FadeCurve::EqualPower, 0.5f) - kHalfRoot2) < 1e-6f,
                   "eqpow in p05");
        // Aralık-dışı/NaN ilerleme fail-closed: 0'a snap'lenir.
        MIX_EXPECT(fadeCurveOutgoing(FadeCurve::Linear, std::numeric_limits<float>::quiet_NaN()) == 1.0f,
                   "nan progress snaps to 0");
        MIX_EXPECT(fadeCurveOutgoing(FadeCurve::Linear, 5.0f) == 0.0f, "overflow clamps to 1");
        MIX_EXPECT(fadeCurveIncoming(FadeCurve::Linear, -2.0f) == 0.0f, "underflow clamps to 0");

        // Motor eğri seçimi: varsayılan Linear, round-trip korunur.
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        MIX_EXPECT(audio.initialize(), "curve engine init");
        MIX_EXPECT(audio.fadeCurve() == FadeCurve::Linear, "default curve linear");
        audio.setFadeCurve(FadeCurve::EqualPower);
        MIX_EXPECT(audio.fadeCurve() == FadeCurve::EqualPower, "curve set eqpow");
        audio.setFadeCurve(FadeCurve::Linear);
        MIX_EXPECT(audio.fadeCurve() == FadeCurve::Linear, "curve set linear");
        audio.shutdown();
    }
    TEST_PASS("Fade curves (linear bit-identical, equal-power endpoints)");

    // ── 4. Ambience bed'leri (gerçek dosya + VFS) ───────────────────────
    {
        const auto projectRoot =
            std::filesystem::temp_directory_path() / "rowl_audio_mixer_beds";
        const auto assetDir = projectRoot / "Assets" / "audio";
        std::filesystem::create_directories(assetDir);
        writeFileMix(assetDir / "mix_bed_a.wav", makeToneWavMix(22050, 440.0f, 0.05f));
        writeFileMix(assetDir / "mix_bed_b.wav", makeToneWavMix(22050, 660.0f, 0.05f));
        const std::string bedA = "audio/mix_bed_a.wav";
        const std::string bedB = "audio/mix_bed_b.wav";

        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(projectRoot.string());
        Rowl::Audio::AudioEngine audio(&vfs);
        MIX_EXPECT(audio.initialize(), "bed engine init");

        // Geçersiz bed fail-closed.
        MIX_EXPECT(!audio.playAmbienceBed(2, bedA), "invalid bed play false");
        MIX_EXPECT(!audio.playAmbienceBed(-1, bedA), "negative bed play false");
        MIX_EXPECT(!audio.isAmbienceBedPlaying(2), "invalid bed playing false");
        MIX_EXPECT(audio.ambienceBedVolume(2) == 0.0f, "invalid bed volume 0");
        MIX_EXPECT(audio.ambienceBedVolume(-1) == 0.0f, "negative bed volume 0");
        MIX_EXPECT(audio.ambienceBedPath(7).empty(), "invalid bed path empty");
        audio.stopAmbienceBed(5); // no-op, çökmez
        audio.setAmbienceBedVolume(5, 0.5f); // no-op, çökmez

        // BedA oynatma + miras bayrak uyumu (tek-bed mirası).
        MIX_EXPECT(audio.playAmbienceBed(0, bedA), "play bedA");
        MIX_EXPECT(audio.isAmbienceBedPlaying(0), "bedA playing");
        MIX_EXPECT(!audio.isAmbienceBedPlaying(1), "bedB silent");
        MIX_EXPECT(audio.isAmbiencePlaying(), "legacy ambience flag mirrors bedA");
        MIX_EXPECT(audio.getCurrentAmbiencePath() == bedA, "legacy ambience path mirrors bedA");
        MIX_EXPECT(audio.ambienceBedPath(0) == bedA, "bedA path");

        // Bed hacimleri bağımsız + clamp + non-finite ignore.
        audio.setAmbienceBedVolume(1, 0.5f);
        MIX_EXPECT(std::abs(audio.ambienceBedVolume(1) - 0.5f) < 1e-6f, "bedB volume");
        audio.setAmbienceBedVolume(1, 7.0f);
        MIX_EXPECT(audio.ambienceBedVolume(1) == 1.0f, "bedB clamp high");
        audio.setAmbienceBedVolume(0, -3.0f);
        MIX_EXPECT(audio.ambienceBedVolume(0) == 0.0f, "bedA clamp low");
        MIX_EXPECT(audio.getAmbienceVolume() == 0.0f, "legacy getter mirrors bedA");
        audio.setAmbienceBedVolume(0, 0.75f);
        audio.setAmbienceBedVolume(0, std::numeric_limits<float>::quiet_NaN());
        MIX_EXPECT(std::abs(audio.ambienceBedVolume(0) - 0.75f) < 1e-6f, "nan keeps bed volume");

        // BedB bağımsız yatak olarak çalar.
        MIX_EXPECT(audio.playAmbienceBed(1, bedB), "play bedB");
        MIX_EXPECT(audio.isAmbienceBedPlaying(1), "bedB playing");
        MIX_EXPECT(audio.isAmbienceBedPlaying(0), "bedA still playing");

        // Zamanlı crossfade tamamlanır: BedA susar, BedB devralır.
        MIX_EXPECT(audio.crossfadeAmbienceTo(bedA, 0.2f,
                                             Rowl::Audio::FadeCurve::Linear),
                   "crossfade starts");
        MIX_EXPECT(audio.isAmbienceCrossfadeActive(), "crossfade active");
        audio.update(0.1f);
        MIX_EXPECT(audio.isAmbienceCrossfadeActive(), "crossfade mid active");
        audio.update(0.15f);
        MIX_EXPECT(!audio.isAmbienceCrossfadeActive(), "crossfade completes");
        MIX_EXPECT(!audio.isAmbienceBedPlaying(0), "from-bed stopped");
        MIX_EXPECT(audio.isAmbienceBedPlaying(1), "to-bed playing");
        MIX_EXPECT(audio.ambienceBedPath(1) == bedA, "to-bed path");

        // Equal-power crossfade de tamamlanır.
        MIX_EXPECT(audio.playAmbienceBed(0, bedB), "replay bedA");
        MIX_EXPECT(audio.crossfadeAmbienceTo(bedB, 0.1f,
                                             Rowl::Audio::FadeCurve::EqualPower),
                   "eqpow crossfade starts");
        audio.update(0.2f);
        MIX_EXPECT(!audio.isAmbienceCrossfadeActive(), "eqpow crossfade completes");
        MIX_EXPECT(audio.isAmbienceBedPlaying(1), "eqpow to-bed playing");

        // Anlık geçiş (süre<=0) = bugünkü davranış: çalan bed yerinde değişir.
        MIX_EXPECT(audio.crossfadeAmbienceTo(bedA, 0.0f,
                                             Rowl::Audio::FadeCurve::Linear),
                   "instant crossfade ok");
        MIX_EXPECT(!audio.isAmbienceCrossfadeActive(), "instant never active");
        MIX_EXPECT(audio.isAmbienceBedPlaying(1), "instant bed still sounding");
        MIX_EXPECT(audio.ambienceBedPath(1) == bedA, "instant replaces in place");
        MIX_EXPECT(!audio.isAmbienceBedPlaying(0), "instant silences other bed");

        // Kayıp dosya fail-closed.
        MIX_EXPECT(!audio.crossfadeAmbienceTo("audio/missing_bed.wav", 1.0f,
                                              Rowl::Audio::FadeCurve::Linear),
                   "missing asset crossfade false");

        // Miras tek-bed yolu değişmedi: playAudio(Ambience) BedA'dir, BedB'yi bozmaz.
        audio.stopAmbienceBed(0);
        audio.stopAmbienceBed(1);
        audio.playAudio(bedB, Rowl::Audio::AudioChannelType::Ambience);
        MIX_EXPECT(audio.isAmbienceBedPlaying(0), "legacy playAudio feeds bedA");
        MIX_EXPECT(audio.getCurrentAmbiencePath() == bedB, "legacy path kept");
        audio.stopAmbienceBed(0);
        MIX_EXPECT(!audio.isAmbiencePlaying(), "legacy flag cleared on stop");

        audio.shutdown();
        std::filesystem::remove_all(projectRoot);
    }
    TEST_PASS("Ambience beds (dual beds, crossfade completion, single-bed legacy)");

    // ── 5. pumpBgmStream maliyet gözlemlenebilirliği ────────────────────
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        MIX_EXPECT(audio.initialize(), "pump engine init");
        // Taze motorda sayaçlar sıfır; stream yokken pump/update örnek üretmez.
        MIX_EXPECT(audio.bgmPumpSampleCount() == 0, "pump count fresh 0");
        MIX_EXPECT(audio.bgmPumpLastMicroseconds() == 0, "pump last fresh 0");
        MIX_EXPECT(audio.bgmPumpAvgMicroseconds() == 0, "pump avg fresh 0");
        MIX_EXPECT(audio.bgmPumpMaxMicroseconds() == 0, "pump max fresh 0");
        audio.pumpBgmStream();
        MIX_EXPECT(audio.bgmPumpSampleCount() == 0, "pump no sample without stream");
        audio.update(1.0f / 60.0f);
        MIX_EXPECT(audio.bgmPumpSampleCount() == 0, "update no sample without stream");
        audio.setOutputSuspended(true);
        audio.update(1.0f / 60.0f);
        MIX_EXPECT(audio.bgmPumpSampleCount() == 0, "suspend produces no sample");
        audio.setOutputSuspended(false);
        const std::string stats = audio.bgmPumpStatsJson();
        MIX_EXPECT(stats.find("\"count\":0") != std::string::npos, "pump json count");
        MIX_EXPECT(stats.find("\"last_us\":0") != std::string::npos, "pump json last");
        MIX_EXPECT(stats.find("\"avg_us\":0") != std::string::npos, "pump json avg");
        MIX_EXPECT(stats.find("\"max_us\":0") != std::string::npos, "pump json max");
        audio.shutdown();
    }
    TEST_PASS("Pump observability (zero fresh, no sample without stream/suspend)");

    // ── 6. C API vektörleri ─────────────────────────────────────────────
    {
        uint64_t caps = 0;
        MIX_EXPECT(RowlEngine_GetCapabilities(&caps) == ROWL_RESULT_OK, "caps ok");
        MIX_EXPECT(ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY == UINT64_C(16384),
                   "capability bit is 16384");
        MIX_EXPECT((caps & UINT64_C(16384)) != 0, "16384 advertised");
        MIX_EXPECT((caps & UINT64_C(0x3FFF)) == UINT64_C(0x3FFF), "bits 1..8192 intact");

        // Null-handle fail-closed.
        uint32_t required = 0;
        MIX_EXPECT(RowlEngine_GetFadeCurve(nullptr) == 0, "null fade curve");
        MIX_EXPECT(RowlEngine_GetSfxPoolDepth(nullptr) == 0, "null pool depth");
        MIX_EXPECT(RowlEngine_GetSfxActiveVoices(nullptr) == 0, "null active voices");
        MIX_EXPECT(RowlEngine_GetSfxActivePaths(nullptr, nullptr, 0, &required) ==
                       ROWL_RESULT_INVALID_HANDLE,
                   "null active paths handle");
        MIX_EXPECT(RowlEngine_PlayAmbienceBed(nullptr, "x.wav", 0) == 0, "null bed play");
        MIX_EXPECT(RowlEngine_GetAmbienceBedVolume(nullptr, 0) == 0.0f, "null bed volume");
        MIX_EXPECT(RowlEngine_IsAmbienceBedPlaying(nullptr, 0) == 0, "null bed playing");
        MIX_EXPECT(RowlEngine_CrossfadeAmbienceTo(nullptr, "x.wav", 1.0f, 0) == 0,
                   "null crossfade");
        MIX_EXPECT(RowlEngine_IsAmbienceCrossfadeActive(nullptr) == 0, "null crossfade active");
        MIX_EXPECT(RowlEngine_GetBgmPumpStatsJson(nullptr, nullptr, 0, &required) ==
                       ROWL_RESULT_INVALID_HANDLE,
                   "null pump stats handle");
        MIX_EXPECT(RowlEngine_GetBgmPumpAvgMicroseconds(nullptr) == 0, "null pump avg");
        RowlEngine_SetFadeCurve(nullptr, 1);
        RowlEngine_SetSfxPoolDepth(nullptr, 4);
        RowlEngine_StopAmbienceBed(nullptr, 0);
        RowlEngine_SetAmbienceBedVolume(nullptr, 0, 0.5f);

        RowlEngineHandle handle = RowlEngine_Create();
        MIX_EXPECT(handle != nullptr, "c api create");
        MIX_EXPECT(RowlEngine_Init(handle, 64, 64, 0) == 1, "c api init");

        // Eğri round-trip + geçersiz girdi yoksayma.
        RowlEngine_SetFadeCurve(handle, 1);
        MIX_EXPECT(RowlEngine_GetFadeCurve(handle) == 1, "eqpow round-trip");
        RowlEngine_SetFadeCurve(handle, 99);
        MIX_EXPECT(RowlEngine_GetFadeCurve(handle) == 1, "invalid curve ignored");
        RowlEngine_SetFadeCurve(handle, 0);
        MIX_EXPECT(RowlEngine_GetFadeCurve(handle) == 0, "linear round-trip");

        // Havuz derinliği round-trip + clamp.
        MIX_EXPECT(RowlEngine_GetSfxPoolDepth(handle) == 8, "default depth 8");
        RowlEngine_SetSfxPoolDepth(handle, 16);
        MIX_EXPECT(RowlEngine_GetSfxPoolDepth(handle) == 16, "depth 16");
        RowlEngine_SetSfxPoolDepth(handle, 0);
        MIX_EXPECT(RowlEngine_GetSfxPoolDepth(handle) == 1, "depth clamp low");
        RowlEngine_SetSfxPoolDepth(handle, 99);
        MIX_EXPECT(RowlEngine_GetSfxPoolDepth(handle) == 16, "depth clamp high");
        RowlEngine_SetSfxPoolDepth(handle, 8);
        MIX_EXPECT(RowlEngine_GetSfxActiveVoices(handle) == 0, "no voices fresh");

        // Aktif yol JSON'u: taze motorda "[]", caller-buffer sözleşmesiyle.
        uint32_t pathsRequired = 0;
        MIX_EXPECT(RowlEngine_GetSfxActivePaths(handle, nullptr, 0, &pathsRequired) ==
                           ROWL_RESULT_OK &&
                       pathsRequired == 3,
                   "paths size query for []");
        std::vector<char> pathsTiny(pathsRequired - 1, 'x');
        uint32_t pathsRepeated = 0;
        MIX_EXPECT(RowlEngine_GetSfxActivePaths(handle, pathsTiny.data(),
                                                static_cast<uint32_t>(pathsTiny.size()),
                                                &pathsRepeated) ==
                           ROWL_RESULT_BUFFER_TOO_SMALL &&
                       pathsRepeated == pathsRequired && pathsTiny.front() == '\0',
                   "paths undersized contract");
        std::vector<char> pathsBuf(pathsRequired, '\0');
        MIX_EXPECT(RowlEngine_GetSfxActivePaths(handle, pathsBuf.data(),
                                                static_cast<uint32_t>(pathsBuf.size()),
                                                &pathsRepeated) == ROWL_RESULT_OK &&
                       std::string(pathsBuf.data()) == "[]",
                   "paths exact copy []");

        // Bed hacim round-trip + clamp + geçersiz bed.
        RowlEngine_SetAmbienceBedVolume(handle, 1, 0.5f);
        MIX_EXPECT(std::abs(RowlEngine_GetAmbienceBedVolume(handle, 1) - 0.5f) < 1e-6f,
                   "bed volume round-trip");
        RowlEngine_SetAmbienceBedVolume(handle, 1, -2.0f);
        MIX_EXPECT(RowlEngine_GetAmbienceBedVolume(handle, 1) == 0.0f, "bed clamp low");
        RowlEngine_SetAmbienceBedVolume(handle, 0, 7.0f);
        MIX_EXPECT(RowlEngine_GetAmbienceBedVolume(handle, 0) == 1.0f, "bed clamp high");
        RowlEngine_SetAmbienceBedVolume(handle, 0, 0.25f);
        RowlEngine_SetAmbienceBedVolume(handle, 0,
                                        std::numeric_limits<float>::quiet_NaN());
        MIX_EXPECT(std::abs(RowlEngine_GetAmbienceBedVolume(handle, 0) - 0.25f) < 1e-6f,
                   "bed nan ignored");
        MIX_EXPECT(RowlEngine_GetAmbienceBedVolume(handle, 5) == 0.0f, "invalid bed volume 0");
        MIX_EXPECT(RowlEngine_IsAmbienceBedPlaying(handle, 5) == 0, "invalid bed playing 0");
        MIX_EXPECT(RowlEngine_PlayAmbienceBed(handle, nullptr, 0) == 0, "null asset bed 0");
        MIX_EXPECT(RowlEngine_PlayAmbienceBed(handle, "audio/missing_bed.wav", 0) == 0,
                   "missing asset bed 0");
        MIX_EXPECT(RowlEngine_PlayAmbienceBed(handle, "audio/missing_bed.wav", 3) == 0,
                   "invalid bed play 0");
        MIX_EXPECT(RowlEngine_CrossfadeAmbienceTo(handle, nullptr, 1.0f, 0) == 0,
                   "null asset crossfade 0");
        MIX_EXPECT(RowlEngine_IsAmbienceCrossfadeActive(handle) == 0, "crossfade inactive fresh");

        // Pump stats JSON şeması + caller-buffer sözleşmesi.
        uint32_t pumpRequired = 0;
        MIX_EXPECT(RowlEngine_GetBgmPumpStatsJson(handle, nullptr, 0, &pumpRequired) ==
                           ROWL_RESULT_OK &&
                       pumpRequired >= 2,
                   "pump size query");
        std::vector<char> pumpBuf(pumpRequired, '\0');
        MIX_EXPECT(RowlEngine_GetBgmPumpStatsJson(handle, pumpBuf.data(),
                                                  static_cast<uint32_t>(pumpBuf.size()),
                                                  &pathsRepeated) == ROWL_RESULT_OK,
                   "pump exact copy");
        const std::string pumpJson(pumpBuf.data());
        MIX_EXPECT(pumpJson.find("\"count\":") != std::string::npos, "pump json count key");
        MIX_EXPECT(pumpJson.find("\"last_us\":") != std::string::npos, "pump json last key");
        MIX_EXPECT(pumpJson.find("\"avg_us\":") != std::string::npos, "pump json avg key");
        MIX_EXPECT(pumpJson.find("\"max_us\":") != std::string::npos, "pump json max key");
        MIX_EXPECT(RowlEngine_GetBgmPumpAvgMicroseconds(handle) == 0, "pump avg fresh 0");

        RowlEngine_Destroy(handle);
    }
    TEST_PASS("C API vectors (16384 bit, fail-closed, caller buffers, pump schema)");
}
