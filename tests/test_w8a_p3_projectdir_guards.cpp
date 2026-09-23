/**
 * test_w8a_p3_projectdir_guards.cpp — W8-a bulgu (3) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p3 (ctest -R w8a_p3). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: RowlEngine_SetProjectDirectory tek guard ile (`!isLiveHandle ||
 * !projectRoot || !*projectRoot`) sessiz donuyor. Beklenen (LoadStoryGraph
 * :136-146 emsali): guard ikiye bolunur — yabanci handle WRONG_THREAD(14)
 * damgali ret; null/bos kok InvalidArgument(2) damgali ret.
 *
 * KIRMIZI (pre-fix): yabanci damga != 14 veya null-kok damga != 2 -> exit 1.
 * YESIL (post-fix): yabanci 14, null-kok 2, bos-kok 2, olu sessiz.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <string>
#include <thread>

namespace {

void p3Fail(const std::string& message) {
    rowlLockFail("w8a-p3-projectdir", message);
}

void p3Require(bool condition, const std::string& message) {
    if (!condition) p3Fail(message);
}

}  // namespace

int main() {
    TEST_SECTION("W8-a P3: SetProjectDirectory guard ayrimi");

    // Olu-handle sessiz (damgalanacak motor yok; crash yok).
    {
        RowlEngineHandle dead = RowlEngine_Create();
        p3Require(dead != nullptr, "RowlEngine_Create null dondu");
        RowlEngine_Destroy(dead);
        RowlEngine_SetProjectDirectory(dead, "/tmp");
        RowlEngine_SetProjectDirectory(dead, nullptr);
        p3Require(RowlEngine_GetLastResultCode(dead) ==
                      static_cast<int32_t>(ROWL_RESULT_INVALID_HANDLE),
                  "olu-handle okumasi INVALID_HANDLE degil");
    }
    TEST_PASS("Olu-handle sessiz + INVALID_HANDLE");

    RowlEngineHandle live = RowlEngine_Create();
    p3Require(live != nullptr, "canli RowlEngine_Create null dondu");
    p3Require(RowlEngine_Init(live, 320, 180, 0) == 1,
              "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");

    // ── RED cekirdegi 1: yabanci-thread WRONG_THREAD damgali ret ──
    {
        int foreignStamp = -999;
        std::string foreignOp;
        std::thread foreign([&] {
            RowlEngine_SetProjectDirectory(live, "/tmp");
            foreignStamp = RowlEngine_GetLastResultCode(live);
            foreignOp = RowlEngine_GetLastResultOperation(live);
        });
        foreign.join();
        p3Require(foreignStamp == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "yabanci damga WRONG_THREAD(14) degil, got: " +
                      std::to_string(foreignStamp));
        p3Require(foreignOp == "set_project_directory",
                  "yabanci op adi yanlis, got: " + foreignOp);
    }
    TEST_PASS("Yabanci-handle WRONG_THREAD damgali ret");

    // ── RED cekirdegi 2: null/bos kok InvalidArgument damgali ret ──
    {
        RowlEngine_SetProjectDirectory(live, nullptr);
        p3Require(RowlEngine_GetLastResultCode(live) ==
                      static_cast<int32_t>(ROWL_RESULT_INVALID_ARGUMENT),
                  "null kok InvalidArgument damgasi yok");
        RowlEngine_SetProjectDirectory(live, "");
        p3Require(RowlEngine_GetLastResultCode(live) ==
                      static_cast<int32_t>(ROWL_RESULT_INVALID_ARGUMENT),
                  "bos kok InvalidArgument damgasi yok");
        p3Require(std::string(RowlEngine_GetLastResultOperation(live)) ==
                      "set_project_directory",
                  "null-kok op adi yanlis");
    }
    TEST_PASS("Null/bos kok InvalidArgument damgali ret");

    RowlEngine_Shutdown(live);
    RowlEngine_Destroy(live);

    std::cout << "W8A-P3 GREEN: SetProjectDirectory guard ayrimi" << std::endl;
    TEST_PASS("W8-a P3 yesil");
    return 0;
}
