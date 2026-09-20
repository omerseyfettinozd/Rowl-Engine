/**
 * test_handle_hygiene.cpp — A4-tur1: C-API handle-önce sıralama kilidi.
 *
 * Konvansiyon (story-TU referans): handle-taşıyan her ResultCode girişi
 * ölü/bozuk-handle'da arg'lara bakmadan ROWL_RESULT_INVALID_HANDLE döner.
 * character_layers / prefetch / provenance 13 girişte arg-önce-handle-sonra
 * idi (ölü-handle + bozuk-arg → INVALID_ARGUMENT); toEngineChecked hoist'i
 * ile handle-önce'ye çekildi. Bu test o sıralamayı kilitler + void/int
 * girişlerinin ölü-handle'da çökmeden fail-closed davrandığını kanıtlar.
 */
#include "rowl_test_harness.hpp"

namespace {

void checkHandleCode(const char* name, RowlEngine_ResultCode got) {
    if (got != ROWL_RESULT_INVALID_HANDLE) {
        rowlLockFail("handle-hygiene",
                     std::string(name) + ": dead-handle expected INVALID_HANDLE(1), got " +
                         std::to_string(static_cast<int>(got)));
    }
}

}  // namespace

void test_handle_hygiene() {
    TEST_SECTION("C-API Handle Hygiene (A4-tur1: handle-once ordering)");

    // Bozuk-handle: hiç yaratılmamış. Ölü-handle: yaratılıp yok edilmiş.
    // (Handle'lar nesilli slot token'ıdır; ölü-handle tekrar geçerli olmaz
    // — use-after-destroy testi için güvenli zemin.)
    auto* const bogus = reinterpret_cast<RowlEngineHandle>(0xDEADBEEFu);
    RowlEngineHandle dead = RowlEngine_Create();
    if (dead == nullptr) rowlLockFail("handle-hygiene", "RowlEngine_Create returned null");
    RowlEngine_Destroy(dead);

    const RowlEngineHandle victims[3] = {nullptr, bogus, dead};
    for (RowlEngineHandle h : victims) {
        // character_layers (7 hoist): bozuk-arg BİLE olsa INVALID_HANDLE.
        checkHandleCode("SetCharacterSlotAsset",
                        RowlEngine_SetCharacterSlotAsset(h, nullptr, nullptr));
        checkHandleCode("GetCharacterSlotAssetUtf8",
                        RowlEngine_GetCharacterSlotAssetUtf8(h, nullptr, nullptr, 0, nullptr));
        checkHandleCode("SetCharacterSlotOpacity",
                        RowlEngine_SetCharacterSlotOpacity(h, nullptr, -1.0f));
        float opacity = 0.0f;
        checkHandleCode("GetCharacterSlotOpacity",
                        RowlEngine_GetCharacterSlotOpacity(h, nullptr, &opacity));
        checkHandleCode("SetCharacterSlotVisible",
                        RowlEngine_SetCharacterSlotVisible(h, nullptr, 1));
        checkHandleCode("RegisterCharacterPreset",
                        RowlEngine_RegisterCharacterPreset(h, nullptr, nullptr));
        checkHandleCode("ApplyCharacterExpression",
                        RowlEngine_ApplyCharacterExpression(h, nullptr));
        // prefetch (5 hoist).
        checkHandleCode("LoadChapterIndexJson",
                        RowlEngine_LoadChapterIndexJson(h, nullptr));
        checkHandleCode("AppendChapterFileJson",
                        RowlEngine_AppendChapterFileJson(h, nullptr));
        checkHandleCode("LoadChapter", RowlEngine_LoadChapter(h, nullptr));
        checkHandleCode("UnloadChapter", RowlEngine_UnloadChapter(h, nullptr));
        checkHandleCode("PrefetchChapterAssets",
                        RowlEngine_PrefetchChapterAssets(h, "\xFF invalid \xFE", 0));
        // provenance (1 hoist).
        checkHandleCode("GetAssetProvenanceJson",
                        RowlEngine_GetAssetProvenanceJson(h, nullptr, nullptr, 0, nullptr));
        // story-TU regresyon-pinleri (zaten handle-önce idi).
        uint32_t count = 0;
        checkHandleCode("GetChapterCount", RowlEngine_GetChapterCount(h, &count));
        if (RowlEngine_EvaluateCondition(h, "true") != 0) {
            rowlLockFail("handle-hygiene", "EvaluateCondition dead-handle must be 0");
        }
        // void/int fail-closed: çökme yok, sessiz/0.
        RowlEngine_SetPaused(h, 1);
        RowlEngine_SetCamera(h, 0.0f, 0.0f, 1.0f);
        RowlEngine_PauseMenuCommand(h, 0);
        if (RowlEngine_IsPaused(h) != 0) {
            rowlLockFail("handle-hygiene", "IsPaused dead-handle must be 0");
        }
    }
    TEST_PASS("dead/bogus/null handle: 13 hoisted entries INVALID_HANDLE, void/int fail-closed");

    // Canlı-handle + bozuk-arg hâlâ INVALID_ARGUMENT (sıralama yalnız
    // ölü-handle yolunu değiştirdi; saf arg-dogrulama korunur).
    RowlEngineHandle live = RowlEngine_Create();
    if (live == nullptr) rowlLockFail("handle-hygiene", "second RowlEngine_Create returned null");
    if (RowlEngine_SetCharacterSlotAsset(live, nullptr, nullptr) != ROWL_RESULT_INVALID_ARGUMENT) {
        rowlLockFail("handle-hygiene", "live-handle + null slot must stay INVALID_ARGUMENT");
    }
    if (RowlEngine_LoadChapter(live, nullptr) != ROWL_RESULT_INVALID_ARGUMENT) {
        rowlLockFail("handle-hygiene", "live-handle + null chapter must stay INVALID_ARGUMENT");
    }
    RowlEngine_Destroy(live);
    TEST_PASS("live-handle + bad-arg still INVALID_ARGUMENT (no over-correction)");
}
