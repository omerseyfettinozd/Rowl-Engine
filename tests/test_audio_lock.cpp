/**
 * test_audio_lock.cpp — Audio DSP guard locks (#77).
 *
 * KILIT (mutant oldurur) vs GOZCU (watch; oldurmez, davranis bekcisi):
 *  - KILIT: sanitize-NaN (Telephone/CaveReverb/Underwater/Normal + zehir-fixture),
 *    clamp-ust-bound (Underwater/Telephone/CaveReverb + sicak-DC fixture,
 *    peak<=1.0), Telephone clamp-vurus bandi (sicak-DC, ~1.0: sabit-0.5
 *    cikti + kazanc-0.5/0.3 dususlerini oldurur) ve kalibrasyon kilidi
 *    (Telephone + dogrusal-bolge 0.1f-DC fixture, hp*2.1 dar bant:
 *    kazanc-1.0/0.5/0.3 sessiz-gecislerini oldurur). Ilgili govde
 *    satiri (sanitize dongusu / dal clamp'i / hp kazanci) silinirse veya
 *    sessizce degisirse mandal kirmiziya duser.
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
 * dogrudan calistirilmaz. Normal dali da sanitize'den gecer (erken-donus
 * sanitize SONRASIDIR): zehir-fixture sifirlanmis sonlu mandal birakir,
 * kilit requireLivePeak ile kilitlenir.
 *
 * NaN-yapiskan mandal (madde 4): tek bir NaN cikis ornegi mandali NaN
 * yapar ve sonraki sonlu ornekler onu temizlemez; boylece CaveReverb
 * kolundaki seyrek NaN (gecikme hatti 5512 frame >> 64-ornek fixture,
 * sonlu clamp komsulari arasinda kaybolurdu) da dusurur.
 *
 * Desen: TEST_SECTION/TEST_PASS + hata=exit(1); timing-assert YOKTUR.
 */
#include "rowl_test_harness.hpp"

#include <nlohmann/json.hpp>
#include <sstream>

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
    // Dogrusal-bolge fixture (#77-tur2): +/-0.1f DC, 256 ornek. Sicak DC
    // clamp'i doyurur (kazanc degisimi gorunmez olur); bu fixture'da clamp
    // doymaz, mandal Telephone hp*2.1 adim-tepkisidir (~0.0756).
    writeBytes(dir / "lock_lin_pos.wav",
               makeFloatWavMono44100(std::vector<float>(256, 0.1f)));
    writeBytes(dir / "lock_lin_neg.wav",
               makeFloatWavMono44100(std::vector<float>(256, -0.1f)));
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

void requireClampUpperBound(float peak, const std::string& context) {
    // Fail-closed: NaN da dusurur (NaN <= 1.0f yanlistir).
    if (!(peak <= 1.0f)) {
        lockFail(context + ": clamp tasti (mandal >1.0f veya NaN)");
    }
}

// Dala-ozel clamp-vurus bandi (Telephone + sicak DC, #77-tur2): hp*2.1
// gecici-tepesi clamp'e carpar, mandal ~1.0 olur. Sabit-0.5 cikti (0.5),
// kazanc-0.5 (0.9) ve kazanc-0.3 (0.54) mutantlari burada duser;
// kazanc-1.0 (1.8 clamp'e carpar, ~1.0) dogrusal-bantta duser.
// Fail-closed: NaN/sifir da dusurur.
void requireTelephoneClampHit(float peak, const std::string& context) {
    requireClampUpperBound(peak, context);
    if (!(peak >= 0.95f)) {
        lockFail(context + ": Telephone clamp vurusuna ulasamadi "
                            "(beklenen ~1.0; sabit-0.5 cikti / kazanc dususu?)");
    }
}

