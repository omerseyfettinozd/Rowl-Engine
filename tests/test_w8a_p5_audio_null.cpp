/**
 * test_w8a_p5_audio_null.cpp — W8-a bulgu (5) RED kilidi.
 *
 * Bagimsiz ikili: rowl_w8a_p5 (ctest -R w8a_p5). D01 konvansiyonu: yalnizca
 * public C API (paylasilan RowlEngineCore'a baglanir). rowl_tests'e gomulmez.
 *
 * Bulgu: dort ResultCode getter audio-null iken (canli ama init'siz motor)
 * INVALID_HANDLE(1) donuyor — oysa INVALID_HANDLE yalniz olu-handle'indir
 * (c_api.h:21-28). Beklenen (checked varyant emsali
 * thread_contract_guard.cpp:60,92): UNKNOWN_ERROR(99).
 *   :235 GetLastAudioErrorUtf8, :477 GetStreamInfoJson,
 *   :619 GetSfxActivePaths, :748 GetBgmPumpStatsJson.
 *
 * KIRMIZI (pre-fix): canli+audio-null ret != 99 -> exit 1.
 * YESIL (post-fix): dortude 99; olu-handle'da 1 aynen.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

void p5Fail(const std::string& message) {
    rowlLockFail("w8a-p5-audio", message);
}

void p5Require(bool condition, const std::string& message) {
    if (!condition) p5Fail(message);
}

constexpr RowlEngine_ResultCode kUnknown =
    static_cast<RowlEngine_ResultCode>(99);  // ROWL_RESULT_UNKNOWN_ERROR

}  // namespace

int main() {
    TEST_SECTION("W8-a P5: audio-null getter'lar UNKNOWN_ERROR");

    // Olu-handle karakterizasyonu (pre/post ayni: INVALID_HANDLE).
    {
        uint32_t required = 0;
        p5Require(RowlEngine_GetLastAudioErrorUtf8(nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "olu GetLastAudioErrorUtf8 1 degil");
        p5Require(RowlEngine_GetStreamInfoJson(nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "olu GetStreamInfoJson 1 degil");
        p5Require(RowlEngine_GetSfxActivePaths(nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "olu GetSfxActivePaths 1 degil");
        p5Require(RowlEngine_GetBgmPumpStatsJson(nullptr, nullptr, 0, &required) ==
                      ROWL_RESULT_INVALID_HANDLE,
                  "olu GetBgmPumpStatsJson 1 degil");
    }
    TEST_PASS("Olu-handle dort getter'da INVALID_HANDLE");

    // Canli ama init'siz motor: handle Mine, audio null (m_audio initialize'da
    // kurulur). RED cekirdegi: dortude UNKNOWN_ERROR beklenir.
    RowlEngineHandle live = RowlEngine_Create();
    p5Require(live != nullptr, "RowlEngine_Create null dondu");
    {
        uint32_t required = 0;
        const auto e = RowlEngine_GetLastAudioErrorUtf8(live, nullptr, 0, &required);
        const auto s = RowlEngine_GetStreamInfoJson(live, nullptr, 0, &required);
        const auto p = RowlEngine_GetSfxActivePaths(live, nullptr, 0, &required);
        const auto b = RowlEngine_GetBgmPumpStatsJson(live, nullptr, 0, &required);
        p5Require(e == kUnknown, "audio-null GetLastAudioErrorUtf8 99 degil, got: " +
                                     std::to_string(static_cast<int>(e)));
        p5Require(s == kUnknown, "audio-null GetStreamInfoJson 99 degil, got: " +
                                     std::to_string(static_cast<int>(s)));
        p5Require(p == kUnknown, "audio-null GetSfxActivePaths 99 degil, got: " +
                                     std::to_string(static_cast<int>(p)));
        p5Require(b == kUnknown, "audio-null GetBgmPumpStatsJson 99 degil, got: " +
                                     std::to_string(static_cast<int>(b)));
    }
    TEST_PASS("Canli+audio-null dort getter'da UNKNOWN_ERROR");

    // Init'li motorda dortu de OK doner (davranis regresyonu yok).
    p5Require(RowlEngine_Init(live, 320, 180, 0) == 1,
              "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");
    {
        uint32_t required = 0;
        std::vector<char> buf(4096, '\0');
        uint32_t outReq = 0;
        p5Require(RowlEngine_GetLastAudioErrorUtf8(live, buf.data(),
                                                  static_cast<uint32_t>(buf.size()),
                                                  &outReq) == ROWL_RESULT_OK,
                  "init'li GetLastAudioErrorUtf8 OK degil");
        p5Require(RowlEngine_GetStreamInfoJson(live, nullptr, 0, &required) ==
                      ROWL_RESULT_OK,
                  "init'li GetStreamInfoJson OK degil");
        p5Require(RowlEngine_GetSfxActivePaths(live, nullptr, 0, &required) ==
                      ROWL_RESULT_OK,
                  "init'li GetSfxActivePaths OK degil");
        p5Require(RowlEngine_GetBgmPumpStatsJson(live, nullptr, 0, &required) ==
                      ROWL_RESULT_OK,
                  "init'li GetBgmPumpStatsJson OK degil");
    }
    TEST_PASS("Init'li motorda dort getter OK");

    RowlEngine_Shutdown(live);
    RowlEngine_Destroy(live);

    std::cout << "W8A-P5 GREEN: audio-null UNKNOWN_ERROR" << std::endl;
    TEST_PASS("W8-a P5 yesil");
    return 0;
}
