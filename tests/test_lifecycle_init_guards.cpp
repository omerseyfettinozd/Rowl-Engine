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
}