// Dogrusal-bolge kazanc bandi (Telephone + 0.1f DC, #77-tur2 kalibrasyon
// kilidi): clamp doymaz, mandal hp*2.1 adim-tepkisidir (OLCULEN deger
// asagida pinlidir). Dar tolerans kazanc-1.0/0.5/0.3 sessiz-gecislerini,
// sabit-0.5 ciktiyi (0.5) ve olu DSP'yi (0.0) oldurur. 2.1->1.8 mesru
// retune AYRI kalibrasyon bandi ister (bu bant o zaman kayar); eski >=0.5
// leniensi ("mesru retune gecer") clamp kilidinden cikarilmistir.
// Fail-closed: NaN da dusurur.
void requireTelephoneLinearGain(float peak, const std::string& context) {
    // OLCULEN (2026-09-20, dummy-cihaz, dogru kazanc 2.1): +/-0.1f DC
    // Telephone mandali = 0.0756 (iki isarette ayni). Bant ~= +/-7%:
    // kazanc-1.8 (0.0648) dahil tum sessiz dususler disarida kalir.
    static constexpr float kLinLo = 0.070f;
    static constexpr float kLinHi = 0.081f;
    if (!(peak >= kLinLo && peak <= kLinHi)) {
        lockFail(context + ": Telephone dogrusal kazanc bandi disinda "
                            "(hp*2.1 kalibrasyon bandi; sessiz kazanc-gecisi?)");
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

    // Normal dal KILIDI: sanitize Normal erken-donus ONCESINDE calisir,
    // o yuzden zehir-fixture sifirlanmis sonlu mandal birakir (sonlu
    // sinyal bit-bit aynidir, yalniz non-finite sifirlanir). requireLivePeak
    // fail-closed'dur: sanitize silinirse mandal NaN/0.0f kalir, exit(1).
    audio.playAudio("audio/lock_nan.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Normal);
    requireLivePeak(audio, "lock_nan/Normal",
                    "NaN sanitize'den gecti",
                    "zehir tamponu oldurdu — sanitize sonrasi sessizlik");
    TEST_PASS("Audio Sanitize Lock — Normal (NaN/+Inf/-Inf, erken-donus sanitize sonrasi)");

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

    // Pozitif DC: clamp kaldirilirsa mandal ~5.0 olur (B1 DUSER). Canlilik
    // alt boundu (#77-tur2) DC fixture'dan ayrildi: burada yalniz ust bound
    // (tasmak yok); canlilik sanitize kilidi + dogrusal-bantta kilitlidir.
    audio.playAudio("audio/lock_dc_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    requireClampUpperBound(audio.testLastDspPeak(), "lock_dc_pos/UnderwaterLowPass");
    TEST_PASS("Audio Clamp Lock — Underwater +5.0f DC (tasmak yok)");

    // Negatif varyant: isaret-simetrisi (fabs uzerinden ayni bound).
    audio.playAudio("audio/lock_dc_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::UnderwaterLowPass);
    requireClampUpperBound(audio.testLastDspPeak(), "lock_dc_neg/UnderwaterLowPass");
    TEST_PASS("Audio Clamp Lock — Underwater -5.0f DC (isaret-simetrisi)");

    // Sicak-sinyal ortusu (madde 2): ayni DC fixture'lar Telephone VE
    // CaveReverb ile de calinir. Telephone'da DC gecisi (hp*2.1) clamp'siz
    // ~3.78'e firlar; CaveReverb'de (input+delayed*0.28) clamp'siz ~5.0'e
    // firlar — iki dalin clamp silmeleri de kirmiziya duser. Telephone
    // dala-ozel clamp-vurus bandiyla (~1.0) kilitlenir: sabit-0.5 cikti
    // (0.5) ve kazanc-0.5/0.3 dususleri burada olur.
    audio.playAudio("audio/lock_dc_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireTelephoneClampHit(audio.testLastDspPeak(), "lock_dc_pos/Telephone");
    TEST_PASS("Audio Clamp Lock — Telephone +5.0f DC (clamp vurus ~1.0)");

    audio.playAudio("audio/lock_dc_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireTelephoneClampHit(audio.testLastDspPeak(), "lock_dc_neg/Telephone");
    TEST_PASS("Audio Clamp Lock — Telephone -5.0f DC (isaret-simetrisi)");

    audio.playAudio("audio/lock_dc_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::CaveReverb);
    requireClampUpperBound(audio.testLastDspPeak(), "lock_dc_pos/CaveReverb");
    TEST_PASS("Audio Clamp Lock — CaveReverb +5.0f DC (sicak-sinyal ortusu)");

    audio.playAudio("audio/lock_dc_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::CaveReverb);
    requireClampUpperBound(audio.testLastDspPeak(), "lock_dc_neg/CaveReverb");
    TEST_PASS("Audio Clamp Lock — CaveReverb -5.0f DC (isaret-simetrisi)");

    // Kalibrasyon kilidi (ayri blok, #77-tur2): dogrusal-bolge fixture
    // (+/-0.1f DC) Telephone ile calinir; clamp doymaz, mandal hp*2.1
    // adim-tepkisidir. Dar bant kazanc-1.0/0.5/0.3 sessiz-gecislerini,
    // sabit-0.5 ciktiyi ve olu DSP'yi oldurur; dogru kazancta (2.1) YESIL
    // kalir. 1.8 retune ayri kalibrasyon bandi ister (bu bant kayar).
    audio.playAudio("audio/lock_lin_pos.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireTelephoneLinearGain(audio.testLastDspPeak(), "lock_lin_pos/Telephone");
    TEST_PASS("Audio Kalibrasyon Kilidi — Telephone +0.1f DC (hp*2.1 dar bant)");

    audio.playAudio("audio/lock_lin_neg.wav", Rowl::Audio::AudioChannelType::Bgm,
                    Rowl::Audio::DSPFilterType::Telephone);
    requireTelephoneLinearGain(audio.testLastDspPeak(), "lock_lin_neg/Telephone");
    TEST_PASS("Audio Kalibrasyon Kilidi — Telephone -0.1f DC (isaret-simetrisi)");
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
 * Gozlem (yedi bagimsiz kontrol a..g, requireMissGuardIntact hepsini cagirir,
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

// Yedi iddia yedi bagimsiz noktada (#78-tur3; once tek guard'daydi):
// (a) intent (isBgmPlaying), (b) stream nesnesi (isStreaming),
// (c) snapshot mode=="stream", (d) reason=="over_threshold" (no_bgm DEGIL),
// (e) asset==kurulum BGM'i (miss dosyasi DEGIL),
// (f) channel==0/Bgm (2/Sfx DEGIL), (g) m_currentBgmPath==kurulum BGM'i
// (bos DEGIL). M2 varyanti (mode stream kalirken reason=no_bgm +
// asset=miss dosyasi + channel=2 + path bos) bu dort ek iddia ile FAIL
// verir (exit 1); yalniz mode bakmak onu oldurmez.
// Her kontrol yalnizca kendi alanini okur; tek-alan mutantini yalnizca
// o alanin kontrolu oldurur. Her kontrol basarida bir kez TEST_PASS yazar
// (static once-flag; guard ~17 kez cagrilir, log sismesin).
static const std::string kMissGuardExpectedBgm = "audio/miss_bgm.ogg";

void requireMissGuardIntent(Rowl::Audio::AudioEngine& audio, const std::string& context) {
    if (!audio.isBgmPlaying()) {
        lockFail(context + ": BGM intent dustu (isBgmPlaying false)");
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (a) intent (isBgmPlaying)");
    }
}

void requireMissGuardStreamObject(Rowl::Audio::AudioEngine& audio,
                                  const std::string& context) {
    if (!audio.isStreaming()) {
        lockFail(context + ": stream nesnesi kapandi (isStreaming false)");
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (b) stream nesnesi (isStreaming)");
    }
}

void requireMissGuardSnapshotMode(Rowl::Audio::AudioEngine& audio,
                                  const std::string& context) {
    const std::string json = audio.streamInfoJson();
    if (json.find("\"mode\":\"stream\"") == std::string::npos) {
        lockFail(context + ": snapshot yalana dustu (mode!=stream): " + json);
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (c) snapshot mode==stream");
    }
}

void requireMissGuardSnapshotReason(Rowl::Audio::AudioEngine& audio,
                                    const std::string& context) {
    const std::string json = audio.streamInfoJson();
    if (json.find("\"reason\":\"over_threshold\"") == std::string::npos) {
        lockFail(context + ": snapshot reason yalani (over_threshold degil): " + json);
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (d) snapshot reason==over_threshold");
    }
}

void requireMissGuardSnapshotAsset(Rowl::Audio::AudioEngine& audio,
                                   const std::string& context) {
    const std::string json = audio.streamInfoJson();
    if (json.find("\"asset\":\"" + kMissGuardExpectedBgm + "\"") == std::string::npos) {
        lockFail(context + ": snapshot asset yalani (kurulum BGM degil): " + json);
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (e) snapshot asset==kurulum BGM'i");
    }
}

void requireMissGuardSnapshotChannel(Rowl::Audio::AudioEngine& audio,
                                     const std::string& context) {
    const std::string json = audio.streamInfoJson();
    if (json.find("\"channel\":0") == std::string::npos) {
        lockFail(context + ": snapshot channel yalani (0/Bgm degil): " + json);
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (f) snapshot channel==0/Bgm");
    }
}

void requireMissGuardCurrentPath(Rowl::Audio::AudioEngine& audio,
                                 const std::string& context) {
    if (audio.getCurrentBgmPath() != kMissGuardExpectedBgm) {
        lockFail(context + ": BGM path yalani (bos/yanlis): '" +
                 audio.getCurrentBgmPath() + "'");
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (g) path==kurulum BGM'i");
    }
}

void requireMissGuardIntact(Rowl::Audio::AudioEngine& audio, const std::string& context) {
    requireMissGuardIntent(audio, context);
    requireMissGuardStreamObject(audio, context);
    requireMissGuardSnapshotMode(audio, context);
    requireMissGuardSnapshotReason(audio, context);
    requireMissGuardSnapshotAsset(audio, context);
    requireMissGuardSnapshotChannel(audio, context);
    requireMissGuardCurrentPath(audio, context);
}

// #78-2.tur kurulum ilerleme kaniti (#78 baglami, kendi fixture'i):
// akan predecessor'in konumu vardir (buffered_seconds>0). pcmPos
// sifirlayan mutantin oldurme gucu buna dayanir: sifir tabanda sifirlama
// gozlenemezdi, o yuzden "json esit" degil "json esit (ilerlemis bazda)"
// istenir.
std::string missGuardStreamingProgressJson(Rowl::Audio::AudioEngine& audio,
                                           const std::string& context) {
    const std::string json = audio.streamInfoJson();
    const auto parsed = nlohmann::json::parse(json);
    const double buffered = parsed.value("buffered_seconds", 0.0);
    if (!(buffered > 0.0)) {
        lockFail(context + ": kurulum akisi ilerlemedi (buffered_seconds==0): " + json);
    }
    return json;
}

// #78-2.tur veri-duzlemi dondurma gozlemi (KENDI helper'i;
// #87 requireBgmPredecessorFrozen KULLANILMAZ — kilitler bagimsiz kalir):
//  - kuyruk delta-0: testQueuedBytes(Bgm) tam-esitlik (q0>0 bazli —
//    bos kuyrukta bosaltma gozlenemezdi; sessiz dalda #87-d kurali gecerlidir:
//    sessiz dal hic kuyruklamaz, orada literal 0 aranir).
//  - snapshot birebirlik: streamInfoJson string tam-esitlik
//    (buffered_seconds dahil — akan predecessor'in konumu bozulmaz;
//    pcmPos sifirlayan mutant json'u degistirir, duser).
//  - kaynak canliligi: BGM stream kaynagi fail sonrasi acik kalir
//    (testBgmStreamSourceOpen — isStreaming bayragi DEGIL, kaynagin kendisi;
//    fail yoluna sizmis bir close() bayragi yesil birakir, bu gozlem oldurur).
void requireMissGuardDataFrozen(Rowl::Audio::AudioEngine& audio, size_t queuedBefore,
                                const std::string& jsonBefore,
                                const std::string& context) {
    const size_t queuedAfter =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (queuedAfter != queuedBefore) {
        lockFail(context + ": fail BGM kuyruguna dokundu (once=" + std::to_string(queuedBefore) +
                 " sonra=" + std::to_string(queuedAfter) + ")");
    }
    const std::string jsonAfter = audio.streamInfoJson();
    if (jsonAfter != jsonBefore) {
        lockFail(context + ": fail akis konumunu/snapshot'i bozdu (once='" + jsonBefore +
                 "' sonra='" + jsonAfter + "')");
    }
    if (!audio.testBgmStreamSourceOpen()) {
        lockFail(context + ": fail BGM stream kaynagini kapatti (kaynak olu, bayrak sag)");
    }
    static bool once = false;
    if (!once) {
        once = true;
        TEST_PASS("Audio BGM Miss Guard — kontrol (h) veri-duzlemi donmus (kuyruk+json+kaynak)");
    }
}

} // namespace

void test_audio_lock_bgm_miss_guard() {
    TEST_SECTION("Audio BGM Miss Guard (#78)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupMissGuardProject(vfs, audio);

    // Sessiz dal (#87-d kurali #78 fixture'ina uyarlanmis — #87
    // literal'leri KOPYALANMAZ): zorlanmis-cihazsiz kosuda sessiz-yedek BGM
    // niyeti kurulur; #78 miss varyantlari (non-BGM + BGM) niyeti ve
    // snapshot'i yikmaz, kuyruk literal 0 kalir (sessiz dal hic kuyruklamaz),
    // BGM miss hatasi caller'a ulasir. requireAudioDeviceOrSkip ONCESI
    // calisir (gercek-cihazsiz kosuda da gecerlidir).
    {
        const bool realDevice = audio.isAudioDeviceAvailable();
        audio.testSetDeviceAvailable(false);
        audio.playAudio("audio/miss_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
        if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/miss_bgm.ogg") {
            lockFail("sessiz-yedek BGM niyeti kurulamadi");
        }
        if (audio.streamInfoJson().find("\"asset\":\"audio/miss_bgm.ogg\"") == std::string::npos) {
            lockFail("sessiz-yedek snapshot kurulamadi: " + audio.streamInfoJson());
        }
        if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != 0) {
            lockFail("sessiz-yedek cihaz kuyruguna yazdi (sessiz dal kuyruklamaz)");
        }
        const std::string silentJson = audio.streamInfoJson();
        // #78 miss varyanti (non-BGM, sessiz): kayda bile gecmez, niyet aynen.
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Sfx);
        if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/miss_bgm.ogg") {
            lockFail("sessiz non-BGM miss niyeti yikti (predecessor korunmadi)");
        }
        if (audio.streamInfoJson() != silentJson) {
            lockFail("sessiz non-BGM miss snapshot'i yikti: " + audio.streamInfoJson());
        }
        if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != 0) {
            lockFail("sessiz non-BGM miss kuyrukladi (fail yolu kuyruklamaz)");
        }
        // Sessiz BGM miss: existence-gate hata + return, niyet aynen.
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/miss_bgm.ogg") {
            lockFail("sessiz BGM miss niyeti yikti (predecessor korunmadi)");
        }
        if (audio.streamInfoJson() != silentJson) {
            lockFail("sessiz BGM miss snapshot'i yikti: " + audio.streamInfoJson());
        }
        if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != 0) {
            lockFail("sessiz BGM miss kuyrukladi (fail yolu kuyruklamaz)");
        }
        if (audio.getLastError().empty()) {
            lockFail("sessiz BGM miss hatasi caller'a ulasmadi");
        }
        audio.testSetDeviceAvailable(realDevice);
        if (audio.isAudioDeviceAvailable() != realDevice) {
            lockFail("kanca donusu cihaz bayragini bozdurdu");
        }
        TEST_PASS("Audio BGM Miss Guard — sessiz dal miss niyet+snapshot'i korur, kuyruk 0 (#87-d kurali)");
    }

    if (!requireAudioDeviceOrSkip(audio, "Audio BGM Miss Guard")) return;

    // Her miss varyanti tek kapidan gecer (#78-2.tur): fail ONCESI tuketim
    // dondurulur (setOutputSuspended) + kuyruk bazi (q0>0) + snapshot bazi
    // (json0) alinir; fail SONRASI once 7 kontrol (requireMissGuardIntact),
    // sonra veri-duzlemi dondurma (requireMissGuardDataFrozen: kuyruk delta-0
    // + json birebir + kaynak acik) kilitlenir. Veri-duzlemi canlilik probu:
    // update bir kez surulur — kuyruk azalmaz, snapshot/konum aynen kalir
    // (dondurma altinda pump no-op'tur; deterministik, timing YOK).
    auto runMissVariant = [&](const std::string& context, auto&& failFn) {
        audio.setOutputSuspended(true);
        const size_t q0 = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
        if (q0 == 0) {
            lockFail(context + ": dondurma bazi bos (q0==0) — bosaltma-oldurme gucu yok");
        }
        const std::string json0 = audio.streamInfoJson();
        failFn();
        requireMissGuardIntact(audio, context);
        requireMissGuardDataFrozen(audio, q0, json0, context);
        audio.update();
        const size_t q1 = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
        if (q1 < q0) {
            lockFail(context + ": canlilik probu kuyruk eridi (once=" + std::to_string(q0) +
                     " sonra=" + std::to_string(q1) + ")");
        }
        if (audio.streamInfoJson() != json0) {
            lockFail(context + ": canlilik probu snapshot/konumu bozdu (once='" + json0 +
                     "' sonra='" + audio.streamInfoJson() + "')");
        }
    };

    // Adim 1: gecerli streaming BGM kurulumu.
    audio.playAudio("audio/miss_bgm.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    requireMissGuardIntact(audio, "kurulum/miss_bgm.ogg");
    // Ilerleme kaniti (buffered_seconds>0 — akis konumu var; pcmPos
    // sifirlayan mutantin oldurme gucu bu baza dayanir).
    (void)missGuardStreamingProgressJson(audio, "kurulum/miss_bgm.ogg");
    TEST_PASS("Audio BGM Miss Guard — kurulum (streaming BGM: intent+stream+snapshot)");

    // Adim 2 (katil gozlem): eksik SFX miss'i BGM'e dokunmaz.
    runMissVariant("eksik Sfx sonrasi", [&]() {
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Sfx);
    });
    TEST_PASS("Audio BGM Miss Guard — eksik SFX BGM stream/snapshot'i korur");

    // Varyant: Voice + Ui kanallarinda eksik dosya.
    runMissVariant("eksik Voice sonrasi", [&]() {
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Voice);
    });
    runMissVariant("eksik Ui sonrasi", [&]() {
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Ui);
    });
    TEST_PASS("Audio BGM Miss Guard — eksik Voice/Ui BGM stream/snapshot'i korur");

    // Varyant: decode helper uzerinden non-BGM miss (public caller'lar
    // helper'i channelIsBgm=false ile cagirir). Hem final-miss dali
    // (olmayan dosya) hem OGG-decode-fail dali (bozuk OGG) gozlenir.
    runMissVariant("eksik ambience-bed sonrasi", [&]() {
        if (audio.playAmbienceBed(0, "audio/does_not_exist.wav")) {
            lockFail("eksik bed beklenmedik basari dondu");
        }
    });
    runMissVariant("eksik crossfade sonrasi", [&]() {
        if (audio.crossfadeAmbienceTo("audio/does_not_exist.wav", 0.0f,
                                      Rowl::Audio::FadeCurve::Linear)) {
            lockFail("eksik crossfade beklenmedik basari dondu");
        }
    });
    runMissVariant("bozuk OGG bed sonrasi", [&]() {
        if (audio.playAmbienceBed(0, "audio/miss_corrupt.ogg")) {
            lockFail("bozuk OGG bed beklenmedik basari dondu");
        }
    });
    TEST_PASS("Audio BGM Miss Guard — helper-dali non-BGM miss BGM'i korur (bed/crossfade/corrupt)");

    // 4. varyant (#78-tur2, V3 katili): bozuk OGG helper disinda DOGUDAN
    // playAudio non-BGM uzerinden surulur (Sfx/Voice/Ui x corrupt OGG).
    // playAudio OGG-decode-fail dali (audio_engine.cpp ~656) Bgm-gated
    // degilse stream kapanir + snapshot yalana duser ve genis guard
    // exit(1) ile oldurur. Helper-varyanti bu dala ugramaz
    // (decodeAssetToFloatPcm ayri sitedir), o yuzden bu varyant zorunludur.
    runMissVariant("bozuk OGG dogrudan Sfx sonrasi", [&]() {
        audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Sfx);
    });
    runMissVariant("bozuk OGG dogrudan Voice sonrasi", [&]() {
        audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Voice);
    });
    runMissVariant("bozuk OGG dogrudan Ui sonrasi", [&]() {
        audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Ui);
    });
    TEST_PASS("Audio BGM Miss Guard — dogrudan playAudio non-BGM corrupt OGG BGM'i korur (Sfx/Voice/Ui)");

    // Karsit-kanit [#87 guncellemesi]: BGM kanalinda miss artik
    // predecessor-preserving'dir (eski "BGM miss kapatir" fail-closed
    // davranisi kaldirildi): stream + snapshot + intent aynen korunur,
    // hata caller'a ulasir.
    runMissVariant("BGM miss sonrasi (#87)", [&]() {
        audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    });
    if (audio.getLastError().empty()) {
        lockFail("BGM miss hatasi caller'a ulasmadi (m_lastError bos)");
    }
    TEST_PASS("Audio BGM Miss Guard — BGM miss predecessor'i korur (#87, karsit-kanit guncellemesi)");

    // Ayrisma kilidi: explicit stopBgm() hâlâ kapatir (miss korumasindan
    // ayristigi kilitlenir; stop sessiz degildir, intent duser).
    audio.setOutputSuspended(false);
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
 *   (a) iki-yonlu: hedef akışın queued-bayt değeri (testQueuedBytes) fail
 *       ONCESI snapshot'a tam eşittir (sıfırlanmamış) VE yeni assetin PCM
 *       boyuna esit DEGILDIR (yanlis-kuyruklama yakalanir; fixture'lar
 *       ayrik-boyludur: eski 64x0.5f, yeni 256x0.25f).
 *   (b) capraz: bayraklar eski asset'i gösterir (UI: isUiPlaying +
 *       getCurrentUiPath; Ambience: isAmbiencePlaying +
 *       getCurrentAmbiencePath; SFX: sfxActivePaths tek-girdi eski yol;
 *       BGM-RAM: getCurrentBgmPath eski yol) VE queued-bayt eski-boyda
 *       kalir (capraz bag: yol+bayt birlikte eskiyi gosterir).
 *   (c) m_lastError "Unable to queue decoded audio" ile doludur
 *       (queue-fail caller'a ulaştı; enjeksiyonun kuyruk adımında
 *       tüketildiğinin kanıtıdır),
 *   (d) karşıt-kanıt [#87 güncellemesi]: aynı senaryoda BGM miss
 *       predecessor-preserving'dir (stream kapanmaz — eski #78 "miss
 *       kapatır" davranışı kalktı), hata caller'a ulaşır.
 *  Ikinci kanca (testFailCommitPut, testFailNextQueue emsali): commit
 *  asamasindaki kuyruk-dususu ayni erken-noktada SDL'ye dokunmadan
 *  basarisiz sayar; Clear-sonrasi commit-Put dusus senaryosunda da
 *  queued-bytes==before korunur (atomiklik iddiasi).
 *  V3 kalibrasyon (kanca saglamlik kaniti): enjeksiyonsuz yeni asset
 *  kuyruklar, bayt DEGISIR (kanca takili kalmamis + fixture'lar ayrik).
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

    // Kanal başına ayrı-isimli geçerli WAV — fixture ayrimi: eski
    // 64x0.5f mono 44100, yeni 256x0.25f mono 44100 (boy+icerik farkli;
    // formatlar ayni oldugu icin SetFormat no-op'tur; SDL belgesi:
    // SetFormat kuyruğu flush etmez). Miss-guard projesinin dizinine
    // eklenir + tekrar remount edilir (aynı kök: streaming BGM + WAV'lar
    // tek VFS'te). Yeni assetin PCM karsiligi 256*4=1024 bayttir.
    const auto qdir = std::filesystem::temp_directory_path() /
                      "rowl_audio_bgm_miss_guard_project" / "Assets" / "audio";
    std::filesystem::create_directories(qdir);
    const auto qOldWav = makeFloatWavMono44100(std::vector<float>(64, 0.5f));
    const auto qNewWav = makeFloatWavMono44100(std::vector<float>(256, 0.25f));
    writeBytes(qdir / "q_ui.wav", qOldWav);
    writeBytes(qdir / "q_ui_new.wav", qNewWav);
    writeBytes(qdir / "q_amb.wav", qOldWav);
    writeBytes(qdir / "q_amb_new.wav", qNewWav);
    writeBytes(qdir / "q_sfx.wav", qOldWav);
    writeBytes(qdir / "q_sfx_new.wav", qNewWav);
    writeBytes(qdir / "q_bgm.wav", qOldWav);
    writeBytes(qdir / "q_bgm_new.wav", qNewWav);
    vfs.remountProject((std::filesystem::temp_directory_path() /
                        "rowl_audio_bgm_miss_guard_project")
                           .string());
    static constexpr size_t kQueueNewPcmBytes = 256 * sizeof(float);

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

    // UI kanalı (a+b+c + ikinci kanca + V3).
    audio.playAudio("audio/q_ui.wav", Rowl::Audio::AudioChannelType::Ui);
    const size_t uiBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui);
    if (uiBefore == 0) {
        lockFail("UI kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (uiBefore == kQueueNewPcmBytes) {
        lockFail("UI fixture ayrimi yok (eski==yeni boy) — 4x boy farki calismadi");
    }
    if (!audio.isUiPlaying() || audio.getCurrentUiPath() != "audio/q_ui.wav") {
        lockFail("UI kurulum bayraklari yanlis");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_ui_new.wav", Rowl::Audio::AudioChannelType::Ui);
    {
        const size_t uiAfter = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui);
        if (uiAfter != uiBefore) {
            lockFail("UI queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
        }
        if (uiAfter == kQueueNewPcmBytes) {
            lockFail("UI queue-fail yeni yuku kuyrukladi (fail yolu kuyrukladi?)");
        }
    }
    if (!audio.isUiPlaying() || audio.getCurrentUiPath() != "audio/q_ui.wav") {
        lockFail("UI queue-fail bayraklari bayatladi (yeni/eski celiskisi)");
    }
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui) != uiBefore) {
        lockFail("UI queue-fail caprazinda bayt eski-boyda degil (yol+bayt ayrismasi)");
    }
    requireQueueFailed("UI queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — UI kuyruk+bayrak korunur, hata caller'a ulaşır");
    // Ikinci kanca: commit-asamasi dususu de kuyrugu korur.
    audio.testFailCommitPut();
    audio.playAudio("audio/q_ui_new.wav", Rowl::Audio::AudioChannelType::Ui);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui) != uiBefore) {
        lockFail("UI commit-Put-dusus eski kuyrugu yok etti");
    }
    if (!audio.isUiPlaying() || audio.getCurrentUiPath() != "audio/q_ui.wav") {
        lockFail("UI commit-Put-dusus bayraklari bayatladi");
    }
    requireQueueFailed("UI commit-Put-dusus sonrasi");
    TEST_PASS("Audio Queue-Fail — UI commit-dususu kuyruk+bayrak korunur");
    // V3 kalibrasyon: enjeksiyonsuz yeni asset kuyruklar, bayt DEGISIR.
    audio.playAudio("audio/q_ui_new.wav", Rowl::Audio::AudioChannelType::Ui);
    {
        const size_t uiNew = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ui);
        if (uiNew == uiBefore) {
            lockFail("UI V3 kalibrasyon bayt degismedi (kanca takili mi / fixture ayni mi?)");
        }
        if (audio.getCurrentUiPath() != "audio/q_ui_new.wav") {
            lockFail("UI V3 kalibrasyon yeni yolu commitlemedi");
        }
    }
    TEST_PASS("Audio Queue-Fail — UI V3 kalibrasyon (enjeksiyonsuz yeni kuyruklar)");

    // Ambience kanalı (a+b+c + ikinci kanca + V3).
    audio.playAudio("audio/q_amb.wav", Rowl::Audio::AudioChannelType::Ambience);
    const size_t ambBefore =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience);
    if (ambBefore == 0) {
        lockFail("Ambience kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (ambBefore == kQueueNewPcmBytes) {
        lockFail("Ambience fixture ayrimi yok (eski==yeni boy)");
    }
    if (!audio.isAmbiencePlaying() || audio.getCurrentAmbiencePath() != "audio/q_amb.wav") {
        lockFail("Ambience kurulum bayraklari yanlis");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_amb_new.wav", Rowl::Audio::AudioChannelType::Ambience);
    {
        const size_t ambAfter =
            audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience);
        if (ambAfter != ambBefore) {
            lockFail("Ambience queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
        }
        if (ambAfter == kQueueNewPcmBytes) {
            lockFail("Ambience queue-fail yeni yuku kuyrukladi");
        }
    }
    if (!audio.isAmbiencePlaying() || audio.getCurrentAmbiencePath() != "audio/q_amb.wav") {
        lockFail("Ambience queue-fail bayraklari bayatladi (yeni/eski celiskisi)");
    }
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience) != ambBefore) {
        lockFail("Ambience queue-fail caprazinda bayt eski-boyda degil");
    }
    requireQueueFailed("Ambience queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — Ambience kuyruk+bayrak korunur, hata caller'a ulaşır");
    audio.testFailCommitPut();
    audio.playAudio("audio/q_amb_new.wav", Rowl::Audio::AudioChannelType::Ambience);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience) != ambBefore) {
        lockFail("Ambience commit-Put-dusus eski kuyrugu yok etti");
    }
    if (!audio.isAmbiencePlaying() || audio.getCurrentAmbiencePath() != "audio/q_amb.wav") {
        lockFail("Ambience commit-Put-dusus bayraklari bayatladi");
    }
    requireQueueFailed("Ambience commit-Put-dusus sonrasi");
    TEST_PASS("Audio Queue-Fail — Ambience commit-dususu kuyruk+bayrak korunur");
    audio.playAudio("audio/q_amb_new.wav", Rowl::Audio::AudioChannelType::Ambience);
    {
        const size_t ambNew =
            audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Ambience);
        if (ambNew == ambBefore) {
            lockFail("Ambience V3 kalibrasyon bayt degismedi");
        }
        if (audio.getCurrentAmbiencePath() != "audio/q_amb_new.wav") {
            lockFail("Ambience V3 kalibrasyon yeni yolu commitlemedi");
        }
    }
    TEST_PASS("Audio Queue-Fail — Ambience V3 kalibrasyon (enjeksiyonsuz yeni kuyruklar)");

    // SFX kanalı (a+b+c + ikinci kanca + V3; derinlik 1 → slot 0 deterministik).
    audio.setSfxPoolDepth(1);
    audio.playAudio("audio/q_sfx.wav", Rowl::Audio::AudioChannelType::Sfx);
    const size_t sfxBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx);
    if (sfxBefore == 0) {
        lockFail("SFX kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (sfxBefore == kQueueNewPcmBytes) {
        lockFail("SFX fixture ayrimi yok (eski==yeni boy)");
    }
    {
        const auto paths = audio.sfxActivePaths();
        if (paths.size() != 1 || paths[0] != "audio/q_sfx.wav") {
            lockFail("SFX kurulum slot-PCM'i yanlis");
        }
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_sfx_new.wav", Rowl::Audio::AudioChannelType::Sfx);
    {
        const size_t sfxAfter = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx);
        if (sfxAfter != sfxBefore) {
            lockFail("SFX queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
        }
        if (sfxAfter == kQueueNewPcmBytes) {
            lockFail("SFX queue-fail yeni yuku kuyrukladi");
        }
    }
    {
        const auto paths = audio.sfxActivePaths();
        if (paths.size() != 1 || paths[0] != "audio/q_sfx.wav") {
            lockFail("SFX queue-fail slot-PCM'i bayatladi (yeni/eski celiskisi)");
        }
    }
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx) != sfxBefore) {
        lockFail("SFX queue-fail caprazinda bayt eski-boyda degil");
    }
    requireQueueFailed("SFX queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — SFX kuyruk+slot korunur, hata caller'a ulaşır");
    audio.testFailCommitPut();
    audio.playAudio("audio/q_sfx_new.wav", Rowl::Audio::AudioChannelType::Sfx);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx) != sfxBefore) {
        lockFail("SFX commit-Put-dusus eski kuyrugu yok etti");
    }
    {
        const auto paths = audio.sfxActivePaths();
        if (paths.size() != 1 || paths[0] != "audio/q_sfx.wav") {
            lockFail("SFX commit-Put-dusus slot-PCM'i bayatladi");
        }
    }
    requireQueueFailed("SFX commit-Put-dusus sonrasi");
    TEST_PASS("Audio Queue-Fail — SFX commit-dususu kuyruk+slot korunur");
    audio.playAudio("audio/q_sfx_new.wav", Rowl::Audio::AudioChannelType::Sfx);
    {
        const size_t sfxNew = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Sfx);
        if (sfxNew == sfxBefore) {
            lockFail("SFX V3 kalibrasyon bayt degismedi");
        }
        const auto paths = audio.sfxActivePaths();
        if (paths.size() != 1 || paths[0] != "audio/q_sfx_new.wav") {
            lockFail("SFX V3 kalibrasyon yeni slotu commitlemedi");
        }
    }
    TEST_PASS("Audio Queue-Fail — SFX V3 kalibrasyon (enjeksiyonsuz yeni kuyruklar)");

    // BGM kanalı, RAM yolu (kısa WAV; non-transition dalı) (a+b+c + ikinci
    // kanca + V3).
    audio.playAudio("audio/q_bgm.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t bgmBefore = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (bgmBefore == 0) {
        lockFail("BGM kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (bgmBefore == kQueueNewPcmBytes) {
        lockFail("BGM fixture ayrimi yok (eski==yeni boy)");
    }
    if (audio.getCurrentBgmPath() != "audio/q_bgm.wav" || !audio.isBgmPlaying()) {
        lockFail("BGM kurulum niyeti yanlis");
    }
    audio.testFailNextQueue();
    audio.playAudio("audio/q_bgm_new.wav", Rowl::Audio::AudioChannelType::Bgm);
    {
        const size_t bgmAfter = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
        if (bgmAfter != bgmBefore) {
            lockFail("BGM queue-fail eski kuyrugu yok etti (pre-clear mutantı)");
        }
        if (bgmAfter == kQueueNewPcmBytes) {
            lockFail("BGM queue-fail yeni yuku kuyrukladi");
        }
    }
    if (audio.getCurrentBgmPath() != "audio/q_bgm.wav" || !audio.isBgmPlaying()) {
        lockFail("BGM queue-fail niyeti bayatladi (yol+bayt caprazi)");
    }
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != bgmBefore) {
        lockFail("BGM queue-fail caprazinda bayt eski-boyda degil");
    }
    requireQueueFailed("BGM queue-fail sonrasi");
    TEST_PASS("Audio Queue-Fail — BGM (non-transition) kuyruk korunur, hata caller'a ulaşır");
    audio.testFailCommitPut();
    audio.playAudio("audio/q_bgm_new.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != bgmBefore) {
        lockFail("BGM commit-Put-dusus eski kuyrugu yok etti");
    }
    if (audio.getCurrentBgmPath() != "audio/q_bgm.wav" || !audio.isBgmPlaying()) {
        lockFail("BGM commit-Put-dusus niyeti bayatladi");
    }
    requireQueueFailed("BGM commit-Put-dusus sonrasi");
    TEST_PASS("Audio Queue-Fail — BGM commit-dususu kuyruk+niyet korunur");
    audio.playAudio("audio/q_bgm_new.wav", Rowl::Audio::AudioChannelType::Bgm);
    {
        const size_t bgmNew = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
        if (bgmNew == bgmBefore) {
            lockFail("BGM V3 kalibrasyon bayt degismedi");
        }
        if (audio.getCurrentBgmPath() != "audio/q_bgm_new.wav") {
            lockFail("BGM V3 kalibrasyon yeni yolu commitlemedi");
        }
    }
    TEST_PASS("Audio Queue-Fail — BGM V3 kalibrasyon (enjeksiyonsuz yeni kuyruklar)");

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
 *  SOZLESME IKILIGI (uretim 614-664 dogrulamali, uretim degisikligi YOK):
 *  cihazli yol fail-closed'dur (asagidaki tum fail dallari predecessor'i
 *  korur, yalniz m_lastError yazar); sessiz-bilinen-dosya yolu COMMIT'tir
 *  (tasarim): existence-gate'i gecen dosya closeBgmStream + intent/snapshot
 *  yazimiyla kayda gecer — intent yeni dosyaya gecer (predecessor dagilir),
 *  snapshot asset yeni dosyadir, kuyruk literal 0, hata BOSTUR. Cap
 *  fail-closed YALNIZ cihazlidir; sessiz-bilinen cap COMMIT'i cap kilidindeki
 *  (s) adiminda pinlidir (yesil-gizli DEGIL).
 *  (d) forced-device-less: gercek-cihazsiz kosu beklenmez, kanca ile
 *      zorlanir (testSetDeviceAvailable(false/true); donus bayrak-
 *      restorasyonudur — sessiz-miss akislara dokunmaz): sessiz-yedekte
 *      gecerli BGM intent'i kurulur (kurulum json'u saklanir), eksik dosya
 *      intent'i korur + snapshot tam-esitlikle aynen (substring DEGIL),
 *      kuyruk literal 0 (TEK-YONLU: sessiz dal hic kuyruklamaz; bu iddia
 *      yalniz sessiz-kirleteni oldurur, bosaltma gucu cihazli q0>0
 *      dallarindadir) + hata set.
 *  (d2) forced-device-less streaming-miss: akan predecessor uzerinde sessiz
 *      miss — akis + kuyruk + snapshot aynen; bayrak geri-cevirme sonrasi
 *      reopenDeviceStreams (uretim donus yolu, #82 deseni) cagrilir: json
 *      birebir + intent + miss-guard aynen, kuyruk sifir DEGIL (ring-restore;
 *      pencere-degeri kurulumdan kucuk olabilir — TASARIM, delta-0 ARANMAZ),
 *      ikinci reopen delta-0 + json birebir (idempotans) kilitlenir (VFS'te
 *      var olan fixture'lar sessiz dalda commit'e girerdi; yalniz
 *      bilinmeyen-dosya dali cihazsiz test edilebilir — bilinen-dosya
 *      COMMIT'i cap (s)'dedir).
 *  (e) intent doğruluğu: başarısız yeni BGM (Fade+duration) transition
 *      başlatmaz (+ kuyruk/snapshot dondurma); başarılı transition swap'ı
 *      aynen (regresyon bekçisi, update(1.0f) ile deterministik sürülür —
 *      duvar-saati YOK).
 *  (f) ayrışma: explicit stopBgm() hâlâ kapatır.
 *
 * #87-tur3 BGM-ozel fark (#78/#81 adimlari SILINMEDI, bu iki iddia eklendi):
 * her fail dalinda kuyruk delta-0 (q0>0 bazli — bosaltma-oldurme gucu) ve
 * streamInfoJson birebirlik (buffered_seconds ilerlemesi/pozisyonu dahil).
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

// #87-tur3 BGM-ozel dondurma gozlemleri (#78/#81 ile ortusmez — adimlar
// SILINMEDI, bu iki iddia eklendi):
//  - kuyruk delta-0: testQueuedBytes tam-esitlik (sessiz-bosaltan VE sessiz-
//    kirleten mutant duser). Baz q0>0 kilitlenir: bos kuyrukta bosaltma
//    gozlenemezdi, o yuzden "kuyruk==0" degil "kuyruk==q0 (q0>0)" istenir.
//    Sessiz dallarda (d/d2) kuyruk gercekten 0'dir, orada literal 0 aranir.
//  - snapshot birebirlik: streamInfoJson string tam-esitlik (buffered_seconds
//    ilerlemesi/pozisyonu dahil — yeni-BGM faili akan predecessor'in
//    konumunu bozmaz; #78/#81'de bu yoktur).
std::string snapStreamingPredecessorJson(Rowl::Audio::AudioEngine& audio,
                                         const std::string& context) {
    const std::string json = audio.streamInfoJson();
    const auto parsed = nlohmann::json::parse(json);
    const double buffered = parsed.value("buffered_seconds", 0.0);
    if (!(buffered > 0.0)) {
        lockFail(context + ": kurulum akisi ilerlemedi (buffered_seconds==0): " + json);
    }
    return json;
}

void requireBgmPredecessorFrozen(Rowl::Audio::AudioEngine& audio, size_t queuedBefore,
                                 const std::string& jsonBefore,
                                 const std::string& context) {
    const size_t queuedAfter =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (queuedAfter != queuedBefore) {
        lockFail(context + ": fail kuyruga dokundu (once=" + std::to_string(queuedBefore) +
                 " sonra=" + std::to_string(queuedAfter) + ")");
    }
    const std::string jsonAfter = audio.streamInfoJson();
    if (jsonAfter != jsonBefore) {
        lockFail(context + ": fail snapshot/pozisyonu bozdu (once='" + jsonBefore +
                 "' sonra='" + jsonAfter + "')");
    }
    requireMissGuardIntact(audio, context);
}

} // namespace

