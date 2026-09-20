/**
 * test_lifecycle_init_guards.cpp — B1a: fail-loud init kilitleri.
 *
 * Bulgular #103/#104/#105/#125 (+ #126-kısmi yarı-pencere temizliği):
 * double-init sessiz-1 + validasyon-atlama; init-retleri kanalsız-0;
 * audio/lua dönüşleri çöpe. Bu testler ret-yollarını kilitler:
 *  - double-init → 0 + StateError(11); shutdown→re-init yine 1.
 *  - bozuk boyut → 0 + ValidationError(6); düzeltilmiş retry aynı
 *    handle'da 1 (yarım-durum kalmaz).
 * Bilerek-boz: guard'lar stash'lenirse (eski `return true` / kontrolsüz
 * init) bu testler kırmızıya döner; audio/lua dalları temp-force ile
 * kırmızı-kanıtlandı (süreç-notu, kalıcı enjeksiyon yok).
 */
#include "rowl_test_harness.hpp"

namespace {

void checkInitCode(const char* name, RowlEngineHandle h, int32_t want) {
    const int32_t got = RowlEngine_GetLastResultCode(h);
    if (got != want) {
        rowlLockFail("lifecycle-init-guards",
                     std::string(name) + ": expected last-result " +
                         std::to_string(want) + ", got " + std::to_string(got));
    }
}

}  // namespace

