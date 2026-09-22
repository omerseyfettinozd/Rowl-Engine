/**
 * test_d13_aux_gate_red_probe.cpp — D13 aux-gate RED/green probu.
 *
 * Yalnizca public C API'yi kullanan bagimsiz ikili (paylasilan RowlEngineCore'a
 * baglanir; uretim topolojisiyle ayni). rowl_tests govdesine gomulmez ki
 * kirmizi-yesil dongusu tum suiti kosmadan saniyeler icinde kanitlansin.
 *
 * P1 (aux-yabanci-yazim): canli handle'a yabanci thread'den aux yazici
 * (SetCharacterSlotAsset / LoadChapter) -> WRONG_THREAD (14) + op damgasi,
 * aux state'e DOKUNULMAZ (owner read-back birebir). int-tasiyicilar
 * (IsCharacterSlotVisible / PumpPrefetch) sessiz 0 + ayni damga.
 * P2 (registry-disi): bogus/olu handle (nullptr, bayat token, hic
 * mintlenmemis) ile aux + lifecycle girisi -> fail-closed: ResultCode
 * INVALID_HANDLE (1), int 0, out-param'a DOKUNULMAZ (hayalet-entry'nin
 * gozlemlenebilir izi yok + registry'e yazim yok: sonraki Create/Init/
 * kullan/Destroy dongusu temiz calisir).
 *
 * RED imzasi: yabanci yazim state'i degistirirse / WRONG_THREAD damgasi
 * kaybolursa, veya olu handle hayalet-entry/out-param kirlenmesi birakirsa
 * PROB-DIRTY + exit 1. YESIL: PROB-CLEAN + exit 0.
 *
 * Calisma: rowl_d13_aux_gate_red_probe (ctest -R d13_aux_gate_red_probe).
 * Ortam: SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy.
 */
#include "rowl_test_harness.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void redFail(const std::string& message) {
    rowlLockFail("d13-aux-gate-red-probe", message);
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

std::string querySlotAsset(RowlEngineHandle h, const char* slot) {
    uint32_t required = 0;
    if (RowlEngine_GetCharacterSlotAssetUtf8(h, slot, nullptr, 0, &required) !=
        ROWL_RESULT_OK) {
        return "<query-failed>";
    }
    std::string buffer(required, '\0');
    if (RowlEngine_GetCharacterSlotAssetUtf8(h, slot, buffer.data(), required,
                                             &required) != ROWL_RESULT_OK) {
        return "<read-failed>";
    }
    buffer.resize(required > 0 ? required - 1 : 0);
    return buffer;
}

}  // namespace

