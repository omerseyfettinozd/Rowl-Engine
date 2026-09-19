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

/**
 * test_audio_lock.cpp eklentisi — BGM Miss Guard (#78) KILIDI.
 *
 * KILIT (mutant oldurur): non-BGM kanalda miss (olmayan dosya / bozuk OGG)
 * calan streaming BGM'i yikmamalidir. Asagidaki 4 fail-closed yolda
 * dogrulandi: eksik dosya x helper/dogrudan + bozuk OGG x helper/dogrudan
 * (6 Miss Guard adimi PASS). V3 (yanlis-uye sifirlama), M2 (tek-gate
 * kaldirma) ve V4 (helper final-miss gatesiz) varyantlari exit=1 ile olur.
 *
 * Gozlem (yedi iddia tek noktada, requireMissGuardIntact, timing-assert YOK):
 *  (a) isBgmPlaying() hâlâ true (intent korunur),
 *  (b) isStreaming() hâlâ true (stream nesnesi sag),
 *  (c) streamInfoJson() mode=="stream" (snapshot dogrulugu),
 *  (d) reason=="over_threshold" (no_bgm DEGIL),
 *  (e) asset==kurulum BGM'i (miss dosyasi DEGIL),
 *  (f) channel==0/Bgm (2/Sfx DEGIL),
 *  (g) getCurrentBgmPath()==kurulum BGM'i (bos DEGIL).
 *
 * KAPSAM-DISI (acik cephe, evrensel-kapsama iddiasi YOKTUR): govdedeki
 * 13 gated siteden bu kilit disinda kalanlar — playAudio alloc (~643),
 * queue-fail (~794) ve helper/playAudio icindeki kalan varyantlar.
 * encoded-cap (~686), decoded-cap (OGG cozumu) ve convert-fail (~718)
 * dallari ayri "Audio BGM Cap Fail-Closed (#87)" kilidiyle kapsanir
 * (asagida); burada kapsam-disi kalirlar.
 * Bu yollarda gate kaldirilinca test yesil kalabilir (~711 karsi-mutanti
 * yesil birakir); kapsama genisletmesi ayri istir.
 *
 * Decode-helper varyanti private decodeAssetToFloatPcm'i public
 * non-BGM caller'lar uzerinden surer: playAmbienceBed /
 * crossfadeAmbienceTo helper'i channelIsBgm=false ile cagirir.
 *
 * BGM fail-closed karsit-kanit [#87 guncellemesi]: BGM kanalinda miss
 * artik predecessor-preserving'dir (eski "miss stream'i KAPATIR" davranisi
 * kaldirildi): stream + snapshot + intent korunur, hata kayda gecer.
 * Explicit stopBgm() kapatmaya devam eder (ayrisma kilidi).
 *
 * Cihaz bagimliligi: streaming kurulum cihaza baglidir; cihazsiz kosuda
 * requireAudioDeviceOrSkip acik SKIP'i aynen uygulanir.
 */
namespace {

// Uzun OGG fixture (2 sn sine, 44100 Hz stereo, 88200 frame):
// test_audio_streaming.cpp'deki kLongToneOggBase64 ile bayt-bayti ayni
// (derleme-sirasinda dogrulanan kopya; splice betigi karsilastirir).
const char* kMissGuardLongToneOggBase64 = "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4AAAEGCDpsTiAIWGrAQAQgIAHIpiWZrmeZ4niqapmiRJ0zxPFEXRNE1TVUmSpnmeKIqiaZqmqrIsTfM8URRF01RVVYWmeZ4oiqJpqqrqwvM8TxRF0TRV1XXheZ4niqJomqrquhBFUTRN01RNVXVdIIqmaZqqqqquC0RPFE1TVV3XdYHniaJpqqqrui4QTdNUVVV1XVkGmKZpqqrryjJAVVXVdV1XlgGqqqqu67qyDFBV13VdWZZlAK7rurIsywIAAA4cAAACjKCTjCqLsNGECw9AoSErAoAoAADAGKYUU8owJiGkEBrGJIQUQiYlpdJSqiCkUlIpFYRUSiolo5RSailVEFIpqZQKQiollVIAANiBAwDYgYVQaMhKACAPAIAwRinGGHNOIqQUY845JxFSijHnnJNKMeacc85JKRlzzDnnpJTOOeecc1JK5pxzzjkppXPOOeeclFJK55xzTkopJYTOQSellNI555wTAABU4AAAEGCjyOYEI0GFhqwEAFIBAAyOY1ma5nmiaJqWJGma53meKJqmJkma5nmeJ4qqyfM8TxRF0TRVled5niiKommqKtcVRdM0TVVVXbIsiqZpmqrqujBN01RV13VdmKZpqqrrui5sW1VV1XVlGbatqqrqurIMXNd1ZdmWgSy7ruzasgAA8AQHAKACG1ZHOCkaCyw0ZCUAkAEAQBiDkEIIIWUQQgohhJRSCAkAABhwAAAIMKEMFBqyEgBIBQAAjLHWWmuttdZAZ6211lprrYDMWmuttdZaa6211lprrbXWUmuttdZaa6211lprrbXWWmuttdZaa6211lprrbXWWmuttdZaa6211lprrbXWWmuttdZaay2llFJKKaWUUkoppZRSSimllFJKBQD6VTgA+D/YsDrCSdFYYKEhKwGAcAAAwBilGHMMQimlVAgx5px0VFqLsUKIMeckpNRabMVzzkEoIZXWYiyecw5CKSnFVmNRKYRSUkottliLSqGjklJKrdVYjDGppNZai63GYoxJKbTUWosxFiNsTam12GqrsRhjayottBhjjMUIX2RsLabaag3GCCNbLC3VWmswxhjdW4ultpqLMT742lIsMdZcAAB3gwMARIKNM6wknRWOBhcashIACAkAIBBSijHGGHPOOeekUow55pxzDkIIoVSKMcaccw5CCCGUjDHmnHMQQgghhFJKxpxzEEII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";

std::vector<uint8_t> missGuardLongToneOggBytes() {
    const auto bytes = decodeBase64(kMissGuardLongToneOggBase64);
    if (bytes.empty() || bytes.size() <= 64 ||
        std::memcmp(bytes.data(), "OggS", 4) != 0) {
        lockFail("Audio BGM Miss Guard (#78): uzun OGG fixture cozule medi");
    }
    return bytes;
}

// OGG page CRC (poly 0x04C11DB7, init 0) — test_audio_streaming.cpp'deki
// oggPageCrc ile ayni algoritma (CRC alani sifirlanir, tum sayfa).
uint32_t missGuardOggPageCrc(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; ++k) {
                r = (r & 0x80000000u) ? ((r << 1) ^ 0x04C11DB7u) : (r << 1);
            }
            table[i] = r;
        }
        ready = true;
    }
    uint32_t crc = 0;
    for (size_t i = 0; i < size; ++i) {
        crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xFFu];
    }
    return crc;
}

