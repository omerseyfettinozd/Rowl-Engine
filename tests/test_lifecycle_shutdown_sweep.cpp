/**
 * test_lifecycle_shutdown_sweep.cpp — D2: shutdown-süpürme + re-init kilitleri.
 *
 * Bulgular #107 #112 #113 #124 #126-rest #127 #136 #137 #140:
 * shutdown sahne/graph/profil/mount/handle süpürmüyordu; aynı handle'da
 * Shutdown->Init önceki oturumu yeni oturuma sızdırıyordu. Bu testler
 * süpürmeyi kilitler:
 *  - re-init sonrası eski sahne (INJECTED) servis edilemez (#112/#127).
 *  - re-init sonrası pause kapalıdır (#136).
 *  - shutdown-sonrası sorgu boş + STATE_ERROR (savunma-derinliği, #127).
 *  - story-yüklüyken SetProjectDirectory reddedilir + StateError (#113);
 *    story'süz motorda mount akışı çalışır.
 *  - Destroy aux-map'leri temizler: Destroy->Create sonrası prefetch
 *    kirli-state'siz çalışır (#140).
 * Bilerek-boz: resetSessionProfile() çağrısı #if 0'lanırsa re-init
 * speaker/paused testleri kırmızıya döner; SetProjectDirectory guard'ı
 * kaldırılırsa story-yüklü ret testi kırmızıya döner.
 */
#include "rowl_test_harness.hpp"

#include <filesystem>
#include <fstream>

namespace {

void checkSweepCode(const char* name, RowlEngineHandle h, int32_t want) {
    const int32_t got = RowlEngine_GetLastResultCode(h);
    if (got != want) {
        rowlLockFail("lifecycle-shutdown-sweep",
                     std::string(name) + ": expected last-result " +
                         std::to_string(want) + ", got " + std::to_string(got));
    }
}

std::string sweepSpeaker(RowlEngineHandle h) {
    char buf[256];
    uint32_t required = 0;
    RowlEngine_GetSpeakerUtf8(h, buf, sizeof(buf), &required);
    return std::string(buf);
}

}  // namespace