void test_audio_lock_bgm_transactional() {
    TEST_SECTION("Audio BGM Transactional Commit (#87)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupTransactionalProject(vfs, audio);

    // (d) forced-device-less: gercek-cihazsiz kosu BEKLENMEZ; kanca ile
    // zorlanir (testSetDeviceAvailable semantigi: yalniz outage'u acar,
    // donusu simulate etmez — donus asagida kancanin geri-cevrilmesidir;
    // sessiz-miss akislara dokunmadigi icin bayrak-restorasyonu yeterlidir).
    // Sessiz kurulum snapshot'i saklanir: miss sonrasi substring DEGIL
    // tam-esitlik aranir (alan-degisimli mutant substring'i atlatirdi).
    // Sessiz dal hic kuyruklamaz: literal kuyruk==0 aranir — TEK-YONLU not:
    // bos kuyrukta bosaltma gozlenemez, bu iddia yalniz sessiz-kirleten
    // (kuyruklayan) mutanti oldurur; bosaltma-oldurme gucu cihazli
    // q0>0 dallarindadir.
    const bool realDevice = audio.isAudioDeviceAvailable();
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/t_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_a.wav") {
        lockFail("sessiz-yedek BGM intent'i kurulamadi");
    }
    if (audio.streamInfoJson().find("\"asset\":\"audio/t_bgm_a.wav\"") == std::string::npos) {
        lockFail("sessiz-yedek snapshot kurulamadi: " + audio.streamInfoJson());
    }
    const std::string silentSetupJson = audio.streamInfoJson();
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != 0) {
        lockFail("sessiz-yedek cihaz kuyruguna yazdi (sessiz dal kuyruklamaz)");
    }
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_a.wav") {
        lockFail("sessiz-yedek miss intent'i yikti (predecessor korunmadi)");
    }
    if (audio.streamInfoJson() != silentSetupJson) {
        lockFail("sessiz-yedek miss snapshot'i degistirdi (once='" + silentSetupJson +
                 "' sonra='" + audio.streamInfoJson() + "')");
    }
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != 0) {
        lockFail("sessiz-yedek miss kuyrukladi (fail yolu kuyruklamaz)");
    }
    if (audio.getLastError().empty()) {
        lockFail("sessiz-yedek miss hatasi caller'a ulasmadi");
    }
    audio.testSetDeviceAvailable(realDevice);
    if (audio.isAudioDeviceAvailable() != realDevice) {
        lockFail("kanca donusu cihaz bayragini bozdurdu");
    }
    TEST_PASS("Audio Transactional — sessiz-yedek miss intent+snapshot'i korur, kuyruk 0 (d)");
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

    // #87-tur3 dondurma: ilerleme kaniti (buffered_seconds>0 — akis konumu
    // var) + tuketim dondurma (setOutputSuspended) altinda kuyruk snapshot'i
    // (q0>0 — bosaltma-oldurme gucu). Sonraki fail dallari delta-0 + json
    // birebir + 7-iddia ile kilitlenir (#78/#81'den fark: BGM-ozel
    // akis-pozisyonu korunumu).
    const std::string kurulumJson =
        snapStreamingPredecessorJson(audio, "kurulum/miss_bgm.ogg");
    audio.setOutputSuspended(true);
    const size_t kurulumQueued =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (kurulumQueued == 0) {
        lockFail("kurulum kuyrugu bos — bosaltma-oldurme gucu yok");
    }

    // (a) decode-fail sonrası predecessor sağ: corrupt + eksik dosya.
    audio.playAudio("audio/miss_corrupt.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "corrupt OGG sonrasi (a)");
    requireErrorSet("corrupt OGG sonrasi (a)");
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "eksik dosya sonrasi (a)");
    requireErrorSet("eksik dosya sonrasi (a)");
    TEST_PASS("Audio Transactional — decode-fail predecessor'i korur (a)");

    // (b) streaming open-fail: probe geçer ama source->open düşer.
    audio.playAudio("audio/miss_openfail.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "open-fail sonrasi (b)");
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
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "basarisiz transition sonrasi (e)");
    requireErrorSet("basarisiz transition sonrasi (e)");
    TEST_PASS("Audio Transactional — basarisiz BGM transition baslatmaz (e)");

    // (d2) forced-device-less streaming-miss: dosya bilinmedigi icin sessiz
    // dal commit'e girmez (intent/snapshot/akis/kuyruk aynen); kanca geri
    // cevrilir. VFS'te VAR olan fixture'lar (openfail/corrupt/cap) sessiz
    // dalda commit'e girerdi — o dallar cihazsiz test EDILEMEZ (bilinen-dosya
    // COMMIT davranisi cap testindeki (s) adiminda pinlidir).
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/does_not_exist.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "sessiz streaming-miss sonrasi (d2)");
    requireErrorSet("sessiz streaming-miss sonrasi (d2)");
    audio.testSetDeviceAvailable(true);
    if (!audio.isAudioDeviceAvailable()) {
        lockFail("kanca donusu cihazi acmadi (d2)");
    }
    // Donus uretim yoludur (kanca donusu simulate etmez, yalniz outage'u
    // acar — #82 deseni): reopen rebuild eder; unknown-miss pending yazmadigi
    // icin replay YOKTUR. Rebuild SDL kuyrugunu dusurup ring penceresini geri
    // kuyruklar (4-chunk pencere tasarimi — kurulum daha fazla chunk
    // kuyruklamisti); o yuzden kuyruk pencere-degerine iner (kurulum
    // snapshot'iyla tam-esitlik ARANMAZ), snapshot/pozisyon (json) ve intent
    // birebir aynen kalmalidir. Ardindan ikinci reopen ile rebuild
    // idempotansi delta-0 + json birebir kilitlenir (tuketim dondurma
    // altinda halka degismez).
    if (!audio.reopenDeviceStreams()) {
        lockFail("reopen-donusu rebuild basarisiz (d2): " + audio.getLastError());
    }
    if (audio.streamInfoJson() != kurulumJson) {
        lockFail("reopen-donusu snapshot/pozisyonu bozdu (d2) (once='" + kurulumJson +
                 "' sonra='" + audio.streamInfoJson() + "')");
    }
    requireMissGuardIntact(audio, "reopen-donusu sonrasi (d2)");
    if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/miss_bgm.ogg") {
        lockFail("reopen-donusu intent'i bozdu (d2)");
    }
    const size_t reopenQueued =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (reopenQueued == 0) {
        lockFail("reopen-donusu akisi dusurdu (ring-restore kuyruklamadi) (d2)");
    }
    if (!audio.reopenDeviceStreams()) {
        lockFail("ikinci reopen rebuild basarisiz (d2): " + audio.getLastError());
    }
    requireBgmPredecessorFrozen(audio, reopenQueued, kurulumJson,
                                "ikinci reopen-donusu sonrasi (d2)");
    TEST_PASS("Audio Transactional — zorlanmis-cihazsiz miss akis+kuyrugu korur, donus aynen (d2)");

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
    const std::string ramJsonBefore = audio.streamInfoJson();
    audio.testFailNextQueue();
    audio.playAudio("audio/t_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != bgmBefore) {
        lockFail("BGM queue-fail eski kuyrugu yikti");
    }
    if (audio.streamInfoJson() != ramJsonBefore) {
        lockFail("BGM queue-fail snapshot'i bozdu (once='" + ramJsonBefore +
                 "' sonra='" + audio.streamInfoJson() + "')");
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
 *  - sessiz-bilinen COMMIT (s): zorlanmis-cihazsiz bilinen cap dosyasi
 *    existence-gate'i gecer, commit'e girer (intent+snapshot yeni dosya,
 *    kuyruk literal 0, hata bos) — bu yikim PINLIDIR (fail-closed mutanti
 *    burada duser: intent eski dosyada kalir + hata dolar).
 *
 * Gozlem (bu dosyadaki desen aynen): TEST_SECTION/TEST_PASS + hata=exit(1);
 * timing-assert/sleep/poll YOK; requireMissGuardIntact 7-iddia (kurulum
 * streaming BGM aynen) + #87-tur3 BGM-ozel dondurma (kuyruk delta-0 q0>0 +
 * streamInfoJson birebir — akis-pozisyonu korunumu; #78/#81'de yoktur);
 * hata-mesaji dal-kaniti (yanlis dala sapma yakalanir).
 * Cihaz bagimliligi: RAM fail-closed yolu cihaza baglidir; cihazsiz kosuda
 * bu adimlar acik SKIP (requireAudioDeviceOrSkip aynen) + gerekce: cap
 * fixture'lari VFS'te VAR oldugu icin sessiz dalda existence-gate'i gecer,
 * closeBgmStream + intent/snapshot yazimiyla COMMIT'e girer, predecessor'i
 * dagitir (tasarim — uretim 614-664) ve hata set EDILMEZ. Bu COMMIT davranisi
 * yesil-gizli DEGILDIR: (s) adimi SKIP oncesinde kosar (kanca-zorlamali,
 * cihaza bagli degildir) ve sessiz-bilinen cap'i birebir kilitler.
 * Yalniz bilinmeyen-dosya dallari sessizde korunur (transactional d/d2'de
 * kapsanir).
 *
 * KAPSAM-DISI (dogrulanmis-erisilemez dal, encoded-cap-only daraltmasi):
 * WAV decoded-cap (~762, "Decoded audio exceeds") bu kilit disindadir ve
 * dogrudan erisilemez — dogrulama: iki cap da birebir 64 MiB'dir
 * (kMaxEncodedAudioBytes == kMaxDecodedAudioBytes); WAV dosya = 44 + data,
 * SDL_LoadWAV audioLen <= dosya-44 oldugundan audioLen > 64 MiB gerektiren
 * her girdi once ~744 encoded-cap'e duser. OGG-ici decoded-cap (zincir)
 * yukarida KAPSANIR. Bu dala upfront-yikim iadesi suite'i yesil birakir
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

    // (s) forced-device-less sessiz-bilinen COMMIT: VFS'te VAR olan cap
    // fixture sessiz dalda existence-gate'i gecer (uretim 614-664: gate yalniz
    // bilinmeyen dosyada erken doner) ve COMMIT'e girer — intent yeni dosyaya
    // gecer (predecessor dagilir, TASARIM), snapshot asset yeni dosyadir,
    // kuyruk literal 0, hata BOSTUR. Kanca-zorlamali oldugu icin cihaza bagli
    // degildir, SKIP oncesinde kosar. Cihazli fail-closed sozlesmesiyle
    // karistirilmamalidir: cap fail-closed YALNIZ cihazli yoldadir.
    {
        const bool realDeviceCap = audio.isAudioDeviceAvailable();
        audio.testSetDeviceAvailable(false);
        audio.playAudio("audio/t_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
        if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/t_bgm_a.wav") {
            lockFail("sessiz kurulum intent'i kurulamadi (s)");
        }
        audio.playAudio("audio/cap_oversize_65m.wav", Rowl::Audio::AudioChannelType::Bgm);
        // COMMIT pin'i: intent yeni dosyadadir (predecessor yikimi TASARIM).
        // Fail-closed mutanti burada duser (intent eski dosyada kalirdi).
        if (!audio.isBgmPlaying() ||
            audio.getCurrentBgmPath() != "audio/cap_oversize_65m.wav") {
            lockFail("sessiz-bilinen cap commitlenmedi (intent yeni dosyada degil) (s)");
        }
        if (audio.streamInfoJson().find("\"asset\":\"audio/cap_oversize_65m.wav\"") ==
            std::string::npos) {
            lockFail("sessiz-bilinen cap snapshot'i yeni dosyada degil (s): " +
                     audio.streamInfoJson());
        }
        if (audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm) != 0) {
            lockFail("sessiz-bilinen cap kuyrukladi (sessiz dal kuyruklamaz) (s)");
        }
        if (!audio.getLastError().empty()) {
            lockFail("sessiz-bilinen cap hata yazdi (commit yolu hatasizdir) (s): '" +
                     audio.getLastError() + "'");
        }
        audio.testSetDeviceAvailable(realDeviceCap);
        if (audio.isAudioDeviceAvailable() != realDeviceCap) {
            lockFail("kanca donusu cihaz bayragini bozdurdu (s)");
        }
        TEST_PASS("Audio Cap — sessiz-bilinen cap COMMIT (intent+snapshot yeni dosya, kuyruk 0, hata bos)");
    }

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

    // #87-tur3 dondurma (transactional ile ayni teknik): ilerleme kaniti +
    // tuketim dondurma altinda kuyruk snapshot'i (q0>0).
    const std::string kurulumJson =
        snapStreamingPredecessorJson(audio, "kurulum/miss_bgm.ogg");
    audio.setOutputSuspended(true);
    const size_t kurulumQueued =
        audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (kurulumQueued == 0) {
        lockFail("kurulum kuyrugu bos — bosaltma-oldurme gucu yok");
    }

    // encoded-cap: 65 MiB sparse WAV reddi predecessor'i korur.
    audio.playAudio("audio/cap_oversize_65m.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "encoded-cap sonrasi");
    requireErrorContains("Audio file exceeds the maximum accepted size",
                         "encoded-cap sonrasi");
    TEST_PASS("Audio Cap Fail-Closed — encoded-cap predecessor'i korur");

    // decoded-cap: zincir-cozum tasmasi predecessor'i korur.
    audio.playAudio("audio/cap_chain200.ogg", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "decoded-cap sonrasi");
    requireErrorContains("Decoded Ogg/Vorbis audio exceeds the maximum accepted size",
                         "decoded-cap sonrasi");
    TEST_PASS("Audio Cap Fail-Closed — decoded-cap predecessor'i korur");

    // convert-fail: 9-kanal ceviri reddi predecessor'i korur.
    audio.playAudio("audio/cap_ch9.wav", Rowl::Audio::AudioChannelType::Bgm);
    requireBgmPredecessorFrozen(audio, kurulumQueued, kurulumJson,
                                "convert-fail sonrasi");
    requireErrorContains("Unable to convert decoded audio",
                         "convert-fail sonrasi");
    TEST_PASS("Audio Cap Fail-Closed — convert-fail predecessor'i korur");

    audio.setOutputSuspended(false);
    audio.stopAll();
    audio.shutdown();
}