// CRC-onarimli granule yamasi: header probe + source-duration geci
// (ov_pcm_total, CRC-duyarli) over-threshold raporlar; decoder gercek
// paketleri calip EOS'a ulasir. test_audio_streaming.cpp'deki
// patchGranuleWithCrcForStream ile ayni islem.
std::vector<uint8_t> missGuardPatchGranuleForStream(std::vector<uint8_t> ogg,
                                                    uint64_t granule) {
    size_t lastPage = std::string::npos;
    for (size_t i = 0; i + 27 <= ogg.size(); ++i) {
        if (std::memcmp(ogg.data() + i, "OggS", 4) == 0) lastPage = i;
    }
    if (lastPage == std::string::npos) {
        lockFail("Audio BGM Miss Guard (#78): granule yamasi icin OggS sayfasi yok");
    }
    if (lastPage + 27 > ogg.size()) {
        lockFail("Audio BGM Miss Guard (#78): kesik OggS sayfasi");
    }
    for (int b = 0; b < 8; ++b) {
        ogg[lastPage + 6 + b] = static_cast<uint8_t>((granule >> (8 * b)) & 0xFFu);
    }
    const size_t nseg = ogg[lastPage + 26];
    if (lastPage + 27 + nseg > ogg.size()) {
        lockFail("Audio BGM Miss Guard (#78): kesik OggS segment tablosu");
    }
    size_t body = 0;
    for (size_t i = 0; i < nseg; ++i) body += ogg[lastPage + 27 + i];
    const size_t pageEnd = lastPage + 27 + nseg + body;
    if (pageEnd > ogg.size()) {
        lockFail("Audio BGM Miss Guard (#78): kesik OggS sayfa govdesi");
    }
    ogg[lastPage + 22] = 0;
    ogg[lastPage + 23] = 0;
    ogg[lastPage + 24] = 0;
    ogg[lastPage + 25] = 0;
    const uint32_t crc = missGuardOggPageCrc(ogg.data() + lastPage, pageEnd - lastPage);
    ogg[lastPage + 22] = static_cast<uint8_t>(crc & 0xFFu);
    ogg[lastPage + 23] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
    ogg[lastPage + 24] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
    ogg[lastPage + 25] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
    return ogg;
}

void setupMissGuardProject(Rowl::VFS::VFSManager& vfs, Rowl::Audio::AudioEngine& audio) {
    if (!audio.initialize() || !audio.isInitialized()) {
        lockFail("Audio BGM Miss Guard (#78): audio init failed");
    }
    const auto root = std::filesystem::temp_directory_path() / "rowl_audio_bgm_miss_guard_project";
    const auto dir = root / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    // Streaming BGM fixture: 1e9 granule (header probe + ov_pcm_total
    // over-threshold; decoder gercek paketleri calar).
    const auto patched =
        missGuardPatchGranuleForStream(missGuardLongToneOggBytes(),
                                       static_cast<uint64_t>(1000000000));
    writeBytes(dir / "miss_bgm.ogg", patched);
    // Bozuk OGG fixture (decode-helper OGG-fail dali icin): ilk 100 bayt.
    const auto tone = missGuardLongToneOggBytes();
    const size_t cut = std::min<size_t>(tone.size(), 100);
    writeBytes(dir / "miss_corrupt.ogg",
               std::vector<uint8_t>(tone.begin(), tone.begin() + static_cast<ptrdiff_t>(cut)));
    vfs.remountProject(root.string());
}

// Uc iddia tek noktada degil, yedi iddia tek noktada (#78-tur2):
// (a) intent (isBgmPlaying), (b) stream nesnesi (isStreaming),
// (c) snapshot mode=="stream", (d) reason=="over_threshold" (no_bgm DEGIL),
// (e) asset==kurulum BGM'i (miss dosyasi DEGIL),
// (f) channel==0/Bgm (2/Sfx DEGIL), (g) m_currentBgmPath==kurulum BGM'i
// (bos DEGIL). M2 varyanti (mode stream kalirken reason=no_bgm +
// asset=miss dosyasi + channel=2 + path bos) bu dort ek iddia ile FAIL
// verir (exit 1); yalniz mode bakmak onu oldurmez.
void requireMissGuardIntact(Rowl::Audio::AudioEngine& audio, const std::string& context) {
    static const std::string kExpectedBgm = "audio/miss_bgm.ogg";
    if (!audio.isBgmPlaying()) {
        lockFail(context + ": BGM intent dustu (isBgmPlaying false)");
    }
    if (!audio.isStreaming()) {
        lockFail(context + ": stream nesnesi kapandi (isStreaming false)");
    }
    const std::string json = audio.streamInfoJson();
    if (json.find("\"mode\":\"stream\"") == std::string::npos) {
        lockFail(context + ": snapshot yalana dustu (mode!=stream): " + json);
    }
    if (json.find("\"reason\":\"over_threshold\"") == std::string::npos) {
        lockFail(context + ": snapshot reason yalani (over_threshold degil): " + json);
    }
    if (json.find("\"channel\":0") == std::string::npos) {
        lockFail(context + ": snapshot channel yalani (0/Bgm degil): " + json);
    }
    if (json.find("\"asset\":\"" + kExpectedBgm + "\"") == std::string::npos) {
        lockFail(context + ": snapshot asset yalani (kurulum BGM degil): " + json);
    }
    if (audio.getCurrentBgmPath() != kExpectedBgm) {
        lockFail(context + ": BGM path yalani (bos/yanlis): '" +
                 audio.getCurrentBgmPath() + "'");
    }
}

} // namespace

