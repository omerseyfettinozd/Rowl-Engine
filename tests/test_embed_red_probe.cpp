/**
 * test_embed_red_probe.cpp — D01 (#135) gömülü-yol fail-closed kilidi.
 *
 * RED (pre-fix) kanıtı: canlı + Init'li handle'a post-Init
 * SetExternalWindowHandle sessiz forward'dı (code=0, damga yok) — bogus
 * handle canlı pencereye yazılıyordu. Fix: StateError(11) damgası + ret.
 *
 * GREEN regresyon bacağı: checked girişler (c_api_embed.hpp) her yolda
 * fail-closed (throw yok): ölü → INVALID_HANDLE, yabancı thread →
 * WRONG_THREAD, null/sıfır-arg → INVALID_ARGUMENT, post-Init SetExternal
 * ve pre-Init Resize → STATE_ERROR, pre-Init SetExternal → OK.
 *
 * Çalışma: rowl_embed_red_probe (ctest -R embed_red_probe).
 * Ortam: SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "rowl/c_api_embed.hpp"

namespace {

void redFail(const std::string& message) {
    rowlLockFail("embed-red-probe", message);
}

int lastCode(RowlEngineHandle h) {
    return static_cast<int>(RowlEngine_GetLastResultCode(h));
}

std::string lastOp(RowlEngineHandle h) {
    const char* op = RowlEngine_GetLastResultOperation(h);
    return op ? std::string(op) : std::string("<null>");
}

void checkChecked(const char* name, RowlEngine_ResultCode got, int want) {
    if (static_cast<int>(got) != want) {
        redFail(std::string(name) + ": expected " + std::to_string(want) +
                ", got " + std::to_string(static_cast<int>(got)));
    }
}

}  // namespace

int main() {
    TEST_SECTION("Embed probe (D01 #135: post-Init SetExternal fail-closed)");

    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) redFail("RowlEngine_Create returned null");

    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        redFail("RowlEngine_Init(320x180) must succeed under dummy drivers");

    // Kanalı temizle: aşağıdaki okuma bu çağrıya ait olsun.
    RowlEngine_ClearLastResult(h);

    // Post-Init gömme denemesi (bogus nonzero native handle).
    RowlEngine_SetExternalWindowHandle(h, reinterpret_cast<void*>(0x1234), 1280, 720);

    const int code = lastCode(h);
    const std::string op = lastOp(h);
    std::cout << "  post-Init SetExternal -> code=" << code << " op=" << op << std::endl;

    // D01 fail-closed sözleşmesi: post-Init SetExternal reddedilmeli ve
    // StateError(11) + op damgası bırakmalı (RED'de kod 0 kalıyordu).
    if (code != 11 /* ROWL_RESULT_STATE_ERROR */)
        redFail("post-Init SetExternalWindowHandle must stamp StateError(11), got code=" +
                std::to_string(code) + " op=" + op);
    if (op != "set_external_window_handle")
        redFail("post-Init SetExternal op stamp must be set_external_window_handle, got " + op);

    // Post-Init checked formu da reddeder (return + damga).
    checkChecked("Checked post-Init SetExternal",
                 RowlEngine_SetExternalWindowHandleChecked(h, reinterpret_cast<void*>(0x1234), 1280, 720),
                 11);
    if (lastOp(h) != "set_external_window_handle")
        redFail("Checked post-Init op stamp lost");

    // Init-sonrası ResizeViewportChecked hâlâ forward eder (canlı-yol korunur).
    checkChecked("Checked post-Init ResizeViewport",
                 RowlEngine_ResizeViewportChecked(h, 800, 600), 0);

    // Ölü/bozuk handle bacağı: legacy void çökmeden sessiz no-op; checked
    // INVALID_HANDLE döner (damgalanacak motor yok).
    auto* const bogus = reinterpret_cast<RowlEngineHandle>(0xDEADBEEFu);
    RowlEngineHandle dead = RowlEngine_Create();
    if (dead == nullptr) redFail("second Create returned null");
    RowlEngine_Destroy(dead);
    const RowlEngineHandle victims[3] = {nullptr, bogus, dead};
    for (RowlEngineHandle v : victims) {
        RowlEngine_SetExternalWindowHandle(v, reinterpret_cast<void*>(0x1234), 1280, 720);
        RowlEngine_ResizeViewport(v, 1280, 720);
        checkChecked("Checked SetExternal dead-handle",
                     RowlEngine_SetExternalWindowHandleChecked(v, reinterpret_cast<void*>(0x1234), 1280, 720),
                     1);
        checkChecked("Checked ResizeViewport dead-handle",
                     RowlEngine_ResizeViewportChecked(v, 1280, 720), 1);
    }

    // Pre-Init canlı handle: null/sıfır-arg INVALID_ARGUMENT + damga,
    // nonzero kabul (OK), pre-Init Resize STATE_ERROR (lifecycle:164 paritesi).
    RowlEngineHandle pre = RowlEngine_Create();
    if (pre == nullptr) redFail("pre-init Create returned null");
    checkChecked("Checked pre-Init null-hwnd",
                 RowlEngine_SetExternalWindowHandleChecked(pre, nullptr, 1280, 720), 2);
    if (lastCode(pre) != 2 || lastOp(pre) != "set_external_window_handle")
        redFail("pre-Init null-hwnd stamp lost");
    checkChecked("Checked pre-Init zero-dims",
                 RowlEngine_SetExternalWindowHandleChecked(pre, reinterpret_cast<void*>(0x1234), 0, 720),
                 2);
    checkChecked("Checked pre-Init accept",
                 RowlEngine_SetExternalWindowHandleChecked(pre, reinterpret_cast<void*>(0x1234), 1280, 720),
                 0);
    checkChecked("Checked pre-Init ResizeViewport",
                 RowlEngine_ResizeViewportChecked(pre, 800, 600), 11);
    if (lastCode(pre) != 11 || lastOp(pre) != "resize_viewport")
        redFail("pre-Init ResizeViewport stamp lost");
    RowlEngine_Destroy(pre);

    // Yabancı thread: loud WRONG_THREAD (ownership tier).
    RowlEngine_ResultCode foreignCode = ROWL_RESULT_OK;
    std::thread foreign([&] {
        foreignCode = RowlEngine_SetExternalWindowHandleChecked(
            h, reinterpret_cast<void*>(0x1234), 1280, 720);
    });
    foreign.join();
    checkChecked("Checked foreign-thread SetExternal", foreignCode, 14);
    if (lastCode(h) != 14)
        redFail("foreign-thread WRONG_THREAD stamp lost");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("post-Init SetExternal fail-closed + checked all-paths fail-closed");
    return 0;
}
