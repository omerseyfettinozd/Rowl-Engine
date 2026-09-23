/**
 * test_w8a_p4_run_foreign.cpp — W8-a bulgu (4) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p4 (ctest -R w8a_p4). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: RowlEngine_Run (:375) yabanci-thread'de sessiz donuyor (damga yok).
 * Beklenen (Destroy :274-277 emsali): Foreign dalinda stampWrongThread.
 *
 * KIRMIZI (pre-fix): yabanci damga != 14 -> exit 1.
 * YESIL (post-fix): yabanci damga 14 + op "run"; cagri doner (bloklanmaz);
 * olu-handle sessiz.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <string>
#include <thread>

namespace {

void p4Fail(const std::string& message) {
    rowlLockFail("w8a-p4-run", message);
}

void p4Require(bool condition, const std::string& message) {
    if (!condition) p4Fail(message);
}

}  // namespace

int main() {
    TEST_SECTION("W8-a P4: Run yabanci-thread loud");

    // Olu-handle sessiz (crash yok).
    {
        RowlEngineHandle dead = RowlEngine_Create();
        p4Require(dead != nullptr, "RowlEngine_Create null dondu");
        RowlEngine_Destroy(dead);
        RowlEngine_Run(dead);
    }
    TEST_PASS("Olu-handle Run sessiz");

    RowlEngineHandle live = RowlEngine_Create();
    p4Require(live != nullptr, "canli RowlEngine_Create null dondu");
    p4Require(RowlEngine_Init(live, 320, 180, 0) == 1,
              "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");

    // ── RED cekirdegi: yabanci Run damgalanmali + donmeli ──
    {
        int foreignStamp = -999;
        std::string foreignOp;
        std::thread foreign([&] {
            RowlEngine_Run(live);  // reddedilmeli; bloklanmamali
            foreignStamp = RowlEngine_GetLastResultCode(live);
            foreignOp = RowlEngine_GetLastResultOperation(live);
        });
        foreign.join();
        p4Require(foreignStamp == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "yabanci Run damgasi WRONG_THREAD(14) degil, got: " +
                      std::to_string(foreignStamp));
        p4Require(foreignOp == "run", "yabanci Run op adi yanlis, got: " + foreignOp);
    }
    TEST_PASS("Yabanci-thread Run WRONG_THREAD damgali + dondu");

    // Red sonrasi motor saglam: sahibi thread'de Step ilerler.
    RowlEngine_Step(live, 1.0f / 60.0f);
    RowlEngine_Shutdown(live);
    RowlEngine_Destroy(live);

    std::cout << "W8A-P4 GREEN: Run yabanci-thread loud" << std::endl;
    TEST_PASS("W8-a P4 yesil");
    return 0;
}