/**
 * test_audio_lock.cpp eklentisi — Outage BGM Intent-Data Sync (#82) KİLİDİ.
 *
 * KILIT (mutant oldurur): cihaz kesintisinde playAudio sessiz-yedek dalı
 * niyeti yazar (path=B, playing=true) ama PCM verisini getirmez — m_bgmData
 * bayat A'da kalır, kaynak ölüdür (closeBgmStream). Dönüşte update()
 * loop-feed bayat A'yı taze akışa kuyruklar: duyulan=A, path=B ayrışması.
 * Düzeltme (pending-path + dönüş-decode, temizle DEĞİL): outage dalı
 * m_pendingBgmPath yazar; reopenDeviceStreams gerçek-dönüş geçişinde tam
 * playAudio taahhudunu çalıştırır (niyetle veri aynı parça olur, routing
 * kararı korunur — memory ve ölü-kaynaklı streamed köşesi tek-noktada).
 * Temizle-mutantında (m_bgmData.clear, pending YOK) qb==0 düşer; commit-
 * kapısı mutantında (reopen'da pending-decode YOK) qb==qa düşer → exit(1).
 *
 * Gözlem (deterministik; timing-assert/sleep/poll YOK; bu dosyanın deseni
 * aynen: TEST_SECTION/TEST_PASS + hata=exit(1) + setOutputSuspended(true)
 * dondurma + testQueuedBytes tam-eşitlik + cihazsızda açık SKIP):
 *  - Fixture: iki farklı-boy float WAV — A=64x0.5f, B=256x0.25f (4x boy
 *    farkı; bayat/taze ayrımı içerik-getter'sız tam-eşitlikle yapılır).
 *  - Sıra: suspend(önce) → A çal → qa → testSetDeviceAvailable(false)
 *    → B çal → niyet assert (path=B + playing) → reopenDeviceStreams
 *    (dönüş=üretim yolu) → update() → qb.
 *  - İddia: qb == q_controlB (aynı VFS'te ikinci engine, aynı suspend-
 *    sırasıyla B) VE qb != qa. Suspend play'lerden ÖNCE kurulur: qa/qb/
 *    consumption yarışı yoktur.
 *  - Streamed köşe (ayrı adım, aynı pending-decode): over-threshold OGG
 *    A → outage → OGG B → reopen → update → isStreaming + path + snapshot
 *    asset B. Pre-fix'te kaynak ölüdür (isStreaming false) → exit(1).
 *  - C API void-sessizliği kapsam-dışı gözlemdir (değişiklik yok).
 */
namespace {

void setupOutageProject(Rowl::VFS::VFSManager& vfs, Rowl::Audio::AudioEngine& audio) {
    if (!audio.initialize() || !audio.isInitialized()) {
        lockFail("Audio Outage BGM Sync (#82): audio init failed");
    }
    const auto root = std::filesystem::temp_directory_path() / "rowl_audio_outage_bgm_project";
    const auto dir = root / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    // 4x boy farkı: bayat/taze ayrımı tam-eşitlikle (içerik-getter'sız).
    writeBytes(dir / "o_bgm_a.wav",
               makeFloatWavMono44100(std::vector<float>(64, 0.5f)));
    writeBytes(dir / "o_bgm_b.wav",
               makeFloatWavMono44100(std::vector<float>(256, 0.25f)));
    // Streamed köşe: aynı over-threshold OGG iki ayrı isimle (header probe +
    // decoder gerçektir; miss-guard fixture tekniği aynen).
    const auto streamed =
        missGuardPatchGranuleForStream(missGuardLongToneOggBytes(),
                                       static_cast<uint64_t>(1000000000));
    writeBytes(dir / "o_stream_a.ogg", streamed);
    writeBytes(dir / "o_stream_b.ogg", streamed);
    vfs.remountProject(root.string());
}

} // namespace

void test_audio_lock_outage_bgm_intent_data_sync() {
    TEST_SECTION("Audio Outage BGM Intent-Data Sync (#82)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupOutageProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio Outage BGM Sync")) return;

    // Tüketim play'lerden ÖNCE donar: qa/qb karşılaştırması tam-eşitliktir.
    audio.setOutputSuspended(true);

    // Kurulum: A çalar (cihazlı), kuyruk snapshot'ı qa.
    audio.playAudio("audio/o_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qa = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qa == 0) {
        lockFail("kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (!audio.isBgmPlaying() || audio.getCurrentBgmPath() != "audio/o_bgm_a.wav") {
        lockFail("kurulum niyeti yanlis");
    }
    TEST_PASS("Audio Outage Sync — kurulum (A calar, qa>0)");

    // Outage: niyet B'ye geçer (sessiz-yedek dalı), veri getirilemez.
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/o_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != "audio/o_bgm_b.wav" || !audio.isBgmPlaying()) {
        lockFail("outage niyeti kurulamadi (path=B + playing)");
    }
    TEST_PASS("Audio Outage Sync — outage niyeti B'de (determinizm korunur)");

    // Dönüş: üretim yoluyla (reopen = gerçek-dönüş geçişi; kanca dönüşü
    // simulate etmez, yalnız outage'u açar).
    if (!audio.reopenDeviceStreams()) {
        lockFail("donus rebuild basarisiz: " + audio.getLastError());
    }
    if (!audio.isAudioDeviceAvailable()) {
        lockFail("donus cihazi acilmadi");
    }
    audio.update();
    const size_t qb = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);

    // Kontrol: aynı VFS'te ikinci engine, aynı suspend-sırasıyla B.
    Rowl::VFS::VFSManager vfsControl;
    Rowl::Audio::AudioEngine control(&vfsControl);
    if (!control.initialize() || !control.isInitialized()) {
        lockFail("kontrol engine init failed");
    }
    vfsControl.remountProject((std::filesystem::temp_directory_path() /
                               "rowl_audio_outage_bgm_project")
                                  .string());
    if (!control.isAudioDeviceAvailable()) {
        lockFail("kontrol engine cihazsiz — ana kosu cihazliydi (tuhaf durum)");
    }
    control.setOutputSuspended(true);
    control.playAudio("audio/o_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qControlB = control.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qControlB == 0) {
        lockFail("kontrol kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (qControlB == qa) {
        lockFail("fixture ayirimi yok (kontrol-B == qa) — 4x boy farki calismadi");
    }
    if (qb != qControlB) {
        lockFail("donus verisi B degil (qb != kontrol-B)");
    }
    if (qb == qa) {
        lockFail("bayat A geri-kuyruklandi (niyet-veri ayrismasi)");
    }
    if (audio.getCurrentBgmPath() != "audio/o_bgm_b.wav" || !audio.isBgmPlaying()) {
        lockFail("donus niyeti bozuldu");
    }
    TEST_PASS("Audio Outage Sync — donus verisi B (qb==kontrol, qb!=qa)");

    // Streamed köşe: over-threshold OGG A → outage → OGG B → reopen → update.
    audio.playAudio("audio/o_stream_a.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (!audio.isStreaming()) {
        lockFail("stream kurulumu acilmadi (isStreaming false)");
    }
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/o_stream_b.ogg", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != "audio/o_stream_b.ogg" || !audio.isBgmPlaying()) {
        lockFail("stream outage niyeti kurulamadi");
    }
    if (!audio.reopenDeviceStreams()) {
        lockFail("stream donus rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    if (!audio.isStreaming()) {
        lockFail("donus stream kaynagi olu (niyet-veri ayrismasi)");
    }
    if (audio.getCurrentBgmPath() != "audio/o_stream_b.ogg") {
        lockFail("donus stream path yalani: '" + audio.getCurrentBgmPath() + "'");
    }
    if (audio.streamInfoJson().find("\"asset\":\"audio/o_stream_b.ogg\"") == std::string::npos) {
        lockFail("donus stream snapshot yalani: " + audio.streamInfoJson());
    }
    TEST_PASS("Audio Outage Sync — streamed kose donuste B'ye kavusur");

    audio.setOutputSuspended(false);
    audio.stopAll();
    audio.shutdown();
    control.stopAll();
    control.shutdown();
}

/**
 * R1 GENISLETMESI — Outage BGM pending-tuketim kilidi (#82, 2. tur).
 *
 * Yukaridaki #82 KILIT blogu ve testi DEGISTIRILMEDEN korunur; bu blok ve
 * asagidaki iki test SADECE kapsama ekler. R1 deligi: basari-yolu pending
 * tuketimleri kilit disindaydi — niyet veriye kavustuktan sonra pending
 * bosalmazsa bir sonraki niyetsiz donus bayat niyeti diriltir (2. kayip:
 * duyulan bayat parca, niyet guncel parca) veya fail-yolu pending'i
 * dusururse retry hakki olur.
 *
 * Kapsanan tuketim siteleri (engine/src/audio/audio_engine.cpp):
 *  - :860 RAM-cihaz basari komiti (davranissal kilit: niyetsiz-donus probu
 *    bayat B'yi diriltmeye calisir; silinirse qb2==qB duser).
 *  - :2201 stream taahhudu komiti (davranissal kilit: streamed cift-niyet
 *    + commit-yolu icrasi; suspend-altinda ring/pompa gozlenemediginden
 *    clear-satirinin TEK BASINA dusmesi gozlem-disi kalir — sozlesme-ayni
 *    site, teftisle pinlidir).
 *  - :899 akissiz-yedek basari komiti: cihaz-var + akis-yok durumu public
 *    API ile kurulamaz (initialize/reopen all-or-nothing acar/yikar; kanca
 *    donusu simulate etmez, yalniz outage'u acar) — ayni tuketim
 *    sozlesmesi, teftisle pinlidir.
 *  - reopen fail-closed: decode/queue duserse pending tutulur, retry bir
 *    sonraki gercek-donus gecisinde commitler (davranissal kilit:
 *    fail-varyanti; kosulsuz-clear mutantinda retry qb==qa duser).
 *
 * Desen aynen: TEST_SECTION/TEST_PASS + hata=exit(1) + setOutputSuspended
 * dondurma + testQueuedBytes tam-esitlik + cihazsizda acik SKIP; timing
 * assert/sleep/poll YOKTUR.
 */
namespace {

void setupR1OutageProject(Rowl::VFS::VFSManager& vfs, Rowl::Audio::AudioEngine& audio) {
    if (!audio.initialize() || !audio.isInitialized()) {
        lockFail("Audio Outage R1: audio init failed");
    }
    const auto root = std::filesystem::temp_directory_path() / "rowl_audio_outage_r1_project";
    const auto dir = root / "Assets" / "audio";
    std::filesystem::create_directories(dir);
    // Uc ayrik boy: bayat/taze/dirilen ayrimi tam-esitlikle (icerik-getter'siz).
    writeBytes(dir / "r1_bgm_a.wav",
               makeFloatWavMono44100(std::vector<float>(64, 0.5f)));
    writeBytes(dir / "r1_bgm_b.wav",
               makeFloatWavMono44100(std::vector<float>(256, 0.25f)));
    writeBytes(dir / "r1_bgm_c.wav",
               makeFloatWavMono44100(std::vector<float>(128, 0.75f)));
    // Streamed kose: ayni over-threshold OGG uc ayri isimle (ayrim path +
    // snapshot asset ile; header probe + decoder gercektir).
    const auto streamed =
        missGuardPatchGranuleForStream(missGuardLongToneOggBytes(),
                                       static_cast<uint64_t>(1000000000));
    writeBytes(dir / "r1_stream_a.ogg", streamed);
    writeBytes(dir / "r1_stream_b.ogg", streamed);
    writeBytes(dir / "r1_stream_c.ogg", streamed);
    vfs.remountProject(root.string());
}

} // namespace

void test_audio_lock_outage_pending_no_stale_replay() {
    TEST_SECTION("Audio Outage Pending No-Stale-Replay (R1)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupR1OutageProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio Outage R1 No-Stale-Replay")) return;

    // Tüketim tum play'lerden ONCE donar: karsilastirmalar tam-esitliktir.
    audio.setOutputSuspended(true);

    // Kontrol: ayni VFS'te ikinci engine, ayni suspend-sirasiyla B sonra C
    // (replace semantigi: sirayla okunan qB/qC gecerlidir).
    Rowl::VFS::VFSManager vfsControl;
    Rowl::Audio::AudioEngine control(&vfsControl);
    if (!control.initialize() || !control.isInitialized()) {
        lockFail("R1 kontrol engine init failed");
    }
    vfsControl.remountProject((std::filesystem::temp_directory_path() /
                               "rowl_audio_outage_r1_project")
                                  .string());
    if (!control.isAudioDeviceAvailable()) {
        lockFail("R1 kontrol engine cihazsiz — ana kosu cihazliydi (tuhaf durum)");
    }
    control.setOutputSuspended(true);

    // Kurulum: A calar (cihazli), B/C kontrolde (ayrisma kontrolu).
    audio.playAudio("audio/r1_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qa = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qa == 0) {
        lockFail("R1 kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    control.playAudio("audio/r1_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qB = control.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    control.playAudio("audio/r1_bgm_c.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qC = control.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qB == 0 || qC == 0) {
        lockFail("R1 kontrol kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (qB == qa || qC == qa || qC == qB) {
        lockFail("R1 fixture ayirimi yok (A/B/C kuyruklari cakisti)");
    }
    TEST_PASS("Audio Outage R1 — kurulum (A/B/C ayrik, qa/qB/qC>0)");

    // Baz (#82 1. kayip aynen): outage B -> reopen -> B verisi.
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/r1_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != "audio/r1_bgm_b.wav" || !audio.isBgmPlaying()) {
        lockFail("R1 outage niyeti kurulamadi (path=B + playing)");
    }
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 donus rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    const size_t qb = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qb != qB) {
        lockFail("R1 donus verisi B degil (qb != qB)");
    }
    if (qb == qa) {
        lockFail("R1 bayat A geri-kuyruklandi (niyet-veri ayrismasi)");
    }
    TEST_PASS("Audio Outage R1 — baz: donus verisi B (qb==qB, qb!=qa)");

    // C cihazda basariyla calar (basari-yolu tuketim :860 calisir).
    audio.playAudio("audio/r1_bgm_c.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qc = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qc != qC) {
        lockFail("R1 C taahhudu bozuldu (qc != qC)");
    }
    TEST_PASS("Audio Outage R1 — C cihazda (qc==qC)");

    // NIYETSIZ ikinci donus: yeni play YOK, outage + reopen. Bayat pending
    // yasasaydi B dirilirdi (2. kayip: duyulan=B, niyet=C); tuketim
    // saglamsa C yasar. :860-sil mutantinda qb2==qB duser -> exit(1).
    audio.testSetDeviceAvailable(false);
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 niyetsiz donus rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    const size_t qb2 = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qb2 != qC) {
        lockFail("R1 niyetsiz donuste C yasamadi (bayat pending dirildi?)");
    }
    if (qb2 == qB) {
        lockFail("R1 niyetsiz donuste bayat B dirildi (2. kayip)");
    }
    if (audio.getCurrentBgmPath() != "audio/r1_bgm_c.wav" || !audio.isBgmPlaying()) {
        lockFail("R1 niyetsiz donus niyeti bozuldu (path=C + playing)");
    }
    TEST_PASS("Audio Outage R1 — niyetsiz donus C'yi korur (RAM tuketim)");

    // Cift-niyet tek-donus: outage'da B sonra C; kazanan SON niyet C olmalidir
    // (ilk-kazanir mutantinda qc3==qB duser -> exit(1)).
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/r1_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    audio.playAudio("audio/r1_bgm_c.wav", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != "audio/r1_bgm_c.wav" || !audio.isBgmPlaying()) {
        lockFail("R1 cift-niyet kurulamadi (path=C + playing)");
    }
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 cift-niyet donus rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    const size_t qc3 = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qc3 != qC) {
        lockFail("R1 cift-niyette son niyet C kazanmadi (qc3 != qC)");
    }
    if (qc3 == qB) {
        lockFail("R1 cift-niyette ilk niyet B yapisti (uzerine-yazma kaybi)");
    }
    TEST_PASS("Audio Outage R1 — cift-niyet tek-donus C'yi commitler");

    // Streamed kose: A -> outage B -> reopen (stream taahhudu :2201 tuketir).
    audio.playAudio("audio/r1_stream_a.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.update();
    if (!audio.isStreaming()) {
        lockFail("R1 stream kurulumu acilmadi (isStreaming false)");
    }
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/r1_stream_b.ogg", Rowl::Audio::AudioChannelType::Bgm);
    if (audio.getCurrentBgmPath() != "audio/r1_stream_b.ogg" || !audio.isBgmPlaying()) {
        lockFail("R1 stream outage niyeti kurulamadi");
    }
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 stream donus rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    if (!audio.isStreaming()) {
        lockFail("R1 stream donuste kaynak olu (niyet-veri ayrismasi)");
    }
    if (audio.getCurrentBgmPath() != "audio/r1_stream_b.ogg") {
        lockFail("R1 stream donus path yalani: '" + audio.getCurrentBgmPath() + "'");
    }
    if (audio.streamInfoJson().find("\"asset\":\"audio/r1_stream_b.ogg\"") == std::string::npos) {
        lockFail("R1 stream donus snapshot yalani: " + audio.streamInfoJson());
    }
    TEST_PASS("Audio Outage R1 — streamed donus B'yi commitler");

    // Streamed cift-niyet: outage'da B sonra C; donus C'yi acmalidir.
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/r1_stream_b.ogg", Rowl::Audio::AudioChannelType::Bgm);
    audio.playAudio("audio/r1_stream_c.ogg", Rowl::Audio::AudioChannelType::Bgm);
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 stream cift-niyet rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    if (!audio.isStreaming()) {
        lockFail("R1 stream cift-niyet kaynagi olu");
    }
    if (audio.getCurrentBgmPath() != "audio/r1_stream_c.ogg") {
        lockFail("R1 stream cift-niyette B yapisti: '" + audio.getCurrentBgmPath() + "'");
    }
    if (audio.streamInfoJson().find("\"asset\":\"audio/r1_stream_c.ogg\"") == std::string::npos) {
        lockFail("R1 stream cift-niyet snapshot yalani: " + audio.streamInfoJson());
    }
    TEST_PASS("Audio Outage R1 — streamed cift-niyet C'yi commitler");

    // Streamed niyetsiz donus: yenisiz outage + reopen durumu korur.
    audio.testSetDeviceAvailable(false);
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 stream niyetsiz rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    if (!audio.isStreaming()) {
        lockFail("R1 stream niyetsiz donuste kaynak oldu");
    }
    if (audio.getCurrentBgmPath() != "audio/r1_stream_c.ogg") {
        lockFail("R1 stream niyetsiz donus path yalani: '" + audio.getCurrentBgmPath() + "'");
    }
    TEST_PASS("Audio Outage R1 — streamed niyetsiz donus durumu korur");

    audio.setOutputSuspended(false);
    audio.stopAll();
    audio.shutdown();
    control.stopAll();
    control.shutdown();
}