void test_audio_lock_bgm_miss_guard() {
    TEST_SECTION("Audio BGM Miss Guard (#78)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupMissGuardProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio BGM Miss Guard")) return;

    // Adim 1: gecerli streaming BGM kurulumu.
    audio.playAudio("audio/miss_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    requireMissGuardIntact(audio, "kurulum/miss_bgm.ogg");
    TEST_PASS("Audio BGM Miss Guard — kurulum (streaming BGM: intent+stream+snapshot)");

    // Adim 2 (katil gozlem): eksik SFX miss'i BGM'e dokunmaz.
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Sfx);
    requireMissGuardIntact(audio, "eksik Sfx sonrasi");
    TEST_PASS("Audio BGM Miss Guard — eksik SFX BGM stream/snapshot'i korur");

    // Varyant: Voice + Ui kanallarinda eksik dosya.
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Voice);
    requireMissGuardIntact(audio, "eksik Voice sonrasi");
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Ui);
    requireMissGuardIntact(audio, "eksik Ui sonrasi");
    TEST_PASS("Audio BGM Miss Guard — eksik Voice/Ui BGM stream/snapshot'i korur");

    // Varyant: decode helper uzerinden non-BGM miss (public caller'lar
    // helper'i channelIsBgm=false ile cagirir). Hem final-miss dali
    // (olmayan dosya) hem OGG-decode-fail dali (bozuk OGG) gozlenir.
    if (audio.playAmbienceBed(0, "audio/does_not_exist.wav")) {
        lockFail("eksik bed beklenmedik basari dondu");
    }
    requireMissGuardIntact(audio, "eksik ambience-bed sonrasi");
    if (audio.crossfadeAmbienceTo("audio/does_not_exist.wav", 0.0f,
                                  Rowl::Audio::FadeCurve::Linear)) {
        lockFail("eksik crossfade beklenmedik basari dondu");
    }
    requireMissGuardIntact(audio, "eksik crossfade sonrasi");
    if (audio.playAmbienceBed(0, "audio/miss_corrupt.ogg")) {
        lockFail("bozuk OGG bed beklenmedik basari dondu");
    }
    requireMissGuardIntact(audio, "bozuk OGG bed sonrasi");
    TEST_PASS("Audio BGM Miss Guard — helper-dali non-BGM miss BGM'i korur (bed/crossfade/corrupt)");

    // 4. varyant (#78-tur2, V3 katili): bozuk OGG helper disinda DOGUDAN
    // playAudio non-BGM uzerinden surulur (Sfx/Voice/Ui x corrupt OGG).
    // playAudio OGG-decode-fail dali (audio_engine.cpp ~656) Bgm-gated
    // degilse stream kapanir + snapshot yalana duser ve genis guard
    // exit(1) ile oldurur. Helper-varyanti bu dala ugramaz
    // (decodeAssetToFloatPcm ayri sitedir), o yuzden bu varyant zorunludur.
    audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Sfx);
    requireMissGuardIntact(audio, "bozuk OGG dogrudan Sfx sonrasi");
    audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Voice);
    requireMissGuardIntact(audio, "bozuk OGG dogrudan Voice sonrasi");
    audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Ui);
    requireMissGuardIntact(audio, "bozuk OGG dogrudan Ui sonrasi");
    TEST_PASS("Audio BGM Miss Guard — dogrudan playAudio non-BGM corrupt OGG BGM'i korur (Sfx/Voice/Ui)");

    // Karsit-kanit [#87 guncellemesi]: BGM kanalinda miss artik
    // predecessor-preserving'dir (eski "BGM miss kapatir" fail-closed
    // davranisi kaldirildi): stream + snapshot + intent aynen korunur,
    // hata caller'a ulasir.
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "BGM miss sonrasi (#87)");
    if (audio.getLastError().empty()) {
        lockFail("BGM miss hatasi caller'a ulasmadi (m_lastError bos)");
    }
    TEST_PASS("Audio BGM Miss Guard — BGM miss predecessor'i korur (#87, karsit-kanit guncellemesi)");

    // Ayrisma kilidi: explicit stopBgm() hâlâ kapatir (miss korumasindan
    // ayristigi kilitlenir; stop sessiz degildir, intent duser).
    audio.stopBgm();
    if (audio.isBgmPlaying() || audio.isStreaming() || !audio.getCurrentBgmPath().empty()) {
        lockFail("explicit stopBgm() kapatmadi — miss korumasi stop'u bozmamali");
    }
    TEST_PASS("Audio BGM Miss Guard — explicit stopBgm() hâlâ kapatir (ayrisma)");

    audio.stopAll();
    audio.shutdown();
}

