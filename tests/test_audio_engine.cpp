/**
 * test_audio_engine.cpp — Audio engine decode, mixing, telemetry.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_audio_engine() {
    TEST_SECTION("Audio Subsystem & DSP Filters");

    Rowl::Audio::AudioEngine audio(&Rowl::VFS::VFSManager::instance());
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
    Rowl::VFS::VFSManager::instance().remountProject(audioProjectRoot.string());
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
    Rowl::VFS::VFSManager::instance().remountProject(std::filesystem::current_path().string());
    TEST_PASS("BGM WAV Decode, Queueing, and Failed-Load State Preservation");

    audio.stopBgm();
    if (!audio.getCurrentBgmPath().empty()) {
        std::cerr << "Audio BGM stop mismatch" << std::endl;
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

    audio.shutdown();
    if (audio.isInitialized()) exit(1);
    TEST_PASS("Audio Engine Clean Shutdown");
}