void test_audio_lock_outage_pending_fail_preserved() {
    TEST_SECTION("Audio Outage Pending Fail-Preserved (R1)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    setupR1OutageProject(vfs, audio);
    if (!requireAudioDeviceOrSkip(audio, "Audio Outage R1 Fail-Preserved")) return;

    // Tüketim play'lerden ONCE donar: karsilastirmalar tam-esitliktir.
    audio.setOutputSuspended(true);

    // Kurulum: A calar (cihazli), kuyruk snapshot'i qa.
    audio.playAudio("audio/r1_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qa = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qa == 0) {
        lockFail("R1 fail kurulum kuyrugu bos — fixture cihaza kuyruklanamadi");
    }

    // Kontrol: ayni VFS'te ikinci engine, ayni suspend-sirasiyla B.
    Rowl::VFS::VFSManager vfsControl;
    Rowl::Audio::AudioEngine control(&vfsControl);
    if (!control.initialize() || !control.isInitialized()) {
        lockFail("R1 fail kontrol engine init failed");
    }
    vfsControl.remountProject((std::filesystem::temp_directory_path() /
                               "rowl_audio_outage_r1_project")
                                  .string());
    if (!control.isAudioDeviceAvailable()) {
        lockFail("R1 fail kontrol engine cihazsiz — ana kosu cihazliydi (tuhaf durum)");
    }
    control.setOutputSuspended(true);
    control.playAudio("audio/r1_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    const size_t qB = control.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qB == 0) {
        lockFail("R1 fail kontrol kuyrugu bos — fixture cihaza kuyruklanamadi");
    }
    if (qB == qa) {
        lockFail("R1 fail fixture ayirimi yok (kontrol-B == qa)");
    }
    TEST_PASS("Audio Outage R1 fail — kurulum (A calar, kontrol-B ayrik)");

    // Outage niyeti B; commit kancayla dusurulur (fail-closed: kanca bayragi
    // prova-oncesi tuketilir, SDL'ye dokunulmaz, fail yolu birebir isler).
    audio.testSetDeviceAvailable(false);
    audio.playAudio("audio/r1_bgm_b.wav", Rowl::Audio::AudioChannelType::Bgm);
    audio.testFailNextQueue();
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 fail rebuild dondu (commit-fail rebuild'i oldurmemeli)");
    }
    if (audio.getLastError().empty()) {
        lockFail("R1 fail commit dusmedi (kanca ateslenmedi?)");
    }
    if (audio.getCurrentBgmPath() != "audio/r1_bgm_b.wav" || !audio.isBgmPlaying()) {
        lockFail("R1 fail niyet korunmadi (path=B + playing)");
    }
    audio.update();
    const size_t qbFail = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qbFail != qa) {
        lockFail("R1 fail kismi-commit (predecessor A korunmadi)");
    }
    TEST_PASS("Audio Outage R1 fail — niyet + predecessor korunur, pending tutulur");

    // Retry: bir sonraki GERCEK-donus gecisi B'yi commitler (pending dusmemistir;
    // kosulsuz-clear mutantinda retry bayat A'da kalir -> exit(1)).
    audio.testSetDeviceAvailable(false);
    if (!audio.reopenDeviceStreams()) {
        lockFail("R1 retry rebuild basarisiz: " + audio.getLastError());
    }
    audio.update();
    const size_t qb = audio.testQueuedBytes(Rowl::Audio::AudioChannelType::Bgm);
    if (qb != qB) {
        lockFail("R1 retry B'yi commitlemedi (pending dustu?)");
    }
    if (qb == qa) {
        lockFail("R1 retry bayat A'da kaldi (pending kaybi)");
    }
    if (audio.getCurrentBgmPath() != "audio/r1_bgm_b.wav" || !audio.isBgmPlaying()) {
        lockFail("R1 retry niyeti bozuldu");
    }
    TEST_PASS("Audio Outage R1 fail — retry pending'i commitler (qb==qB)");

    audio.setOutputSuspended(false);
    audio.stopAll();
    audio.shutdown();
    control.stopAll();
    control.shutdown();
}

/**
 * test_audio_lock.cpp eklentisi — Dead-Handle Spectrum Zero-Fill (#76) KILIDI.
 *
 * KILIT (mutant oldurur): olu-handle'da GetAudioSpectrum caller tamponuna
 * dokunmuyordu — bayat bantlar canli gibi okunuyordu. Duzeltme (sessiz-tier):
 * olu-handle dali outBands[0..bandCount)'u sifir-doldurur, kayit tutmaz.
 * Sifir-doldurma satiri silinirse asagidaki zehir-tampon assert'i exit(1)
 * ile duser.
 *
 * Cekirdek (#76-tur2): genislikler 1-4-8-16, cagri-oncesi tum bantlar
 * 3.14159f zehirli, Destroy sonrasi tek iddia: tum bantlar bit-bit ==0.0f
 * (tolerans YOK, -0.0f bile RED — bit deseni 0x00000000 olmalidir).
 * bant0 ve son-bant ayri belirtilir. Tek-genislik (sabit-8) ve m!=8
 * mutantlari dar genisliklerde duser. Damga YOKTUR: GetLastResultCode
 * INVALID_HANDLE kalir (WrongThread damgasi RED — Dead/Foreign ayrimli
 * mutantin Dead'i-Foreign-sanmasi burada olur).
 *
 * Gozlem (deterministik; cihaz-bagimsiz — olu-handle yolu audio cihaza
 * ugramaz, o yuzden requireAudioDeviceOrSkip YOKTUR; timing-assert YOK):
 *  Null-guard + Peak/Rms skaler 0.0f sozlesmesi
 *  test_audio_lock_spectrum_null_guard_lock kilidinde, bandCount<=0
 *  gozcusu test_audio_lock_spectrum_nonpositive_guard_watcher'dadir
 *  (kapsama silinmez, bolunur). Bu cekirdek SADECE sifir-doldur kilididir.
 */
void test_audio_lock_dead_handle_spectrum_zero_fill() {
    TEST_SECTION("Audio Dead-Handle Spectrum Zero-Fill (#76)");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) {
        lockFail("Audio Dead-Handle Spectrum (#76): RowlEngine_Create failed");
    }
    RowlEngine_Destroy(handle); // handle artik olu (retention: tekrar gecerli olamaz)

    const int widths[4] = {1, 4, 8, 16};
    for (const int width : widths) {
        std::vector<float> bands(static_cast<size_t>(width), 3.14159f);
        RowlEngine_GetAudioSpectrum(handle, bands.data(), width);
        for (int i = 0; i < width; ++i) {
            uint32_t bits = 0;
            std::memcpy(&bits, &bands[i], sizeof(bits));
            if (bits != 0u) {
                const std::string where =
                    (i == 0) ? "bant0"
                             : ((i == width - 1) ? "son-bant"
                                                 : ("bant " + std::to_string(i)));
                lockFail("Audio Dead-Handle Spectrum (#76): genislik " +
                         std::to_string(width) + " " + where +
                         " sifirlanmadi (olu-handle tamponu bayat birakti)");
            }
        }
        TEST_PASS(("Audio Dead-Handle Spectrum — genislik " + std::to_string(width) +
                   " tum bantlar bit-bit 0.0f")
                      .c_str());
    }
    // Damga YOK: olu-handle kayit tutmaz — GetLastResultCode INVALID_HANDLE
    // kalir. Dead'i-Foreign-sanip damgalayan ayrimli mutant burada OLUR.
    if (RowlEngine_GetLastResultCode(handle) !=
        static_cast<int32_t>(ROWL_RESULT_INVALID_HANDLE)) {
        lockFail("Audio Dead-Handle Spectrum (#76): olu-handle damga birakti "
                 "(INVALID_HANDLE beklenir, WrongThread RED)");
    }
    if (RowlEngine_GetLastResultCode(handle) ==
        static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD)) {
        lockFail("Audio Dead-Handle Spectrum (#76): olu-handle WrongThread "
                 "damgasi tasiyor (Dead/Foreign ayrimi bozuk)");
    }
    TEST_PASS("Audio Dead-Handle Spectrum — olu-handle kayit tutmaz (damga YOK)");
}

/**
 * test_audio_lock.cpp eklentisi — Spectrum Foreign-Thread (#76-tur2) KILIDI.
 *
 * KILIT (mutant oldurur): !isLiveHandle Dead+Foreign'i birlestiriyordu —
 * yabanci-thread cagrisi da sessiz sifir-dolduruyor, tampona dokunuyor ve
 * damga birakmiyordu. Duzeltme (loud-tier): canli handle'a yabanci-thread
 * cagrisi tampona DOKUNMAZ + WRONG_THREAD (14) damgalar.
 *  - Foreign'i-Dead-sanip dolduran/ayrimli mutant: zehir-tampon degisir ->
 *    exit(1) (tampon-dokunmazlik).
 *  - Damgayi kaldiran mutant: GetLastResultCode 14 degil -> exit(1) (damga).
 *
 * Gozlem (deterministik; timing-assert/sleep/poll YOK): sahiplik
 * RowlEngine_Init ile kurulur (cagiran thread owner olur); ayri thread
 * zehirli tamponla cagirir, tamponu ve damgayi thread-icinde okur.
 * Damga paylasimli context'tedir — owner da join sonrasi 14 gorur.
 */
void test_audio_lock_spectrum_foreign_thread() {
    TEST_SECTION("Audio Spectrum Foreign-Thread (#76)");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) {
        lockFail("Audio Spectrum Foreign (#76): RowlEngine_Create failed");
    }
    if (RowlEngine_Init(handle, 320, 180, 0) != 1) {
        RowlEngine_Destroy(handle);
        lockFail("Audio Spectrum Foreign (#76): fixture Init failed (sahiplik kurulamadi)");
    }

    static constexpr float kForeignPoison = 2.71828f;
    static constexpr int kForeignBands = 8;
    uint32_t poisonBits = 0;
    std::memcpy(&poisonBits, &kForeignPoison, sizeof(poisonBits));
    bool foreignUntouched = false;
    int32_t foreignCode = -1;
    std::thread foreign([&] {
        float bands[kForeignBands];
        for (int i = 0; i < kForeignBands; ++i) bands[i] = kForeignPoison;
        RowlEngine_GetAudioSpectrum(handle, bands, kForeignBands);
        bool intact = true;
        for (int i = 0; i < kForeignBands; ++i) {
            uint32_t bits = 0;
            std::memcpy(&bits, &bands[i], sizeof(bits));
            if (bits != poisonBits) {
                intact = false;
                break;
            }
        }
        foreignUntouched = intact;
        foreignCode = RowlEngine_GetLastResultCode(handle);
    });
    foreign.join();
    if (!foreignUntouched) {
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        lockFail("Audio Spectrum Foreign (#76): yabanci-thread cagrisi tampona "
                 "dokundu (tampon degismemis olmaliydi)");
    }
    if (foreignCode != static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD)) {
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        lockFail("Audio Spectrum Foreign (#76): WrongThread damgasi yok (kod " +
                 std::to_string(foreignCode) + ", 14 beklenir)");
    }
    // Damga paylasimli context'tedir: owner da join sonrasi 14 + op gorur.
    if (RowlEngine_GetLastResultCode(handle) !=
        static_cast<int32_t>(ROWL_RESULT_WRONG_THREAD)) {
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        lockFail("Audio Spectrum Foreign (#76): damga owner'a gorunmuyor "
                 "(paylasimli context bozuk)");
    }
    if (std::string(RowlEngine_GetLastResultOperation(handle)) !=
        "get_audio_spectrum") {
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        lockFail("Audio Spectrum Foreign (#76): damga op'u yanlis "
                 "(get_audio_spectrum beklenir)");
    }
    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    TEST_PASS("Audio Spectrum Foreign — tampon degismemis + WrongThread damgasi (kilit)");
}

/**
 * test_audio_lock.cpp eklentisi — Spectrum Null-Guard (#76) KILIDI.
 *
 * KILIT (cokme gozlemi): null tampon sessiz no-op'tur (cokme yok).
 * null-kontrol kaldirilirsa asagidaki nullptr cagrisi NULL deref ile
 * cokar — suit kirmiziya duser. Peak/Rms skaler 0.0f sozlesmesi degismez
 * (olu-handle 0.0f doner; davranis bekcisi, bu kilitle ayni fonksiyonda
 * pinlidir).
 *
 * Deterministik; cihaz-bagimsiz (requireAudioDeviceOrSkip YOKTUR);
 * timing-assert YOKTUR.
 */
void test_audio_lock_spectrum_null_guard_lock() {
    TEST_SECTION("Audio Spectrum Null-Guard (#76)");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) {
        lockFail("Audio Spectrum Null-Guard (#76): RowlEngine_Create failed");
    }
    RowlEngine_Destroy(handle); // handle artik olu (retention: tekrar gecerli olamaz)

    // Null tampon: sessiz no-op, cokme yok (kaldirma-karsi mutant cokar).
    RowlEngine_GetAudioSpectrum(handle, nullptr, 4);
    RowlEngine_GetAudioSpectrum(handle, nullptr, 16);
    // Peak/Rms skaler sozlesmesi degismez: olu-handle 0.0f doner.
    if (RowlEngine_GetAudioChannelPeak(handle, 3, 0) != 0.0f ||
        RowlEngine_GetAudioChannelRms(handle, 3, 1) != 0.0f) {
        lockFail("Audio Spectrum Null-Guard (#76): Peak/Rms olu-handle sozlesmesi bozuldu");
    }
    TEST_PASS("Audio Spectrum Null-Guard — null sessiz no-op, Peak/Rms 0.0f korunur (kilit)");
}

/**
 * test_audio_lock.cpp eklentisi — Spectrum NonPositive-Guard (#76) GOZCUSU.
 *
 * GOZCU (oldurmez, gozlem-disi): bandCount<=0 acik guard'inin kaldirilmasinin
 * gozlenebilir etkisi YOKTUR — sifir-doldur dongusu `for (i=0; i<bandCount)`
 * zaten bos doner (bos-dongu), o yuzden kill-matrisi iddiasi YOKTUR.
 * Yalniz davranis bekcisidir: sessiz no-op, zehir-tampona dokunulmaz.
 *
 * Deterministik; cihaz-bagimsiz (requireAudioDeviceOrSkip YOKTUR);
 * timing-assert YOKTUR.
 */
void test_audio_lock_spectrum_nonpositive_guard_watcher() {
    TEST_SECTION("Audio Spectrum NonPositive-Guard Watcher (#76)");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) {
        lockFail("Audio Spectrum NonPositive-Guard (#76): RowlEngine_Create failed");
    }
    RowlEngine_Destroy(handle); // handle artik olu (retention: tekrar gecerli olamaz)

    float bands[4] = {0.5f, 0.75f, 1.0f, 0.25f};
    RowlEngine_GetAudioSpectrum(handle, bands, 0);
    RowlEngine_GetAudioSpectrum(handle, bands, -1);
    const float expected[4] = {0.5f, 0.75f, 1.0f, 0.25f};
    for (int i = 0; i < 4; ++i) {
        if (bands[i] != expected[i]) {
            lockFail("Audio Spectrum NonPositive-Guard (#76): guard cagrisi tamponu bozdu "
                     "(bant " + std::to_string(i) + ")");
        }
    }
    TEST_PASS("Audio Spectrum NonPositive-Guard — bandCount<=0 sessiz no-op (gozcu, oldurmez)");
}

/**
 * test_audio_lock.cpp eklentisi — Offscreen Global Pump (#75) KILIDI.
 *
 * KILIT (mutant oldurur): offscreen runtime pencere kaydetmez; pollEvents
 * (m_eventWindowId==0) erken doner, dispatch pini sahipsiz kalir ve
 * takeGlobalEvents SDL_PollEvent'e dokunmadan no-op'a duser — global
 * audio-device/minimize eventleri SDL kuyrugunda olu kalir. Duzeltme:
 * Engine::step offscreen dalinda once SdlEventDispatcher::pumpOnly()
 * (sahipsizse-claim + pump) cagirir. pumpOnly satiri veya isOffscreen dali
 * silinirse asagidaki minimize adimi suspend'i kuramaz -> exit(1).
 * test_audio_device_recovery.cpp 3. adim ayni kablolari dogrular ama KAYITLI
 * pencereyle (pin sahipli) — bu kilit kayitsiz offscreen-default halini
 * kapsar; visible cift-pump duzeni aynen korunur (gozlem).
 *
 * Gozlem (deterministik; timing-assert/sleep/poll YOK; bu dosyanin deseni
 * aynen: TEST_SECTION/TEST_PASS + hata=exit(1)):
 *  - Onkosul (gozlemlenebilir pin kanali): Step oncesi pin SAHIPSIZDIR
 *    (isDispatchThread()==false + isEligibleForRegister()==true); ilk Step
 *    sonrasi pin bu thread'e claim'lenmistir (isDispatchThread()==true).
 *    pumpOnly'daki claim-if-unclaimed silinirse Step-sonrasi claim duser.
 *  - MINIMIZED itilir + RowlEngine_Step -> IsAudioOutputSuspended==1 +
 *    SDL kuyrugu bos. Yalniz MINIMIZED dalini bozan mutant (MINIMIZED
 *    case'inin silinmesi / isGlobalEvent'ten dusurulmesi) bu fazda duser.
 *  - RESTORED itilir + Step -> suspend==0 + SDL kuyrugu bos (resume yolu da
 *    global kuyruktan; MINIMIZED ile kurulan suspend onkosulune karsi).
 *    Yalniz RESTORED dalini bozan mutant (resume-case silinmesi /
 *    terslenmesi) bu fazda duser.
 *  - ADDED itilir + Step -> suspend'e sizma yok + SDL kuyrugu bos (pump
 *    kaniti; cihaz varken ADDED no-op dalidir, handleDeviceEvent ~1327).
 *    ADDED'yi suspend'e sizdiran mutant bu fazda duser.
 *  - MAXIMIZED itilir + Step -> suspend==0 + SDL kuyrugu bos (RESTORED
 *    emsali; switch'teki AYRI case satiri). Onkosul: MINIMIZED ile suspend
 *    kurulur; bu faz yalniz MAXIMIZED dalini gozler. MAXIMIZED-satir-silme
 *    mutanti (case silinmesi / isGlobalEvent'ten dusurulmesi) burada duser.
 *  - REMOVED / FORMAT_CHANGED tuketimi + reopen-kaniti: cihaz varken bu iki
 *    dal handleDeviceEvent'e duser, reopenDeviceStreams giriste son ses
 *    hatasini TEMIZLER ve cihazi sag birakir. Kayip-dosya BGM play'iyle hata
 *    tohumu kurulur; olay + Step sonrasi suspend==0 + kuyruk bos + hata
 *    BOSALMIS + cihaz==1 aranir. Kalibrasyon: olaysiz Step tohumu korur
 *    (yalniz reopen temizler). REMOVED/FORMAT-satir-silme mutanti hatayi
 *    bayat birakir -> duser.
 *  - Yabanci-pin no-op: pin baska thread'de tutulurken (basarili
 *    registerWindow + is bitince unregister) bu thread'den pumpOnly cagrilir
 *    -> isDispatchThread false VE isEligibleForRegister false KALIR.
 *    Kosulsuz-steal mutanti (claim-if-unclaimed kosulunun kaldirilmasi)
 *    pini calar -> duser. Randevu C++20 atomic wait/notify'ledir.
 *
 * Cihaz bagimsizdir: suspend bayragi CPU-side'dir (cihaz SKIP'i YOKTUR).
 * CTest dummy suruculeri saglar; init basarisizsa FAIL-LOUD exit(1)
 * (sessiz SKIP YOKTUR).
 */
