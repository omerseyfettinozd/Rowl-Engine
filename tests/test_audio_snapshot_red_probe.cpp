/**
 * test_audio_snapshot_red_probe.cpp — D02 audio snapshot-struct davranışsız taşıma kilidi.
 *
 * Bağımsız ikili: rowl_audio_snapshot_red_probe (ctest -R audio_snapshot_red_probe).
 * D01 konvansiyonu: yalnızca public C API + dummy driver (SDL_AUDIODRIVER=dummy,
 * SDL_VIDEODRIVER=dummy). rowl_tests gövdesine gömülmez.
 *
 * Bacak 1 — taşıma-öncesi bypass durum okuması (RED'i veren gözlem):
 *   Prob, henüz var olmayan yeni TU karşılığını doğrular ve KIRMIZI düşer.
 *   Somut senaryo: boş-asset synth blip sonrası snapshot'ın BOŞ + ctx kodunun 0
 *   olduğunu VE bu temizleme davranışının yeni `audio_blip_error_channel`
 *   Locked-yardımcısından geldiğini iddia eden yapısal assertion — taşıma öncesi
 *   yeni header include'u derlenemez / sembol bağlanamaz → KIRMIZI.
 *   (Derleme-RED'i: bu dosyanın `rowl/audio/audio_blip_error_channel.hpp`
 *   include'u taşıma öncesi çözülemez. Bağlanma-RED'i: helper sembolleri yoktur.)
 *
 * Bacak 2 — davranış paritesi (taşıma-öncesi YEŞİL, sonrası da YEŞİL kalmalı):
 *   (a) synth-başarı → kod 0 + snapshot boş (2337-2338 clear'in gözlemi);
 *   (b) bulunamayan-asset blip (VFS çıplak: asset ıskalar → synth fallback
 *       başarılı; üretimdeki bozuk-asset/decode-fail yolunun gözlenebilir
 *       sonucu aynıdır: kirli snapshot kalmaz) → kod 0;
 *   (c) blip sonrası `RowlEngine_GetLastResultCode` damgası `play_voice_blip`
 *       op ile eşleşir.
 *   Bu gözlemler taşıma-öncesi kaydedilir, taşıma-sonrası birebir aynı olmalıdır;
 *   fark YEŞİL'den KIRMIZI'ya dönüş = davranış sızıntısı.
 *   (Decode-fail→clear birim semantiği ayrıca Bacak 1'in doğrudan-helper
 *   çağrılarıyla kilitlenir; public C API tek başına çıplak-VFS altında gerçek
 *   decode-fail üretemez.)
 *
 * GREEN (taşıma-sonrası): Bacak 1 yapısal assertion geçer (yeni TU derlenir/
 * bağlanır, Locked-yardımcılar dış kilit altında aynı yazımları üretir) +
 * Bacak 2 parite gözlemleri değişmez → exit 0. Ayrıca deadlock-yakalama: prob
 * ctest --timeout ile koşar; helper'a yanlış çevirim (kilit alan helper çağrısı)
 * askıya düşer → timeout = KIRMIZI.
 *
 * KIRMIZI-GREEN SÖZLEŞMESİ: taşıma sırasında Bacak 2 gözlemlerinden herhangi
 * biri değişirse (özellikle kod 0/10 değişimi) fix TURU sayılır (max 3);
 * kırmızıda commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "rowl/audio/audio_blip_error_channel.hpp"

namespace {

void snapFail(const std::string& message) {
    rowlLockFail("d02-audio-snapshot-probe", message);
}

int lastCode(RowlEngineHandle h) {
    return static_cast<int>(RowlEngine_GetLastResultCode(h));
}

std::string lastOp(RowlEngineHandle h) {
    const char* op = RowlEngine_GetLastResultOperation(h);
    return op ? std::string(op) : std::string("<null>");
}

std::string lastAudioError(RowlEngineHandle h) {
    const char* err = RowlEngine_GetLastAudioError(h);
    return err ? std::string(err) : std::string("<null>");
}

using ErrClass = Rowl::Audio::AudioEngine::AudioErrorClass;

}  // namespace

int main() {
    TEST_SECTION("Audio snapshot probe (D02: blip error-channel tasima kilidi)");

    // ── Bacak 1: yapısal assertion — yeni Locked-yardımcılar aynı yazımları üretir.
    // Taşıma-öncesi bu blok derlenemez/bağlanamaz (RED). Taşıma-sonrası her
    // assertion, taşınan çekirdeğin birebirliğini kilitler.
    {
        // 2224-2225 çekirdeği: OGG-decode-fail KOŞULSUZ yazar (önceki hatayı ezer).
        std::string err = "stale prior error";
        ErrClass cls = ErrClass::Device;
        Rowl::Audio::recordBlipOggDecodeFailureLocked(err, cls, "Ogg/Vorbis stream is corrupt");
        if (err != "Ogg/Vorbis stream is corrupt" || cls != ErrClass::Decode)
            snapFail("Bacak1 ogg-decode: unconditional overwrite lost (err='" + err + "')");

        // 2264-2269: asset-blip kuyruk-fail'i ilk-hatayı KORUR; boşken Decode yazar.
        {
            std::string stale = "stale prior error";
            ErrClass staleCls = ErrClass::Device;
            Rowl::Audio::recordAssetBlipQueueFailureLocked(stale, staleCls);
            if (stale != "stale prior error" || staleCls != ErrClass::Device)
                snapFail("Bacak1 asset-queue: stale error not preserved (err='" + stale + "')");
        }
        {
            std::string fresh;
            ErrClass freshCls = ErrClass::None;
            Rowl::Audio::recordAssetBlipQueueFailureLocked(fresh, freshCls);
            if (fresh.empty() || freshCls != ErrClass::Decode)
                snapFail("Bacak1 asset-queue: empty-state write lost (err='" + fresh + "')");
        }

        // 2330-2335: synth kuyruk-fail'i aynı koruma sözleşmesini izler.
        {
            std::string stale = "stale prior error";
            ErrClass staleCls = ErrClass::Device;
            Rowl::Audio::recordSynthBlipQueueFailureLocked(stale, staleCls);
            if (stale != "stale prior error" || staleCls != ErrClass::Device)
                snapFail("Bacak1 synth-fail: stale error not preserved (err='" + stale + "')");
        }
        {
            std::string fresh;
            ErrClass freshCls = ErrClass::None;
            Rowl::Audio::recordSynthBlipQueueFailureLocked(fresh, freshCls);
            if (fresh.empty() || freshCls != ErrClass::Decode)
                snapFail("Bacak1 synth-fail: empty-state write lost (err='" + fresh + "')");
        }

        // 2337-2341 else-kolu BÜTÜNDÜR (clear + None-reset + sayaç ayrılamaz).
        // Kirli snapshot'la çağrılır: tertemiz dönmelidir (EN YÜKSEK RİSKLİ blok).
        {
            std::string dirty = "stale decode residue";
            ErrClass dirtyCls = ErrClass::Decode;
            std::atomic<uint32_t> synth{41};
            Rowl::Audio::recordSynthBlipQueueSuccessLocked(dirty, dirtyCls, synth);
            if (!dirty.empty() || dirtyCls != ErrClass::None || synth.load() != 42)
                snapFail("Bacak1 synth-success: clear+None+count atomicity broken");
        }

        // 2704-2709: pool-open fail'i Device-sınıfıdır (Decode ile karışmaz).
        {
            std::string stale = "stale prior error";
            ErrClass staleCls = ErrClass::Decode;
            Rowl::Audio::recordSfxPoolStreamOpenFailureLocked(stale, staleCls);
            if (stale != "stale prior error" || staleCls != ErrClass::Decode)
                snapFail("Bacak1 pool-open: stale error not preserved (err='" + stale + "')");
        }
        {
            std::string fresh;
            ErrClass freshCls = ErrClass::None;
            Rowl::Audio::recordSfxPoolStreamOpenFailureLocked(fresh, freshCls);
            if (fresh.empty() || freshCls != ErrClass::Device)
                snapFail("Bacak1 pool-open: Device-class write lost (err='" + fresh + "')");
        }
    }
    TEST_PASS("Bacak1 — Locked-yardimcilar tasinan cekirdekleri birebir uretir");

    // ── Bacak 2: davranış paritesi (yalnızca public C API + dummy driver).
    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) snapFail("RowlEngine_Create returned null");

    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        snapFail("RowlEngine_Init(320x180) must succeed under dummy drivers");

    // (a) synth-başarı → kod 0 + snapshot boş (2337-2338 clear'in gözlemi).
    RowlEngine_ClearLastResult(h);
    RowlEngine_PlayVoiceBlip(h, "", 1.0f, 0.85f, 1);
    {
        const int code = lastCode(h);
        const std::string op = lastOp(h);
        const std::string audioErr = lastAudioError(h);
        std::cout << "  synth blip -> code=" << code << " op=" << op
                  << " audioErr='" << audioErr << "'" << std::endl;
        if (code != 0)
            snapFail("Bacak2a: synth blip must yield code 0, got " + std::to_string(code));
        if (!audioErr.empty())
            snapFail("Bacak2a: snapshot not empty after synth success: '" + audioErr + "'");
        if (op != "play_voice_blip")
            snapFail("Bacak2a: op stamp must be play_voice_blip, got " + op);
    }

    // (b) bulunamayan-asset blip → synth fallback başarılı → kod 0 (kirlilik yok).
    RowlEngine_ClearLastResult(h);
    RowlEngine_PlayVoiceBlip(h, "audio/d02_missing_blip.wav", 1.0f, 0.85f, 1);
    {
        const int code = lastCode(h);
        const std::string op = lastOp(h);
        const std::string audioErr = lastAudioError(h);
        std::cout << "  missing-asset blip -> code=" << code << " op=" << op
                  << " audioErr='" << audioErr << "'" << std::endl;
        if (code != 0)
            snapFail("Bacak2b: missing-asset blip must yield code 0, got " + std::to_string(code));
        if (!audioErr.empty())
            snapFail("Bacak2b: snapshot not empty after fallback success: '" + audioErr + "'");
        if (op != "play_voice_blip")
            snapFail("Bacak2b: op stamp must be play_voice_blip, got " + op);
    }

    // (c) damga tazeliği: Clear sonrası blip yine play_voice_blip damgası bırakır.
    RowlEngine_ClearLastResult(h);
    RowlEngine_PlayVoiceBlip(h, "", 0.9f, 0.5f, 1);
    if (lastCode(h) != 0 || lastOp(h) != "play_voice_blip")
        snapFail("Bacak2c: stamp lost after ClearLastResult cycle");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("d02 audio snapshot parite + yapi kilidi yesil");
    return 0;
}
