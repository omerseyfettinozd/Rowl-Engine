/**
 * test_audio_lock.cpp — Audio DSP guard locks (#77).
 *
 * KILIT (mutant oldurur) vs GOZCU (watch; oldurmez, davranis bekcisi):
 *  - KILIT: sanitize-NaN (Telephone/CaveReverb/Underwater + zehir-fixture)
 *    ve clamp (Underwater/Telephone/CaveReverb + DC fixture). Ilgili govde
 *    satiri (sanitize dongusu / dal clamp'i) silinirse mandal kirmiziya
 *    duser (NaN/0.0f canlilik kaybi veya >1.0f tasma).
 *  - GOZCU: kalicilik (temiz Normal) ve OGG yanlis-pozitif bekcisi.
 *    Sanitize/clamp mutantini OLDURMEZLER; regresyon bekcisidirler
 *    (asiri-duzeltme / testler-arasi sizma gozlemi).
 *
 * Cihaz bagimliligi (madde 1): mandal YALNIZCA cihaz mevcutken playAudio
 * RAM yolunda yazilir; sessiz-yedekte (!isAudioDeviceAvailable()) playAudio
 * erken doner ve mandala dokunmaz (audio_engine.cpp sessiz-yedek dali).
 * Bu yuzden her kilit fonksiyonu cihaz yokken acik SKIP ile doner;
 * cihaz-bagimsizlik iddiasi YOKTUR.
 *
 * Kapsam (madde 3): kilit iddiasi YALNIZCA playAudio-RAM yoludur.
 * decodeAssetToFloatPcm ve pumpBgmStream ayni applyDspToFloatPcm
 * govdesini cagirir (tek mandal yazma noktasi) ama bu kilitlerce
 * dogrudan calistirilmaz. Normal dali passthrough'dur (sanitize oncesi
 * doner): zehir mandala NaN olarak yansir — bu kapsam-disidir, asagida
 * gozlem olarak belgelenir.
 *
 * NaN-yapiskan mandal (madde 4): tek bir NaN cikis ornegi mandali NaN
 * yapar ve sonraki sonlu ornekler onu temizlemez; boylece CaveReverb
 * kolundaki seyrek NaN (gecikme hatti 5512 frame >> 64-ornek fixture,
 * sonlu clamp komsulari arasinda kaybolurdu) da dusurur. Normal
 * passthrough'ta da ayni yapiskanlik gozlenir (asagidaki gozlem).
 *
 * Desen: TEST_SECTION/TEST_PASS + hata=exit(1); timing-assert YOKTUR.
 */
#include "rowl_test_harness.hpp"

