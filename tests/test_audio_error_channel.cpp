/**
 * test_audio_error_channel.cpp — Ses hata-kanalı kilitleri (M1–M4).
 *
 * Oylama: M1 DÜŞTÜ (artığı M4'e gömülü: handleDeviceEvent içi bool +
 * Device-sınıfı kayıt) + M2 YAŞIYOR (hook-önce + else-kayıt) +
 * M3-daraltılmış (in-flight Ui restore + outage fail-loud, re-queue YOK) +
 * M4 iskeleti (sınıf kanalı + 5 nokta eşleme).
 *
 * RED öngörüleri (dosya-içi "KIRMIZI-KANIT" notlarında):
 *  K1: derleme-RED'i (testFailNextReopen satırı — sembol fix'le gelir).
 *  K2: derleme-RED'i (testFailRestoreFormat satırı — sembol fix'le gelir).
 *  K3a: davranış-RED'i (Ui queued==0 — restore yokken).
 *  K3b: davranış-RED'i (getLastError boş — drop sessizken).
 *  K4-7: davranış-RED'i (kod 10, beklenen 7 — kör damgayken).
 *  K4-10: GREEN-pin (corrupt/missing her iki halde de 10).
 */
#include "rowl_test_harness.hpp"

namespace {

[[noreturn]] void errChanFail(const std::string& message) {
    std::cerr << message << std::endl;
    std::exit(1);
}

void errChanU32LE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void errChanU16LE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void errChanFloatLE(std::vector<uint8_t>& out, float v) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    errChanU32LE(out, bits);
}

