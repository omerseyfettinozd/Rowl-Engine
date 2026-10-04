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
 *  - Shutdown da aux-map'leri temizler: aux dolu handle'da Shutdown sonrası
 *    harita boş, Shutdown->Init taze-handle ile özdeş, çapraz-handle
 *    temizliği korunur, N handle döngüsü girdi biriktirmez (P2-5).
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

// P2-5 yardımcıları: JSON tasiyıcısı (NULL/0 boyut-sorgu sonra dar tampon).
using JsonCarrier = RowlEngine_ResultCode (*)(RowlEngineHandle, char*, uint32_t, uint32_t*);

std::string auxJson(RowlEngineHandle h, JsonCarrier fn) {
    uint32_t required = 0;
    if (fn(h, nullptr, 0, &required) != ROWL_RESULT_OK) return {};
    std::vector<char> buffer(required + 1u, '\0');
    if (fn(h, buffer.data(), static_cast<uint32_t>(buffer.size()), &required) !=
        ROWL_RESULT_OK) {
        return {};
    }
    return std::string(buffer.data());
}

std::string auxSlotAsset(RowlEngineHandle h) {
    uint32_t required = 0;
    if (RowlEngine_GetCharacterSlotAssetUtf8(h, "body", nullptr, 0, &required) !=
        ROWL_RESULT_OK) {
        return {};
    }
    std::vector<char> buffer(required + 1u, '\0');
    if (RowlEngine_GetCharacterSlotAssetUtf8(h, "body", buffer.data(),
                                             static_cast<uint32_t>(buffer.size()),
                                             &required) != ROWL_RESULT_OK) {
        return {};
    }
    return std::string(buffer.data());
}

float auxSlotOpacity(RowlEngineHandle h) {
    float value = -1.0f;
    RowlEngine_GetCharacterSlotOpacity(h, "body", &value);
    return value;
}

// P2-5 fixture'ı: aux-map'leri (prefetch ChapterLoader + kuyruk, character
// slot varlığı) gerçek public C API yollarından DOLDURUR.
const char* const kP25IndexJson =
    R"json({"format_version":5,"start_node_id":1,
            "chapters":[{"id":"a","order":0,"start_node_id":1},
                        {"id":"b","order":1,"start_node_id":3}]})json";

const char* const kP25FileA =
    R"json({"chapter_id":"a","nodes":[
             {"id":1,"chapter_id":"a","speaker":"S","dialogue":"hi",
              "background":"PROJE_A/bg_1.png","character":"PROJE_A/hero.png",
              "next_nodes":[{"id":2,"label":"n"}]},
             {"id":2,"chapter_id":"a","speaker":"S","dialogue":"mid",
              "background":"PROJE_A/bg_2.png"}]})json";

const char* const kP25FileB =
    R"json({"chapter_id":"b","nodes":[
             {"id":3,"chapter_id":"b","speaker":"S","dialogue":"yo",
              "background":"PROJE_A/bg_3.png",
              "next_nodes":[{"id":4,"label":"n"}]},
             {"id":4,"chapter_id":"b","speaker":"S","dialogue":"end",
              "background":"PROJE_A/bg_4.png"}]})json";

bool p25FillAux(RowlEngineHandle h) {
    if (RowlEngine_LoadChapterIndexJson(h, kP25IndexJson) != ROWL_RESULT_OK) return false;
    if (RowlEngine_AppendChapterFileJson(h, kP25FileA) != ROWL_RESULT_OK) return false;
    if (RowlEngine_AppendChapterFileJson(h, kP25FileB) != ROWL_RESULT_OK) return false;
    RowlEngine_LoadChapter(h, "b");
    RowlEngine_PrefetchChapterAssets(h, "b", 33554432u);
    if (RowlEngine_SetCharacterSlotAsset(h, "body", "PROJE_A/face.png") != ROWL_RESULT_OK)
        return false;
    if (RowlEngine_SetCharacterSlotOpacity(h, "body", 0.25f) != ROWL_RESULT_OK) return false;
    return true;
}

uint64_t auxPrefetchCount() { return Rowl::Core::RowlTest_PrefetchAuxEntryCount(); }
uint64_t auxCharacterCount() { return Rowl::Core::RowlTest_CharacterAuxEntryCount(); }

