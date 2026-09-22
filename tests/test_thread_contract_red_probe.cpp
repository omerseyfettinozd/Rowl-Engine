/**
 * test_thread_contract_red_probe.cpp — D14 legacy-mixer threading-contract kilidi.
 *
 * RED (pre-fix) kaniti: legacy SetFadeCurve/GetFadeCurve yalnizca isLiveHandle
 * bakar (sessiz-tier); canli handle'a yabanci thread'den cagri sessiz duser
 * (no-op / 0, damga yok) — ownership tier'in WRONG_THREAD damgasi kaybolur.
 * Fix: c_api_thread_contract_guard.cpp checked loud varyantlari
 * (RowlEngine_SetFadeCurveChecked / RowlEngine_GetFadeCurveChecked):
 * yabanci -> WRONG_THREAD (14) + op damgasi, olu -> sessiz INVALID_HANDLE (1).
 *
 * GREEN regresyon bacagi: checked girisler her yolda fail-closed (throw yok):
 * owner -> OK + uygula/oku, yabanci -> WRONG_THREAD + damga (owner thread
 * GetLastResultCode/Message ile okur), olu (null/bogus/destroyed) -> sessiz
 * INVALID_HANDLE, gecersiz curve (0/1 disi) -> INVALID_ARGUMENT + damga,
 * null out -> INVALID_ARGUMENT. Legacy void/int formlarin canli-yolu korunur.
 *
 * Baslik rotusu YOK: prototipler burada lokal extern "C" ile bildirilir
 * (c_api.h'ye dokunulmaz -> DN kapisi duser, D12 :29-31 bandiyla cakisma yok).
 *
 * Calisma: rowl_thread_contract_red_probe (ctest -R thread_contract_red_probe).
 * Ortam: SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

// D14 checked varyantlar (c_api_thread_contract_guard.cpp; c_api.h'ye
// dokunulmadigi icin lokal bildirim — bkz. docs/THREAD_CONTRACT_LEGACY_BRIDGE.md).
extern "C" {
RowlEngine_ResultCode RowlEngine_SetFadeCurveChecked(RowlEngineHandle handle, int curve);
RowlEngine_ResultCode RowlEngine_GetFadeCurveChecked(RowlEngineHandle handle, int* outValue);
}

namespace {

void redFail(const std::string& message) {
    rowlLockFail("thread-contract-red-probe", message);
}

int lastCode(RowlEngineHandle h) {
    return static_cast<int>(RowlEngine_GetLastResultCode(h));
}

std::string lastOp(RowlEngineHandle h) {
    const char* op = RowlEngine_GetLastResultOperation(h);
    return op ? std::string(op) : std::string("<null>");
}

void checkResult(const char* name, RowlEngine_ResultCode got, int want) {
    if (static_cast<int>(got) != want) {
        redFail(std::string(name) + ": expected " + std::to_string(want) +
                ", got " + std::to_string(static_cast<int>(got)));
    }
}

}  // namespace

int main() {
    TEST_SECTION("Thread-contract probe (D14: mixer checked loud-tier)");

    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) redFail("RowlEngine_Create returned null");

    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        redFail("RowlEngine_Init(320x180) must succeed under dummy drivers");

    // Canli-yol: owner uygular + okur; legacy formlar ayni degeri gorur.
    checkResult("Checked SetFadeCurve(EqualPower)", RowlEngine_SetFadeCurveChecked(h, 1), 0);
    int value = -1;
    checkResult("Checked GetFadeCurve", RowlEngine_GetFadeCurveChecked(h, &value), 0);
    if (value != 1) redFail("Checked GetFadeCurve must read back 1, got " + std::to_string(value));
    if (RowlEngine_GetFadeCurve(h) != 1)
        redFail("legacy GetFadeCurve must observe the checked write");
    checkResult("Checked SetFadeCurve(Linear)", RowlEngine_SetFadeCurveChecked(h, 0), 0);
    checkResult("Checked GetFadeCurve back", RowlEngine_GetFadeCurveChecked(h, &value), 0);
    if (value != 0) redFail("Checked GetFadeCurve must read back 0");

    // Gecersiz curve + null out: INVALID_ARGUMENT + op damgasi, mixer korunur.
    checkResult("Checked SetFadeCurve(7)", RowlEngine_SetFadeCurveChecked(h, 7), 2);
    if (lastCode(h) != 2 || lastOp(h) != "set_fade_curve")
        redFail("invalid-curve stamp lost");
    if (RowlEngine_GetFadeCurve(h) != 0)
        redFail("invalid curve must not touch the live mixer");
    checkResult("Checked GetFadeCurve(null)", RowlEngine_GetFadeCurveChecked(h, nullptr), 2);
    if (lastCode(h) != 2 || lastOp(h) != "get_fade_curve")
        redFail("null-out stamp lost");

    // Yabanci thread: loud WRONG_THREAD (ownership tier) + damga.
    RowlEngine_ClearLastResult(h);
    RowlEngine_ResultCode foreignSet = ROWL_RESULT_OK;
    RowlEngine_ResultCode foreignGet = ROWL_RESULT_OK;
    std::thread foreign([&] {
        foreignSet = RowlEngine_SetFadeCurveChecked(h, 1);
        foreignGet = RowlEngine_GetFadeCurveChecked(h, &value);
    });
    foreign.join();
    checkResult("Checked foreign-thread SetFadeCurve", foreignSet, 14);
    checkResult("Checked foreign-thread GetFadeCurve", foreignGet, 14);
    if (lastCode(h) != 14)
        redFail("foreign-thread WRONG_THREAD stamp lost");
    const std::string op = lastOp(h);
    if (op != "set_fade_curve" && op != "get_fade_curve")
        redFail("foreign-thread op stamp lost, got " + op);
    // Yabanci yazma motora dokunamaz.
    if (RowlEngine_GetFadeCurve(h) != 0)
        redFail("foreign write must not touch the live mixer");

    // Olu handle bacagi: sessiz INVALID_HANDLE (WRONG_THREAD degil, damga yok).
    auto* const bogus = reinterpret_cast<RowlEngineHandle>(0xDEADBEEFu);
    RowlEngineHandle dead = RowlEngine_Create();
    if (dead == nullptr) redFail("second Create returned null");
    RowlEngine_Destroy(dead);
    const RowlEngineHandle victims[3] = {nullptr, bogus, dead};
    for (RowlEngineHandle v : victims) {
        // Legacy formlar cokmeden sessiz duser (no-op / 0).
        RowlEngine_SetFadeCurve(v, 1);
        if (RowlEngine_GetFadeCurve(v) != 0)
            redFail("legacy dead-handle GetFadeCurve must stay 0");
        checkResult("Checked SetFadeCurve dead-handle",
                    RowlEngine_SetFadeCurveChecked(v, 1), 1);
        int sink = -1;
        checkResult("Checked GetFadeCurve dead-handle",
                    RowlEngine_GetFadeCurveChecked(v, &sink), 1);
        if (sink != -1) redFail("dead-handle Get must not touch the out-param");
    }

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("mixer checked loud-tier: owner OK, foreign WRONG_THREAD, dead silent INVALID_HANDLE");
    return 0;
}