/**
 * test_audio_lock.cpp eklentisi — Queue-Fail Atomicity (#81) KİLİDİ.
 *
 * KILIT (mutant oldurur): playAudio'daki pre-clear (eski sıra
 * Clear → SetFormat/Put) kuyruk-hatasında çalmakta olan sesi yok ederdi:
 *  - BGM (non-transition): sağlıklı BGM imha olurdu (fail yolu ayrıca
 *    fail-closed kapatırdı),
 *  - Ambience/UI/SFX: kuyruk boşaltılmış kalır + bayraklar eski sesi iddia
 *    ederdi (bayat/çelişkili durum).
 * Düzeltme (sonraya-taşıma/prova): SetFormat → prova-Put → (başarıda)
 * Clear-artığı + commit-Put + state yazımı. Transition-BGM scratch akışı
 * pre-clear'li kalır (doğru desen, dokunulmadı).
 *
 * Gözlem (deterministik; timing-assert YOK; sleep/polling YOK):
 *  Her kanal (UI/Ambience/SFX/BGM-RAM) için: geçerli WAV çalınır (kuyruk
 *  snapshot'ı >0 + bayraklar eski asset), ardından testFailNextQueue
 *  kancasıyla kuyruk-hatası enjekte edilip aynı kanalda ikinci WAV çalınır:
 *   (a) hedef akışın queued-bayt değeri (testQueuedBytes) fail ÖNCESİ
 *       snapshot'a tam eşittir (sıfırlanmamış),
 *   (b) bayraklar eski asset'i gösterir (UI: isUiPlaying + getCurrentUiPath;
 *       Ambience: isAmbiencePlaying + getCurrentAmbiencePath; SFX:
 *       sfxActivePaths tek-girdi eski yol; BGM-RAM: bayt + hata),
 *   (c) m_lastError "Unable to queue decoded audio" ile doludur
 *       (queue-fail caller'a ulaştı; enjeksiyonun kuyruk adımında
 *       tüketildiğinin kanıtıdır),
 *   (d) karşıt-kanıt [#87 güncellemesi]: aynı senaryoda BGM miss
 *       predecessor-preserving'dir (stream kapanmaz — eski #78 "miss
 *       kapatır" davranışı kalktı), hata caller'a ulaşır.
 * Pre-clear mutantında (a) düşer: kuyruk 0'lanır, exit(1).
 *
 * Determinizm notları:
 *  - decode+queue senkron RAM yoludur; karşılaştırma tam eşitliktir.
 *  - Cihaz tüketim yarışı setOutputSuspended(true) ile dondurulur
 *    (akışlar duraklatılır, kuyruklar sabit kalır); test sonunda açılır.
 *  - SFX havuz derinliği 1'e indirilir (slot 0 deterministik hedeftir).
 *  - Cihaz yoksa requireAudioDeviceOrSkip açık SKIP'i aynen uygulanır.
 */