namespace {

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

void appendFloatLE(std::vector<uint8_t>& out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32LE(out, bits);
}

// test_audio_engine.cpp makeWavHeader emsali: 44-byte header, IEEE-float
// tag-3 mono 44100Hz/32-bit. S16 decode [-1,1]'e normalize ettigi icin
// tasma-fixture'i float olmak ZORUNDADIR.
std::vector<uint8_t> makeFloatWavMono44100(const std::vector<float>& samples) {
    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * sizeof(float));
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(out, 36u + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32LE(out, 16u);
    appendU16LE(out, 3u); // IEEE float
    appendU16LE(out, 1u); // mono
    appendU32LE(out, 44100u);
    appendU32LE(out, 44100u * 4u); // byteRate
    appendU16LE(out, 4u); // blockAlign
    appendU16LE(out, 32u); // bitsPerSample
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32LE(out, dataBytes);
    for (const float sample : samples) appendFloatLE(out, sample);
    return out;
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

[[noreturn]] void lockFail(const std::string& message) {
    std::cerr << message << std::endl;
    std::exit(1);
}

// Fixture B (yanlis-pozitif bekcisi): test_audio_engine.cpp'deki mevcut
// base64 OGG tonu aynen (duzeltmenin cikisi susturmadigini kanitlar).
const char* kLockToneOggBase64 = "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";

void setupLockProject(Rowl::VFS::VFSManager& vfs, Rowl::Audio::AudioEngine& audio) {
    if (!audio.initialize() || !audio.isInitialized()) {
        lockFail("Audio DSP Guard Locks (#77): audio init failed");
    }
    const auto root = std::filesystem::temp_directory_path() / "rowl_audio_lock_vfs_project";
    const auto dir = root / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    // Fixture A (katil): [quiet_NaN, +Inf, -Inf, 0.5f x61] — !isfinite'in
    // uc kolu tek fixture'da testlenir.
    std::vector<float> poison;
    poison.push_back(std::numeric_limits<float>::quiet_NaN());
    poison.push_back(std::numeric_limits<float>::infinity());
    poison.push_back(-std::numeric_limits<float>::infinity());
    poison.insert(poison.end(), 61, 0.5f);
    writeBytes(dir / "lock_nan.wav", makeFloatWavMono44100(poison));
    // Temiz fixture (kalicilik): 64 x 0.5f.
    writeBytes(dir / "lock_clean.wav",
               makeFloatWavMono44100(std::vector<float>(64, 0.5f)));
    // Clamp katilleri: +/-5.0f DC, 256 ornek (alpha=0.10 ile lowPass ~60
    // ornekte ~4.99'a yakinsar; tekil spike YETERSIZDIR — fixture DC olmak
    // zorundadir).
    writeBytes(dir / "lock_dc_pos.wav",
               makeFloatWavMono44100(std::vector<float>(256, 5.0f)));
    writeBytes(dir / "lock_dc_neg.wav",
               makeFloatWavMono44100(std::vector<float>(256, -5.0f)));
    const std::vector<uint8_t> ogg = decodeBase64(kLockToneOggBase64);
    if (ogg.empty()) {
        lockFail("Audio DSP Guard Locks (#77): OGG fixture decode failed");
    }
    writeBytes(dir / "lock_tone.ogg", ogg);
    vfs.remountProject(root.string());
}

// Madde 1 bekcisi: mandal yalnizca cihazli playAudio-RAM yolunda yazilir.
// Cihazsiz kosuda kilitler calismaz; sessiz SKIP (hata DEGILDIR, exit YOK).
bool requireAudioDeviceOrSkip(Rowl::Audio::AudioEngine& audio, const char* lockName) {
    if (audio.isAudioDeviceAvailable()) return true;
    std::cout << "  SKIP " << lockName
              << " (cihaz yok; mandal yalnizca cihazli playAudio-RAM yolunda yazilir)"
              << std::endl;
    return false;
}

// Iki-yonlu bound: sifirlanmamis ama sonlu cikti. Karsilastirmalar
// fail-closed yazilir (NaN her iki kontrolu de dusurur).
float requireLivePeak(Rowl::Audio::AudioEngine& audio, const std::string& context,
                      const char* nanMessage, const char* deadMessage) {
    const float peak = audio.testLastDspPeak();
    if (!std::isfinite(peak)) {
        lockFail(context + ": " + nanMessage);
    }
    if (!(peak > 0.0f)) {
        lockFail(context + ": " + deadMessage);
    }
    return peak;
}

void requireClampedUnitPeak(float peak, const std::string& context) {
    if (!(peak <= 1.0f)) {
        lockFail(context + ": Underwater tasti");
    }
    if (!(peak >= 0.999f)) {
        lockFail(context + ": sinyal gecti ama boguldu — asiri-duzeltme bekcisi");
    }
}

} // namespace

void test_audio_lock_sanitize_nan() {
    TEST_SECTION("Audio DSP Guard Locks (#77)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupLockProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio Sanitize Lock")) return;

    // Telephone: NaN lowPass durumunu zehirler (surekli kuyruk). Sanitize
    // silinirse tamponun tamami NaN olur, mandal NaN/0.0f kalir.
    audio.playAudio("audio/lock_nan.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireLivePeak(audio, "lock_nan/Telephone",
                    "NaN clamp'ten gecti",
                    "zehir tamponu oldurdu — temiz kuyruk sustu");
    TEST_PASS("Audio Sanitize Lock — Telephone (NaN/+Inf/-Inf, lowPass durum-zehiri)");

