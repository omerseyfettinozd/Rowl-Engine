/**
 * test_e2a_transition_spritesheet_red_probe.cpp — E2a gecis-repertuvari
 * (dissolve/push/iris) + sprite-sheet kilidi.
 *
 * RED (pre-E2a) kaniti: "dissolve"/"push_left"/"iris_in" gibi turler
 * isKnownKind'dan gecemezdi (StartTransition sessiz no-op, active=0);
 * kinds-JSON/state-JSON giris noktalari yoktu (baglanti hatasi);
 * sheet alanli character JSON'u yutulur, state hep "[]" donerdi.
 * Fix: TransitionManager repertuvari + CharacterRenderData sheet saati +
 * yalniz-eklemeli C API (2 capability biti + 2 fonksiyon, imza degisikligi
 * yok) + IsPreviewFrameStatic/dynamicsInFlight sheet kosullari.
 *
 * GREEN regresyon bacagi: yalnizca public C API (paylasilan RowlEngineCore'a
 * baglanir; uretim topolojisiyle ayni). rowl_tests govdesine gomulmez ki
 * kirmizi-yesil dongusu tum suiti kosmadan saniyeler icinde kanitlansin.
 * C0 kilidine dokunulmaz (ses dosyalari degismedi; esik >0.005f aynen).
 *
 * Calisma: rowl_e2a_transition_spritesheet_red_probe (ctest -R e2a_repertoire).
 * Ortam: SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

void redFail(const std::string& message) {
    rowlLockFail("e2a-repertoire-probe", message);
}

std::string queryKinds() {
    uint32_t required = 0;
    RowlEngine_ResultCode rc = RowlEngine_GetSupportedTransitionKindsJson(
        nullptr, 0, &required);
    if (rc != ROWL_RESULT_OK || required < 2)
        redFail("kinds size-query must return OK + NUL-inclusive size");
    std::vector<char> buf(required);
    rc = RowlEngine_GetSupportedTransitionKindsJson(
        buf.data(), required, &required);
    if (rc != ROWL_RESULT_OK)
        redFail("kinds exact-buffer query must return OK");
    return std::string(buf.data());
}

std::string querySheetState(RowlEngineHandle h) {
    uint32_t required = 0;
    RowlEngine_ResultCode rc = RowlEngine_GetSpriteSheetStateJson(
        h, nullptr, 0, &required);
    if (rc != ROWL_RESULT_OK || required < 2)
        redFail("sheet-state size-query must return OK + NUL-inclusive size");
    std::vector<char> buf(required);
    rc = RowlEngine_GetSpriteSheetStateJson(h, buf.data(), required, &required);
    if (rc != ROWL_RESULT_OK)
        redFail("sheet-state exact-buffer query must return OK");
    return std::string(buf.data());
}

RowlEngineHandle makeEngine() {
    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) redFail("RowlEngine_Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        redFail("RowlEngine_Init(320x180) must succeed under dummy drivers");
    return h;
}

void seedCharacter(RowlEngineHandle h, const char* sceneJson) {
    RowlEngine_UpdateSceneFromJson(h, sceneJson);
    if (RowlEngine_GetLastResultCode(h) != ROWL_RESULT_OK)
        redFail("character seed must stamp OK");
}

}  // namespace

int main() {
    TEST_SECTION("E2a probe (transition repertoire + sprite-sheet)");

    // Bacak 1: capability bitleri (yalniz-eklemeli; eski bitler aynen).
    {
        uint64_t caps = 0;
        if (RowlEngine_GetCapabilities(&caps) != ROWL_RESULT_OK)
            redFail("GetCapabilities must return OK");
        if ((caps & UINT64_C(524288)) == 0)
            redFail("TRANSITION_REPERTOIRE bit (524288) missing");
        if ((caps & UINT64_C(1048576)) == 0)
            redFail("SPRITE_SHEET bit (1048576) missing");
        if ((caps & ROWL_ENGINE_CAPABILITY_RESULT_CODES) == 0)
            redFail("legacy RESULT_CODES bit lost");
        TEST_PASS("capability bits additive (legacy preserved)");
    }

    // Bacak 2: kinds JSON (kesif yuzeyi; dar tampon sozlesmesi dahil).
    {
        const std::string kinds = queryKinds();
        std::cout << "  kinds=" << kinds << std::endl;
        const char* want[] = {"crossfade",   "fade_black", "wipe_left",
                              "dissolve",    "push_left",  "push_right",
                              "push_up",     "push_down",  "iris_in",
                              "iris_out",    nullptr};
        for (int i = 0; want[i] != nullptr; ++i) {
            if (kinds.find(std::string("\"") + want[i] + "\"") == std::string::npos)
                redFail(std::string("kinds JSON missing ") + want[i]);
        }
        char tiny[8];
        uint32_t required = 0;
        if (RowlEngine_GetSupportedTransitionKindsJson(tiny, sizeof(tiny),
                                                       &required) !=
            ROWL_RESULT_BUFFER_TOO_SMALL)
            redFail("undersized kinds buffer must return BUFFER_TOO_SMALL");
        if (tiny[0] != '\0')
            redFail("undersized kinds buffer must be cleared");
        TEST_PASS("kinds JSON lists repertoire + caller-buffer contract");
    }

    // Bacak 3: her yeni tur baslar ve biter; bilinmeyen tur sessiz no-op
    // (D6-#147 kilidi korunur).
    {
        RowlEngineHandle h = makeEngine();
        RowlEngine_UpdateSceneFromJson(h, R"([
            {"type":"background","id":"bg","enabled":true,
             "data":{"texture":"baseline.png"}}
        ])");
        const char* kinds[] = {"dissolve", "push_left", "push_right",
                               "push_up",  "push_down", "iris_in",
                               "iris_out", "push",      "iris",
                               nullptr};
        for (int i = 0; kinds[i] != nullptr; ++i) {
            RowlEngine_StartTransition(h, kinds[i], 0.1f, "");
            if (RowlEngine_IsTransitionActive(h) != 1)
                redFail(std::string("kind must start: ") + kinds[i]);
            RowlEngine_Step(h, 0.25f);
            if (RowlEngine_IsTransitionActive(h) != 0)
                redFail(std::string("kind must complete: ") + kinds[i]);
        }
        RowlEngine_ClearLastResult(h);
        RowlEngine_StartTransition(h, "bogus-kind", 1.0f, "");
        if (RowlEngine_IsTransitionActive(h) != 0)
            redFail("invalid kind must not start a transition");
        if (RowlEngine_GetLastResultCode(h) != ROWL_RESULT_OK)
            redFail("gate rejection must stay silent (D6-#147)");
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        TEST_PASS("repertoire starts/completes; invalid kind silent");
    }

    // Bacak 4: sprite-sheet saati (loop sarmali + non-loop durmasi +
    // fail-closed legacy).
    {
        RowlEngineHandle h = makeEngine();
        seedCharacter(h, R"([
            {"type":"character","id":"c","enabled":true,
             "data":{"sprite":"hero.png","sheet_cols":4,"sheet_rows":2,
                     "sheet_fps":8.0,"sheet_loop":true}}
        ])");
        std::string s0 = querySheetState(h);
        std::cout << "  sheet0=" << s0 << std::endl;
        if (s0.find("\"frame\":0") == std::string::npos)
            redFail("loop sheet must start at frame 0, got " + s0);
        if (s0.find("\"frames\":8") == std::string::npos)
            redFail("4x2 sheet must report 8 frames, got " + s0);
        RowlEngine_Step(h, 0.25f);
        RowlEngine_Step(h, 0.25f);
        const std::string s1 = querySheetState(h);
        if (s1.find("\"frame\":4") == std::string::npos)
            redFail("8fps x 0.5s must reach frame 4, got " + s1);
        if (RowlEngine_IsPreviewFrameStatic(h) != 0)
            redFail("looping sheet must hold preview non-static");
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        TEST_PASS("loop sheet advances 0->4 in 0.5s; preview non-static");

        h = makeEngine();
        seedCharacter(h, R"([
            {"type":"character","id":"c","enabled":true,
             "data":{"sprite":"hero.png","sheet_cols":4,"sheet_rows":2,
                     "sheet_fps":8.0,"sheet_loop":false}}
        ])");
        for (int i = 0; i < 8; ++i) RowlEngine_Step(h, 0.25f);
        const std::string s2 = querySheetState(h);
        std::cout << "  sheet-end=" << s2 << std::endl;
        if (s2.find("\"frame\":7") == std::string::npos)
            redFail("non-loop sheet must park at last frame 7, got " + s2);
        if (s2.find("\"playing\":false") == std::string::npos)
            redFail("parked non-loop sheet must report playing=false, got " + s2);
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        TEST_PASS("non-loop sheet parks at last frame + playing=false");

        h = makeEngine();
        seedCharacter(h, R"([
            {"type":"character","id":"c","enabled":true,
             "data":{"sprite":"hero.png","sheet_cols":0,"sheet_rows":2,
                     "sheet_fps":8.0}}
        ])");
        const std::string s3 = querySheetState(h);
        if (s3 != "[]")
            redFail("invalid sheet must fail closed to [], got " + s3);
        if (RowlEngine_IsPreviewFrameStatic(h) != 1)
            redFail("sheet-less scene must stay preview-static");
        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        TEST_PASS("invalid sheet fails closed (legacy); static preserved");
    }

    // Bacak 5: olu/init-siz handle sozlesmesi.
    {
        auto* const bogus = reinterpret_cast<RowlEngineHandle>(0xDEADBEEFu);
        uint32_t required = 0;
        if (RowlEngine_GetSpriteSheetStateJson(bogus, nullptr, 0, &required) !=
            ROWL_RESULT_INVALID_HANDLE)
            redFail("dead handle must report INVALID_HANDLE");
        RowlEngineHandle pre = RowlEngine_Create();
        if (pre == nullptr) redFail("pre-init Create returned null");
        if (RowlEngine_GetSpriteSheetStateJson(pre, nullptr, 0, &required) !=
            ROWL_RESULT_OK)
            redFail("pre-init size-query must return OK");
        std::vector<char> buf(required);
        if (RowlEngine_GetSpriteSheetStateJson(pre, buf.data(), required,
                                               &required) != ROWL_RESULT_OK)
            redFail("pre-init query must return OK");
        if (std::string(buf.data()) != "[]")
            redFail("pre-init state must be []");
        if (RowlEngine_GetLastResultCode(pre) != ROWL_RESULT_STATE_ERROR)
            redFail("pre-init query must stamp StateError");
        RowlEngine_Destroy(pre);
        TEST_PASS("dead/pre-init handle contract fail-closed");
    }

    TEST_PASS("E2a transition repertoire + sprite-sheet GREEN");
    return 0;
}