namespace {

// Her Step sonrasi SDL kuyrugu-bos kaniti (#75): dispatcher'a dokunmadan ham
// SDL_PollEvent ile tek olu-event bile birakilmadigi dogrulanir.
void requireSdlQueueDrained75(const char* phase) {
    SDL_Event leftover{};
    if (SDL_PollEvent(&leftover)) {
        std::cerr << "Offscreen Global Pump (#75): " << phase
                  << " Step sonrasi SDL kuyrugu bosalmadi (olu event type="
                  << static_cast<int>(leftover.type) << ")" << std::endl;
        std::exit(1);
    }
}

} // namespace

void test_audio_lock_offscreen_global_pump() {
    TEST_SECTION("Audio Offscreen Global Pump (#75)");

    // SDL_PushEvent events-alt-sistemi ister (device_recovery emsali).
    const bool eventsAlreadyInit = SDL_WasInit(SDL_INIT_EVENTS) != 0;
    if (!eventsAlreadyInit && !SDL_InitSubSystem(SDL_INIT_EVENTS)) {
        std::cerr << "Offscreen Global Pump (#75): SDL events init failed" << std::endl;
        std::exit(1);
    }

    // Offscreen-default onkosulu: kayitli pencere YOK, pin sahipsiz. Bu
    // thread'de kalmis bos-tablo pin varsa dummy-kayit+sil ile birakilir;
    // yabanci pin veya dolu tabloya dokunulmaz (register basarisizsa pas).
    constexpr uint32_t kPinReleaseProbe = 0x0750FF75u; // "75" temali, gercek SDL id'si degil
    if (Rowl::Platform::SdlEventDispatcher::registerWindow(kPinReleaseProbe)) {
        Rowl::Platform::SdlEventDispatcher::unregisterWindow(kPinReleaseProbe);
    }
    // Pin-sahipsizlik onkosulu (gozlemlenebilir kanal): Step oncesi pin bu
    // thread'de DEGILDIR (sahipsiz) ve kayit-uygundur (sahipsiz -> true).
    // Yabanci thread'in pinine dokunulmaz; o durumda Eligible false olur ve
    // kilit acikca duser (sessiz gecis YOK).
    if (Rowl::Platform::SdlEventDispatcher::isDispatchThread()) {
        std::cerr << "Offscreen Global Pump (#75): onkosul bozuldu — pin Step oncesi sahipli (offscreen-default degil)" << std::endl;
        std::exit(1);
    }
    if (!Rowl::Platform::SdlEventDispatcher::isEligibleForRegister()) {
        std::cerr << "Offscreen Global Pump (#75): onkosul bozuldu — pin yabanci thread'de (sahipsiz degil)" << std::endl;
        std::exit(1);
    }

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || RowlEngine_Init(handle, 320, 180, 0) != 1) {
        std::cerr << "Offscreen Global Pump (#75): offscreen engine init olmadi (fail-loud; sessiz SKIP YOK)"
                  << std::endl;
        if (handle) RowlEngine_Destroy(handle);
        if (!eventsAlreadyInit) SDL_QuitSubSystem(SDL_INIT_EVENTS);
        std::exit(1);
    }
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): taze runtime suspend'li basladi" << std::endl;
        std::exit(1);
    }

    // Init-artigi SDL eventleri probu kirletmesin (dispatcher'a dokunmadan
    // ham bosaltma; pin sahipsizken take no-op olurdu).
    {
        SDL_Event stale{};
        while (SDL_PollEvent(&stale)) {
        }
    }

    // 1. MINIMIZED global kuyruktan suspend'i kurmalidir (pre-fix: pin
    // sahipsiz -> take no-op -> suspend 0 kalir -> exit(1)).
    SDL_Event minimizedEvent{};
    minimizedEvent.type = SDL_EVENT_WINDOW_MINIMIZED;
    minimizedEvent.window.windowID = 0;
    if (!SDL_PushEvent(&minimizedEvent)) {
        std::cerr << "Offscreen Global Pump (#75): minimize probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 1) {
        std::cerr << "Offscreen Global Pump (#75): MINIMIZED offscreen Step'te suspend kuramadi "
                     "(SDL kuyrugu pompalanmiyor)"
                  << std::endl;
        std::exit(1);
    }
    // Step-sonrasi claim kaniti: pumpOnly sahipsiz pini bu thread'e
    // claim'lemistir (claim-if-unclaimed silinirse duser).
    if (!Rowl::Platform::SdlEventDispatcher::isDispatchThread()) {
        std::cerr << "Offscreen Global Pump (#75): ilk Step pin'i claim'leyemedi (pumpOnly claim calismiyor)"
                  << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("MINIMIZED");
    TEST_PASS("Audio Offscreen Pump — MINIMIZED suspend kurar (penceresiz global drain)");

    // 2. RESTORED resume eder (ayni global yolun diger yonu). Izolasyon
    // onkosulu: MINIMIZED fazi suspend'i kurmustur (suspend==1); bu faz yalniz
    // RESTORED dalini gozler — RESTORED-only mutanti (resume-case silinmesi /
    // terslenmesi) burada duser, MINIMIZED-only mutanti faz 1'de duser.
    if (RowlEngine_IsAudioOutputSuspended(handle) != 1) {
        std::cerr << "Offscreen Global Pump (#75): RESTORED onkosulu bozuldu — suspend kurulu degil (faz-1 sizintisi?)"
                  << std::endl;
        std::exit(1);
    }
    SDL_Event restoredEvent{};
    restoredEvent.type = SDL_EVENT_WINDOW_RESTORED;
    restoredEvent.window.windowID = 0;
    if (!SDL_PushEvent(&restoredEvent)) {
        std::cerr << "Offscreen Global Pump (#75): restore probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): RESTORED offscreen Step'te resume edemedi" << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("RESTORED");
    TEST_PASS("Audio Offscreen Pump — RESTORED resume eder (penceresiz global drain)");

    // 3. AUDIO_DEVICE_ADDED (cihaz varken no-op dali): suspend'e sizmaz ve
    // SDL kuyrugunda olu kalmaz — audio-device sinifi pump kaniti. Izolasyon
    // onkosulu: suspend==0 (RESTORED fazi resume etmistir); ADDED'yi suspend'e
    // sizdiran mutant burada duser.
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): ADDED onkosulu bozuldu — suspend kurulu (faz-2 sizintisi?)"
                  << std::endl;
        std::exit(1);
    }
    SDL_Event addedEvent{};
    addedEvent.type = SDL_EVENT_AUDIO_DEVICE_ADDED;
    addedEvent.adevice.which = 7;
    if (!SDL_PushEvent(&addedEvent)) {
        std::cerr << "Offscreen Global Pump (#75): ADDED probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): cihaz eventi suspend'e sizdi" << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("ADDED");
    if (RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "Offscreen Global Pump (#75): runtime prob sonrasi kosar durumda degil" << std::endl;
        std::exit(1);
    }
    TEST_PASS("Audio Offscreen Pump — ADDED tuketilir, kuyruk bos, runtime sag");

    // 4. MAXIMIZED resume eder (RESTORED emsali; switch'teki AYRI case
    // satiri). Izolasyon onkosulu: MINIMIZED ile suspend==1 kurulur; bu faz
    // yalniz MAXIMIZED dalini gozler — MAXIMIZED-satir-silme mutanti (case
    // silinmesi / isGlobalEvent'ten dusurulmesi) burada duser, MINIMIZED /
    // RESTORED-only mutantlari onceki fazlarda duser.
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): MAXIMIZED onkosulu bozuldu — suspend kurulu (faz-3 sizintisi?)"
                  << std::endl;
        std::exit(1);
    }
    SDL_Event minimSetupEvent{};
    minimSetupEvent.type = SDL_EVENT_WINDOW_MINIMIZED;
    minimSetupEvent.window.windowID = 0;
    if (!SDL_PushEvent(&minimSetupEvent)) {
        std::cerr << "Offscreen Global Pump (#75): MAXIMIZED kurulum minimize probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 1) {
        std::cerr << "Offscreen Global Pump (#75): MAXIMIZED kurulum MINIMIZED'i suspend kuramadi" << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("MAXIMIZED-kurulum");
    SDL_Event maximizedEvent{};
    maximizedEvent.type = SDL_EVENT_WINDOW_MAXIMIZED;
    maximizedEvent.window.windowID = 0;
    if (!SDL_PushEvent(&maximizedEvent)) {
        std::cerr << "Offscreen Global Pump (#75): maximize probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): MAXIMIZED offscreen Step'te resume edemedi (MAXIMIZED dali islenmiyor)"
                  << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("MAXIMIZED");
    TEST_PASS("Audio Offscreen Pump — MAXIMIZED resume eder (penceresiz global drain)");

    // 5. AUDIO_DEVICE_REMOVED / FORMAT_CHANGED tuketimi + reopen-kaniti:
    // cihaz varken bu iki dal handleDeviceEvent'e duser, reopenDeviceStreams
    // giriste son ses hatasini TEMIZLER (clearLastError) ve cikista cihazi sag
    // birakir. Kayip-dosya BGM play'iyle hata tohumu kurulur (native_c_api
    // emsali: dolu string); olay + Step sonrasi suspend==0 + kuyruk bos +
    // hata BOSALMIS + cihaz==1 aranir. Kalibrasyon: olaysiz Step tohumu korur
    // (yalniz reopen temizler — Step'in baska temizlik sipari yok).
    // REMOVED/FORMAT-satir-silme mutanti (switch case'i silinmesi veya
    // isGlobalEvent'ten dusurulmesi) hatayi bayat birakir -> exit(1).
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): REMOVED onkosulu bozuldu — suspend kurulu (faz-4 sizintisi?)"
                  << std::endl;
        std::exit(1);
    }
    RowlEngine_PlayAudio(handle, "audio/does_not_exist_75.wav", 0, 0);
    if (std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Offscreen Global Pump (#75): hata tohumu kurulamadi (kayip-dosya play'i hata yazmadi)"
                  << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Offscreen Global Pump (#75): kalibrasyon bozuldu — olaysiz Step hata tohumunu temizledi (reopen-disi temizlik?)"
                  << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("HATA-TOHUMU-KALIBRASYON");
    SDL_Event removedEvent{};
    removedEvent.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
    removedEvent.adevice.which = 7;
    if (!SDL_PushEvent(&removedEvent)) {
        std::cerr << "Offscreen Global Pump (#75): REMOVED probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): REMOVED eventi suspend'e sizdi" << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("REMOVED");
    if (!std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Offscreen Global Pump (#75): REMOVED reopen'a ugramadi — hata bayat kaldi (REMOVED dali silinmis/kopuk?)"
                  << std::endl;
        std::exit(1);
    }
    if (RowlEngine_IsAudioDeviceAvailable(handle) != 1) {
        std::cerr << "Offscreen Global Pump (#75): REMOVED reopen cihazi dusurdu" << std::endl;
        std::exit(1);
    }
    TEST_PASS("Audio Offscreen Pump — REMOVED tuketilir + reopen hatayi temizler, cihaz sag");
    // FORMAT_CHANGED: tohum tazelenir (reopen temizledi), ayni dort iddia.
    RowlEngine_PlayAudio(handle, "audio/does_not_exist_75.wav", 0, 0);
    if (std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Offscreen Global Pump (#75): FORMAT_CHANGED tohumu kurulamadi" << std::endl;
        std::exit(1);
    }
    SDL_Event formatEvent{};
    formatEvent.type = SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED;
    formatEvent.adevice.which = 7;
    if (!SDL_PushEvent(&formatEvent)) {
        std::cerr << "Offscreen Global Pump (#75): FORMAT_CHANGED probu kuyruga giremedi" << std::endl;
        std::exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(handle) != 0) {
        std::cerr << "Offscreen Global Pump (#75): FORMAT_CHANGED eventi suspend'e sizdi" << std::endl;
        std::exit(1);
    }
    requireSdlQueueDrained75("FORMAT_CHANGED");
    if (!std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Offscreen Global Pump (#75): FORMAT_CHANGED reopen'a ugramadi — hata bayat kaldi (FORMAT_CHANGED dali silinmis/kopuk?)"
                  << std::endl;
        std::exit(1);
    }
    if (RowlEngine_IsAudioDeviceAvailable(handle) != 1) {
        std::cerr << "Offscreen Global Pump (#75): FORMAT_CHANGED reopen cihazi dusurdu" << std::endl;
        std::exit(1);
    }
    TEST_PASS("Audio Offscreen Pump — FORMAT_CHANGED tuketilir + reopen hatayi temizler, cihaz sag");

    // 6. Yabanci-pin no-op: pumpOnly'daki claim-if-unclaimed kosulu kaldirilip
    // kosulsuz-steal'e cevrilirse yabanci thread'in pini calinir. Pin tutucu
    // thread'de tutulur (kPinReleaseProbe emsali: basarili registerWindow +
    // is bitince unregister); bu thread'den pumpOnly cagrilir ->
    // isDispatchThread false VE isEligibleForRegister false KALIR.
    // Kosulsuz-steal ikisini de true'ya cevirir -> exit(1). Randevu C++20
    // atomic wait/notify'ledir (sleep/poll/timing-assert YOK); erken-cikis
    // yollarinda tutucu once birakilir + join'lenir (terminate yok).
    // Onkosul: pin sahipsiz birakilir — bu thread'in Step'lerden kalan claim'i
    // prob-kayit+sil ile temizlenir (tablo zaten bostur, pin sifirlanir).
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(kPinReleaseProbe)) {
        std::cerr << "Offscreen Global Pump (#75): yabanci-pin onkosulu — prob kaydi olmadi (pin yabancida mi?)"
                  << std::endl;
        std::exit(1);
    }
    Rowl::Platform::SdlEventDispatcher::unregisterWindow(kPinReleaseProbe);
    if (Rowl::Platform::SdlEventDispatcher::isDispatchThread() ||
        !Rowl::Platform::SdlEventDispatcher::isEligibleForRegister()) {
        std::cerr << "Offscreen Global Pump (#75): yabanci-pin onkosulu bozuldu — pin sahipsiz degil" << std::endl;
        std::exit(1);
    }
    std::atomic<bool> holderReady{false};
    std::atomic<bool> holderPinned{false};
    std::atomic<bool> releaseHolder{false};
    std::thread holderThread([&] {
        holderPinned.store(Rowl::Platform::SdlEventDispatcher::registerWindow(kPinReleaseProbe),
                           std::memory_order_relaxed);
        holderReady.store(true, std::memory_order_relaxed);
        holderReady.notify_one();
        releaseHolder.wait(false, std::memory_order_relaxed);
        if (holderPinned.load(std::memory_order_relaxed)) {
            Rowl::Platform::SdlEventDispatcher::unregisterWindow(kPinReleaseProbe);
        }
    });
    holderReady.wait(false, std::memory_order_relaxed);
    auto releaseHolderThread = [&] {
        releaseHolder.store(true, std::memory_order_relaxed);
        releaseHolder.notify_one();
        holderThread.join();
    };
    if (!holderPinned.load(std::memory_order_relaxed)) {
        releaseHolderThread();
        std::cerr << "Offscreen Global Pump (#75): tutucu thread pin'i alamadi (register basarisiz)" << std::endl;
        std::exit(1);
    }
    // Kurulum kaniti: bu thread yabancidir (sahip degil, kayit-uygun degil).
    std::string foreignSetupFail;
    if (Rowl::Platform::SdlEventDispatcher::isDispatchThread()) {
        foreignSetupFail = "Offscreen Global Pump (#75): yabanci-pin kurulumu yabanci degil (bu thread sahip?)";
    } else if (Rowl::Platform::SdlEventDispatcher::isEligibleForRegister()) {
        foreignSetupFail = "Offscreen Global Pump (#75): yabanci-pin kurulumu kayit-uygun (pin sahipsiz mi?)";
    }
    // Katil gozlem: yabanci pin uzerinde pumpOnly no-op kalmalidir.
    std::string foreignPumpFail;
    if (foreignSetupFail.empty()) {
        Rowl::Platform::SdlEventDispatcher::pumpOnly();
        if (Rowl::Platform::SdlEventDispatcher::isDispatchThread()) {
            foreignPumpFail = "Offscreen Global Pump (#75): pumpOnly yabanci pin'i caldi (kosulsuz-steal mutanti)";
        } else if (Rowl::Platform::SdlEventDispatcher::isEligibleForRegister()) {
            foreignPumpFail =
                "Offscreen Global Pump (#75): pumpOnly yabanci pin'i caldi (kayit-uygunlugu bozuldu)";
        }
    }
    releaseHolderThread();
    if (!foreignSetupFail.empty()) {
        std::cerr << foreignSetupFail << std::endl;
        std::exit(1);
    }
    if (!foreignPumpFail.empty()) {
        std::cerr << foreignPumpFail << std::endl;
        std::exit(1);
    }
    if (Rowl::Platform::SdlEventDispatcher::isDispatchThread() ||
        !Rowl::Platform::SdlEventDispatcher::isEligibleForRegister()) {
        std::cerr << "Offscreen Global Pump (#75): tutucu pin'i birakmadi (tablo/pin sizintisi?)" << std::endl;
        std::exit(1);
    }
    TEST_PASS("Audio Offscreen Pump — pumpOnly yabanci pin'e dokunmaz (kosulsuz-steal oldurme)");

    RowlEngine_Destroy(handle);
    // Pin temizligi: prob pini bu thread'e claim'ledi; bos-tabloysa birak ki
    // sonraki testler (single-image, lifecycle) taze-surec gorunumuyle baslasin.
    if (Rowl::Platform::SdlEventDispatcher::registerWindow(kPinReleaseProbe)) {
        Rowl::Platform::SdlEventDispatcher::unregisterWindow(kPinReleaseProbe);
    }
    if (!eventsAlreadyInit) SDL_QuitSubSystem(SDL_INIT_EVENTS);
    TEST_PASS("Audio Offscreen Global Pump (#75) — temizlik (pin birakildi)");
}

/**
 * test_audio_lock.cpp eklentisi — Scene-Restore Ses Snapshot (#86) kilitleri.
 *
 * KILIT (mutant oldurur):
 *  1a. test_audio_lock_scene_restore_no_restart_on_clean_throw: BGM A
 *      calarken MUTASYONSUZ throw (sahnede yalniz a2: ertelenmis blokta
 *      string volume -> type_error, hicbir ses satiri kosmaz). catch snapshot'i
 *      geri yazar; sapma-gardi ayni-parca replay'i atlar. Oldurur: snapshot
 *      yakalamanin silinmesi, catch'te apply* kaldirma, kazanc/filtre satir
 *      silme + SAPMA-GARDI SILME. Restart gozlemi lastError mandalidir:
 *      throw-oncesi SFX-miss ile ekilen yapiskan hata gard'lida korunur
 *      (apply yalniz setter kosar, lastError'e dokunmaz), gard'siz replay
 *      playAudio'yu yeniden kosup clearLastError ile mandali siler -> KIZARIR.
 *      Fixture HIC saptirmadigi icin gard burada KOSAR (onceki tek-testte
 *      fixture hep saptirir, gard hic kosmazdi).
 *  1b. test_audio_lock_scene_restore_stopped_bgm_stays_stopped: DURMUS-BGM
 *      fixture (snapshot bgmPlaying=false; a1 Telephone + 0.9 + gecerli B'ye
 *      switch ile BGM baslatir, SONRA a2 throw eder). catch else-stop dali
 *      stopBgm cagirir. Oldurur: ELSE-STOP stopBgm SILME (isBgmPlaying false
 *      + path-bos pini KIZARIR) + kazanc/filtre satir silmeleri. Onceki
 *      tek-testte snapshot hep playing oldugu icin else dali ULASILAMAZDI
 *      (stopBgm silinmesi yesil kalirdi).
 *  (Ambience/Ui METINDEN CIKARILDI: sahne comp yolu + AudioSnapshot uretimde
 *   bu alanlara hic dokunmaz (alan yok); sahne-esitlik pini her mutantta
 *   tutardi (dissiz). Yerine Test-3'teki save-sema yabanci-anahtar ignore-pini
 *   gecti — gerekce orada.)
 *  (Uc-kanal-saptirir iddiasi DUSURULDU: sahne comp yolu uretimde yalniz
 *   bgmVol/filtre/BGM-path saptirir; sfx_track m_isPlaying kapilidir ve
 *   fixture'da yoktur. master/sfx/voice Test-1'de yalniz snapshot-tasima
 *   pinidir (capture-alani silme mutantini oldurur: baseline 0.5/0.75/0.125
 *   restore'da birebir aranir); dort-kanal apply-side Test-2'dedir
 *   (load/rewind dort kanali da bastan yazar).)
 *  2. test_audio_lock_mixer_persistence: volume setter'lar state'e commitler
 *     (step ilerlemez), save/load + rewind tam mikseri restore eder, rewind
 *     playtime'i senkronlar. Mutantlar: commit cagrilarinin kaldirilmasi,
 *     restoreAudioStateFromGameState'te master/sfx/voice satirlarinin
 *     silinmesi, rewind playtime senkronunun kaldirilmasi.
 *  3. test_audio_lock_save_format_v4_mixer: v4 round-trip + withMixerVolumes
 *     sozlesmesi + legacy Migrated + yabanci-red + bozuk-mikser red
 *     (HER KANAL icin SINIR esigi: 1.0+eps disi / -eps disi / NaN / string-tip
 *     InvalidData, 0.0/1.0 ici Loaded; 2.0/-0.5 daraltilmis bandi
 *     yakalayamazdi) + save-sema YABANCI-ANAHTAR ignore-pini (ambience/ui
 *     anahtarlari mikseri oynatmaz, Loaded korunur; anahtar-baglama mutantini
 *     oldurur. strict-red uretim degisikligi isterdi (test-only turda yasak),
 *     o yuzden ignore-pini secildi.)
 *
 * Cihaz bagimsizdir (kazanc/filtre setter'lari + snapshot'lar SDL cihazina
 * dokunmaz; BGM miss fail-closed'dur; lastError mandali her iki modda da
 * ayni calisir: miss yazar, replay siler): requireAudioDeviceOrSkip YOKTUR,
 * cihazsiz kosuda da calisir. Timing-assert YOKTUR; tum karsilastirmalar
 * kayitli deger/float-tam esitliktir (0.25'in katlari ikili-tamdir).
 */
namespace {

std::string lock86ProjectRoot(const char* tag) {
    static int counter = 0;
    std::ostringstream name;
    name << "rowl_audio_lock86_" << tag << "_" << ++counter;
    auto dir = std::filesystem::temp_directory_path() / name.str();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

void lock86Check(bool condition, const std::string& what) {
    if (!condition) lockFail("#86: " + what);
}

RowlEngineHandle lock86CreateEngine(const std::string& root) {
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) lockFail("#86: Create failed");
    RowlEngine_SetProjectDirectory(handle, root.c_str());
    if (!RowlEngine_Init(handle, 320, 180, 0)) lockFail("#86: Init failed");
    return handle;
}

void lock86WriteTwoNodeGraph(const std::string& path) {
    std::ofstream graph(path);
    graph << R"({"format_version":4,"start_node_id":101,"nodes":[
      {"id":101,"speaker":"Guide","dialogue":"First.","next_nodes":[{"id":102}]},
      {"id":102,"speaker":"Guide","dialogue":"Second."}]})";
}

void lock86CheckMixer(RowlEngineHandle handle, float master, float bgm,
                      float sfx, float voice, const std::string& tag) {
    lock86Check(RowlEngine_GetMasterVolume(handle) == master, tag + ": master");
    lock86Check(RowlEngine_GetBgmVolume(handle) == bgm, tag + ": bgm");
    lock86Check(RowlEngine_GetSfxVolume(handle) == sfx, tag + ": sfx");
    lock86Check(RowlEngine_GetVoiceVolume(handle) == voice, tag + ": voice");
    auto* engine = Rowl::Core::testEngineFromHandle(handle);
    lock86Check(engine != nullptr, tag + ": test bridge null");
    const auto state = engine->getGameState();
    lock86Check(state != nullptr, tag + ": null game state");
    lock86Check(state->masterVolume == master, tag + ": state master");
    lock86Check(state->bgmVolume == bgm, tag + ": state bgm");
    lock86Check(state->sfxVolume == sfx, tag + ": state sfx");
    lock86Check(state->voiceVolume == voice, tag + ": state voice");
}

double lock86SlotPlaytime(RowlEngineHandle handle, int32_t slot) {
    uint32_t required = 0;
    if (RowlEngine_GetSaveSlotMetadataJson(handle, slot, nullptr, 0,
                                            &required) != ROWL_RESULT_OK ||
        required < 1) {
        lockFail("#86: metadata size query failed");
    }
    std::vector<char> buffer(required, '\0');
    uint32_t repeated = 0;
    if (RowlEngine_GetSaveSlotMetadataJson(handle, slot, buffer.data(),
                                            static_cast<uint32_t>(buffer.size()),
                                            &repeated) != ROWL_RESULT_OK) {
        lockFail("#86: metadata copy failed");
    }
    return nlohmann::json::parse(buffer.data()).at("playtime_seconds").get<double>();
}

// Test-1 a1 comp'u: gecerli mutasyon (DSP + bgmVol + gecerli B'ye BGM
// switch). B streaming OGG'dur: cihazli RAM yolu (audio_engine.cpp'de
// playAudio govdesindeki applyDspFilter) Telephone'u ezerdi; streaming dali
// (openBgmStream) canli filtreye dokunmaz — uc boyutta da sapma her iki
// modda da korunur. a2: ertelenmis blokta deterministik throw (string
// volume -> nlohmann type_error).
const char* kLock86A1Json =
    R"({"type":"audio","id":"a1","enabled":true,
        "data":{"dsp_filter":"Telephone","volume":0.9,)"
    R"("bgm_track":"audio/lock86_bgm_b.ogg","bgm_transition":"instant"}})";
const char* kLock86A2Json =
    R"({"type":"audio","id":"a2","enabled":true,
        "data":{"dsp_filter":"Normal","volume":"loud"}})";

// #86 sahne-fixture dosyalari motor Init'inden ONCE yazilir (VFS project
// kokunden cozer: "audio/x" -> Assets/audio/x; miss-guard projesindeki
// teknik aynen). Yalniz BGM dosyalari: Ambience/Ui metinden cikarildi
// (uretimde alan yok), sahne-esitlik pinleri tasiyan amb/ui wav'lere gerek
// kalmadi.
void lock86WriteSceneAudioFixtures(const std::string& root) {
    const auto dir = std::filesystem::path(root) / "Assets" / "audio";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    writeBytes(dir / "lock86_bgm_a.wav",
               makeFloatWavMono44100(std::vector<float>(64, 0.5f)));
    // a1'in B parcasi: over-threshold OGG (header probe + decoder gercek).
    writeBytes(dir / "lock86_bgm_b.ogg",
               missGuardPatchGranuleForStream(missGuardLongToneOggBytes(),
                                              static_cast<uint64_t>(1000000000)));
}

// Bilinen mikser + Normal filtre kurar. BGM'e DOKUNMAZ (calan/durmus
// ayrimi her testin kendi kurulumudur).
void lock86MixerBaseline(RowlEngineHandle handle,
                         Rowl::Audio::AudioEngine* audio) {
    RowlEngine_SetMasterVolume(handle, 0.5f);
    RowlEngine_SetBgmVolume(handle, 0.25f);
    RowlEngine_SetSfxVolume(handle, 0.75f);
    RowlEngine_SetVoiceVolume(handle, 0.125f);
    audio->applyDspFilter(Rowl::Audio::DSPFilterType::Normal);
    lock86Check(audio->getMasterVolume() == 0.5f, "baseline master kurulamadi");
    lock86Check(audio->getBgmVolume() == 0.25f, "baseline bgmVol kurulamadi");
    lock86Check(audio->getSfxVolume() == 0.75f, "baseline sfxVol kurulamadi");
    lock86Check(audio->getVoiceVolume() == 0.125f, "baseline voiceVol kurulamadi");
    lock86Check(audio->getActiveFilter() == Rowl::Audio::DSPFilterType::Normal,
                "baseline filter not Normal");
}

// BGM-A niyeti kurar (varlik-gate'li: dosya fixture'da mevcut).
void lock86PlayBgmA(Rowl::Audio::AudioEngine* audio) {
    audio->playAudio("audio/lock86_bgm_a.wav", Rowl::Audio::AudioChannelType::Bgm);
    lock86Check(audio->isBgmPlaying(), "baseline BGM intent kurulamadi");
    lock86Check(audio->getCurrentBgmPath() == "audio/lock86_bgm_a.wav",
                "baseline BGM path yanlis");
}

} // namespace

