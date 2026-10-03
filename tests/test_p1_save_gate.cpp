/**
 * test_p1_save_gate.cpp — P1-1 ve P1-2 kayit kapi regresyon testleri.
 *
 * Iki AYRI, birbirini dogrulayan senaryo. Ikisi de ayni slot dosyasina
 * dokunur ama farkli katmana aittir:
 *
 *   SENARYO A (P1-1) — engine.cpp `accepted &&` guard'i.
 *     Sandbox 64 KiB ustu degeri reddeder. Oncesinde Engine::setScriptVariable
 *     yine de createNextState ile o degeri OYUN DURUMUNA yaziyordu; state
 *     katmaninda o deger icin 64 KiB siniri olmadigi icin state 10 MiB'a
 *     ballooned ve slot dosyasi okunamaz halde yaziliyordu. Beklenen: reddedilen
 *     buyuk degisken state'e hic girmez, slot dosyasi okunabilir ve BOZULMAMIS
 *     icerigi (onceki iyi kayit) tasir.
 *     YAMA ONCESI: save 10 MiB dosya yazar -> load basarisiz -> KIRMIZI.
 *
 *   SENARYO B (P1-2) — session_persistence.cpp 4 MiB yazma kapisi.
 *     4 MiB'i asan payload GERCEKTEN uretilebilir: serializeJson aktif state'in
 *     yaninda en fazla kMaxSerializedHistoryEntries (4) oncul halka yazar ve
 *     her halka degisken haritasinin TAM bir kopyasidir. Bu yuzden ~1 MiB
 *     ham degisken (16 x 64 KiB) ~3.7 MiB, ~1.2 MiB (20 x 64 KiB) 4 MiB'i
 *     asar. Sandbox'in 64 KiB deger / 4 MiB harita butcesi buna engel
 *     DEGILDIR: degerler tek tek 64 KiB'dir, toplam harita 4 MiB altindadir.
 *     Beklenen: kayit reddedilir, hata yuzeye cikar (IoError), eski iyi kayit
 *     diskte saglam kalir ve yuklenebilir.
 *     YAMA ONCESI: 4 MiB'i asan dosya yazilir, load FileTooLarge -> KIRMIZI.
 *
 * Hicbir senaryo kalici HOME dizinini kirletmez: her test sonunda
 * DeleteSaveSlot ile yazdigimiz slotu siler (P1-7 regresyonunun kaynagi:
 * slot birakmak ctest'i ikinci kosuda kirletiyor).
 */
#include "rowl_test_harness.hpp"
#include "rowl/c_api.h"

#include <iostream>
#include <string>