int main() {
    TEST_SECTION("D13 aux-gate probe (P1 foreign-write, P2 dead-handle)");

    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) redFail("RowlEngine_Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        redFail("RowlEngine_Init(320x180) must succeed under dummy drivers");

    // P1 kurulum: owner aux state'i yazar + geri okur (canli-yol saglam).
    checkResult("owner SetCharacterSlotAsset(body)",
                RowlEngine_SetCharacterSlotAsset(h, "body", "owner.png"), 0);
    if (querySlotAsset(h, "body") != "owner.png")
        redFail("owner read-back must see owner.png");
    checkResult("owner LoadChapter(unknown) rejects",
                RowlEngine_LoadChapter(h, "no-such-chapter"), 2);

    // P1: yabanci thread aux yazicilari damgali reddeder, state'e dokunmaz.
    RowlEngine_ClearLastResult(h);
    RowlEngine_ResultCode foreignSet = ROWL_RESULT_OK;
    RowlEngine_ResultCode foreignLoad = ROWL_RESULT_OK;
    int foreignVisible = -1;
    int foreignPumped = -1;
    std::thread foreign([&] {
        foreignSet = RowlEngine_SetCharacterSlotAsset(h, "body", "evil.png");
        foreignLoad = RowlEngine_LoadChapter(h, "ch1");
        foreignVisible = RowlEngine_IsCharacterSlotVisible(h, "body");
        foreignPumped = RowlEngine_PumpPrefetch(h, 4.0f);
    });
    foreign.join();
    checkResult("foreign SetCharacterSlotAsset", foreignSet, 14);
    checkResult("foreign LoadChapter", foreignLoad, 14);
    if (foreignVisible != 0)
        redFail("foreign IsCharacterSlotVisible must stay 0");
    if (foreignPumped != 0)
        redFail("foreign PumpPrefetch must stay 0");
    if (lastCode(h) != 14)
        redFail("foreign WRONG_THREAD stamp lost");
    const std::string op = lastOp(h);
    if (op != "set_character_slot_asset" && op != "load_chapter" &&
        op != "is_character_slot_visible" && op != "pump_prefetch")
        redFail("foreign op stamp lost, got " + op);
    if (querySlotAsset(h, "body") != "owner.png")
        redFail("foreign write touched aux state (owner.png lost)");

    // P2: bogus/olu handle'lar fail-closed; out-param'lara dokunulmaz.
    RowlEngineHandle dead = RowlEngine_Create();
    if (dead == nullptr) redFail("second Create returned null");
    RowlEngine_Destroy(dead);
    auto* const bogus = reinterpret_cast<RowlEngineHandle>(0xDEADBEEFu);
    const RowlEngineHandle victims[3] = {nullptr, bogus, dead};
    for (RowlEngineHandle v : victims) {
        checkResult("dead SetCharacterSlotAsset",
                    RowlEngine_SetCharacterSlotAsset(v, "body", "ghost.png"), 1);
        checkResult("dead LoadChapter", RowlEngine_LoadChapter(v, "ch1"), 1);
        if (RowlEngine_IsCharacterSlotVisible(v, "body") != 0)
            redFail("dead IsCharacterSlotVisible must stay 0");
        if (RowlEngine_IsChapterBoundaryNode(v, 1) != 0)
            redFail("dead IsChapterBoundaryNode must stay 0");
        if (RowlEngine_PumpPrefetch(v, 4.0f) != 0)
            redFail("dead PumpPrefetch must stay 0");
        if (RowlEngine_IsRunning(v) != 0)
            redFail("dead IsRunning must stay 0");
        char progress[64];
        std::memset(progress, 0xAB, sizeof(progress));
        uint32_t required = 0;
        checkResult("dead GetLoadedChaptersJson",
                    RowlEngine_GetLoadedChaptersJson(v, progress, sizeof(progress),
                                                   &required),
                    1);
        checkResult("dead GetPrefetchProgressJson",
                    RowlEngine_GetPrefetchProgressJson(v, progress, sizeof(progress),
                                                     &required),
                    1);
        char asset[64];
        std::memset(asset, 0xAB, sizeof(asset));
        checkResult("dead GetCharacterSlotAssetUtf8",
                    RowlEngine_GetCharacterSlotAssetUtf8(v, "body", asset,
                                                       sizeof(asset), &required),
                    1);
        for (char c : asset) {
            if (c != static_cast<char>(0xAB))
                redFail("dead read touched the caller buffer (ghost write)");
        }
        float opacity = -1.0f;
        checkResult("dead GetCharacterSlotOpacity",
                    RowlEngine_GetCharacterSlotOpacity(v, "body", &opacity), 1);
        if (opacity != -1.0f)
            redFail("dead read touched the out-param");
    }

    // P2 registry-temizlik izi: olu-cagrilardan sonra yeni handle temiz calisir.
    RowlEngineHandle fresh = RowlEngine_Create();
    if (fresh == nullptr) redFail("fresh Create after dead calls returned null");
    if (RowlEngine_Init(fresh, 320, 180, 0) != 1)
        redFail("fresh Init must succeed (registry untouched by dead calls)");
    checkResult("fresh SetCharacterSlotAsset",
                RowlEngine_SetCharacterSlotAsset(fresh, "face", "fresh.png"), 0);
    if (querySlotAsset(fresh, "face") != "fresh.png")
        redFail("fresh handle sees polluted aux state");
    if (querySlotAsset(h, "body") != "owner.png")
        redFail("owner aux state polluted by dead-handle calls");
    RowlEngine_Shutdown(fresh);
    RowlEngine_Destroy(fresh);

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("d13 aux-gate: foreign WRONG_THREAD+untouched, dead fail-closed+clean");
    return 0;
}