void test_audio_lock_scene_restore_no_restart_on_clean_throw() {
    TEST_SECTION("Scene-Restore No-Restart on Clean Throw (#86/1a)");

    const std::string root = lock86ProjectRoot("scene-a");
    lock86WriteSceneAudioFixtures(root);
    RowlEngineHandle handle = lock86CreateEngine(root);
    auto* engine = Rowl::Core::testEngineFromHandle(handle);
    lock86Check(engine != nullptr && engine->getAudio() != nullptr &&
                    engine->getCamera() != nullptr,
                "engine/audio/camera missing");
    auto* audio = engine->getAudio();
    auto* camera = engine->getCamera();

    audio->setOutputSuspended(true);

    // Kurulum: bilinen mikser + calan BGM-A. Fixture HIC sapma icermez:
    // sahnede yalniz a2 vardir (ertelenmis blokta ilk ses satirindan ONCE
    // throw eder — volume tip-hatasi deger okumada firlar, hicbir ses
    // setter'i kosmaz). Snapshot == canli durum oldugu icin sapma-gardi
    // burada KOSAR (skip dali).
    lock86MixerBaseline(handle, audio);
    lock86PlayBgmA(audio);

    // lastError mandali: SFX-miss yapiskan hata yazar (#87: BGM niyetine
    // dokunmaz — asagida intent pinlenir). Cihazli/sessiz her iki modda da
    // miss hata yazar; playAudio/stopBgm basinda clearLastError kosar.
    // Gard'li restore setter'lardan ibarettir (mandal korunur); gard'siz
    // replay ayni parcayi yeniden calip mandali siler -> KIZARIR.
    audio->playAudio("audio/lock86_missing_sfx.wav",
                     Rowl::Audio::AudioChannelType::Sfx);
    const std::string plantedError = audio->getLastError();
    lock86Check(!plantedError.empty(), "SFX-miss lastError ekemedi");
    lock86Check(audio->isBgmPlaying() &&
                    audio->getCurrentBgmPath() == "audio/lock86_bgm_a.wav",
                "SFX-miss BGM niyetini yikti (#87 ihlal?)");

    RowlEngine_SetCamera(handle, 100.0f, 200.0f, 2.0f);
    const auto beforeState = engine->getGameState();
    const std::string beforeBackground = engine->getActiveBackground();

    // Zincir: background + kamera mutasyonu, SONRA yalniz a2 (throw).
    // updateScene catch'i hatayi yutar ve geri alir; disariya throw cikmaz.
    const std::string sceneJson =
        std::string("[") +
        R"({"type":"background","id":"bg","enabled":true,
             "data":{"texture":"bg_changed.png"}},)" +
        R"({"type":"camera","id":"cam","enabled":true,
             "data":{"x":500.0,"y":500.0,"zoom":3.0}},)" +
        kLock86A2Json + "]";
    RowlEngine_UpdateSceneFromJson(handle, sceneJson.c_str());

    // Gozlem cubugu: sahne-gorsel + ses + kamera esitligi + mandal.
    lock86Check(engine->getGameState() == beforeState,
                "game state pointer not restored");
    lock86Check(engine->getActiveBackground() == beforeBackground,
                "background not restored");
    lock86Check(camera->getPositionX() == 100.0f &&
                    camera->getPositionY() == 200.0f,
                "camera position not restored");
    lock86Check(camera->getZoom() == 2.0f, "camera zoom not restored");
    lock86Check(camera->getRotation() == 0.0f, "camera rotation not restored");
    lock86Check(audio->getMasterVolume() == 0.5f, "master not restored");
    lock86Check(audio->getBgmVolume() == 0.25f, "bgm volume not restored");
    lock86Check(audio->getSfxVolume() == 0.75f, "sfx volume not restored");
    lock86Check(audio->getVoiceVolume() == 0.125f, "voice volume not restored");
    lock86Check(audio->getActiveFilter() == Rowl::Audio::DSPFilterType::Normal,
                "dsp filter not restored");
    lock86Check(audio->isBgmPlaying(), "BGM intent dustu (isBgmPlaying false)");
    lock86Check(audio->getCurrentBgmPath() == "audio/lock86_bgm_a.wav",
                std::string("BGM path degisti (restart/replay?): '") +
                    audio->getCurrentBgmPath() + "'");
    // Sapma-gardi pini: ayni-parca replay olmadi (olaydi clearLastError
    // mandali silerdi).
    lock86Check(audio->getLastError() == plantedError,
                "lastError mandali silindi (ayni-parca replay? sapma-gardi dusmus?)");
    TEST_PASS("Scene restore clean-throw — no BGM restart (guard + error-latch)");

    audio->setOutputSuspended(false);
    RowlEngine_Destroy(handle);
}

void test_audio_lock_scene_restore_stopped_bgm_stays_stopped() {
    TEST_SECTION("Scene-Restore Stopped BGM Stays Stopped (#86/1b)");

    const std::string root = lock86ProjectRoot("scene-b");
    lock86WriteSceneAudioFixtures(root);
    RowlEngineHandle handle = lock86CreateEngine(root);
    auto* engine = Rowl::Core::testEngineFromHandle(handle);
    lock86Check(engine != nullptr && engine->getAudio() != nullptr &&
                    engine->getCamera() != nullptr,
                "engine/audio/camera missing");
    auto* audio = engine->getAudio();
    auto* camera = engine->getCamera();

    audio->setOutputSuspended(true);

    // Durmus-BGM kurulumu: mikser bilinir, BGM HIC baslatilmaz (snapshot
    // bgmPlaying=false).
    lock86MixerBaseline(handle, audio);
    lock86Check(!audio->isBgmPlaying(), "kurulumda BGM calmamali");

    // Dis-disi bacak: a1 TEK BASINA (throwsuz sahnede) once kosar — BGM
    // baslatir + bgmVol + filtre saptirir. a1 notrlenirse bu adim duser.
    {
        const std::string teeth = std::string("[") + kLock86A1Json + "]";
        RowlEngine_UpdateSceneFromJson(handle, teeth.c_str());
        lock86Check(audio->getActiveFilter() == Rowl::Audio::DSPFilterType::Telephone,
                    "teeth: a1 filtreyi oynatmadi");
        lock86Check(audio->getBgmVolume() == 0.9f, "teeth: a1 bgmVol'u oynatmadi");
        lock86Check(audio->isBgmPlaying() &&
                        audio->getCurrentBgmPath() == "audio/lock86_bgm_b.ogg",
                    "teeth: a1 BGM baslatmadi (B'ye switch olmadi)");
    }
    TEST_PASS("Scene-restore teeth (a1: BGM-start + bgmVol + filter saptirir)");

    // Durmus snapshot'a don: stop + mikser sifirla. Snapshot canli degerleri
    // yakalar; game-state pointer'i da burada pinlenir.
    audio->stopBgm();
    lock86MixerBaseline(handle, audio);
    lock86Check(!audio->isBgmPlaying(), "reset sonrasi BGM durmali");
    lock86Check(audio->getCurrentBgmPath().empty(), "reset sonrasi BGM path bos olmali");
    RowlEngine_SetCamera(handle, 100.0f, 200.0f, 2.0f);
    const auto beforeState = engine->getGameState();
    const std::string beforeBackground = engine->getActiveBackground();

    // Zincir: background + kamera + gecerli a1 (BGM baslatir), SONRA a2
    // throw eder. catch else-stop dali stopBgm cagirmalidir.
    const std::string sceneJson =
        std::string("[") +
        R"({"type":"background","id":"bg","enabled":true,
             "data":{"texture":"bg_changed.png"}},)" +
        R"({"type":"camera","id":"cam","enabled":true,
             "data":{"x":500.0,"y":500.0,"zoom":3.0}},)" +
        kLock86A1Json + "," + kLock86A2Json + "]";
    RowlEngine_UpdateSceneFromJson(handle, sceneJson.c_str());

    // Gozlem cubugu: gorsel + mikser + kamera + DURMUS-BGM niyeti.
    lock86Check(engine->getGameState() == beforeState,
                "game state pointer not restored");
    lock86Check(engine->getActiveBackground() == beforeBackground,
                "background not restored");
    lock86Check(camera->getPositionX() == 100.0f &&
                    camera->getPositionY() == 200.0f,
                "camera position not restored");
    lock86Check(camera->getZoom() == 2.0f, "camera zoom not restored");
    lock86Check(camera->getRotation() == 0.0f, "camera rotation not restored");
    lock86Check(audio->getMasterVolume() == 0.5f, "master not restored");
    lock86Check(audio->getBgmVolume() == 0.25f, "bgm volume not restored (a1 0.9 sizdi?)");
    lock86Check(audio->getSfxVolume() == 0.75f, "sfx volume not restored");
    lock86Check(audio->getVoiceVolume() == 0.125f, "voice volume not restored");
    lock86Check(audio->getActiveFilter() == Rowl::Audio::DSPFilterType::Normal,
                "dsp filter not restored (a1 Telephone sizdi?)");
    // Else-stop pini: a1 BGM baslatmisti; stopBgm'siz restore burada playing
    // + B path'te kalir.
    lock86Check(!audio->isBgmPlaying(),
                "BGM durmadi (else-stop stopBgm dusmus?)");
    lock86Check(audio->getCurrentBgmPath().empty(),
                std::string("BGM path temizlenmedi: '") + audio->getCurrentBgmPath() + "'");
    TEST_PASS("Scene restore stopped-BGM — else-stop kills started BGM");

    audio->setOutputSuspended(false);
    RowlEngine_Destroy(handle);
}