    // CaveReverb: gecikme hatti zehiri (seyrek periyodik re-enjeksiyon).
    // Ayni fixture, ayri mekanizma; mandal korunan govdededir. NaN-yapiskan
    // mandal olmasa seyrek NaN sonlu clamp komsulari arasinda kaybolurdu.
    audio.playAudio("audio/lock_nan.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::CaveReverb);
    requireLivePeak(audio, "lock_nan/CaveReverb",
                    "NaN clamp'ten gecti",
                    "zehir tamponu oldurdu — temiz kuyruk sustu");
    TEST_PASS("Audio Sanitize Lock — CaveReverb (NaN/+Inf/-Inf, delay-hatti zehiri)");

    // Underwater capraz bacak (madde 3): zehir-fixture ucuncu dalda da
    // calinir. Sanitize silinirse lowPass NaN'e yapisir, tamponun tamami
    // NaN olur, mandal NaN kalir.
    audio.playAudio("audio/lock_nan.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    requireLivePeak(audio, "lock_nan/UnderwaterLowPass",
                    "NaN clamp'ten gecti",
                    "zehir tamponu oldurdu — temiz kuyruk sustu");
    TEST_PASS("Audio Sanitize Lock — Underwater (NaN/+Inf/-Inf, capraz bacak)");

    // Normal dal gozlemi (madde 3): passthrough dali sanitize ONCESI doner,
    // o yuzden zehir mandala NaN olarak yansir. Bu kapsam-disidir (durumsuz
    // dal; zehirlenecek lowPass/delay hatti yok) — kilit degil, belgelenmis
    // gozlemdir. NaN-yapiskan semantik kaniti: tek NaN mandali NaN yapar,
    // sonraki 61 sonlu ornek onu temizlemez.
    audio.playAudio("audio/lock_nan.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Normal);
    {
        const float peak = audio.testLastDspPeak();
        if (!std::isnan(peak)) {
            lockFail("lock_nan/Normal: passthrough gozlemi bozuldu — "
                     "mandal NaN beklenirken sonlu deger goruldu "
                     "(Normal dali sanitize oncesi donmelidir)");
        }
    }
    TEST_PASS("Audio Normal Gozlem — passthrough NaN-yapiskandir (kapsam-disi, kilit degil)");

    // Kalicilik GOZCUSU (madde 4): zehirli calistan hemen sonra temiz fixture
    // Normal filtreyle calinir — mandal hâlâ canli (zehir testler-arasi sizma
    // yapmadi). Sanitize/clamp mutantini OLDURMEZ (temiz sinyal tek basina
    // canlidir); sizma/asiri-duzeltme bekcisidir, kilit degildir.
    audio.playAudio("audio/lock_clean.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Normal);
    requireLivePeak(audio, "lock_clean/Normal (kalicilik)",
                    "temiz calis NaN uretti — zehir sonraki calismaya sizdi",
                    "temiz calis sustu — zehir sonraki calismaya sizdi");
    TEST_PASS("Audio Sanitize GOZCU — kalicilik (temiz Normal canli; kilit degil)");

    // OGG yanlis-pozitif GOZCUSU (madde 4): duzeltme cikisi susturmadi.
    // Sanitize mutantini OLDURMEZ (temiz OGG sanitize'siz de canlidir);
    // asiri-duzeltme bekcisidir, kilit degildir.
    audio.playAudio("audio/lock_tone.ogg", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    requireLivePeak(audio, "lock_tone.ogg/UnderwaterLowPass",
                    "OGG cikisi NaN uretti",
                    "OGG cikisi sustu — duzeltme gecerli sinyali bogdu");
    TEST_PASS("Audio Sanitize GOZCU — OGG bekcisi (cikis canli; kilit degil)");
}

void test_audio_lock_underwater_clamp() {
    TEST_SECTION("Audio DSP Guard Locks (#77)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupLockProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio Clamp Lock")) return;

    // Pozitif DC: clamp kaldirilirsa mandal ~5.0 olur (B1 DUSER).
    audio.playAudio("audio/lock_dc_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    requireClampedUnitPeak(audio.testLastDspPeak(), "lock_dc_pos/UnderwaterLowPass");
    TEST_PASS("Audio Clamp Lock — Underwater +5.0f DC (tasmak yok, bogulmak yok)");

    // Negatif varyant: isaret-simetrisi (fabs uzerinden ayni boundlar).
    audio.playAudio("audio/lock_dc_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    requireClampedUnitPeak(audio.testLastDspPeak(), "lock_dc_neg/UnderwaterLowPass");
    TEST_PASS("Audio Clamp Lock — Underwater -5.0f DC (isaret-simetrisi)");

    // Sicak-sinyal ortusu (madde 2): ayni DC fixture'lar Telephone VE
    // CaveReverb ile de calinir. Telephone'da DC gecisi (hp*2.1) clamp'siz
    // ~3.78'e firlar; CaveReverb'de (input+delayed*0.28) clamp'siz ~5.0'e
    // firlar — iki dalin clamp silmeleri de kirmiziya duser. Alt bound
    // (>=0.999f) asiri-duzeltme bekcisidir: mandal gecis anindaki clamp
    // vurusunu gorur (maks), surekli-hal susturmasini degil.
    audio.playAudio("audio/lock_dc_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireClampedUnitPeak(audio.testLastDspPeak(), "lock_dc_pos/Telephone");
    TEST_PASS("Audio Clamp Lock — Telephone +5.0f DC (sicak-sinyal ortusu)");

    audio.playAudio("audio/lock_dc_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireClampedUnitPeak(audio.testLastDspPeak(), "lock_dc_neg/Telephone");
    TEST_PASS("Audio Clamp Lock — Telephone -5.0f DC (isaret-simetrisi)");

    audio.playAudio("audio/lock_dc_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::CaveReverb);
    requireClampedUnitPeak(audio.testLastDspPeak(), "lock_dc_pos/CaveReverb");
    TEST_PASS("Audio Clamp Lock — CaveReverb +5.0f DC (sicak-sinyal ortusu)");

    audio.playAudio("audio/lock_dc_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::CaveReverb);
    requireClampedUnitPeak(audio.testLastDspPeak(), "lock_dc_neg/CaveReverb");
    TEST_PASS("Audio Clamp Lock — CaveReverb -5.0f DC (isaret-simetrisi)");
}