// test_audio_lock.cpp makeFloatWavMono44100 emsali: 44-byte header,
// IEEE-float tag-3 mono 44100Hz/32-bit.
std::vector<uint8_t> errChanToneWav() {
    std::vector<float> samples(256, 0.25f);
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    errChanU32LE(out, 36u + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    errChanU32LE(out, 16u);
    errChanU16LE(out, 3u);
    errChanU16LE(out, 1u);
    errChanU32LE(out, 44100u);
    errChanU32LE(out, 44100u * 4u);
    errChanU16LE(out, 4u);
    errChanU16LE(out, 32u);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    errChanU32LE(out, dataBytes);
    for (const float sample : samples) errChanFloatLE(out, sample);
    return out;
}

void errChanWrite(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

void errChanWriteText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

using ErrClass = Rowl::Audio::AudioEngine::AudioErrorClass;

void errChanRequire(bool cond, const std::string& message) {
    if (!cond) errChanFail(message);
}

} // namespace

void test_audio_error_channel() {
    TEST_SECTION("Audio Error Channel (M1-M4)");

    const auto root = std::filesystem::temp_directory_path() / "rowl_audio_error_channel";
    const auto assetDir = root / "Assets" / "audio";
    std::filesystem::create_directories(assetDir);
    errChanWrite(assetDir / "ch_sfx.wav", errChanToneWav());
    errChanWrite(assetDir / "ch_ui.wav", errChanToneWav());
    errChanWriteText(assetDir / "ch_corrupt.wav", "definitely-not-a-wav-payload");
    const std::string rootStr = root.string();

    // ── K1 (M1-artığı + M4): hook-zorlamalı reopen-fail → Device-sınıfı.
    // KIRMIZI-KANIT: fix'sizde testFailNextReopen sembolü yoktur —
    // derleme bu satırdan kızarır.
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        if (!audio.initialize() || !audio.isInitialized()) {
            errChanFail("ErrorChannel K1: audio init failed");
        }
        if (!audio.isAudioDeviceAvailable()) {
            std::cout << "  SKIP ErrorChannel K1/K2/K3a: no physical device "
                         "(hook-driven reopen asserts need a device; "
                         "K3b/K4 below are device-independent)"
                      << std::endl;
        } else {
            vfs.remountProject(rootStr);
            audio.setOutputSuspended(true);
            audio.playAudio("audio/ch_sfx.wav", Rowl::Audio::AudioChannelType::Sfx);
            errChanRequire(audio.getLastError().empty(),
                           "ErrorChannel K1: SFX setup left an error: '" +
                               audio.getLastError() + "'");
            audio.testFailNextReopen();
            audio.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED);
            errChanRequire(!audio.getLastError().empty(),
                           "ErrorChannel K1: injected reopen failure left no error (bool discard)");
            errChanRequire(audio.getLastErrorClass() == ErrClass::Device,
                           "ErrorChannel K1: reopen failure not Device-class");
            errChanRequire(audio.getLastError().find("reopen") != std::string::npos,
                           "ErrorChannel K1: unexpected reopen message: '" +
                               audio.getLastError() + "'");
            audio.setOutputSuspended(false);
            TEST_PASS("ErrorChannel K1 — injected reopen failure is Device-class (M1+M4)");
        }
        audio.shutdown();
    }

    // ── K2 (M2): hook-zorlamalı restore-format düşüşü → Device kaydı.
    // KIRMIZI-KANIT: fix'sizde testFailRestoreFormat sembolü yoktur —
    // derleme bu satırdan kızarır.
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        if (!audio.initialize() || !audio.isInitialized()) {
            errChanFail("ErrorChannel K2: audio init failed");
        }
        if (!audio.isAudioDeviceAvailable()) {
            std::cout << "  SKIP ErrorChannel K2: no physical device" << std::endl;
        } else {
            vfs.remountProject(rootStr);
            audio.setOutputSuspended(true);
            audio.playAudio("audio/ch_sfx.wav", Rowl::Audio::AudioChannelType::Sfx);
            errChanRequire(audio.getLastError().empty(),
                           "ErrorChannel K2: SFX setup left an error: '" +
                               audio.getLastError() + "'");
            audio.testFailRestoreFormat(true);
            audio.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED);
            audio.testFailRestoreFormat(false);
            errChanRequire(!audio.getLastError().empty(),
                           "ErrorChannel K2: forced restore-format drop left no error (else-less)");
            errChanRequire(audio.getLastError().find("restore format failed") !=
                               std::string::npos,
                           "ErrorChannel K2: unexpected restore message: '" +
                               audio.getLastError() + "'");
            errChanRequire(audio.getLastErrorClass() == ErrClass::Device,
                           "ErrorChannel K2: restore-format failure not Device-class");
            audio.setOutputSuspended(false);
            TEST_PASS("ErrorChannel K2 — forced restore-format drop is recorded Device-class (M2)");
        }
        audio.shutdown();
    }

    // ── K3a (M3a): in-flight Ui + outage + reopen → Ui kuyruğu restore.
    // KIRMIZI-KANIT: fix'sizde Ui restore yoktur — queued==0 olur, bu
    // satırdan kızarır.
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        if (!audio.initialize() || !audio.isInitialized()) {
            errChanFail("ErrorChannel K3a: audio init failed");
        }
        if (!audio.isAudioDeviceAvailable()) {
            std::cout << "  SKIP ErrorChannel K3a: no physical device" << std::endl;
        } else {
            vfs.remountProject(rootStr);
            // Tüketim dondurulur (bayt-kesin; test_audio_lock emsali).
            audio.setOutputSuspended(true);
            audio.playAudio("audio/ch_ui.wav", Rowl::Audio::AudioChannelType::Ui);
            errChanRequire(audio.isUiPlaying(),
                           "ErrorChannel K3a: UI setup did not start");
            errChanRequire(audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui) > 0,
                           "ErrorChannel K3a: UI setup queue is empty");
            audio.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED);
            errChanRequire(audio.isAudioDeviceAvailable(),
                           "ErrorChannel K3a: dummy reopen did not restore the device");
            const size_t uiRestored =
                audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui);
            errChanRequire(uiRestored > 0,
                           "ErrorChannel K3a: in-flight UI was not re-queued after reopen");
            audio.setOutputSuspended(false);
            TEST_PASS("ErrorChannel K3a — in-flight UI one-shot is re-queued after reopen (M3a)");
        }
        audio.shutdown();
    }

    // ── K3b (M3b): outage'da YENİ Sfx/Ui niyeti → kayıt+log, kuyruk YOK.
    // KIRMIZI-KANIT: fix'sizde drop sessizdir (getLastError boş) — bu
    // satırdan kızarır. Cihaz-bağımsızdır (outage bayrağı hook'la kurulur).
    {
        Rowl::VFS::VFSManager vfs;
        Rowl::Audio::AudioEngine audio(&vfs);
        if (!audio.initialize() || !audio.isInitialized()) {
            errChanFail("ErrorChannel K3b: audio init failed");
        }
        vfs.remountProject(rootStr);
        audio.testSetDeviceAvailable(false);
        audio.playAudio("audio/ch_sfx.wav", Rowl::Audio::AudioChannelType::Sfx);
        errChanRequire(!audio.getLastError().empty(),
                       "ErrorChannel K3b: outage SFX drop is silent (no record)");
        errChanRequire(audio.getLastError().find("dropped while output device unavailable") !=
                           std::string::npos,
                       "ErrorChannel K3b: unexpected drop message: '" +
                           audio.getLastError() + "'");
        errChanRequire(audio.getLastErrorClass() == ErrClass::Device,
                       "ErrorChannel K3b: outage drop not Device-class");
        errChanRequire(audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx) == 0,
                       "ErrorChannel K3b: outage SFX was queued (re-queue forbiden)");
        audio.playAudio("audio/ch_ui.wav", Rowl::Audio::AudioChannelType::Ui);
        errChanRequire(!audio.getLastError().empty(),
                       "ErrorChannel K3b: outage UI drop is silent (no record)");
        errChanRequire(audio.getLastErrorClass() == ErrClass::Device,
                       "ErrorChannel K3b: outage UI drop not Device-class");
        errChanRequire(audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui) == 0,
                       "ErrorChannel K3b: outage UI was queued (re-queue forbidden)");
        audio.shutdown();
        TEST_PASS("ErrorChannel K3b — outage SFX/UI drops are recorded, never queued (M3b)");
    }

    // ── K4 (M4): C-API kod eşlemesi — Device→7, Decode→10.
    // KIRMIZI-KANIT (K4-7): fix'sizde kör damga 10 verir — bu satırdan
    // kızarır. K4-10 GREEN-pindir (her iki halde de 10).
    {
        RowlEngineHandle handle = RowlEngine_Create();
        if (!handle || RowlEngine_Init(handle, 320, 180, 0) != 1) {
            errChanFail("ErrorChannel K4: C-API init failed");
        }
        Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(handle);
        if (!engine || !engine->getAudio() || !engine->getAudio()->getVfs()) {
            errChanFail("ErrorChannel K4: no engine/audio/vfs behind handle");
        }
        engine->getAudio()->getVfs()->remountProject(rootStr);
        if (RowlEngine_IsAudioDeviceAvailable(handle) != 0) {
            auto* audio = engine->getAudio();
            // Device-kolu: hook-zorlamalı queue-fail (mevcut #81 kancası).
            audio->testFailNextQueue();
            RowlEngine_PlayAudio(handle, "audio/ch_sfx.wav", 2, 0);
            errChanRequire(RowlEngine_GetLastResultCode(handle) == 7,
                           "ErrorChannel K4-7: queue failure did not yield IoError (7), got: " +
                               std::to_string(RowlEngine_GetLastResultCode(handle)));
            errChanRequire(!std::string(RowlEngine_GetLastAudioError(handle)).empty(),
                           "ErrorChannel K4-7: audio error string empty");
            TEST_PASS("ErrorChannel K4-7 — device-class failure yields IoError (7)");
        } else {
            std::cout << "  SKIP ErrorChannel K4-7: no physical device" << std::endl;
        }
        // Decode-kolu: corrupt asset (cihaz-bağımsız decode yolu).
        RowlEngine_PlayAudio(handle, "audio/ch_corrupt.wav", 2, 0);
        errChanRequire(RowlEngine_GetLastResultCode(handle) == 10,
                       "ErrorChannel K4-10: corrupt asset did not yield AudioDecodeError (10), got: " +
                           std::to_string(RowlEngine_GetLastResultCode(handle)));
        RowlEngine_PlayAudio(handle, "audio/ch_missing.wav", 2, 0);
        errChanRequire(RowlEngine_GetLastResultCode(handle) == 10,
                       "ErrorChannel K4-10: missing asset did not yield AudioDecodeError (10), got: " +
                           std::to_string(RowlEngine_GetLastResultCode(handle)));
        RowlEngine_Destroy(handle);
        TEST_PASS("ErrorChannel K4-10 — decode-class failures stay AudioDecodeError (10)");
    }

    std::filesystem::remove_all(root);
}