void test_audio_lock_mixer_persistence() {
    TEST_SECTION("Mixer Persistence (#86)");

    const std::string root = lock86ProjectRoot("mixer");
    RowlEngineHandle handle = lock86CreateEngine(root);
    const std::string graphPath = root + "/two_node.json";
    lock86WriteTwoNodeGraph(graphPath);
    RowlEngine_LoadStoryGraph(handle, graphPath.c_str());
    RowlEngine_Step(handle, 0.0f);
    RowlEngine_AdvanceNode(handle, 0);
    RowlEngine_Step(handle, 0.0f);
    lock86Check(RowlEngine_GetCurrentNodeId(handle) == 102, "setup node != 102");

    // Setter commit: state'e damgalar ama adim ilerletmez.
    const uint64_t step0 = RowlEngine_GetCurrentStepId(handle);
    RowlEngine_SetMasterVolume(handle, 0.5f);
    RowlEngine_SetBgmVolume(handle, 0.25f);
    RowlEngine_SetSfxVolume(handle, 0.75f);
    RowlEngine_SetVoiceVolume(handle, 0.125f);
    lock86Check(RowlEngine_GetCurrentStepId(handle) == step0,
                "setter commit advanced stepId");
    lock86CheckMixer(handle, 0.5f, 0.25f, 0.75f, 0.125f, "setter-commit");

    // Pozitif-kontrol: kaydet -> saptir -> yukle -> mikser geri doner.
    lock86Check(RowlEngine_SaveGameSlotResult(handle, 1) == ROWL_RESULT_OK,
                "control save failed");
    RowlEngine_SetMasterVolume(handle, 1.0f);
    RowlEngine_SetBgmVolume(handle, 0.5f);
    RowlEngine_SetSfxVolume(handle, 0.25f);
    RowlEngine_SetVoiceVolume(handle, 0.75f);
    lock86Check(RowlEngine_LoadGameSlotResult(handle, 1) == ROWL_RESULT_OK,
                "control load failed");
    lock86CheckMixer(handle, 0.5f, 0.25f, 0.75f, 0.125f, "load-restore");
    TEST_PASS("Mixer setter-commit + save/load restore (no step bump)");

    // Rewind tam mikseri restore eder (bgm-disi kazanc mutantini oldurur).
    RowlEngine_SetVariable(handle, "m86", "v");
    RowlEngine_SetMasterVolume(handle, 1.0f);
    RowlEngine_SetBgmVolume(handle, 0.5f);
    RowlEngine_SetSfxVolume(handle, 0.25f);
    RowlEngine_SetVoiceVolume(handle, 0.75f);
    lock86Check(RowlEngine_Rewind(handle, 1) == 1, "rewind failed");
    lock86CheckMixer(handle, 0.5f, 0.25f, 0.75f, 0.125f, "rewind-restore");
    TEST_PASS("Mixer rewind restore (master/sfx/voice)");

    // Rewind playtime senkronu (load 2499 karsiligi): slot metadata
    // playtime_seconds gozlemi. 0.25 adimlari ikili-tamdir.
    RowlEngine_SetPlayState(handle, 1);
    RowlEngine_Step(handle, 0.25f);
    RowlEngine_Step(handle, 0.25f);
    lock86Check(RowlEngine_SaveGameSlotResult(handle, 3) == ROWL_RESULT_OK,
                "playtime save failed");
    lock86Check(lock86SlotPlaytime(handle, 3) == 0.5, "slot3 playtime != 0.5");
    RowlEngine_Step(handle, 0.25f);
    RowlEngine_Step(handle, 0.25f);
    RowlEngine_SetVariable(handle, "m86p", "v");
    lock86Check(RowlEngine_Rewind(handle, 1) == 1, "playtime rewind failed");
    lock86Check(RowlEngine_SaveGameSlotResult(handle, 4) == ROWL_RESULT_OK,
                "post-rewind save failed");
    // Fix'siz 1.0 (bayat), fix'li 0.5 (hedef state'in playtime'i).
    lock86Check(lock86SlotPlaytime(handle, 4) == 0.5,
                "rewind did not sync playtime (stale 1.0?)");
    TEST_PASS("Rewind playtime sync (slot metadata 0.5, not stale 1.0)");

    RowlEngine_Destroy(handle);
}

void test_audio_lock_save_format_v4_mixer() {
    TEST_SECTION("Save Format v4 Mixer (#86)");
    using Rowl::State::GameState;

    // withMixerVolumes: deger + step-sabiti + zincir-paylasimi.
    const auto s0 = GameState::createInitialState();
    const uint64_t step0 = s0->stepId;
    const auto s1 = GameState::withMixerVolumes(s0, 0.5f, 0.25f, 0.75f, 0.125f);
    lock86Check(s1 != nullptr, "withMixerVolumes null");
    lock86Check(s1->stepId == step0, "withMixerVolumes advanced stepId");
    lock86Check(s1->previousState == s0->previousState,
                "withMixerVolumes extended rewind chain");
    lock86Check(s1->masterVolume == 0.5f && s1->bgmVolume == 0.25f &&
                    s1->sfxVolume == 0.75f && s1->voiceVolume == 0.125f,
                "withMixerVolumes values");
    // Null -> nullptr.
    lock86Check(GameState::withMixerVolumes(nullptr, 0.5f, 0.5f, 0.5f, 0.5f) == nullptr,
                "withMixerVolumes null must return nullptr");
    // Non-finite korur, sonlu clamp'lenir.
    const auto s2 = GameState::withMixerVolumes(
        s1, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), 0.5f);
    lock86Check(s2->masterVolume == 0.5f && s2->bgmVolume == 0.25f &&
                    s2->sfxVolume == 0.75f && s2->voiceVolume == 0.5f,
                "withMixerVolumes non-finite must keep current");
    const auto s3 = GameState::withMixerVolumes(s1, 5.0f, -2.0f, 0.25f, 0.75f);
    lock86Check(s3->masterVolume == 1.0f && s3->bgmVolume == 0.0f &&
                    s3->sfxVolume == 0.25f && s3->voiceVolume == 0.75f,
                "withMixerVolumes must clamp to [0,1]");
    TEST_PASS("withMixerVolumes contract (values, no-step, null, non-finite, clamp)");

    // v4 round-trip: Loaded, kaynak 4, mikser birebir.
    const std::string json = s1->serializeJson();
    const auto res = GameState::decodeJson(json);
    lock86Check(res.succeeded() &&
                    res.status == Rowl::State::GameStateDecodeStatus::Loaded &&
                    res.sourceVersion == 4,
                "v4 must decode Loaded/4");
    lock86Check(res.state->masterVolume == 0.5f && res.state->bgmVolume == 0.25f &&
                    res.state->sfxVolume == 0.75f && res.state->voiceVolume == 0.125f,
                "v4 mixer round-trip");
    TEST_PASS("Save format v4 round-trip (Loaded, mixer intact)");

    // Legacy v3: Migrated + mikser defaultlari.
    auto legacy = nlohmann::json::parse(json);
    legacy["version"] = 3;
    legacy.erase("master_volume");
    legacy.erase("sfx_volume");
    legacy.erase("voice_volume");
    const auto res3 = GameState::decodeJson(legacy.dump());
    lock86Check(res3.succeeded() &&
                    res3.status == Rowl::State::GameStateDecodeStatus::Migrated &&
                    res3.sourceVersion == 3,
                "v3 must decode Migrated/3");
    lock86Check(res3.state->masterVolume == 1.0f && res3.state->sfxVolume == 1.0f &&
                    res3.state->voiceVolume == 1.0f && res3.state->bgmVolume == 0.25f,
                "v3 mixer defaults (bgm preserved)");
    TEST_PASS("Legacy v3 Migrated (mixer defaults 1.0)");

    // Yabanci sürüm reddedilir.
    auto foreign = nlohmann::json::parse(json);
    foreign["version"] = 5;
    const auto res5 = GameState::decodeJson(foreign.dump());
    lock86Check(!res5.succeeded() &&
                    res5.status == Rowl::State::GameStateDecodeStatus::UnsupportedVersion,
                "v5 must be UnsupportedVersion");
    TEST_PASS("Foreign version rejected (UnsupportedVersion)");

    // Bozuk-mikser HER KANAL icin SINIR + tip problari: (1.0+eps disi,
    // -eps disi, NaN, string-tip) x (master, bgm, sfx, voice); gecerli
    // sinirlar 0.0/1.0 Loaded. 2.0/-0.5 daraltilmis bandi (orn. >1.001 kabul
    // eden gevsetme) yakalayamazdi; eps problar bandi birebir pinler.
    // NaN dump'ta null'a iner, string-tip dogrudan type_error uretir — ikisi
    // de decode catch-yolundan InvalidData'ya duser (varsayilan status).
    auto requireInvalidMixer = [&](const char* key, nlohmann::json badValue,
                                   const char* probe) {
        auto mutated = nlohmann::json::parse(json);
        mutated[key] = std::move(badValue);
        const auto res = GameState::decodeJson(mutated.dump());
        const std::string tag =
            std::string("bozuk-mikser [") + key + "/" + probe + "]";
        lock86Check(!res.succeeded(), tag + " kabul edildi");
        lock86Check(res.status == Rowl::State::GameStateDecodeStatus::InvalidData,
                    tag + " InvalidData degil");
    };
    auto requireValidMixerEdge = [&](const char* key, double edgeValue) {
        auto mutated = nlohmann::json::parse(json);
        mutated[key] = edgeValue;
        const auto res = GameState::decodeJson(mutated.dump());
        const std::string tag =
            std::string("mikser-sinir-ici [") + key + "=" +
            std::to_string(edgeValue) + "]";
        lock86Check(res.succeeded() &&
                        res.status == Rowl::State::GameStateDecodeStatus::Loaded,
                    tag + " reddedildi");
        const float got = (std::string(key) == "master_volume") ? res.state->masterVolume :
                          (std::string(key) == "bgm_volume") ? res.state->bgmVolume :
                          (std::string(key) == "sfx_volume") ? res.state->sfxVolume :
                                                               res.state->voiceVolume;
        lock86Check(got == static_cast<float>(edgeValue), tag + " deger kaydi");
    };
    const char* kMixerKeys[4] = {"master_volume", "bgm_volume", "sfx_volume", "voice_volume"};
    for (const char* key : kMixerKeys) {
        requireInvalidMixer(key, 1.0001, "ust-sinir-disi");
        requireInvalidMixer(key, -0.0001, "alt-sinir-disi");
        requireInvalidMixer(key, std::numeric_limits<double>::quiet_NaN(), "nan");
        requireInvalidMixer(key, "loud", "string-tip");
        requireValidMixerEdge(key, 0.0);
        requireValidMixerEdge(key, 1.0);
    }
    TEST_PASS("Bozuk-mikser per-channel boundary InvalidData + edge Loaded (4 kanal)");
    // Save-sema YABANCI-ANAHTAR ignore-pini (Ambience/Ui'nin dis karsiligi):
    // semada olmayan ambience/ui anahtarlari mikseri oynatmaz, decode Loaded
    // kalir. Anahtar-baglama mutantini (orn. ambience_volume'u sfx'e yazan)
    // oldurur. strict-red uretim degisikligi isterdi (test-only turda yasak),
    // o yuzden ignore-pini secildi.
    {
        auto hostileKeys = nlohmann::json::parse(json);
        hostileKeys["ambience_volume"] = 0.0;
        hostileKeys["ui_volume"] = 0.0;
        hostileKeys["ambience_track"] = "audio/lock86_amb.wav";
        hostileKeys["ui_track"] = "audio/lock86_ui.wav";
        const auto resKeys = GameState::decodeJson(hostileKeys.dump());
        lock86Check(resKeys.succeeded() &&
                        resKeys.status == Rowl::State::GameStateDecodeStatus::Loaded,
                    "yabanci-anahtar decode bozmamali (Loaded)");
        lock86Check(resKeys.state->masterVolume == 0.5f &&
                        resKeys.state->bgmVolume == 0.25f &&
                        resKeys.state->sfxVolume == 0.75f &&
                        resKeys.state->voiceVolume == 0.125f,
                    "yabanci-anahtar mikseri oynatti (anahtar sizintisi?)");
    }
    TEST_PASS("Unknown mixer keys ignored (schema ignore-pin)");
}

// Hedef #74: typewriter-blip yazimlari ile host telemetri/hata okumalari
// arasindaki data-race kilidi. N thread x M blip (bos asset: VFS/havuz
// dokunulmaz, synth/erken-donus yolu), join sonrasi tam-esitlik:
//   getVoiceBlipCount() == N*M (kayipsiz; plain-sayac mutantinda kayip olur),
//   getSynthBlipCount() <= voice (A5-tur3 konvansiyonu),
//   getDropCount() == 0 (drop sayaci karismaz).
// Fixture gerekcesi ("" yolu hukmu): playVoiceBlip govdesinde "" icin
// erken-return YOKTUR — `if (!assetPath.empty())` blogu atlanir, akis
// `if (!assetPlayed)` synth dalina girer (cihaz/kuyruk tam-yolu). Sayac
// artisi cihaz kapisindan (`if (!m_deviceAvailable) return;`) ONCE yapilir,
// yani cihazsiz kosuda da calisir, SKIP YOKTUR. Bu yuzden mevcut "" fixture
// tam-yol hammer'dir; erken-return kolu ayri mini teste aittir (burada YOK).
// warn-once notu: playVoiceBlip ROWL_LOG_WARN'u dogrudan kullanir;
// warnAudioOnce sayac kancasi YOKTUR (gozlenebilirlik yok) — bu yuzden
// uretim degisikligi YAPILMADI, yalnizca test guclendirildi.
// Pitch/telemetri tam-deger iddiasi YOKTUR (kilit: sayim + sonluluk).
// Sayac cihaz kapisindan ONCE artar: cihazsiz kosuda da calisir, SKIP YOKTUR.
// Timing-assert YOKTUR (sadece join + tam-esitlik).
void test_audio_lock_voice_blip_concurrent_counts() {
    TEST_SECTION("Audio Voice-Blip Concurrent Counts Lock (#74)");

    Rowl::VFS::VFSManager vfs;
    Rowl::Audio::AudioEngine audio(&vfs);
    if (!audio.initialize() || !audio.isInitialized()) {
        lockFail("Voice-Blip Concurrent Counts (#74): audio init failed");
    }
    audio.resetVoiceBlipCount();

    constexpr int kThreads = 8;
    constexpr int kBlipsPerThread = 250;
    constexpr uint32_t kExpected = static_cast<uint32_t>(kThreads * kBlipsPerThread);

    // Okuyucu-tutarlilik: her okuyucu yerel min/max + monotonluk + sonluluk
    // izler (yerel izleme — ek atomik cekisme yok; birlesim join sonrasi).
    struct ReaderObs {
        uint32_t minVoice = UINT32_MAX;
        uint32_t maxVoice = 0;
        uint32_t lastVoice = 0;
        bool hasSample = false;
        bool sawDecrease = false;
        bool sawNonFinite = false;
    };
    std::atomic<bool> readersRun{true};
    auto readerBody = [&](ReaderObs& obs) {
        while (readersRun.load()) {
            const std::string err = audio.getLastError();
            const uint32_t voice = audio.getVoiceBlipCount();
            const uint32_t synth = audio.getSynthBlipCount();
            const bool playing = audio.isVoicePlaying();
            const float pitch = audio.getLastVoiceBlipPitch();
            const float peak = audio.getChannelPeak(1, 0);
            const float rms = audio.getChannelRms(1, 0);
            const float peakR = audio.getChannelPeak(1, 1);
            const float rmsR = audio.getChannelRms(1, 1);
            (void)err;
            (void)synth;
            (void)playing;
            if (voice < obs.minVoice) obs.minVoice = voice;
            if (voice > obs.maxVoice) obs.maxVoice = voice;
            if (obs.hasSample && voice < obs.lastVoice) obs.sawDecrease = true;
            obs.lastVoice = voice;
            obs.hasSample = true;
            if (!std::isfinite(pitch) || !std::isfinite(peak) || !std::isfinite(rms) ||
                !std::isfinite(peakR) || !std::isfinite(rmsR)) {
                obs.sawNonFinite = true;
            }
        }
    };
    ReaderObs obsA;
    ReaderObs obsB;
    std::thread readerA([&]() { readerBody(obsA); });
    std::thread readerB([&]() { readerBody(obsB); });

    auto hammerBody = [&](int threadIndex) {
        // Turlu pitch: her karakter farkli pitch'le gelir (engine.cpp
        // typewriter yolu emsali); degere bakilmaz, sadece yazim yarisi.
        const float pitch = 0.9f + 0.05f * static_cast<float>(threadIndex);
        for (int i = 0; i < kBlipsPerThread; ++i) {
            audio.playVoiceBlip("", pitch, 0.85f,
                                Rowl::Audio::AudioChannelType::Voice);
        }
    };
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back(hammerBody, t);
    }
    for (auto& worker : workers) worker.join();
    readersRun.store(false);
    readerA.join();
    readerB.join();

    const uint32_t voice = audio.getVoiceBlipCount();
    if (voice != kExpected) {
        lockFail("Voice-Blip Concurrent Counts (#74): kayip sayim (voice=" +
                 std::to_string(voice) + ", beklenen=" + std::to_string(kExpected) + ")");
    }
    TEST_PASS("Audio Voice-Blip Counts — N x M kayipsiz (voice==2000)");

    const uint32_t synth = audio.getSynthBlipCount();
    if (!(synth <= voice)) {
        lockFail("Voice-Blip Concurrent Counts (#74): synth konvansiyonu bozuldu");
    }
    TEST_PASS("Audio Voice-Blip Counts — synth<=voice konvansiyonu");

    if (audio.getDropCount() != 0u) {
        lockFail("Voice-Blip Concurrent Counts (#74): drop sayaci karisti");
    }
    TEST_PASS("Audio Voice-Blip Counts — drop karismaz (0)");

    // Okuyucu-tutarlilik birlesimi: final ornek (ana thread) gozleme katilir;
    // boylece gozlenen-max == 2000 deterministiktir (okuyucu son ornegi
    // kacirabilir; max'in kaynagi final ornektir, monotonluk okuyucudan).
    uint32_t obsMax = obsA.maxVoice;
    if (obsB.maxVoice > obsMax) obsMax = obsB.maxVoice;
    if (voice > obsMax) obsMax = voice;
    if (!obsA.hasSample || !obsB.hasSample) {
        lockFail("Voice-Blip Concurrent Counts (#74): okuyucu ornek uretemedi");
    }
    if (obsA.sawDecrease || obsB.sawDecrease) {
        lockFail("Voice-Blip Concurrent Counts (#74): okuyucu azalan sayim gordu");
    }
    if (obsMax != kExpected) {
        lockFail("Voice-Blip Concurrent Counts (#74): gozlenen max tutarsiz (max=" +
                 std::to_string(obsMax) + ", beklenen=" + std::to_string(kExpected) + ")");
    }
    TEST_PASS("Audio Voice-Blip Counts — okuyucu gozlemi monoton-artan (max==2000)");

    const float finalPitch = audio.getLastVoiceBlipPitch();
    const float finalPeak = audio.getChannelPeak(1, 0);
    const float finalRms = audio.getChannelRms(1, 0);
    const float finalPeakR = audio.getChannelPeak(1, 1);
    const float finalRmsR = audio.getChannelRms(1, 1);
    if (obsA.sawNonFinite || obsB.sawNonFinite ||
        !std::isfinite(finalPitch) || !std::isfinite(finalPeak) || !std::isfinite(finalRms) ||
        !std::isfinite(finalPeakR) || !std::isfinite(finalRmsR)) {
        lockFail("Voice-Blip Concurrent Counts (#74): pitch/telemetri sonlu-kalmadi");
    }
    TEST_PASS("Audio Voice-Blip Counts — pitch/telemetri sonlu-kalir");

    audio.stopAll();
    audio.shutdown();
}