void test_lifecycle_shutdown_sweep() {
    TEST_SECTION("Lifecycle Shutdown Sweep (D2: re-init isolation)");

    // #112/#127: Shutdown->Init eski sahneyi yeni oturuma sızdıramaz.
    // Süpürme yoksa INJECTED ikinci oturumun açılış sahnesi olur.
    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "first Init must succeed");
    RowlEngine_SetPlayState(h, 1);
    RowlEngine_SetPaused(h, 1);
    RowlEngine_SetTextSpeedMultiplier(h, 4.0f);
    RowlEngine_SetAutoAdvanceDelayOffset(h, 30.0f);
    RowlEngine_UpdateScene(h, "INJECTED_SPEAKER", "INJECTED_DIALOGUE", "bg",
                           0, 0, 100, 100, "ch", 0, 0, 50, 50, 0, 0, 200, 40);
    if (sweepSpeaker(h).find("INJECTED") == std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep", "session setup must show INJECTED scene");
    RowlEngine_Shutdown(h);

    // Shutdown-sonrası canlı-handle sorgusu: boş + STATE_ERROR (#127).
    {
        char buf[256];
        uint32_t required = 0;
        if (RowlEngine_GetSpeakerUtf8(h, buf, sizeof(buf), &required) != 11)
            rowlLockFail("lifecycle-shutdown-sweep",
                         "post-shutdown GetSpeakerUtf8 must be STATE_ERROR");
        if (buf[0] != '\0')
            rowlLockFail("lifecycle-shutdown-sweep",
                         "post-shutdown GetSpeakerUtf8 must be empty (stale scene)");
    }
    // #126-rest: shutdown idempotent — ikinci çağrı da sessizce geçmeli.
    RowlEngine_Shutdown(h);

    // Re-init: taze-handle ile özdeş başlangıç.
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "shutdown->re-init must succeed");
    if (sweepSpeaker(h).find("INJECTED") != std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "re-init served the previous session's scene (sweep missing)");
    // #136: pause profili sıfırlanır (oynatılmamış oturumda overlay yok).
    if (RowlEngine_IsPaused(h) != 0)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "re-init must not inherit paused state");
    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    // #113: story-yüklüyken proje-değişimi reddedilir; story'süzde serbest.
    RowlEngineHandle p = RowlEngine_Create();
    if (p == nullptr) rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
    if (RowlEngine_Init(p, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "Init must succeed");
    const auto projDir =
        std::filesystem::temp_directory_path() / "rowl_d2_sweep_proj";
    {
        std::error_code dirError;
        std::filesystem::create_directories(projDir, dirError);
        if (dirError) rowlLockFail("lifecycle-shutdown-sweep", "fixture dir failed");
        std::ofstream graphFile(projDir / "story.json");
        graphFile << R"json({"format_version": 5, "start_node_id": 101, "nodes": [
                     {"id": 101, "next_nodes": [{"id": 102}]}, {"id": 102}]})json";
    }
    // Story'süz motorda SetProjectDirectory kabul edilir (mount akışı).
    RowlEngine_SetProjectDirectory(p, projDir.string().c_str());
    RowlEngine_LoadStoryGraph(p, (projDir / "story.json").string().c_str());
    if (RowlEngine_GetCurrentNodeId(p) == 0)
        rowlLockFail("lifecycle-shutdown-sweep", "fixture story must load");
    // Oynanmamış oturumda proje-değişimi serbesttir (yapılandırma aşaması:
    // silinecek ilerleme yoktur; ses-mount akışı bu serbestliğe dayanır).
    RowlEngine_SetProjectDirectory(p, projDir.string().c_str());
    if (RowlEngine_GetLastResultCode(p) == 11)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "unplayed session must accept SetProjectDirectory");
    // Oturumu oynat: advance cursor'u start'tan ayırır. İlk çağrı
    // MS-6 typewriter'ı tamamlar, ikincisi ilerler.
    RowlEngine_AdvanceNode(p, 0);
    RowlEngine_AdvanceNode(p, 0);
    if (RowlEngine_GetCurrentNodeId(p) != 102)
        rowlLockFail("lifecycle-shutdown-sweep", "AdvanceNode must move to 102");
    // Oynanmış oturumda ret: no-op + StateError (ilerleme korunur).
    RowlEngine_SetProjectDirectory(p, projDir.string().c_str());
    checkSweepCode("mid-session set-project-dir", p, 11 /* StateError */);
    if (RowlEngine_GetCurrentNodeId(p) != 102)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "rejected SetProjectDirectory must not wipe the session");
    // EndSession oynanmış oturumu mountsuz sonlandırır: cursor başa, step/
    // history sıfırlanır (replay'siz — reset'in aksine step geri zıplamaz),
    // sonraki mount kabul edilir.
    RowlEngine_EndSession(p);
    if (RowlEngine_GetCurrentNodeId(p) != 101)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "EndSession must return the cursor to the start node");
    RowlEngine_ClearLastResult(p);
    RowlEngine_SetProjectDirectory(p, projDir.string().c_str());
    if (RowlEngine_GetLastResultCode(p) == 11)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "ended session must accept SetProjectDirectory");
    RowlEngine_Shutdown(p);
    // Shutdown-sonrası story'süz motorda yine kabul (re-init öncesi mount).
    RowlEngine_SetProjectDirectory(p, projDir.string().c_str());
    {
        std::error_code rmError;
        std::filesystem::remove_all(projDir, rmError);
    }
    RowlEngine_Destroy(p);

    // #140: Destroy aux-map'leri temizler — Destroy->Create sonrası
    // prefetch kirli-state'siz, crash-free çalışır.
    RowlEngineHandle d1 = RowlEngine_Create();
    if (d1 == nullptr) rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
    if (RowlEngine_Init(d1, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "Init must succeed");
    // Geçersiz girdi aux-map'e dokunmadan INVALID_HANDLE yolunu işletir;
    // Destroy ardından aynı adres yeniden kullanılırsa bile temizdir.
    RowlEngine_Destroy(d1);
    RowlEngineHandle d2 = RowlEngine_Create();
    if (d2 == nullptr) rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
    if (RowlEngine_Init(d2, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "Init must succeed");
    if (RowlEngine_PumpPrefetch(d2, 4.0f) != 0)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "fresh handle must pump 0 (stale aux state?)");
    RowlEngine_Shutdown(d2);
    RowlEngine_Destroy(d2);

    TEST_PASS("Lifecycle shutdown sweep isolates re-init sessions (D2)");
}
