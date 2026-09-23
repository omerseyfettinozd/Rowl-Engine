/**
 * test_w8a_p6_getvariable_nullkey.cpp — W8-a bulgu (6) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p6 (ctest -R w8a_p6). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: RowlEngine_GetVariableUtf8 (:287) null-key'i dead-handle ile ayni
 * kanaldan INVALID_HANDLE(1) donuyor. Beklenen (EvaluateCondition :301-312
 * emsali): once handle (olu -> INVALID_HANDLE aynen), sonra null-key
 * InvalidArgument(2) + damga.
 *
 * KIRMIZI (pre-fix): canli + null-key ret != 2 -> exit 1.
 * YESIL (post-fix): canli + null-key 2 + damga 2 + op "get_variable";
 * olu-handle 1 aynen. Eski sessiz-davranisa kilitli test
 * (test_c_api_contract.cpp B2a blogu) BILINCLI guncellenir (gerekce: asagida).
 * Kirmizida commit YOK.
 *
 * Gerekce: INVALID_HANDLE yalniz olu-handle'indir (c_api.h:21-28); canli
 * handle'da bozuk arg InvalidArgument'tir. B2a blogundaki
 * `GetVariableUtf8(handle, nullptr, ...) == INVALID_HANDLE` satiri bu
 * sozlesmeyle celisir, INVALID_ARGUMENT'a cekilir.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <string>

namespace {

void p6Fail(const std::string& message) {
    rowlLockFail("w8a-p6-getvariable", message);
}

void p6Require(bool condition, const std::string& message) {
    if (!condition) p6Fail(message);
}

}  // namespace

int main() {
    TEST_SECTION("W8-a P6: GetVariableUtf8 null-key ayrimi");

    // Olu-handle karakterizasyonu (pre/post ayni: INVALID_HANDLE, argsiz).
    {
        uint32_t required = 0;
        p6Require(RowlEngine_GetVariableUtf8(nullptr, nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "olu + null-key INVALID_HANDLE degil");
        RowlEngineHandle dead = RowlEngine_Create();
        p6Require(dead != nullptr, "RowlEngine_Create null dondu");
        RowlEngine_Destroy(dead);
        p6Require(RowlEngine_GetVariableUtf8(dead, nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "destroy + null-key INVALID_HANDLE degil");
    }
    TEST_PASS("Olu-handle null-key'de INVALID_HANDLE (handle-once)");

    RowlEngineHandle live = RowlEngine_Create();
    p6Require(live != nullptr, "canli RowlEngine_Create null dondu");
    p6Require(RowlEngine_Init(live, 320, 180, 0) == 1,
              "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");

    // ── RED cekirdegi: canli + null-key InvalidArgument + damga ──
    {
        uint32_t required = 0;
        const auto ret =
            RowlEngine_GetVariableUtf8(live, nullptr, nullptr, 0, &required);
        p6Require(ret == ROWL_RESULT_INVALID_ARGUMENT,
                  "canli + null-key INVALID_ARGUMENT(2) degil, got: " +
                      std::to_string(static_cast<int>(ret)));
        p6Require(RowlEngine_GetLastResultCode(live) ==
                      static_cast<int32_t>(ROWL_RESULT_INVALID_ARGUMENT),
                  "null-key damgasi InvalidArgument degil");
        p6Require(std::string(RowlEngine_GetLastResultOperation(live)) == "get_variable",
                  "null-key op adi yanlis");
    }
    TEST_PASS("Canli + null-key InvalidArgument damgali");

    // Normal yol saglam: round-trip calisir.
    {
        RowlEngine_SetVariable(live, "w8a_key", "w8a_val");
        char buf[64];
        uint32_t outReq = 0;
        p6Require(RowlEngine_GetVariableUtf8(live, "w8a_key", buf, sizeof(buf),
                                             &outReq) == ROWL_RESULT_OK,
                  "normal GetVariableUtf8 OK degil");
        p6Require(std::string(buf) == "w8a_val", "round-trip degeri yanlis");
    }
    TEST_PASS("Normal yol round-trip saglam");

    RowlEngine_Shutdown(live);
    RowlEngine_Destroy(live);

    std::cout << "W8A-P6 GREEN: GetVariableUtf8 null-key ayrimi" << std::endl;
    TEST_PASS("W8-a P6 yesil");
    return 0;
}