namespace {

constexpr int kSlot = 42;

// Sandbox'in deger basina kabul ettigi azami boyut (kMaxVariableValueBytes).
constexpr std::size_t kSandboxValueBytes = 64u * 1024u;

int g_failures = 0;

void fail(const std::string& label, const std::string& detail) {
    std::cerr << "RED [" << label << "] " << detail << std::endl;
    ++g_failures;
}

// Testin dokundugu slotu geride birakmaz. ctest ayni HOME'u paylasan ikinci
// bir kosu daha yapabilir; birakilan slot MS-6 "slot occupancy" kontrolunu
// kirletir (P1-7 CI kirilmasinin kok nedeni).
void cleanupSlot(RowlEngineHandle handle) {
    RowlEngine_DeleteSaveSlot(handle, kSlot);
}

// --- SENARYO A -----------------------------------------------------------
// Sandbox reddetti, state kirletilmemis olmali; slot onceki iyi kaydi tasimali.
void scenarioA_engineGuardKeepsSlotIntact() {
    const std::string label = "A/P1-1 engine.cpp accepted-guard";

    RowlEngineHandle handle = RowlEngine_Create();
    RowlEngine_Init(handle, 320, 240, 1);

    // 1) Bilinen iyi kayit yaz.
    RowlEngine_SetVariable(handle, "a_good", "a_good_value");
    if (RowlEngine_SaveGameSlot(handle, kSlot) != 1) {
        fail(label, "baseline save failed; test cannot proceed");
        RowlEngine_Destroy(handle);
        return;
    }

    // 2) Sandbox'in reddedecegi buyuk degisken + rezerve ad.
    //    5 MiB: 64 KiB deger sinirini ve 4 MiB harita butcesini de asar.
    const std::string oversized(5u * 1024u * 1024u, 'A');
    RowlEngine_SetVariable(handle, "a_huge", oversized.c_str());
    RowlEngine_SetVariable(handle, "print", "hacked_print");

    // 3) Buyuk degisken oyun durumuna GIRMEMIS olmali.
    //    createNextState her degeri kabul ederse state 5 MiB buyur.
    const char* readBack = RowlEngine_GetVariable(handle, "a_huge");
    if (readBack && std::string(readBack).size() > 1024u) {
        fail(label, "rejected 5 MiB variable reached game state (" +
                        std::to_string(std::string(readBack).size()) + " bytes)");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    // 4) Kayit hala calismali ve onceki iyi icerigi tasimali.
    //    Yama oncesi: state kirletilmis oldugu icin ya save 4 MiB kapisina
    //    takilir ya da okunamaz dosya yazar; ikisinde de asagidaki load basarisiz.
    const int saveRc = RowlEngine_SaveGameSlot(handle, kSlot);
    if (saveRc != 1) {
        fail(label, "save after rejected write failed (rc=" +
                        std::to_string(saveRc) + "); slot would be damaged");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    if (RowlEngine_LoadGameSlot(handle, kSlot) != 1) {
        fail(label, "slot file unreadable after rejected write; good backup lost");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    const char* good = RowlEngine_GetVariable(handle, "a_good");
    if (!good || std::string(good) != "a_good_value") {
        fail(label, "slot content corrupted: a_good did not survive");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    // Rezerve ad state'e de girmemeli (bu zaten game_state.cpp'te eleniyordu).
    const char* reserved = RowlEngine_GetVariable(handle, "print");
    if (reserved && std::string(reserved) == "hacked_print") {
        fail(label, "reserved variable name 'print' was written to game state");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    cleanupSlot(handle);
    RowlEngine_Destroy(handle);
    std::cout << "YESIL [A/P1-1] sandbox reddi -> state temiz kaldi, slot saglam" << std::endl;
}

// --- SENARYO B -----------------------------------------------------------
// Gercekten 4 MiB'i asan payload uret: 20 x 64 KiB degisken.
// Aktif state + 4 oncul halka => 4 MiB'i asan JSON.
void scenarioB_writeGateRejectsOversizedPayload() {
    const std::string label = "B/P1-2 4 MiB write gate";

    RowlEngineHandle handle = RowlEngine_Create();
    RowlEngine_Init(handle, 320, 240, 1);

    // 1) Bilinen iyi kayit yaz — bu, reddedilmemeli.
    RowlEngine_SetVariable(handle, "b_good", "b_good_value");
    if (RowlEngine_SaveGameSlot(handle, kSlot) != 1) {
        fail(label, "baseline save failed; test cannot proceed");
        RowlEngine_Destroy(handle);
        return;
    }

    // 2) Gercekten 4 MiB'i asan state uret.
    //    Her deger 64 KiB (sandbox kabul eder), toplam harita 1.25 MiB
    //    (4 MiB butcesinin altinda) — yani 4 MiB yazma kapisina
    //    deger butcesi degil, history halkasi carpandirligiyla ulasilir.
    const std::string chunk(kSandboxValueBytes, 'B');
    for (int i = 0; i < 20; ++i) {
        RowlEngine_SetVariable(handle, ("b_v" + std::to_string(i)).c_str(), chunk.c_str());
    }

    // 3) Kayit reddedilmeli ve hata yuzeye cikmali.
    const int saveRc = RowlEngine_SaveGameSlot(handle, kSlot);
    if (saveRc == 1) {
        fail(label, "oversized (>4 MiB) payload was accepted and written to disk");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    const int32_t code = RowlEngine_GetLastResultCode(handle);
    if (code != ROWL_RESULT_IO_ERROR) {
        fail(label, "expected IoError(" + std::to_string(ROWL_RESULT_IO_ERROR) +
                        ") on the surface, got " + std::to_string(code));
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    // 4) Onceki iyi kayit diskte SAGLAM kalmali ve yuklenebilmeli.
    if (RowlEngine_LoadGameSlot(handle, kSlot) != 1) {
        fail(label, "rejected save destroyed the previous good slot file");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    const char* good = RowlEngine_GetVariable(handle, "b_good");
    if (!good || std::string(good) != "b_good_value") {
        fail(label, "previous good slot content corrupted after rejected save");
        cleanupSlot(handle);
        RowlEngine_Destroy(handle);
        return;
    }

    cleanupSlot(handle);
    RowlEngine_Destroy(handle);
    std::cout << "YESIL [B/P1-2] 4 MiB ustu payload reddedildi, eski kayit saglam" << std::endl;
}

} // namespace

int main() {
    TEST_SECTION("P1-1 / P1-2 Save Gate");

    scenarioA_engineGuardKeepsSlotIntact();
    scenarioB_writeGateRejectsOversizedPayload();

    if (g_failures != 0) {
        std::cerr << "P1 save gate: " << g_failures << " senaryo basarisiz" << std::endl;
        return 1;
    }
    std::cout << "P1 save gate GREEN" << std::endl;
    return 0;
}
