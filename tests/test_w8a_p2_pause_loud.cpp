/**
 * test_w8a_p2_pause_loud.cpp — W8-a bulgu (2) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p2 (ctest -R w8a_p2). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: SetQuickSaveSlot(:12) + QuickSave(:29) + QuickLoad(:36) yabanci
 * thread'de sessiz duser (damga yok). Beklenen: kardes SaveGameSlotResult
 * emsali (c_api_state.cpp:129-133) claimHandleOrClassify + stampWrongThread
 * ile loud (ret 0 + WRONG_THREAD damgasi).
 *
 * KIRMIZI (pre-fix): yabanci damga != 14 -> exit 1 (ret zaten 0).
 * YESIL (post-fix): ucunde de damga 14 + state'e dokunulmaz (slot 0 kalir).
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <string>
#include <thread>

namespace {

void p2Fail(const std::string& message) {
    rowlLockFail("w8a-p2-pause", message);
}

void p2Require(bool condition, const std::string& message) {
    if (!condition) p2Fail(message);
}

}  // namespace

int main() {
    TEST_SECTION("W8-a P2: quick-slot ailesi yabanci-thread loud");

    // Pre-init ayni-thread davranis korunur (sessiz 0; claim Mine).
    {
        RowlEngineHandle pre = RowlEngine_Create();
        p2Require(pre != nullptr, "RowlEngine_Create null dondu");
        p2Require(RowlEngine_SetQuickSaveSlot(pre, 2) == 0 ||
                      RowlEngine_SetQuickSaveSlot(pre, 2) == 1,
                  "pre-init SetQuickSaveSlot crash?");
        p2Require(RowlEngine_QuickSave(pre) == 0, "pre-init QuickSave 0 degil");
        p2Require(RowlEngine_QuickLoad(pre) == 0, "pre-init QuickLoad 0 degil");
        RowlEngine_Destroy(pre);
    }
    TEST_PASS("Pre-init ayni-thread davranis korunur");

    RowlEngineHandle live = RowlEngine_Create();
    p2Require(live != nullptr, "canli RowlEngine_Create null dondu");
    p2Require(RowlEngine_Init(live, 320, 180, 0) == 1,
              "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");
    p2Require(RowlEngine_GetQuickSaveSlot(live) == 0, "baslangic slotu 0 degil");

    // ── Yabanci-thread: RED cekirdegi (uc giris de damgalamali) ──
    // Damga-izolasyonu op-kanaliyla: her red kendi op adini damgalar
    // (ClearLastResult yabanci-thread'de sessiz no-op'tur, kullanilmaz).
    {
        int rSlot = -1, rSave = -1, rLoad = -1;
        int cSlot = -1, cSave = -1, cLoad = -1;
        std::string oSlot, oSave, oLoad;
        std::thread foreign([&] {
            rSlot = RowlEngine_SetQuickSaveSlot(live, 2);
            cSlot = RowlEngine_GetLastResultCode(live);
            oSlot = RowlEngine_GetLastResultOperation(live);
            rSave = RowlEngine_QuickSave(live);
            cSave = RowlEngine_GetLastResultCode(live);
            oSave = RowlEngine_GetLastResultOperation(live);
            rLoad = RowlEngine_QuickLoad(live);
            cLoad = RowlEngine_GetLastResultCode(live);
            oLoad = RowlEngine_GetLastResultOperation(live);
        });
        foreign.join();
        p2Require(rSlot == 0, "yabanci SetQuickSaveSlot ret 0 degil");
        p2Require(rSave == 0, "yabanci QuickSave ret 0 degil");
        p2Require(rLoad == 0, "yabanci QuickLoad ret 0 degil");
        p2Require(cSlot == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "SetQuickSaveSlot damgasi 14 degil, got: " + std::to_string(cSlot));
        p2Require(cSave == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "QuickSave damgasi 14 degil, got: " + std::to_string(cSave));
        p2Require(cLoad == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "QuickLoad damgasi 14 degil, got: " + std::to_string(cLoad));
        p2Require(oSlot == "set_quick_save_slot",
                  "SetQuickSaveSlot op adi yanlis, got: " + oSlot);
        p2Require(oSave == "quick_save", "QuickSave op adi yanlis, got: " + oSave);
        p2Require(oLoad == "quick_load", "QuickLoad op adi yanlis, got: " + oLoad);
    }
    TEST_PASS("Yabanci-thread uc giriste WRONG_THREAD damgali");

    // State'e dokunulmadi: slot hâlâ 0.
    p2Require(RowlEngine_GetQuickSaveSlot(live) == 0,
              "yabanci SetQuickSaveSlot state'i degistirmis");

    RowlEngine_Shutdown(live);
    RowlEngine_Destroy(live);

    std::cout << "W8A-P2 GREEN: quick-slot ailesi loud" << std::endl;
    TEST_PASS("W8-a P2 yesil");
    return 0;
}
