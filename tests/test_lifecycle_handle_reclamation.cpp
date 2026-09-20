// test_lifecycle_handle_reclamation.cpp — R1 bulgu #7: generational slot
// havuzu kilidi.
//
// Eski kod her Create'te yeni HandleRecord üretip Destroy'da hiç geri
// almazdı (süreç-çıkışına dek saklama): 2048 çıplak Create/Destroy döngüsü
// slot sayısını 2048 büyütürdü. Yeni kod ölü slotu free-list'e döndürür
// (bellek peak-live ile sınırlı) + nesil sayacı ABA savunması verir.
//
// Kural: (1) N döngü sonrası slot-büyümesi gevşek-pay (8) içinde kalır,
// canlı sayısı tabana döner; (2) ABA pini — yok edilmiş h0 hep Dead kalır,
// sonraki Create farklı bir token verir (slot yeniden kullanılsa bile).
#include "rowl_test_harness.hpp"

namespace {

void checkReclaim(bool cond, const char* what) {
    if (!cond) rowlLockFail("handle-reclamation", what);
}

}  // namespace

void test_lifecycle_handle_reclamation() {
    TEST_SECTION("Lifecycle Handle Reclamation (R1 #7: generational slots)");

    // ---- (1) geri-alım: 2048 çıplak Create/Destroy (Init yok — sızıntı
    // Init-öncesi kayıt büyümesidir) ----
    const uint64_t slotBase = Rowl::Core::RowlTest_HandleSlotCount();
    const uint64_t liveBase = Rowl::Core::RowlTest_LiveHandleCount();
    constexpr int kCycles = 2048;
    for (int i = 0; i < kCycles; ++i) {
        RowlEngineHandle h = RowlEngine_Create();
        if (h == nullptr) {
            rowlLockFail("handle-reclamation", "RowlEngine_Create returned null");
            return;
        }
        RowlEngine_Destroy(h);
    }
    const uint64_t slotAfter = Rowl::Core::RowlTest_HandleSlotCount();
    const uint64_t liveAfter = Rowl::Core::RowlTest_LiveHandleCount();
    checkReclaim(liveAfter == liveBase, "live-handle count did not return to baseline");
    checkReclaim(slotAfter - slotBase <= 8, "slot pool grew without bound on Create/Destroy cycles");

    // ---- (2) ABA pini: ölü h0 hep Dead, sonraki Create farklı token ----
    RowlEngineHandle h0 = RowlEngine_Create();
    checkReclaim(h0 != nullptr, "ABA fixture Create returned null");
    RowlEngine_Destroy(h0);
    uint32_t chapters = 0;
    checkReclaim(RowlEngine_GetChapterCount(h0, &chapters) == ROWL_RESULT_INVALID_HANDLE,
                 "destroyed handle did not stay Dead");
    RowlEngineHandle h1 = RowlEngine_Create();
    checkReclaim(h1 != nullptr, "post-destroy Create returned null");
    checkReclaim(h1 != h0, "reused slot returned an identical token (ABA)");
    checkReclaim(RowlEngine_GetChapterCount(h0, &chapters) == ROWL_RESULT_INVALID_HANDLE,
                 "stale token became valid after slot reuse");
    checkReclaim(Rowl::Core::RowlTest_LiveHandleCount() == liveBase + 1,
                 "live-handle count did not track the open fixture handle");
    RowlEngine_Destroy(h1);
    checkReclaim(Rowl::Core::RowlTest_LiveHandleCount() == liveBase,
                 "live-handle count did not return to baseline after fixture");
    checkReclaim(RowlEngine_GetChapterCount(h1, &chapters) == ROWL_RESULT_INVALID_HANDLE,
                 "destroyed fixture handle did not stay Dead");

    TEST_PASS("Lifecycle handle reclamation: bounded slots + ABA-safe tokens (R1 #7)");
}
