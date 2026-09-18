/**
 * test_audio_engine.cpp — Audio engine decode, mixing, telemetry.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"
#include "rowl/audio/long_audio_contract.hpp"

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {

// Minimal PCM WAV builders for header-probe fixtures. Claim sizes are the
// signal: a header may advertise seconds of PCM without carrying them, which
// is exactly how the over-threshold intent is detected without a decode.
void appendU16LE(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void appendU32LE(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
}

std::vector<uint8_t> makeWavHeader(uint32_t sampleRateHz, uint16_t channels,
                                   uint16_t bitsPerSample,
                                   uint32_t claimedDataBytes) {
    const uint32_t byteRate =
        sampleRateHz * channels * (bitsPerSample / 8u);
    const uint16_t blockAlign =
        static_cast<uint16_t>(channels * (bitsPerSample / 8u));
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(out, 36u + claimedDataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32LE(out, 16u);
    appendU16LE(out, 1u); // PCM
    appendU16LE(out, channels);
    appendU32LE(out, sampleRateHz);
    appendU32LE(out, byteRate);
    appendU16LE(out, blockAlign);
    appendU16LE(out, bitsPerSample);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32LE(out, claimedDataBytes);
    return out;
}

uint64_t currentRssKb() {
#if defined(_WIN32)
    // GetProcessWorkingSetSize reports quota LIMITS, not usage, so it can
    // never observe growth; WorkingSetSize is the real resident figure.
    // (psapi link is added in tests/CMakeLists.txt, WIN32-only.)
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters,
                             sizeof(counters)) == 0) {
        return 0;
    }
    return static_cast<uint64_t>(counters.WorkingSetSize / 1024u);
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
    return static_cast<uint64_t>(usage.ru_maxrss); // KiB on Linux.
#endif
}

} // namespace

void test_audio_engine() {
    TEST_SECTION("Audio Subsystem & DSP Filters");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    if (!audio.initialize() || !audio.isInitialized()) {
        std::cerr << "Audio init failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Audio Subsystem Initialization");

    // Voice ducking test
    audio.setBgmVolume(1.0f);
    audio.setDuckingFactor(0.5f);
    if (std::abs(audio.getBgmGain() - 1.0f) > 0.001f) {
        std::cerr << "Audio bgm gain initial mismatch" << std::endl;
        exit(1);
    }

    audio.triggerVoiceDucking(true);
    if (!audio.isDuckingActive() || std::abs(audio.getBgmGain() - 0.5f) > 0.001f) {
        std::cerr << "Audio voice ducking active mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Voice Ducking BGM Attenuation (-6dB / 50% Gain)");

    audio.triggerVoiceDucking(false);
    if (audio.isDuckingActive() || std::abs(audio.getBgmGain() - 1.0f) > 0.001f) {
        std::cerr << "Audio voice ducking restore mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("Voice Ducking BGM Gain Restoration (100% Full Gain)");

    // std::clamp does not itself sanitize NaN. The public audio boundary must
    // preserve the last valid gain instead of forwarding it to SDL.
    audio.setBgmVolume(std::numeric_limits<float>::quiet_NaN());
    audio.setDuckingFactor(std::numeric_limits<float>::quiet_NaN());
    audio.triggerVoiceDucking(true);
    if (!std::isfinite(audio.getBgmGain()) || std::abs(audio.getBgmGain() - 0.5f) > 0.001f) {
        std::cerr << "Non-finite audio inputs corrupted the active gain" << std::endl;
        exit(1);
    }
    audio.triggerVoiceDucking(false);
    TEST_PASS("Audio Gain Rejects Non-Finite Inputs");

    // DSP Filters
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::Telephone);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::Telephone) exit(1);
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::UnderwaterLowPass) exit(1);
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::CaveReverb);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::CaveReverb) exit(1);
    audio.applyDspFilter(Rowl::Audio::DSPFilterType::Normal);
    if (audio.getActiveFilter() != Rowl::Audio::DSPFilterType::Normal) exit(1);
    TEST_PASS("DSP Filter Switching (Normal, Telephone, Underwater, Cave)");

    // Audio playback uses a real, minimal PCM WAV rather than only recording
    // an intent string. This exercises SDL's decode and stream queue path.
    const auto audioProjectRoot = std::filesystem::temp_directory_path() / "rowl_audio_vfs_test_project";
    const auto audioAssetDir = audioProjectRoot / "Assets" / "audio";
    const auto tonePath = audioAssetDir / "rowl_audio_test_tone.wav";
    const std::string toneAssetPath = "audio/rowl_audio_test_tone.wav";
    std::filesystem::create_directories(audioAssetDir);
    const uint8_t wavData[] = {
        'R','I','F','F', 38,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
        68,172,0,0, 136,88,1,0, 2,0, 16,0,
        'd','a','t','a', 2,0,0,0, 0,0
    };
    {
        std::ofstream tone(tonePath, std::ios::binary);
        tone.write(reinterpret_cast<const char*>(wavData), sizeof(wavData));
    }
    vfs.remountProject(audioProjectRoot.string());
    audio.playAudio(toneAssetPath, Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (audio.getCurrentBgmPath() != toneAssetPath) {
        std::cerr << "Audio BGM path mismatch" << std::endl;
        exit(1);
    }
    // This small checked-in fixture is decoded through the VFS stream path,
    // covering real OGG/Vorbis decoding independently from SDL's WAV loader.
    const auto oggPath = audioAssetDir / "rowl_audio_test_tone.ogg";
    const std::string oggAssetPath = "audio/rowl_audio_test_tone.ogg";
    const auto oggData = decodeBase64(
        "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");
    {
        std::ofstream ogg(oggPath, std::ios::binary);
        ogg.write(reinterpret_cast<const char*>(oggData.data()), static_cast<std::streamsize>(oggData.size()));
    }
    audio.playAudio(oggAssetPath, Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != oggAssetPath) {
        std::cerr << "Ogg/Vorbis decode did not replace the BGM playback state" << std::endl;
        exit(1);
    }
    TEST_PASS("BGM OGG/Vorbis Decode through VFS Stream");

    // The replacement track must be fully decoded and queued before a fade
    // starts, so a bad transition can never silence the currently playing BGM.
    audio.playBgm(toneAssetPath, Rowl::Audio::BgmTransitionKind::Crossfade, 0.1f);
    if (audio.getCurrentBgmPath() != toneAssetPath) {
        std::cerr << "BGM transition did not commit the replacement track" << std::endl;
        exit(1);
    }
    if (audio.isAudioDeviceAvailable()) {
        if (!audio.isBgmTransitionActive()) {
            std::cerr << "Crossfade did not start on an available audio device" << std::endl;
            exit(1);
        }
        audio.update(0.2f);
        if (audio.isBgmTransitionActive()) {
            std::cerr << "Crossfade did not complete after its configured duration" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("BGM Transition Queueing and Silent Fallback Contract");
    const auto oversizedAudioPath = audioAssetDir / "rowl_oversized_audio.wav";
    std::ofstream(oversizedAudioPath, std::ios::binary).close();
    std::filesystem::resize_file(oversizedAudioPath, 128ULL * 1024 * 1024 + 1);
    audio.playAudio("audio/rowl_oversized_audio.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != toneAssetPath) {
        std::cerr << "Oversized audio load replaced the current playback state" << std::endl;
        exit(1);
    }
    std::filesystem::remove(oversizedAudioPath);
    if (audio.isAudioDeviceAvailable()) {
        audio.playAudio("missing_theme.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (audio.getCurrentBgmPath() != toneAssetPath) {
            std::cerr << "Failed BGM load replaced the current playback state" << std::endl;
            exit(1);
        }
        audio.playAudio("missing_voice.wav", Rowl::Audio::AudioChannelType::Voice,
                        Rowl::Audio::DSPFilterType::Telephone);
        if (audio.isDuckingActive() || audio.getActiveFilter() != Rowl::Audio::DSPFilterType::Normal) {
            std::cerr << "Failed voice load leaked ducking or DSP side effects" << std::endl;
            exit(1);
        }
    }
    std::filesystem::remove_all(audioProjectRoot);
    // A5-tur1: 64 MiB reddi error snapshot'ında görünür kılındı (C API
    // körlüğü kapandı). Kırmızı-kanıt: E2 m_lastError yazması kaldırılırsa
    // aşağıdaki assert düşer.
    if (audio.getLastError().empty()) {
        std::cerr << "Oversized audio rejection left no audio error snapshot"
                  << std::endl;
        exit(1);
    }
    // No global-restore remount: vfs is function-local, so nothing leaks into
    // later tests.
    TEST_PASS("BGM WAV Decode, Queueing, and Failed-Load State Preservation");

    audio.stopBgm();
    if (!audio.getCurrentBgmPath().empty()) {
        std::cerr << "Audio BGM stop mismatch" << std::endl;
        exit(1);
    }
    // A5-tur1: stop error snapshot'ını temizler — önceki failed-load/oversize
    // hatası stop sonrası görünmemelidir ("son tamamlanan çağrı" sözleşmesi).
    // Kırmızı-kanıt: stopBgm girişindeki m_lastError.clear() kaldırılırsa
    // aşağıdaki assert düşer (oversize reddi snapshot'ı kirletir).
    if (!audio.getLastError().empty()) {
        std::cerr << "Stop did not reset the audio error snapshot: "
                  << audio.getLastError() << std::endl;
        exit(1);
    }
    TEST_PASS("BGM Stop & Track Reset");

    // Typewriter Voice Blips & Audio Effects (Milestone 25)
    audio.resetVoiceBlipCount();
    if (audio.getVoiceBlipCount() != 0) {
        std::cerr << "Initial voice blip count should be zero" << std::endl;
        exit(1);
    }

    // Trigger procedural voice blips with different pitches
    audio.playVoiceBlip("", 1.0f, 0.85f, Rowl::Audio::AudioChannelType::Voice);
    if (audio.getVoiceBlipCount() != 1 || std::abs(audio.getLastVoiceBlipPitch() - 1.0f) > 0.001f) {
        std::cerr << "Voice blip count or pitch mismatch after first blip" << std::endl;
        exit(1);
    }

    audio.playVoiceBlip("", 1.35f, 0.90f, Rowl::Audio::AudioChannelType::Sfx);
    if (audio.getVoiceBlipCount() != 2 || std::abs(audio.getLastVoiceBlipPitch() - 1.35f) > 0.001f) {
        std::cerr << "Voice blip count or pitch mismatch after second blip" << std::endl;
        exit(1);
    }

    // Verify telemetry deflection from voice blip (both individual channels and Master)
    if (audio.getChannelPeak(1) <= 0.0f && audio.getChannelPeak(2) <= 0.0f) {
        std::cerr << "Voice blip should deflect audio channel peak telemetry" << std::endl;
        exit(1);
    }
    if (audio.getChannelPeak(3) <= 0.0f) {
        std::cerr << "Voice blip should immediately deflect Master peak telemetry" << std::endl;
        exit(1);
    }

    // Edge Case: Invalid / non-existent / 0-byte asset path falls back to procedural synth cleanly
    audio.playVoiceBlip("corrupt_or_missing_audio.wav", 1.5f, 0.75f, Rowl::Audio::AudioChannelType::Voice);
    if (audio.getVoiceBlipCount() != 3 || std::abs(audio.getLastVoiceBlipPitch() - 1.5f) > 0.001f) {
        std::cerr << "Fallback to procedural synth for missing/corrupt asset failed" << std::endl;
        exit(1);
    }
    // A5-tur3: synth-fallback ayırt edilebilirliği — missing-asset blip'i
    // synth'e düştü. Cihazlıda synth sayacı artar ve synth<=voice olur
    // (cihazsızda synth dalına girilmez, gate'li). Kırmızı-kanıt: synth++
    // kaldırılırsa cihazlı koşuda aşağıdaki assert düşer.
    if (audio.isAudioDeviceAvailable()) {
        if (audio.getSynthBlipCount() == 0) {
            std::cerr << "Synth fallback blip was not counted" << std::endl;
            exit(1);
        }
        if (audio.getSynthBlipCount() > audio.getVoiceBlipCount()) {
            std::cerr << "Synth blip count exceeds voice blip count" << std::endl;
            exit(1);
        }
    }
    // A5-tur3: sağlıklı dummy'de drop olmaz (sayaç mekanizması var, değer 0;
    // drop-fail headless-dummy'de tetiklenemez, o yüzden değer-kanıtı).
    if (audio.getDropCount() != 0) {
        std::cerr << "Unexpected audio drop count on healthy device" << std::endl;
        exit(1);
    }

    // Edge Case: Extreme pitch and volume values get safely clamped
    audio.playVoiceBlip("", 99.0f, -2.0f, Rowl::Audio::AudioChannelType::Voice);
    if (audio.getLastVoiceBlipPitch() > 4.0f || audio.getVoiceBlipCount() != 4) {
        std::cerr << "Clamping of extreme voice blip pitch/volume failed" << std::endl;
        exit(1);
    }

    audio.resetVoiceBlipCount();
    if (audio.getVoiceBlipCount() != 0) {
        std::cerr << "Reset voice blip count failed" << std::endl;
        exit(1);
    }
    TEST_PASS("Typewriter Character Voice Blips, Master Telemetry & Fallback Synthesis");

    audio.setOutputSuspended(true);
    audio.setOutputSuspended(true);
    if (!audio.isOutputSuspended()) {
        std::cerr << "Output suspend did not latch" << std::endl;
        exit(1);
    }
    audio.setOutputSuspended(false);
    audio.setOutputSuspended(false);
    if (audio.isOutputSuspended()) {
        std::cerr << "Output resume did not latch" << std::endl;
        exit(1);
    }
    TEST_PASS("Output Suspend Edge-Triggering (Repeat Calls Are No-Ops)");

    // Faz 4.5 Dilim 3 — long-audio threshold formula. Expected values are
    // HARD-CODED literals (64 MiB / rate*channels*bytes): retuning the
    // budget constant must fail this test, by design.
    if (Rowl::Audio::kLongAudioBudgetBytes != 67108864ULL) {
        std::cerr << "Long-audio budget is no longer 64 MiB" << std::endl;
        exit(1);
    }
    struct ThresholdVector {
        uint32_t rateHz;
        uint32_t channels;
        uint32_t bytesPerSample;
        double expectedSeconds;
    };
    const ThresholdVector thresholdVectors[] = {
        {44100, 2, 2, 380.43573696145125},
        {48000, 2, 2, 349.5253333333333},
        {44100, 1, 2, 760.8714739229025},
        {48000, 8, 4, 43.690666666666665},
        {8000, 1, 1, 8388.608},
    };
    for (const auto& vector : thresholdVectors) {
        const double got = Rowl::Audio::longAudioThresholdSeconds(
            vector.rateHz, vector.channels, vector.bytesPerSample);
        if (std::abs(got - vector.expectedSeconds) >
            1e-9 * vector.expectedSeconds) {
            std::cerr << "Threshold formula mismatch for " << vector.rateHz
                      << "Hz/" << vector.channels << "ch: got " << got
                      << ", want " << vector.expectedSeconds << std::endl;
            exit(1);
        }
    }
    // Degenerate inputs fail closed: unknown audio never claims a threshold.
    if (Rowl::Audio::longAudioThresholdSeconds(0, 2, 2) != 0.0 ||
        Rowl::Audio::longAudioThresholdSeconds(44100, 0, 2) != 0.0 ||
        Rowl::Audio::longAudioThresholdSeconds(44100, 2, 0) != 0.0) {
        std::cerr << "Threshold formula did not fail closed on degenerate input"
                  << std::endl;
        exit(1);
    }
    TEST_PASS("Long-Audio Threshold Formula (64 MiB vectors + degenerate inputs)");

    // Header-probe durations: exact WAV fixture (1.0 s silence, 44100 Hz
    // mono 16-bit) probed from memory and from a real file, the checked-in
    // OGG fixture probed from its bytes, and garbage staying unknown/silent.
    {
        auto wav = makeWavHeader(44100, 1, 16, 88200);
        wav.insert(wav.end(), 88200, 0);
        const auto memInfo = Rowl::Audio::probeAudioHeaderDuration(
            wav.data(), wav.size());
        if (!memInfo.known || memInfo.sampleRateHz != 44100 ||
            memInfo.channelCount != 1 || memInfo.bytesPerSample != 2 ||
            std::abs(memInfo.durationSeconds - 1.0) > 1e-9) {
            std::cerr << "WAV header probe mismatch (memory buffer)" << std::endl;
            exit(1);
        }
        const auto probeRoot =
            std::filesystem::temp_directory_path() / "rowl_audio_probe_test";
        const auto probeDir = probeRoot / "Assets" / "audio";
        std::filesystem::create_directories(probeDir);
        {
            std::ofstream fixture(probeDir / "probe_tone.wav", std::ios::binary);
            fixture.write(reinterpret_cast<const char*>(wav.data()),
                          static_cast<std::streamsize>(wav.size()));
        }
        std::vector<uint8_t> fromDisk;
        {
            std::ifstream fixture(probeDir / "probe_tone.wav", std::ios::binary);
            fromDisk.assign(std::istreambuf_iterator<char>(fixture),
                            std::istreambuf_iterator<char>());
        }
        const auto diskInfo = Rowl::Audio::probeAudioHeaderDuration(
            fromDisk.data(), fromDisk.size());
        std::filesystem::remove_all(probeRoot);
        if (!diskInfo.known ||
            std::abs(diskInfo.durationSeconds - 1.0) > 1e-9 ||
            diskInfo.sampleRateHz != 44100 || diskInfo.channelCount != 1) {
            std::cerr << "WAV header probe mismatch (on-disk fixture)" << std::endl;
            exit(1);
        }
        // The OGG bytes decoded above carry real Vorbis headers; the probe
        // must recover format + a positive duration without decoding.
        const auto oggInfo = Rowl::Audio::probeAudioHeaderDuration(
            oggData.data(), oggData.size());
        if (!oggInfo.known || oggInfo.sampleRateHz == 0 ||
            oggInfo.channelCount == 0 || oggInfo.channelCount > 8 ||
            oggInfo.bytesPerSample != 2 ||
            !(oggInfo.durationSeconds > 0.0)) {
            std::cerr << "OGG header probe did not recover format/duration"
                      << std::endl;
            exit(1);
        }
        const uint8_t junk[] = {'N', 'O', 'P', 'E'};
        if (Rowl::Audio::probeAudioHeaderDuration(junk, sizeof(junk)).known ||
            Rowl::Audio::probeAndAssessLongAudio(junk, sizeof(junk),
                                                 "junk.bin")
                .exceedsThreshold) {
            std::cerr << "Unknown header must stay unknown and silent" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Header-Probe Durations (WAV memory/disk fixture, OGG fixture, unknown silent)");

    // Boundary: header-claimed 29 s stays silent, exactly 30 s stays silent
    // (strictly-greater semantics), 31 s warns — against a fixed 30 s
    // contract point. The production threshold comes from the formula above;
    // 30 s is the test's fixed boundary proving the comparison operator.
    {
        constexpr uint32_t kCdByteRate = 44100u * 2u * 2u;
        const auto under = makeWavHeader(44100, 2, 16, 29u * kCdByteRate);
        const auto over = makeWavHeader(44100, 2, 16, 31u * kCdByteRate);
        const auto underInfo = Rowl::Audio::probeAudioHeaderDuration(
            under.data(), under.size());
        const auto overInfo = Rowl::Audio::probeAudioHeaderDuration(
            over.data(), over.size());
        if (!underInfo.known ||
            std::abs(underInfo.durationSeconds - 29.0) > 1e-9 ||
            !overInfo.known ||
            std::abs(overInfo.durationSeconds - 31.0) > 1e-9) {
            std::cerr << "Boundary fixture probe mismatch" << std::endl;
            exit(1);
        }
        const auto silent29 = Rowl::Audio::assessLongAudio(
            underInfo.durationSeconds, 30.0, "boundary_29s.wav");
        const auto silent30 =
            Rowl::Audio::assessLongAudio(30.0, 30.0, "boundary_30s.wav");
        const auto warns31 = Rowl::Audio::assessLongAudio(
            overInfo.durationSeconds, 30.0, "boundary_31s.wav");
        if (silent29.exceedsThreshold || silent30.exceedsThreshold ||
            !warns31.exceedsThreshold) {
            std::cerr << "29 s / 30 s / 31 s boundary semantics broken" << std::endl;
            exit(1);
        }
        // Unknown durations and degenerate thresholds stay silent (closed).
        if (Rowl::Audio::assessLongAudio(-1.0, 30.0, "unknown.wav")
                .exceedsThreshold ||
            Rowl::Audio::assessLongAudio(31.0, 0.0, "no_threshold.wav")
                .exceedsThreshold) {
            std::cerr << "Assessment did not fail closed" << std::endl;
            exit(1);
        }
    }
    // Over-threshold fixture: the header claims 100 MiB of CD-quality PCM
    // (no 100 MiB ever allocated or decoded); the assessment must warn WITH
    // the computed seconds on both sides of the comparison.
    {
        auto big = makeWavHeader(44100, 2, 16, 100u * 1024u * 1024u);
        const auto verdict = Rowl::Audio::probeAndAssessLongAudio(
            big.data(), big.size(), "over_threshold.wav");
        constexpr double kWantDuration = 594.4308390022676; // 100 MiB / 176400
        constexpr double kWantThreshold = 380.43573696145125; // 64 MiB / 176400
        if (!verdict.exceedsThreshold ||
            std::abs(verdict.durationSeconds - kWantDuration) >
                1e-9 * kWantDuration ||
            std::abs(verdict.thresholdSeconds - kWantThreshold) >
                1e-9 * kWantThreshold) {
            std::cerr << "Over-threshold fixture did not warn with computed seconds"
                      << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Long-Audio Boundary (29 s silent / 30 s silent / 31 s warns; over-threshold warns)");

    // Long-audio soak on a VIRTUAL clock: 360 x update(1.0 s) == 6 simulated
    // minutes over a tiny real looped BGM. CI pays milliseconds, not minutes,
    // while the loop-feed, telemetry windows, and stream state traverse the
    // same code as wall-clock playback. Mid-soak device events perturb the
    // run; device-switch routing itself stays covered by
    // test_audio_device_recovery.cpp and is not duplicated here.
    {
        Rowl::VFS::VFSManager soakVfs;
        Rowl::Audio::AudioEngine soak(&soakVfs);
        const auto soakRoot =
            std::filesystem::temp_directory_path() / "rowl_audio_long_soak";
        const auto soakDir = soakRoot / "Assets" / "audio";
        std::filesystem::create_directories(soakDir);
        auto soakWav = makeWavHeader(44100, 1, 16, 8820); // 0.1 s loop
        soakWav.insert(soakWav.end(), 8820, 0);
        {
            std::ofstream loop(soakDir / "soak_loop.wav", std::ios::binary);
            loop.write(reinterpret_cast<const char*>(soakWav.data()),
                       static_cast<std::streamsize>(soakWav.size()));
        }
        const std::string soakAsset = "audio/soak_loop.wav";
        soakVfs.remountProject(soakRoot.string());
        if (!soak.initialize() || !soak.isInitialized()) {
            std::cerr << "Soak audio init failed" << std::endl;
            exit(1);
        }
        soak.playAudio(soakAsset, Rowl::Audio::AudioChannelType::Bgm);
        soak.update();
        if (!soak.isBgmPlaying() || soak.getCurrentBgmPath() != soakAsset) {
            std::cerr << "Soak BGM setup failed" << std::endl;
            exit(1);
        }
        const bool deviceWasAvailable = soak.isAudioDeviceAvailable();
        const uint64_t rssBefore = currentRssKb();
        for (int tick = 0; tick < 360; ++tick) {
            soak.update(1.0f);
            if (tick == 120) {
                soak.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED);
            }
            if (tick == 121) {
                if (!soak.isBgmPlaying() ||
                    soak.getCurrentBgmPath() != soakAsset) {
                    std::cerr << "Soak BGM did not resume after device removal"
                              << std::endl;
                    exit(1);
                }
                if (deviceWasAvailable && !soak.isAudioDeviceAvailable()) {
                    std::cerr << "Soak device was not reopened after removal"
                              << std::endl;
                    exit(1);
                }
            }
            if (tick == 240) {
                soak.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED);
            }
            if (tick == 241) {
                if (!soak.isBgmPlaying() ||
                    soak.getCurrentBgmPath() != soakAsset) {
                    std::cerr << "Soak BGM did not continue after format change"
                              << std::endl;
                    exit(1);
                }
            }
        }
        const uint64_t rssAfter = currentRssKb();
        soak.shutdown();
        std::filesystem::remove_all(soakRoot);
        if (rssBefore > 0 && rssAfter > rssBefore + 8192) {
            std::cerr << "Soak RSS grew unboundedly: " << rssBefore << " KiB -> "
                      << rssAfter << " KiB" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Long-Audio Soak (6 virtual minutes: RSS stable, device-removed resume, format-changed continuation)");

    {
        uint64_t capabilities = 0;
        if (RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
            (capabilities & ROWL_ENGINE_CAPABILITY_LONG_AUDIO_CONTRACT) == 0) {
            std::cerr << "ROWL_ENGINE_CAPABILITY_LONG_AUDIO_CONTRACT (2048) missing"
                      << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Capability LONG_AUDIO_CONTRACT (2048) advertised");

    // A5-tur1: bozuk ".ogg" blip'i decode hatası üretir (ov_open fail →
    // m_lastError dolar) ama synth fallback BAŞARILI olur — error snapshot'ı
    // temizlenmelidir (başarı + kirlilik yok). Cihazsız koşuda da geçer:
    // synth dalı cihaz yokken başarıyı erken ilan eder ve yine clear() çalışır.
    // Kırmızı-kanıt: playVoiceBlip synth-clear kaldırılırsa aşağıdaki assert
    // düşer (decode hatası snapshot'ta kalır).
    {
        const auto blipRoot =
            std::filesystem::temp_directory_path() / "rowl_blip_broken_ogg";
        std::filesystem::create_directories(blipRoot / "audio");
        {
            std::ofstream broken(blipRoot / "audio" / "blip_broken.ogg",
                                 std::ios::binary);
            const char garbage[] = "BOZUK-OGG-ICERIK-0123456789ABCDEF";
            broken.write(garbage, sizeof(garbage) - 1);
        }
        vfs.remountProject(blipRoot.string());
        audio.playVoiceBlip("audio/blip_broken.ogg", 1.5f, 0.75f,
                            Rowl::Audio::AudioChannelType::Voice);
        std::filesystem::remove_all(blipRoot);
        if (!audio.getLastError().empty()) {
            std::cerr << "Synth fallback success left a stale audio error: "
                      << audio.getLastError() << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Voice-Blip Synth Success Clears Error Snapshot (broken-OGG decode failure absorbed)");

    audio.shutdown();
    if (audio.isInitialized()) exit(1);
    TEST_PASS("Audio Engine Clean Shutdown");
}