void test_lifecycle_init_guards() {
    TEST_SECTION("Lifecycle Init Guards (B1a: fail-loud init)");

    // #103/#125: double-init yüksek sesle reddedilir.
    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-init-guards", "first Init must succeed");
    if (RowlEngine_Init(h, 320, 180, 0) != 0)
        rowlLockFail("lifecycle-init-guards", "double-init must return 0 (was silent 1)");
    checkInitCode("double-init", h, 11 /* StateError */);
    // shutdown→re-init akışı korunur (desteklenen akış; #103'ün hedefi değil).
    RowlEngine_Shutdown(h);
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-init-guards", "shutdown->re-init must still succeed");
    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    // #104: bozuk boyut kanala işlenir; retry temizdir (#126-kısmi).
    RowlEngineHandle bad = RowlEngine_Create();
    if (bad == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    if (RowlEngine_Init(bad, 0, 1080, 0) != 0)
        rowlLockFail("lifecycle-init-guards", "zero-width Init must return 0");
    checkInitCode("zero-width", bad, 6 /* ValidationError */);
    if (RowlEngine_Init(bad, 20000, 1080, 0) != 0)
        rowlLockFail("lifecycle-init-guards", "oversize Init must return 0");
    checkInitCode("oversize", bad, 6 /* ValidationError */);
    // Başarısız init yarım-durum bırakmaz: aynı handle düzeltilmiş
    // boyutla yaşar.
    if (RowlEngine_Init(bad, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-init-guards", "retry after failed Init must succeed");
    if (RowlEngine_IsRunning(bad) != 1)
        rowlLockFail("lifecycle-init-guards", "retried engine must be running");
    RowlEngine_Shutdown(bad);
    RowlEngine_Destroy(bad);

    TEST_PASS("Lifecycle init guards fail loud with last-result codes");

    // ── D1 (B1b #108-#132): init-öncesi fail-closed kilitleri ──

    // #109/#129/#130: pre-init save/load/rewind dosya yazmadan
    // StateError(11) + false döner (quick varyantlar otomatik kapsanır).
    RowlEngineHandle pre = RowlEngine_Create();
    if (pre == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    // Önceki kırmızı-kanıt koşularının artığı kalmış olabilir; bilinçli
    // temiz başlangıç (silme init gerektirmez, dosya-düzeyi işlemdir).
    RowlEngine_DeleteSaveSlot(pre, 97);
    if (RowlEngine_HasSaveSlot(pre, 97) != 0)
        rowlLockFail("lifecycle-init-guards", "slot 97 must start empty");
    if (RowlEngine_SaveGameSlot(pre, 97) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init Save must return 0 (wrote node-0 file before)");
    checkInitCode("pre-init save", pre, 11 /* StateError */);
    if (RowlEngine_HasSaveSlot(pre, 97) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init Save must not write a file");
    if (RowlEngine_LoadGameSlot(pre, 97) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init Load must return 0");
    checkInitCode("pre-init load", pre, 11 /* StateError */);
    if (RowlEngine_Rewind(pre, 1) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init Rewind must return 0");
    checkInitCode("pre-init rewind", pre, 11 /* StateError */);
    if (RowlEngine_QuickSave(pre) != 0 || RowlEngine_QuickLoad(pre) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init QuickSave/QuickLoad must return 0");

    // #108: init, önceki last-result'u geçersiz kılar (bayat load-OK kalamaz).
    if (RowlEngine_Init(pre, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-init-guards", "Init after pre-init attempts must succeed");
    checkInitCode("post-init channel", pre, 0 /* Ok: channel belongs to init */);
    // Op da init'e aittir (init-içi loadStoryGraphFile success'i değil):
    // setSuccess("init") olmazsa op bayat kalır, kod tek başına yakalamaz.
    if (std::string(RowlEngine_GetLastResultOperation(pre)) != "init")
        rowlLockFail("lifecycle-init-guards",
                     std::string("post-init op must be 'init', got '") +
                         RowlEngine_GetLastResultOperation(pre) + "'");

    // #111: pre-init sahne yazımı enjekte olamaz; okuma demo veriyi
    // gerçek sanmaz (legacy "" + Utf8 STATE_ERROR + boş).
    RowlEngineHandle scene = RowlEngine_Create();
    if (scene == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    RowlEngine_UpdateScene(scene, "INJECTED_SPEAKER", "INJECTED_DIALOGUE", "bg",
                           0, 0, 100, 100, "ch", 0, 0, 50, 50, 0, 0, 200, 40);
    checkInitCode("pre-init update-scene", scene, 11 /* StateError */);
    if (std::string(RowlEngine_GetSpeaker(scene)) != "")
        rowlLockFail("lifecycle-init-guards", "pre-init GetSpeaker must be empty (was demo default)");
    checkInitCode("pre-init get-speaker", scene, 11 /* StateError */);
    {
        char buf[256];
        uint32_t required = 0;
        if (RowlEngine_GetSpeakerUtf8(scene, buf, sizeof(buf), &required) != 11)
            rowlLockFail("lifecycle-init-guards", "pre-init GetSpeakerUtf8 must be STATE_ERROR");
        if (buf[0] != '\0')
            rowlLockFail("lifecycle-init-guards", "pre-init GetSpeakerUtf8 must be empty");
    }
    if (RowlEngine_Init(scene, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-init-guards", "Init must succeed");
    if (std::string(RowlEngine_GetSpeaker(scene)).find("INJECTED") != std::string::npos)
        rowlLockFail("lifecycle-init-guards", "pre-init scene write leaked into the session");
    RowlEngine_Shutdown(scene);
    RowlEngine_Destroy(scene);

    // R1 (#4): pre-init JSON sahne yazımı da enjekte olamaz (kardes
    // guardla ayna: no-op + StateError(11)).
    RowlEngineHandle jscene = RowlEngine_Create();
    if (jscene == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    RowlEngine_UpdateSceneFromJson(jscene, "[{\"type\":\"dialogue\",\"enabled\":true,"
        "\"data\":{\"speaker\":\"INJECTED_FROM_JSON\",\"dialogue\":\"injected\"}}]");
    checkInitCode("pre-init update-scene-from-json", jscene, 11 /* StateError */);
    if (RowlEngine_Init(jscene, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-init-guards", "Init must succeed");
    if (std::string(RowlEngine_GetSpeaker(jscene)).find("INJECTED_FROM_JSON") != std::string::npos)
        rowlLockFail("lifecycle-init-guards", "pre-init JSON scene write leaked into the session");
    RowlEngine_Shutdown(jscene);
    RowlEngine_Destroy(jscene);

    // #131: pre-init prefetch OK-yalanı kapanır.
    RowlEngineHandle pf = RowlEngine_Create();
    if (pf == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    if (RowlEngine_PrefetchChapterAssets(pf, nullptr, 0) != 11)
        rowlLockFail("lifecycle-init-guards", "pre-init PrefetchChapterAssets must be STATE_ERROR");
    if (RowlEngine_PumpPrefetch(pf, 4.0f) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init PumpPrefetch must pump 0");
    checkInitCode("pre-init pump", pf, 11 /* StateError */);
    if (RowlEngine_IsChapterBoundaryNode(pf, 5) != 0)
        rowlLockFail("lifecycle-init-guards", "pre-init IsChapterBoundaryNode must be 0");
    RowlEngine_Destroy(pf);

    // #110/#132: pre-init ses/kamera setter sinyali + sahte-0.0 ayırımı.
    RowlEngineHandle av = RowlEngine_Create();
    if (av == nullptr) rowlLockFail("lifecycle-init-guards", "Create returned null");
    RowlEngine_SetMasterVolume(av, 0.5f);
    checkInitCode("pre-init set-master-volume", av, 11 /* StateError */);
    if (RowlEngine_GetMasterVolume(av) != 0.0f)
        rowlLockFail("lifecycle-init-guards", "pre-init GetMasterVolume must stay 0.0");
    checkInitCode("pre-init get-master-volume", av, 11 /* StateError */);
    RowlEngine_SetCamera(av, 10.0f, 20.0f, 1.5f);
    checkInitCode("pre-init set-camera", av, 11 /* StateError */);
    RowlEngine_ResizeViewport(av, 800, 600);
    checkInitCode("pre-init resize-viewport", av, 11 /* StateError */);
    RowlEngine_Destroy(av);

    RowlEngine_Shutdown(pre);
    RowlEngine_Destroy(pre);

    TEST_PASS("Lifecycle pre-init entries fail closed with StateError (D1/B1b)");
}
