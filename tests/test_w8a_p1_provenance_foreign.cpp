/**
 * test_w8a_p1_provenance_foreign.cpp — W8-a bulgu (1) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p1 (ctest -R w8a_p1). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: RowlEngine_GetAssetProvenanceJson yabanci-thread'den cagrilinca
 * damgasiz INVALID_HANDLE donuyor (canli handle'a ragmen) + cift-bakis
 * TOCTOU (toEngineChecked sonra isLiveHandle). Beklenen: tek classifyHandle;
 * Foreign dalinda stampWrongThread + WRONG_THREAD(14); olu-handle'da
 * INVALID_HANDLE(1) aynen.
 *
 * KIRMIZI (pre-fix): yabanci ret INVALID_HANDLE veya damga != 14 -> exit 1.
 * YESIL (post-fix): yabanci ret WRONG_THREAD + damga 14, olu INVALID_HANDLE.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <string>
#include <thread>

namespace {

void p1Fail(const std::string& message) {
    rowlLockFail("w8a-p1-provenance", message);
}

void p1Require(bool condition, const std::string& message) {
    if (!condition) p1Fail(message);
}

}  // namespace

int main() {
    TEST_SECTION("W8-a P1: GetAssetProvenanceJson foreign/olu ayrimi");

    // ── Olu-handle karakterizasyonu (pre/post ayni: INVALID_HANDLE) ──
    RowlEngineHandle dead = RowlEngine_Create();
    p1Require(dead != nullptr, "RowlEngine_Create null dondu");
    RowlEngine_Destroy(dead);
    {
        uint32_t required = 0;
        p1Require(RowlEngine_GetAssetProvenanceJson(dead, "audio/music.ogg", nullptr, 0,
                                                   &required) == ROWL_RESULT_INVALID_HANDLE,
                  "olu handle INVALID_HANDLE degil");
        p1Require(RowlEngine_GetAssetProvenanceJson(dead, nullptr, nullptr, 0, nullptr) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "olu handle + bozuk-arg INVALID_HANDLE degil (handle-once)");
    }
    TEST_PASS("Olu-handle INVALID_HANDLE (handle-once korunur)");

    // ── Canli handle: main thread Init'ler (sahiplenir) ──
    RowlEngineHandle live = RowlEngine_Create();
    p1Require(live != nullptr, "canli RowlEngine_Create null dondu");
    p1Require(RowlEngine_Init(live, 320, 180, 0) == 1,
              "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");

    // Sahip-thread'de canli + bozuk-arg: INVALID_ARGUMENT (pre/post ayni).
    {
        uint32_t required = 0;
        p1Require(RowlEngine_GetAssetProvenanceJson(live, nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_ARGUMENT,
                  "canli + null-path INVALID_ARGUMENT degil");
    }
    TEST_PASS("Canli + bozuk-arg INVALID_ARGUMENT");

    // ── Yabanci-thread: RED cekirdegi ──
    {
        int foreignRet = -999;
        int foreignStamp = -999;
        std::thread foreign([&] {
            uint32_t required = 0;
            foreignRet = static_cast<int>(RowlEngine_GetAssetProvenanceJson(
                live, "audio/music.ogg", nullptr, 0, &required));
            foreignStamp = RowlEngine_GetLastResultCode(live);
        });
        foreign.join();
        p1Require(foreignRet == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "yabanci ret WRONG_THREAD(14) degil, got: " + std::to_string(foreignRet));
        p1Require(foreignStamp == static_cast<int>(ROWL_RESULT_WRONG_THREAD),
                  "yabanci damga WRONG_THREAD(14) degil, got: " + std::to_string(foreignStamp));
    }
    TEST_PASS("Yabanci-thread WRONG_THREAD + damga");

    // Yabanci red sonrasi motor saglam: taze islem ayni handle'da calisir.
    RowlEngine_Shutdown(live);
    RowlEngine_Destroy(live);

    std::cout << "W8A-P1 GREEN: provenance foreign/olu ayrimi" << std::endl;
    TEST_PASS("W8-a P1 yesil");
    return 0;
}