void test_audio_lock_queue_fail_atomic() {
    TEST_SECTION("Audio Queue-Fail Atomicity (#81)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupMissGuardProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio Queue-Fail Atomicity")) return;

    // Kanal başına ayrı-isimli geçerli WAV (64 x 0.5f mono 44100 — formatlar
    // aynı olduğu için SetFormat no-op'tur; SDL belgesi: SetFormat kuyruğu
    // flush etmez). Miss-guard projesinin dizinine eklenir + tekrar remount
    // edilir (aynı kök: streaming BGM + WAV'lar tek VFS'te).
    const auto qdir = std::filesystem::temp_directory_path() /
                      "rowl_audio_bgm_miss_guard_project" / "Assets" / "audio";
    std::filesystem::create_directories(qdir);
    const auto qWav = makeFloatWavMono44100(std::vector<float>(64, 0.5f));
    writeBytes(qdir / "q_ui.wav", qWav);
    writeBytes(qdir / "q_ui_new.wav", qWav);
    writeBytes(qdir / "q_amb.wav", qWav);
    writeBytes(qdir / "q_amb_new.wav", qWav);
    writeBytes(qdir / "q_sfx.wav", qWav);
    writeBytes(qdir / "q_sfx_new.wav", qWav);
    writeBytes(qdir / "q_bgm.wav", qWav);
    writeBytes(qdir / "q_bgm_new.wav", qWav);
    vfs.remountProject((std::filesystem::temp_directory_path() /
                        "rowl_audio_bgm_miss_guard_project")
                           .string());

    auto requireQueueFailed = [&](const std::string& context) {
        const std::string err = audio.getLastError();
        if (err.find("Unable to queue decoded audio") == std::string::npos) {
            lockFail(context + ": queue-fail caller'a ulaşmadı (m_lastError): '" + err + "'");
        }
    };

    // Adım 1: streaming BGM kurulumu (karşıt-kanıt zemini).
    audio.playAudio("audio/miss_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "kurulum/miss_bgm.ogg");
    TEST_PASS("Audio Queue-Fail — kurulum (streaming BGM intact)");

    // Karşıt-kanıt (d) [#87 güncellemesi]: BGM miss artık
    // predecessor-preserving'dir (stream kapanmaz); queue-fail atomikliği
    // ile çelişmez, hata caller'a ulaşır.
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "BGM miss sonrasi (#87)");
    if (audio.getLastError().empty()) {
        lockFail("BGM miss hatasi caller'a ulasmadi (m_lastError bos)");
    }
    TEST_PASS("Audio Queue-Fail — BGM miss predecessor'i korur (#87, karsit-kanit)");

    // Bayt-kesin karşılaştırma için cihaz tüketimi dondurulur.
    audio.setOutputSuspended(true);

    // UI kanalı (a+b+c).
    audio.playAudio("audio/q_ui.wav", Rowl::Audio::AudioChannelType::Ui);
    const size_t uiBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui);
    if (uiBefore == 0) {
        lockFail("UI kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (!audio.isUiPlaying() || audio.getCurrentUiPath() != "audio/q_ui.wav") {
        lockFail("UI kurulum bayraklari yanlis");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_ui_new.wav", Rowl::Audio::AudioChannelType::Ui);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui) != uiBefore) {
        lockFail("UI queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
    }
    if (!audio.isUiPlaying() || audio.getCurrentUiPath() != "audio/q_ui.wav") {
        lockFail("UI queue-fail bayraklari bayatladi (yeni/eski celiskisi)");
    }
    requireQueueFailed("UI queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — UI kuyruk+bayrak korunur, hata caller'a ulaşır");

    // Ambience kanalı (a+b+c).
    audio.playAudio("audio/q_amb.wav", Rowl::Audio::AudioChannelType::Ambience);
    const size_t ambBefore =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience);
    if (ambBefore == 0) {
        lockFail("Ambience kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (!audio.isAmbiencePlaying() || audio.getCurrentAmbiencePath() != "audio/q_amb.wav") {
        lockFail("Ambience kurulum bayraklari yanlis");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_amb_new.wav", Rowl::Audio::AudioChannelType::Ambience);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience) != ambBefore) {
        lockFail("Ambience queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
    }
    if (!audio.isAmbiencePlaying() || audio.getCurrentAmbiencePath() != "audio/q_amb.wav") {
        lockFail("Ambience queue-fail bayraklari bayatladi (yeni/eski celiskisi)");
    }
    requireQueueFailed("Ambience queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — Ambience kuyruk+bayrak korunur, hata caller'a ulaşır");

    // SFX kanalı (a+b+c; derinlik 1 → slot 0 deterministik).
    audio.setSfxPoolDepth(1);
    audio.playAudio("audio/q_sfx.wav", Rowl::Audio::AudioChannelType::Sfx);
    const size_t sfxBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx);
    if (sfxBefore == 0) {
        lockFail("SFX kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    {
        const auto paths = audio.sfxActivePaths();
        if (paths.size() != 1 || paths[0] != "audio/q_sfx.wav") {
            lockFail("SFX kurulum slot-PCM'i yanlis");
        }
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_sfx_new.wav", Rowl::Audio::AudioChannelType::Sfx);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx) != sfxBefore) {
        lockFail("SFX queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
    }
    {
        const auto paths = audio.sfxActivePaths();
        if (paths.size() != 1 || paths[0] != "audio/q_sfx.wav") {
            lockFail("SFX queue-fail slot-PCM'i bayatladi (yeni/eski celiskisi)");
        }
    }
    requireQueueFailed("SFX queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — SFX kuyruk+slot korunur, hata caller'a ulaşır");

    // BGM kanalı, RAM yolu (kısa WAV; non-transition dalı) (a+c).
    audio.playAudio("audio/q_bgm.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t bgmBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (bgmBefore == 0) {
        lockFail("BGM kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_bgm_new.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != bgmBefore) {
        lockFail("BGM queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
    }
    requireQueueFailed("BGM queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — BGM (non-transition) kuyruk korunur, hata caller'a ulaşır");

    audio.setOutputSuspended(false);
    audio.stopAll();
    audio.shutdown();
}

/**
 * test_audio_lock.cpp eklentisi — BGM Transactional Commit (#87) KİLİDİ.
 *
 * KILIT (mutant oldurur): BGM fail yolları predecessor'ı yıkmamalıdır.
 * decode-once → queue/prova → SADECE başarıda commit (close+snapshot+intent).
 * Fail yolu yalnız m_lastError yazar. Kapsanan upfront-yıkım siteleri:
 *  decodeAssetToFloatPcm 6 site, openBgmStream girişi (2143), playAudio
 *  no-device dalı (585/601), playAudio RAM 6 site + queue-fail (822) +
 *  final-miss (944). Cap dallari (encoded/decoded/convert) ayri
 *  "Audio BGM Cap Fail-Closed (#87)" kilidinde kapsanir (asagida).
 * Herhangi biri geri gelirse asagidaki adimlardan biri exit(1) ile duser.
 *
 * Gözlem (tests/test_audio_lock.cpp deseni aynen): TEST_SECTION/TEST_PASS +
 * hata=exit(1); timing-assert/sleep/poll YOK; requireMissGuardIntact 7-iddia;
 * deterministik hook'lar (testFailNextQueue, testQueuedBytes,
 * setOutputSuspended(true) dondurma).
 *  (a) decode-fail (corrupt + eksik) sonrası predecessor sağ + hata dolu.
 *  (b) streaming open-fail (probe geçer, source->open düşer): predecessor
 *      sağ + hata "could not be opened" (yol-kanıtı).
 *  (c) queue-fail (testFailNextQueue + BGM RAM WAV): testQueuedBytes(Bgm)
 *      değişmez + intent eski asset'te + hata "Unable to queue decoded audio".
 *  (d) no-device existence: cihazsız koşar (requireAudioDeviceOrSkip YOK;
 *      cihazlı koşuda açık SKIP notu): sessiz-yedekte geçerli BGM intent'i
 *      kurulur, eksik dosya intent+snapshot'ı yıkmaz + hata set.
 *  (e) intent doğruluğu: başarısız yeni BGM (Fade+duration) transition
 *      başlatmaz; başarılı transition swap'ı aynen (regresyon bekçisi,
 *      update(1.0f) ile deterministik sürülür — duvar-saati YOK).
 *  (f) ayrışma: explicit stopBgm() hâlâ kapatır.
 *
 * KAPSAM-DIŞI: pump-mid-stream-corrupt stopBgm (2317), shutdown (1266) ve
 * init-fail destroy kolları bilinçli stop'tur; bu kilit fail-yolu commit
 * kapısını kilitler, bilinçli stopları değil.
 */
namespace {

// #87 open-fail fixture: header-probe over-threshold raporlar (son-sayfa
// granule 1e9 aynen korunur) ama source->open deterministik düşer (2. sayfa
// comment-paketinin ilk baytı bozuldu + CRC onarılmadı). Probe CRC'ye bakmaz
// (boyut-tablosu yürüyüşü), ov_open ise setup'ı doğruladığı için açılış düşer.
std::vector<uint8_t> transactionalOpenFailOgg() {
    auto ogg = missGuardPatchGranuleForStream(missGuardLongToneOggBytes(),
                                              static_cast<uint64_t>(1000000000));
    size_t offset = 0;
    int pageIndex = 0;
    while (offset + 27 <= ogg.size()) {
        if (std::memcmp(ogg.data() + offset, "OggS", 4) != 0) {
            lockFail("Audio BGM Transactional (#87): OggS sayfa yuruyusu bozuldu");
        }
        const size_t segCount = ogg[offset + 26];
        if (offset + 27 + segCount > ogg.size()) {
            lockFail("Audio BGM Transactional (#87): kesik segment tablosu");
        }
        size_t body = 0;
        for (size_t i = 0; i < segCount; ++i) body += ogg[offset + 27 + i];
        if (offset + 27 + segCount + body > ogg.size()) {
            lockFail("Audio BGM Transactional (#87): kesik sayfa govdesi");
        }
        if (pageIndex == 1) {
            if (body == 0) {
                lockFail("Audio BGM Transactional (#87): comment sayfasi govdesiz");
            }
            ogg[offset + 27 + segCount] ^= 0xFFu; // paket tipi 0x03 -> gecersiz
            return ogg;
        }
        offset += 27 + segCount + body;
        ++pageIndex;
    }
    lockFail("Audio BGM Transactional (#87): 2. OGG sayfasi yok");
}

void setupTransactionalProject(Rowl::VFS::VFSManager& vfs, Rowl::Audio::AudioEngine& audio) {
    setupMissGuardProject(vfs, audio);
    const auto dir = std::filesystem::temp_directory_path() /
                     "rowl_audio_bgm_miss_guard_project" / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    writeBytes(dir / "miss_openfail.ogg", transactionalOpenFailOgg());
    const auto wav = makeFloatWavMono44100(std::vector<float>(64, 0.5f));
    writeBytes(dir / "t_bgm_a.wav", wav);
    writeBytes(dir / "t_bgm_b.wav", wav);
    vfs.remountProject((std::filesystem::temp_directory_path() /
                        "rowl_audio_bgm_miss_guard_project")
                           .string());
}

} // namespace

void test_audio_lock_bgm_transactional() {
    TEST_SECTION("Audio BGM Transactional Commit (#87)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupTransactionalProject(vfs, audio);

    // (d) no-device existence: yalnız gerçek-cihazsız koşuda çalışır
    // (requireAudioDeviceOrSkip YOK; cihazlı koşuda açık SKIP notu).
    if (!audio.isAudioDeviceAvailable()) {
        audio.playAudio("audio/t_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_a.wav") {
            lockFail("sessiz-yedek BGM intent'i kurulamadi");
        }
        if (audio.streamInfoJson().find("\"asset\":\"audio/t_bgm_a.wav\"") == std::string::npos) {
            lockFail("sessiz-yedek snapshot kurulamadi: " + audio.streamInfoJson());
        }
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_a.wav") {
            lockFail("sessiz-yedek miss intent'i yikti (predecessor korunmadi)");
        }
        if (audio.streamInfoJson().find("\"asset\":\"audio/t_bgm_a.wav\"") == std::string::npos) {
            lockFail("sessiz-yedek miss snapshot'i yikti: " + audio.streamInfoJson());
        }
        if (audio.getLastError().empty()) {
            lockFail("sessiz-yedek miss hatasi caller'a ulasmadi");
        }
        TEST_PASS("Audio Transactional — sessiz-yedek miss intent+snapshot'i korur (d)");
    } else {
        std::cout << "  SKIP Audio Transactional (d) no-device existence"
                     " (cihaz mevcut; sessiz dal erisilemez)"
                  << std::endl;
    }
    if (!requireAudioDeviceOrSkip(audio, "Audio BGM Transactional")) return;

    auto requireErrorSet = [&](const std::string& context) {
        if (audio.getLastError().empty()) {
            lockFail(context + ": fail caller'a ulasmadi (m_lastError bos)");
        }
    };

    // Kurulum: geçerli streaming BGM.
    audio.playAudio("audio/miss_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    requireMissGuardIntact(audio, "kurulum/miss_bgm.ogg");
    TEST_PASS("Audio Transactional — kurulum (streaming BGM intact)");

    // (a) decode-fail sonrası predecessor sağ: corrupt + eksik dosya.
    audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "corrupt OGG sonrasi (a)");
    requireErrorSet("corrupt OGG sonrasi (a)");
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "eksik dosya sonrasi (a)");
    requireErrorSet("eksik dosya sonrasi (a)");
    TEST_PASS("Audio Transactional — decode-fail predecessor'i korur (a)");

    // (b) streaming open-fail: probe geçer ama source->open düşer.
    audio.playAudio("audio/miss_openfail.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "open-fail sonrasi (b)");
    {
        const std::string err = audio.getLastError();
        if (err.find("could not be opened") == std::string::npos) {
            lockFail("open-fail acilis adiminda dusmedi, baska yola saptı (m_lastError): '" + err + "'");
        }
    }
    TEST_PASS("Audio Transactional — streaming open-fail predecessor'i korur (b)");

    // (e-fail) transition intent: başarısız yeni BGM transition başlatmaz.
    audio.playBgm("audio/does_not_exist.wav", Rowl::Audio::BgmTransitionKind::Fade, 5.0f);
    if (audio.isBgmTransitionActive()) {
        lockFail("basarisiz BGM transition baslatti (intent kirliligi)");
    }
    requireMissGuardIntact(audio, "basarisiz transition sonrasi (e)");
    requireErrorSet("basarisiz transition sonrasi (e)");
    TEST_PASS("Audio Transactional — basarisiz BGM transition baslatmaz (e)");

    // (e-ok) başarılı transition regresyon bekçisi: swap aynen çalışır.
    audio.playAudio("audio/t_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_a.wav") {
        lockFail("RAM BGM kurulumu commitlenmedi");
    }
    audio.playBgm("audio/t_bgm_b.wav", Rowl::Audio::BgmTransitionKind::Fade, 0.5f);
    if (!audio.isBgmTransitionActive()) {
        lockFail("gecerli Fade transition baslamadi (regresyon)");
    }
    // Sozlesme (test_audio_engine.cpp Transition Queueing kilidiyle ayni):
    // intent commit'te yeni parcaya gecer, fiziksel swap update ile tamamlanir.
    if (audio.getCurrentBgmPath() != "audio/t_bgm_b.wav") {
        lockFail("transition intent commitlenmedi (regresyon)");
    }
    audio.update(1.0f);
    if (audio.isBgmTransitionActive()) {
        lockFail("transition suresi bitmesine ragmen swap olmadi (regresyon)");
    }
    if (audio.getCurrentBgmPath() != "audio/t_bgm_b.wav") {
        lockFail("transition swap commitlenmedi (regresyon)");
    }
    TEST_PASS("Audio Transactional — basarili transition swap aynen (e regresyon bekcisi)");

    // (c) queue-fail: suspend altında RAM predecessor + fail-next enjeksiyonu.
    audio.setOutputSuspended(true);
    audio.playAudio("audio/t_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t bgmBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (bgmBefore == 0) {
        lockFail("BGM kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/t_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != bgmBefore) {
        lockFail("BGM queue-fail eski kuyrugu yikti");
    }
    if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_b.wav") {
        lockFail("BGM queue-fail intent'i yikti (predecessor korunmadi)");
    }
    {
        const std::string err = audio.getLastError();
        if (err.find("Unable to queue decoded audio") == std::string::npos) {
            lockFail("BGM queue-fail caller'a ulasmadi (m_lastError): '" + err + "'");
        }
    }
    TEST_PASS("Audio Transactional — queue-fail kuyruk+intent'i korur (c)");
    audio.setOutputSuspended(false);

    // (f) ayrışma: explicit stopBgm() hâlâ kapatır.
    audio.stopBgm();
    if (audio.isBgmPlaying() || audio.isStreaming() || !audio.getCurrentBgmPath().empty()) {
        lockFail("explicit stopBgm() kapatmadi (koruma stop'u bozmamali)");
    }
    TEST_PASS("Audio Transactional — explicit stopBgm() hâlâ kapatir (f)");

    audio.stopAll();
    audio.shutdown();
}

/**
 * test_audio_lock.cpp eklentisi — BGM Cap Fail-Closed (#87) KİLİDİ.
 *
 * KILIT (mutant oldurur): playAudio RAM yolundaki uc cap/fail dali da
 * fail-closed'dur — predecessor (intent + stream + snapshot) korunur, yalniz
 * m_lastError yazilir. Herhangi bir dala upfront-yikim mutantı
 * (closeBgmStream + resetStreamInfoNoBgm) eklenirse asagidaki adimlardan
 * biri exit(1) ile duser:
 *  - encoded-cap (~686): 65 MiB sparse WAV (128 MiB loose-alti, 64 MiB
 *    encoded-ustu). Bu dalin mutantı yesil birakan delikti (2. tur kirmizi
 *    kapi sondasi: isStreaming==false + mode!=stream).
 *  - decoded-cap (OGG cozumu): 200-zincirli OGG (~1.8 MiB dosya, ~70 MiB
 *    cozulmus S16). Zincirler ayni serial ile birlesirse decoder "corrupt"
 *    der; her zincir benzersiz serial + onarilmis CRC tasir (yapi gecerli,
 *    karar memory — probe ilk/son-halka suresini gorur, threshold-alti).
 *  - convert-fail (~718): 9-kanal tiny float WAV. SDL yukler ama
 *    SDL_ConvertAudioSamples reddeder ("src_spec->channels is invalid").
 *
 * Gozlem (bu dosyadaki desen aynen): TEST_SECTION/TEST_PASS + hata=exit(1);
 * timing-assert/sleep/poll YOK; requireMissGuardIntact 7-iddia (kurulum
 * streaming BGM aynen); hata-mesaji dal-kaniti (yanlis dala sapma yakalanir).
 * Cihaz bagimliligi: RAM yolu cihaza baglidir; cihazsiz kosuda acik SKIP
 * (requireAudioDeviceOrSkip aynen).
 *
 * KAPSAM-DISI (savunma-derinligi, erisilemez dal): WAV decoded-cap (~704,
 * "Decoded audio exceeds") bu kilit disindadir. Uncompressed WAV'de
 * encoded~=decoded+44 oldugundan 704'u tetikleyecek girdi once encoded-cap
 * (~686) duser; 704'e ayirt-edici erisim yoktur (OGG-ici decoded-cap
 * yukarida kapsanir). Bu dala upfront-yikim iadesi suite'i yesil birakir
 * (V5 saman-adam); dal govdede korunur ama kilitlenmez.
 */
namespace {

// 65 MiB sparse WAV basligi (IEEE-float mono 44100): data chunk iddiasi
// 65 MiB-44 bayttir; dosya resize_file ile seyrek sisme yapar (4 KiB disk,
// VFS okumasi 65 MiB — hizli, page-cache).
std::vector<uint8_t> capOversizeWavHeader() {
    static constexpr uint64_t kTotal = 65ULL * 1024 * 1024;
    static constexpr uint32_t kData = static_cast<uint32_t>(kTotal - 44);
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(out, 36u + kData);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32LE(out, 16u);
    appendU16LE(out, 3u); // IEEE float
    appendU16LE(out, 1u); // mono
    appendU32LE(out, 44100u);
    appendU32LE(out, 44100u * 4u);
    appendU16LE(out, 4u);
    appendU16LE(out, 32u);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32LE(out, kData);
    return out;
}

void writeSparseOversizeWav(const std::filesystem::path& path) {
    static constexpr uint64_t kTotal = 65ULL * 1024 * 1024;
    const std::vector<uint8_t> header = capOversizeWavHeader();
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            lockFail("Audio BGM Cap Fail-Closed (#87): oversize WAV acilamadi");
        }
        out.write(reinterpret_cast<const char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
        out.close();
        if (!out) {
            lockFail("Audio BGM Cap Fail-Closed (#87): oversize WAV basligi yazilamadi");
        }
    }
    std::error_code ec;
    std::filesystem::resize_file(path, kTotal, ec);
    if (ec) {
        lockFail("Audio BGM Cap Fail-Closed (#87): oversize WAV seyrek sisirme basarisiz: " +
                 ec.message());
    }
}

// Zincirli OGG decode-bombasi: ayni ton 200 kez uclanir. Her halka benzersiz
// serial + onarilmis sayfa-CRC tasir (missGuardOggPageCrc aynen), yoksa
// decoder zinciri "corrupt" sayar. Dosya ~1.8 MiB (cap-alti), cozulmus S16
// ~70 MiB (cap-ustu): decodeOggVorbis "Decoded Ogg/Vorbis audio exceeds the
// maximum accepted size" ile duser. Probe halka-suresini gorur
// (threshold-alti, memory-rotasi) — akis-taahhudu degil RAM cozumu isler.
std::vector<uint8_t> capChainedDecodeBombOgg() {
    const std::vector<uint8_t> tone = missGuardLongToneOggBytes();
    uint32_t baseSerial = static_cast<uint32_t>(tone[14]) |
                          (static_cast<uint32_t>(tone[15]) << 8) |
                          (static_cast<uint32_t>(tone[16]) << 16) |
                          (static_cast<uint32_t>(tone[17]) << 24);
    std::vector<uint8_t> out;
    out.reserve(tone.size() * 200);
    for (int link = 0; link < 200; ++link) {
        std::vector<uint8_t> copy = tone;
        size_t offset = 0;
        while (offset + 27 <= copy.size()) {
            if (std::memcmp(copy.data() + offset, "OggS", 4) != 0) {
                lockFail("Audio BGM Cap Fail-Closed (#87): zincir sayfa yuruyusu bozuldu");
            }
            const size_t segCount = copy[offset + 26];
            if (offset + 27 + segCount > copy.size()) {
                lockFail("Audio BGM Cap Fail-Closed (#87): zincir kesik segment tablosu");
            }
            size_t body = 0;
            for (size_t i = 0; i < segCount; ++i) body += copy[offset + 27 + i];
            const size_t pageEnd = offset + 27 + segCount + body;
            if (pageEnd > copy.size()) {
                lockFail("Audio BGM Cap Fail-Closed (#87): zincir kesik sayfa govdesi");
            }
            const uint32_t serial = baseSerial + static_cast<uint32_t>(link) + 1u;
            copy[offset + 14] = static_cast<uint8_t>(serial & 0xFFu);
            copy[offset + 15] = static_cast<uint8_t>((serial >> 8) & 0xFFu);
            copy[offset + 16] = static_cast<uint8_t>((serial >> 16) & 0xFFu);
            copy[offset + 17] = static_cast<uint8_t>((serial >> 24) & 0xFFu);
            copy[offset + 22] = 0;
            copy[offset + 23] = 0;
            copy[offset + 24] = 0;
            copy[offset + 25] = 0;
            const uint32_t crc =
                missGuardOggPageCrc(copy.data() + offset, pageEnd - offset);
            copy[offset + 22] = static_cast<uint8_t>(crc & 0xFFu);
            copy[offset + 23] = static_cast<uint8_t>((crc >> 8) & 0xFFu);
            copy[offset + 24] = static_cast<uint8_t>((crc >> 16) & 0xFFu);
            copy[offset + 25] = static_cast<uint8_t>((crc >> 24) & 0xFFu);
            offset = pageEnd;
        }
        if (offset != copy.size()) {
            lockFail("Audio BGM Cap Fail-Closed (#87): zincir artigi kaldi");
        }
        out.insert(out.end(), copy.begin(), copy.end());
    }
    return out;
}

// Convert-hostile tiny WAV: 9-kanal float (SDL yukler, ConvertAudioSamples
// "src_spec->channels is invalid" ile reddeder). 64 frame, bayt-kucuk.
std::vector<uint8_t> capNineChannelWav() {
    static constexpr uint16_t kChannels = 9;
    static constexpr uint32_t kFrames = 64;
    const uint32_t dataBytes = kFrames * kChannels * sizeof(float);
    std::vector<uint8_t> out;
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    appendU32LE(out, 36u + dataBytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E'});
    out.insert(out.end(), {'f', 'm', 't', ' '});
    appendU32LE(out, 16u);
    appendU16LE(out, 3u); // IEEE float
    appendU16LE(out, kChannels);
    appendU32LE(out, 44100u);
    appendU32LE(out, 44100u * kChannels * 4u);
    appendU16LE(out, static_cast<uint16_t>(kChannels * 4u));
    appendU16LE(out, 32u);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    appendU32LE(out, dataBytes);
    for (uint32_t i = 0; i < kFrames * kChannels; ++i) appendFloatLE(out, 0.5f);
    return out;
}

} // namespace

void test_audio_lock_bgm_cap_fail_closed() {
    TEST_SECTION("Audio BGM Cap Fail-Closed (#87)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupTransactionalProject(vfs, audio);
    const auto dir = std::filesystem::temp_directory_path() /
                     "rowl_audio_bgm_miss_guard_project" / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    writeSparseOversizeWav(dir / "cap_oversize_65m.wav");
    writeBytes(dir / "cap_chain200.ogg", capChainedDecodeBombOgg());
    writeBytes(dir / "cap_ch9.wav", capNineChannelWav());

    if (!requireAudioDeviceOrSkip(audio, "Audio BGM Cap Fail-Closed")) return;

    auto requireErrorContains = [&](const std::string& needle, const std::string& context) {
        const std::string err = audio.getLastError();
        if (err.find(needle) == std::string::npos) {
            lockFail(context + ": fail baska yola sapti (m_lastError): '" + err + "'");
        }
    };

    // Kurulum: gecerli streaming BGM (7-iddiali bekci aynen).
    audio.playAudio("audio/miss_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    requireMissGuardIntact(audio, "kurulum/miss_bgm.ogg");
    TEST_PASS("Audio Cap Fail-Closed — kurulum (streaming BGM intact)");

    // encoded-cap: 65 MiB sparse WAV reddi predecessor'i korur.
    audio.playAudio("audio/cap_oversize_65m.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "encoded-cap sonrasi");
    requireErrorContains("Audio file exceeds the maximum accepted size",
                         "encoded-cap sonrasi");
    TEST_PASS("Audio Cap Fail-Closed — encoded-cap predecessor'i korur");

    // decoded-cap: zincir-cozum tasmasi predecessor'i korur.
    audio.playAudio("audio/cap_chain200.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "decoded-cap sonrasi");
    requireErrorContains("Decoded Ogg/Vorbis audio exceeds the maximum accepted size",
                         "decoded-cap sonrasi");
    TEST_PASS("Audio Cap Fail-Closed — decoded-cap predecessor'i korur");

    // convert-fail: 9-kanal ceviri reddi predecessor'i korur.
    audio.playAudio("audio/cap_ch9.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireMissGuardIntact(audio, "convert-fail sonrasi");
    requireErrorContains("Unable to convert decoded audio",
                         "convert-fail sonrasi");
    TEST_PASS("Audio Cap Fail-Closed — convert-fail predecessor'i korur");

    audio.stopAll();
    audio.shutdown();
}
