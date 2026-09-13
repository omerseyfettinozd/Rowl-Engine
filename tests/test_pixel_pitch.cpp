/**
 * test_pixel_pitch.cpp — MS-0 pitch contract & length-reporting string getters.
 *
 * Guards the post-audit memory contract:
 *  - RowlEngine_GetPixelBufferEx reports a pitch >= width*4; legacy entry point
 *    agrees with Ex on pointer and dimensions (it only discards the pitch).
 *  - Every scanline start (base + row*pitch) is readable; row stride is uniform.
 *  - Every ...WithLength variant reports strlen(content) and matches the plain
 *    getter byte-for-byte, including the null-handle fallback.
 */
#include "rowl_test_harness.hpp"

void test_pixel_pitch() {
    TEST_SECTION("MS-0 Pitch Contract & Length-Reporting Getters");

    // ── Null-handle fallback ────────────────────────────────────────────
    // Every WithLength variant must byte-match its plain getter (whatever the
    // fallback is: "", "[]", "none") and report strlen of that fallback.
    {
        uint32_t w = 99, h = 99, p = 99;
        if (RowlEngine_GetPixelBufferEx(nullptr, &w, &h, &p) != nullptr ||
            w != 0 || h != 0 || p != 0) {
            std::cerr << "GetPixelBufferEx null-handle fallback broken" << std::endl;
            exit(1);
        }
        // NOTE: sequence explicitly — reading `len` in the same full-expression
        // as the WithLength call would race unspecified argument evaluation order.
        auto checkWl = [](const char* name, const char* (*plainFn)(), const char* (*wlFn)(uint32_t*)) {
            uint32_t len = 99;
            const char* withLen = wlFn(&len);
            const char* plain = plainFn();
            if (std::strcmp(plain, withLen) != 0 || len != static_cast<uint32_t>(std::strlen(plain))) {
                std::cerr << "WithLength null fallback diverged on: " << name << std::endl;
                exit(1);
            }
        };
        checkWl("speaker", []() { return RowlEngine_GetSpeaker(nullptr); },
                [](uint32_t* l) { return RowlEngine_GetSpeakerWithLength(nullptr, l); });
        checkWl("dialogue", []() { return RowlEngine_GetDialogue(nullptr); },
                [](uint32_t* l) { return RowlEngine_GetDialogueWithLength(nullptr, l); });
        checkWl("history", []() { return RowlEngine_GetDialogueHistoryJson(nullptr); },
                [](uint32_t* l) { return RowlEngine_GetDialogueHistoryJsonWithLength(nullptr, l); });
        checkWl("operation", []() { return RowlEngine_GetLastResultOperation(nullptr); },
                [](uint32_t* l) { return RowlEngine_GetLastResultOperationWithLength(nullptr, l); });
        checkWl("message", []() { return RowlEngine_GetLastResultMessage(nullptr); },
                [](uint32_t* l) { return RowlEngine_GetLastResultMessageWithLength(nullptr, l); });
        {
            uint32_t len = 99;
            const char* withLen = RowlEngine_GetVariableWithLength(nullptr, "k", &len);
            const char* plain = RowlEngine_GetVariable(nullptr, "k");
            if (std::strcmp(plain, withLen) != 0 || len != static_cast<uint32_t>(std::strlen(plain))) {
                std::cerr << "WithLength null fallback diverged on: variable" << std::endl;
                exit(1);
            }
        }
    }
    TEST_PASS("MS-0 WithLength + Ex Null-Handle Fallback Contract");

    // ── Live offscreen surface ────────────────────────────────────────────
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) exit(1);
    if (RowlEngine_Init(handle, 640, 480, 0) != 1) {
        std::cerr << "MS-0 test engine init failed" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);

    uint32_t w = 0, h = 0, pitch = 0;
    const uint8_t* exPixels = RowlEngine_GetPixelBufferEx(handle, &w, &h, &pitch);
    uint32_t lw = 0, lh = 0;
    const uint8_t* legacyPixels = RowlEngine_GetPixelBuffer(handle, &lw, &lh);
    if (exPixels == nullptr || legacyPixels == nullptr || exPixels != legacyPixels ||
        w != 640 || h != 480 || lw != w || lh != h) {
        std::cerr << "Ex/legacy pixel buffer disagreement" << std::endl;
        exit(1);
    }
    if (pitch < w * 4 || pitch % 4 != 0) {
        std::cerr << "Reported pitch violates contract: pitch=" << pitch
                  << " width=" << w << std::endl;
        exit(1);
    }
    TEST_PASS("MS-0 Ex/Legacy Agreement & Pitch >= width*4");

    // ── Every scanline start is readable; stride is uniform ──────────────
    {
        volatile uint8_t sink = 0;
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t* row = exPixels + static_cast<size_t>(y) * pitch;
            sink ^= row[0];                                   // first byte of row
            sink ^= row[w * 4 - 1];                           // last byte of tight row
            if (y + 1 < h) {
                const uint8_t* next = exPixels + static_cast<size_t>(y + 1) * pitch;
                if (next - row != static_cast<ptrdiff_t>(pitch)) {
                    std::cerr << "Non-uniform row stride at row " << y << std::endl;
                    exit(1);
                }
            }
        }
        (void)sink;
    }
    TEST_PASS("MS-0 Scanline Walk: All Rows Readable, Uniform Stride");

    // ── WithLength agrees with plain getters on live state ───────────────
    // NOTE: strictly sequenced — len is read only after its WithLength call
    // returns, and each verdict compares against a fresh plain call because
    // both spellings share one per-getter thread_local buffer.
    {
        RowlEngine_SetVariable(handle, "ms0_probe", "ms0_value");
        uint32_t len = 99;
        const char* wl = nullptr;
        std::string captured;
        const char* fresh = nullptr;
#define MS0_CHECK_LIVE(getterBase, label)                          \
    wl = RowlEngine_##getterBase##WithLength(handle, &len);        \
    captured.assign(wl, len);                                      \
    fresh = RowlEngine_##getterBase(handle);                       \
    if (len != static_cast<uint32_t>(std::strlen(fresh)) || captured != fresh) { \
        std::cerr << "WithLength mismatch on live getter: " << label << std::endl; \
        exit(1);                                                   \
    }
        MS0_CHECK_LIVE(GetSpeaker, "speaker");
        MS0_CHECK_LIVE(GetDialogue, "dialogue");
        MS0_CHECK_LIVE(GetDialogueHistoryJson, "history");
        MS0_CHECK_LIVE(GetScriptRuntimeDiagnosticsJson, "diagnostics");
        MS0_CHECK_LIVE(GetLastResultOperation, "operation");
        MS0_CHECK_LIVE(GetLastResultMessage, "message");
        MS0_CHECK_LIVE(GetLastResultTarget, "target");
#undef MS0_CHECK_LIVE
        uint32_t lVar = 0;
        const char* varLen = RowlEngine_GetVariableWithLength(handle, "ms0_probe", &lVar);
        if (std::strcmp(varLen, "ms0_value") != 0 || lVar != 9) {
            std::cerr << "WithLength variable round-trip failed" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("MS-0 WithLength Matches Plain Getters On Live State");

    RowlEngine_Destroy(handle);
}
