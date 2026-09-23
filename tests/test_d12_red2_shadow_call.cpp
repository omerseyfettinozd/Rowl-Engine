/**
 * test_d12_red2_shadow_call.cpp — D12 golge-cagri RED kilidi (RED-2).
 *
 * Bagimsiz ikili: rowl_d12_red2_shadow_call (ctest -R d12_red2).
 * D01 konvansiyonu: yalnizca public C API (paylasilan RowlEngineCore'a
 * baglanir; uretim topolojisiyle ayni). rowl_tests govdesine gomulmez.
 *
 * Sessiz legacy yuzey fail-closed kilidi: bogus/olu handle ile
 *   void       -> no-op (motor untouched, exit 0 yolu),
 *   int        -> 0,
 *   const char*-> "" (non-null),
 * yabanci-thread okuma -> sessiz default.
 * Orneklenenler: Destroy (:161), IsRunning (:288 -> olude 0),
 * GetSpeaker (:692 -> olude ""), SetBgmVolume (:758 -> olude no-op).
 * c_api_markup.cpp / c_api_text_shaping.cpp guard'sizlari handle'siz saf
 * helper'dir (Parse/Strip/ShapeMarkup) — golge-cagri adayi DEGIL.
 * D14 Checked loud varyantlar (WRONG_THREAD 14) bu probun DISI.
 *
 * Ongoru: guardsiz legacy cagrida crash/garbage -> exit 1 (veya abort);
 * fix sonrasi exit 0. Bu dilimde davranis zaten fail-closed oldugundan
 * probun post-fix exit 0 vermesi beklenir; RED degeri pre-fix celiski
 * kanitindadir (RED-1). Bu prob karakterizasyon-pinidir (davranis muhrü),
 * RED-kilidi RED-1'dir (curutme-hakemligi notu).
 *
 * KIRMIZI-YESIL SOZLESMESI: fail-closed exit 0 / crash-garbage exit 1.
 * Kirmizida commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cstring>
#include <string>
#include <thread>

namespace {

void red2Fail(const std::string& message) {
    rowlLockFail("d12-red2-shadow-call", message);
}

void red2Require(bool condition, const std::string& message) {
    if (!condition) red2Fail(message);
}

}  // namespace

int main() {
    TEST_SECTION("D12 RED-2: golge-cagri fail-closed probu");

    // Bogus handle: kayit-disi token (classifyHandle -> Dead, sessiz).
    auto* bogus = reinterpret_cast<RowlEngineHandle>(0x1234);

    // ── Bogus handle: sessiz fail-closed (crash/garbage -> RED) ──
    RowlEngine_Destroy(bogus);  // no-op; donerse yasar
    red2Require(RowlEngine_IsRunning(bogus) == 0,
                "bogus IsRunning 0 degil");
    {
        const char* speaker = RowlEngine_GetSpeaker(bogus);
        red2Require(speaker != nullptr, "bogus GetSpeaker null dondu");
        red2Require(std::strcmp(speaker, "") == 0,
                    "bogus GetSpeaker '' degil");
    }
    RowlEngine_SetBgmVolume(bogus, 0.5f);  // no-op; donerse yasar
    TEST_PASS("Bogus handle sessiz fail-closed");

    // ── Olu handle: Create + Init + Destroy sonrasi golge cagrilar ──
    RowlEngineHandle dead = RowlEngine_Create();
    red2Require(dead != nullptr, "RowlEngine_Create null dondu");
    red2Require(RowlEngine_Init(dead, 320, 180, 0) == 1,
                "RowlEngine_Init(320x180) dummy driver altinda basarili olmali");
    RowlEngine_Destroy(dead);
    RowlEngine_Destroy(dead);  // tekrar destroy: sessiz no-op
    red2Require(RowlEngine_IsRunning(dead) == 0,
                "olu IsRunning 0 degil");
    {
        const char* speaker = RowlEngine_GetSpeaker(dead);
        red2Require(speaker != nullptr, "olu GetSpeaker null dondu");
        red2Require(std::strcmp(speaker, "") == 0,
                    "olu GetSpeaker '' degil");
    }
    RowlEngine_SetBgmVolume(dead, 0.5f);  // sessiz no-op
    TEST_PASS("Olu handle sessiz fail-closed");

    // ── Yabanci-thread okuma: sessiz default (loud varyantlar D14 disi) ──
    RowlEngineHandle live = RowlEngine_Create();
    red2Require(live != nullptr, "canli RowlEngine_Create null dondu");
    red2Require(RowlEngine_Init(live, 320, 180, 0) == 1,
                "canli RowlEngine_Init(320x180) basarili olmali");
    {
        int foreignRunning = -1;
        std::thread reader([&] {
            foreignRunning = RowlEngine_IsRunning(live);
        });
        reader.join();
        red2Require(foreignRunning == 0,
                    "yabanci-thread IsRunning sessiz-default 0 degil, got: " +
                        std::to_string(foreignRunning));
    }
    TEST_PASS("Yabanci-thread okuma sessiz-default");

    // ── Motor untouched: golge cagrilar sonrasi taze motor calisir ──
    {
        RowlEngineHandle fresh = RowlEngine_Create();
        red2Require(fresh != nullptr, "taze RowlEngine_Create null dondu");
        red2Require(RowlEngine_Init(fresh, 320, 180, 0) == 1,
                    "golge cagrilar sonrasi taze Init basarisiz (motor touched?)");
        red2Require(RowlEngine_IsRunning(fresh) == 1,
                    "taze motor IsRunning 1 degil");
        RowlEngine_Destroy(fresh);
    }
    RowlEngine_Destroy(live);
    TEST_PASS("Motor untouched (taze Create+Init yesil)");

    std::cout << "D12-RED2 GREEN: sessiz legacy yuzey fail-closed" << std::endl;
    TEST_PASS("D12 RED-2 yesil");
    return 0;
}