// P2-5 kapısı: Shutdown per-handle aux-map'leri (prefetch + character)
// boşaltır. Sözleşme c_api.h "aynı handle'da Shutdown→Init taze-handle ile
// özdeş başlar" diyor; #140 bu temizliği yalnız Destroy'a eklemişti.
// Kirpma (Shutdown'daki clear çağrıları silinirse) KIRMIZI döner:
//  - aux girdi sayacı Shutdown'dan sonra 1 kalır,
//  - re-Init + PumpPrefetch ESKI projenin yollarını "missing" sayar,
//  - N handle döngüsünde girdi birikir.
void checkShutdownAuxSweep() {
    // 1) Tek handle: aux dolu -> Shutdown -> harita BOŞ.
    RowlEngineHandle h = RowlEngine_Create();
    if (h == nullptr) rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "Init must succeed");
    if (!p25FillAux(h))
        rowlLockFail("lifecycle-shutdown-sweep", "P2-5 aux fixture must fill both maps");
    // Referans: girdiler gerçekten yazıldı (sayaç 0'da değil) — aksi hâlde
    // aşağıdaki "0" kontrolleri boşuna yeşil olurdu.
    if (auxPrefetchCount() < 1 || auxCharacterCount() < 1)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "P2-5 aux fixture must create map entries before Shutdown");
    if (auxJson(h, RowlEngine_GetLoadedChaptersJson).find("\"active\":\"a\"") ==
        std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep", "P2-5 fixture must load chapter 'a'");

    RowlEngine_Shutdown(h);
    if (auxPrefetchCount() != 0)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "Shutdown must clear this handle's prefetch aux entry");
    if (auxCharacterCount() != 0)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "Shutdown must clear this handle's character aux entry");

    // 2) Shutdown->Init izolasyonu: taze handle ile özdeş.
    if (RowlEngine_Init(h, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "shutdown->re-init must succeed");
    const std::string chapters = auxJson(h, RowlEngine_GetLoadedChaptersJson);
    if (chapters.find("\"active\":\"a\"") != std::string::npos ||
        chapters.find("PROJE_A") != std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "re-init served the previous session's chapters (aux not swept)");
    if (!auxSlotAsset(h).empty())
        rowlLockFail("lifecycle-shutdown-sweep",
                     "re-init inherited the previous session's slot asset");
    if (auxSlotOpacity(h) < 0.99f)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "re-init inherited the previous session's slot opacity");

    // 3) Yeni oturumun I/O sonucu DOĞRU: hicbir sey yuklenmeden PumpPrefetch
    //    ESKI projenin yollarini "missing" saymamali (#140'un kotu yolu).
    RowlEngine_PumpPrefetch(h, 4.0f);
    const std::string progress = auxJson(h, RowlEngine_GetPrefetchProgressJson);
    if (progress.find("PROJE_A") != std::string::npos ||
        progress.find("\"missing_assets\":0") == std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "a fresh session's PumpPrefetch reported the previous project's assets");

    // 4) Handle yeniden kullanilabilir (Shutdown kalici degildir).
    if (RowlEngine_SetCharacterSlotAsset(h, "body", "yeni.png") != ROWL_RESULT_OK ||
        RowlEngine_LoadChapterIndexJson(h, kP25IndexJson) != ROWL_RESULT_OK)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "handle must stay reusable after Shutdown");
    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    // 5) Capraz-handle temizligi: bir handle'in Shutdown'u BASKASININ aux
    //    girdisini silmemeli (haritalar handle-anahtarli; son-handle
    //    ozel durumu YOK).
    RowlEngineHandle a = RowlEngine_Create();
    RowlEngineHandle b = RowlEngine_Create();
    if (a == nullptr || b == nullptr)
        rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
    if (RowlEngine_Init(a, 320, 180, 0) != 1 || RowlEngine_Init(b, 320, 180, 0) != 1)
        rowlLockFail("lifecycle-shutdown-sweep", "Init must succeed");
    if (!p25FillAux(a) || !p25FillAux(b))
        rowlLockFail("lifecycle-shutdown-sweep", "P2-5 aux fixture must fill both maps");
    RowlEngine_Shutdown(b);
    if (auxJson(b, RowlEngine_GetLoadedChaptersJson).find("\"active\":\"a\"") !=
        std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep", "Shutdown must sweep the handle's own aux");
    if (auxJson(a, RowlEngine_GetLoadedChaptersJson).find("\"active\":\"a\"") ==
        std::string::npos)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "one handle's Shutdown erased another handle's aux state");
    RowlEngine_Shutdown(a);
    RowlEngine_Shutdown(b);
    RowlEngine_Destroy(a);
    RowlEngine_Destroy(b);

    // 6) N handle ac/kapat dongusu: girdi BIRIKMEZ (asil olcum).
    constexpr int kCycles = 64;
    for (int i = 0; i < kCycles; ++i) {
        RowlEngineHandle k = RowlEngine_Create();
        if (k == nullptr) rowlLockFail("lifecycle-shutdown-sweep", "Create returned null");
        if (RowlEngine_Init(k, 320, 180, 0) != 1)
            rowlLockFail("lifecycle-shutdown-sweep", "Init must succeed");
        if (!p25FillAux(k))
            rowlLockFail("lifecycle-shutdown-sweep", "P2-5 aux fixture must fill both maps");
        RowlEngine_Shutdown(k);
        const uint64_t prefetch = auxPrefetchCount();
        const uint64_t character = auxCharacterCount();
        RowlEngine_Destroy(k);
        if (prefetch != 0 || character != 0) {
            rowlLockFail("lifecycle-shutdown-sweep",
                         "cycle " + std::to_string(i) +
                             ": Shutdown left aux entries behind (prefetch=" +
                             std::to_string(prefetch) + ", character=" +
                             std::to_string(character) + ")");
        }
    }
    if (auxPrefetchCount() != 0 || auxCharacterCount() != 0)
        rowlLockFail("lifecycle-shutdown-sweep",
                     "aux maps must be empty after the Create/Shutdown/Destroy cycle");
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

    // P2-5: Shutdown da per-handle aux-map'leri (prefetch + character)
    // temizler — #140 bunu yalnız Destroy'a eklemişti.
    checkShutdownAuxSweep();

    TEST_PASS("Lifecycle shutdown sweep isolates re-init sessions (D2)");
}
