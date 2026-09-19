// Save/load/rewind lock tests — Faz D, dilim D4 (Bulgular #43-#53, #70).
//
// Kural: kayıt/rewind kilitleri; her bölüm bulgu-kilitlidir. Bölümler:
//   D1 (#51): save stage-then-commit — başarısız save oyun-durumunu ilerletmez,
//             hayalet slot dosyası bırakmaz, hata tekrarlanabilir.
//   R  (#48): başarısız load/rewind oturumu kirletmez (rollback).
//   V  (#43/#44/#46/#50/#52): kayıt validasyonu şüpheliyse yüklemez.
//   C  (#45): koşul yan-etkisi saflığı (ConditionPurityGuard).
//   T  (#49): sahipsiz-pencerede Save/Load/Step/Rewind claim-or-reject
//             (WrongThread(14), tufan-testi).
//   D2 (#53): kayıt-dizini yazılabilirlik yoklaması (set-time refuse,
//             IoError(7); getter yine yol döner).
//   G  (#70): graf-kimliği kilidi (node 102 grafında node 101 kaydı reddedilir).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "rowl/c_api.h"

namespace {

int g_failures = 0;

void checkSave(bool cond, const char* what) {
    if (!cond) {
        std::cerr << "SAVE-LOCK FAIL: " << what << std::endl;
        ++g_failures;
    }
}

std::string uniqueSaveRoot(const char* tag) {
    static int counter = 0;
    std::ostringstream name;
    name << "rowl_save_lock_" << tag << "_" << ++counter;
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / name.str();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

std::string saveDirOf(RowlEngineHandle h) {
    char buf[1024];
    uint32_t required = 0;
    if (RowlEngine_GetSaveDirectoryUtf8(h, buf, sizeof(buf), &required) !=
        ROWL_RESULT_OK) {
        return std::string();
    }
    return std::string(buf);
}

void writeTwoNodeGraph(const std::string& path) {
    std::ofstream graph(path);
    graph << R"({"format_version":4,"start_node_id":101,"nodes":[
      {"id":101,"speaker":"Guide","dialogue":"First.","next_nodes":[{"id":102}]},
      {"id":102,"speaker":"Guide","dialogue":"Second."}]})";
}

void writeSingleNodeGraph(const std::string& path) {
    std::ofstream graph(path);
    graph << R"({"format_version":4,"start_node_id":101,"nodes":[
      {"id":101,"speaker":"Guide","dialogue":"Only."}]})";
}

// G (#70): aynı node-id'li ama farklı içerikli graf. V kilidi geçer
// (101/102 mevcut), graf-kimliği farklıdır.
void writeTwoNodeGraphVariant(const std::string& path, const char* first,
                              const char* second) {
    std::ofstream graph(path);
    graph << R"({"format_version":4,"start_node_id":101,"nodes":[
      {"id":101,"speaker":"Guide","dialogue":")" << first
              << R"(","next_nodes":[{"id":102}]},
      {"id":102,"speaker":"Guide","dialogue":")" << second << R"("}]})";
}

void checkSessionIdentity(RowlEngineHandle h, uint64_t step, uint64_t node,
                          const char* varKey, const char* varValue,
                          const char* tag) {
    char what[160];
    std::snprintf(what, sizeof(what), "%s: stepId changed", tag);
    checkSave(RowlEngine_GetCurrentStepId(h) == step, what);
    std::snprintf(what, sizeof(what), "%s: node changed", tag);
    checkSave(RowlEngine_GetCurrentNodeId(h) == node, what);
    std::snprintf(what, sizeof(what), "%s: lua var changed", tag);
    const char* got = RowlEngine_GetVariable(h, varKey);
    checkSave(got != nullptr && std::strcmp(got, varValue) == 0, what);
}

std::string readFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

}  // namespace

void test_save_lock() {
    // ---- D1 (#51): stage-then-commit -------------------------------------
    // Kurulum active-story overlay ile imleci state'ten ayırır (cursor 102,
    // state 101/adım-1): hizalanmamış checkpoint, eski koda göre başarısız
    // save'te BİLE stepId'yi ilerletiyordu. Kilit: IoError + stepId sabit +
    // hayalet-dosya yok + tekrarlanabilir; kaldırma-sonrası BAŞARILI save
    // stepId'yi 2'ye taşır (mekanizma-duyarlılık kanıtı).
    // Enjektör: saves/ salt-okunur (chmod 0555); yazma hâlâ geçiyorsa
    // (root'lu ortam) yedek: saves/ yerine düzenli-dosya.
    {
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "D1: Create failed");

        const std::string root = uniqueSaveRoot("d1");
        {
            // VFS proje-kökünün Assets/ altını mount'lar: VFS adayı
            // "json/active_story.json" → <root>/Assets/json/active_story.json.
            std::error_code ec;
            std::filesystem::create_directories(root + "/Assets/json", ec);
            std::ofstream overlay(root + "/Assets/json/active_story.json");
            overlay << R"({"node_id":102,"speaker":"Ghost","dialogue":"Overlay."})";
        }
        RowlEngine_SetProjectDirectory(h, root.c_str());
        // Init-sırası fiziksel-dosyayolu yedeği CWD-bağımlıdır (repo Assets/
        // test-CWD'sinde her çıplak Init'i 101'e hizalar). Overlay'e erişmek
        // için Init boyunca CWD geçici-olarak proje-köküdür: VFS mount'ları
        // mutlaktır, Init senkrondur, koşucu seridir — hemen geri alınır.
        std::error_code cwdEc;
        const std::filesystem::path savedCwd =
            std::filesystem::current_path(cwdEc);
        std::filesystem::current_path(root, cwdEc);
        const int initOk = RowlEngine_Init(h, 320, 180, 0);
        std::filesystem::current_path(savedCwd, cwdEc);
        checkSave(initOk == 1, "D1: Init failed");
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102,
                  "D1: overlay did not diverge cursor to 102");
        checkSave(RowlEngine_GetCurrentStepId(h) == 1,
                  "D1: expected root step 1 before failed save");
        const uint64_t stepBefore = RowlEngine_GetCurrentStepId(h);

        const std::string savesDir = saveDirOf(h);
        checkSave(!savesDir.empty(), "D1: empty save directory");
        std::error_code ec;
        std::filesystem::create_directories(savesDir, ec);
        std::filesystem::permissions(
            savesDir,
            std::filesystem::perms::owner_write |
                std::filesystem::perms::group_write |
                std::filesystem::perms::others_write,
            std::filesystem::perm_options::remove, ec);
        bool readOnlyHolds = false;
        {
            std::ofstream probe(savesDir + "/probe.tmp", std::ios::binary);
            readOnlyHolds = !probe.is_open();
        }
        const bool usedBlocker = !readOnlyHolds;
        if (usedBlocker) {
            std::filesystem::permissions(
                savesDir,
                std::filesystem::perms::owner_write |
                    std::filesystem::perms::group_write |
                    std::filesystem::perms::others_write,
                std::filesystem::perm_options::add, ec);
            std::filesystem::remove_all(savesDir, ec);
            std::ofstream blocker(savesDir, std::ios::binary);
            blocker << "blocked";
        }

        checkSave(RowlEngine_SaveGameSlotResult(h, 1) == ROWL_RESULT_IO_ERROR,
                  "D1: blocked save must report IoError");
        checkSave(RowlEngine_GetCurrentStepId(h) == stepBefore,
                  "D1: failed save advanced stepId (stage-then-commit)");
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102,
                  "D1: failed save moved cursor");
        checkSave(RowlEngine_HasSaveSlot(h, 1) == 0,
                  "D1: failed save left a phantom slot file");
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) == ROWL_RESULT_IO_ERROR,
                  "D1: blocked save must repeat IoError");

        // Kaldırma: BAŞARILI save hizalanmamış checkpoint'i commit'ler
        // (adım 2) — kilit hassasiyetinin kanıtı.
        if (!usedBlocker) {
            std::filesystem::permissions(
                savesDir,
                std::filesystem::perms::owner_write |
                    std::filesystem::perms::group_write |
                    std::filesystem::perms::others_write,
                std::filesystem::perm_options::add, ec);
        } else {
            std::filesystem::remove(savesDir, ec);
            std::filesystem::create_directories(savesDir, ec);
        }
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) == ROWL_RESULT_OK,
                  "D1: save after unblock must succeed");
        checkSave(RowlEngine_GetCurrentStepId(h) == stepBefore + 1,
                  "D1: successful save must commit the pending checkpoint");
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102,
                  "D1: session cursor changed across failed save");

        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        std::filesystem::remove_all(root, ec);
    }

    // ---- R (#48): failed load/rewind rollback --------------------------------
    // Başarısız load/rewind oturumu bit-bit aynı bırakır: stepId, cursor,
    // lua-değişkeni. Pozitif-kontrol: BAŞARILI load gerçekten eski duruma
    // döner (kilit duyarlılığının kanıtı).
    {
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "R: Create failed");
        const std::string root = uniqueSaveRoot("r");
        RowlEngine_SetProjectDirectory(h, root.c_str());
        checkSave(RowlEngine_Init(h, 320, 180, 0) == 1, "R: Init failed");

        const std::string graphPath = root + "/two_node.json";
        writeTwoNodeGraph(graphPath);
        RowlEngine_LoadStoryGraph(h, graphPath.c_str());
        RowlEngine_Step(h, 0.0f);
        RowlEngine_AdvanceNode(h, 0);
        RowlEngine_Step(h, 0.0f);
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102, "R: setup node != 102");
        RowlEngine_SetVariable(h, "r_key", "r_val");
        const uint64_t step = RowlEngine_GetCurrentStepId(h);

        // R1: kayıp slot.
        checkSave(RowlEngine_LoadGameSlotResult(h, 5) == ROWL_RESULT_FILE_NOT_FOUND,
                  "R1: missing slot must report FileNotFound");
        checkSessionIdentity(h, step, 102, "r_key", "r_val", "R1");

        // R2: bozuk slot dosyası.
        {
            std::error_code ec;
            std::filesystem::create_directories(saveDirOf(h), ec);
            std::ofstream corrupt(saveDirOf(h) + "/save_slot_6.json",
                                  std::ios::binary | std::ios::trunc);
            corrupt << "not-json{{{";
        }
        checkSave(RowlEngine_LoadGameSlotResult(h, 6) == ROWL_RESULT_PARSE_ERROR,
                  "R2: corrupt slot must report ParseError");
        checkSessionIdentity(h, step, 102, "r_key", "r_val", "R2");

        // R3: aralık-dışı slot.
        checkSave(RowlEngine_LoadGameSlotResult(h, 100) == ROWL_RESULT_INVALID_ARGUMENT,
                  "R3: out-of-range slot must report InvalidArgument");
        checkSessionIdentity(h, step, 102, "r_key", "r_val", "R3");

        // R4: sıfır-adım rewind reddedilir (aşırı-rewind ile aynı
        // refuse-yolu: commit-öncesi false + InvalidArgument). Önce nötr
        // save: hizalı checkpoint adımı kımıldatmaz ama context'i Ok yapar —
        // R4 assert'i bayat-kodda kırmızıyı görür (duyarlılık).
        checkSave(RowlEngine_SaveGameSlotResult(h, 2) == ROWL_RESULT_OK,
                  "R4: neutral save failed");
        checkSessionIdentity(h, step, 102, "r_key", "r_val", "R4-neutral");
        checkSave(RowlEngine_Rewind(h, 0) == 0,
                  "R4: zero-step rewind must fail");
        checkSave(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_INVALID_ARGUMENT,
                  "R4: zero-step rewind must report InvalidArgument");
        checkSessionIdentity(h, step, 102, "r_key", "r_val", "R4");

        // Pozitif-kontrol: kaydet → saptır → yükle → eski duruma dön.
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) == ROWL_RESULT_OK,
                  "R: control save failed");
        const uint64_t savedStep = RowlEngine_GetCurrentStepId(h);
        RowlEngine_SetVariable(h, "r_key", "changed");
        checkSave(RowlEngine_LoadGameSlotResult(h, 1) == ROWL_RESULT_OK,
                  "R: control load failed");
        checkSessionIdentity(h, savedStep, 102, "r_key", "r_val", "R-ctrl");

        // R48a (#48 gerçek-tetkik): load zinciri koparır → Rewind(1)
        // reddedilir; bayat Ok DEĞİL InvalidArgument raporlanır, oturum aynı.
        checkSave(RowlEngine_Rewind(h, 1) == 0,
                  "R48a: post-load rewind must fail");
        checkSave(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_INVALID_ARGUMENT,
                  "R48a: post-load rewind must report InvalidArgument (no stale Ok)");
        checkSessionIdentity(h, savedStep, 102, "r_key", "r_val", "R48a");

        // R48b: BAŞARILI rewind Ok yazar ve bir adım geri döner (duyarlılık).
        // Ayraç-başarısız-load context'i FileNotFound yapar — Ok assert'i
        // bayat-kodda kırmızıyı görür; ayraç oturumu kımıldatmaz (R1-kilidi).
        RowlEngine_SetVariable(h, "r_key", "forward");
        const uint64_t fwdStep = RowlEngine_GetCurrentStepId(h);
        checkSave(fwdStep == savedStep + 1, "R48b: setup step did not advance");
        checkSave(RowlEngine_LoadGameSlotResult(h, 7) == ROWL_RESULT_FILE_NOT_FOUND,
                  "R48b: separator load must fail FileNotFound");
        checkSessionIdentity(h, fwdStep, 102, "r_key", "forward", "R48b-sep");
        checkSave(RowlEngine_Rewind(h, 1) == 1, "R48b: rewind must succeed");
        checkSave(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_OK,
                  "R48b: successful rewind must report Ok");
        checkSessionIdentity(h, savedStep, 102, "r_key", "r_val", "R48b");

        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        std::error_code ecR;
        std::filesystem::remove_all(root, ecR);
    }

    // ---- V (#43/#44/#46/#50/#52): sarkan-cursor reddi -------------------------
    // Grafında olmayan node'a işaret eden kayıt commitlenmeden reddedilir
    // (ValidationError); oturum bit-bit aynı kalır. Pozitif-kontrol: aynı
    // graftan kayıt yüklenir (duyarlılık).
    {
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "V: Create failed");
        const std::string root = uniqueSaveRoot("v");
        RowlEngine_SetProjectDirectory(h, root.c_str());
        checkSave(RowlEngine_Init(h, 320, 180, 0) == 1, "V: Init failed");

        const std::string widePath = root + "/wide.json";
        writeTwoNodeGraph(widePath);
        RowlEngine_LoadStoryGraph(h, widePath.c_str());
        RowlEngine_Step(h, 0.0f);
        RowlEngine_AdvanceNode(h, 0);
        RowlEngine_Step(h, 0.0f);
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102, "V: setup node != 102");
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) == ROWL_RESULT_OK,
                  "V: node-102 save failed");

        // Graf daraltılır: 102 artık mevcut değil.
        const std::string narrowPath = root + "/narrow.json";
        writeSingleNodeGraph(narrowPath);
        RowlEngine_LoadStoryGraph(h, narrowPath.c_str());
        RowlEngine_Step(h, 0.0f);
        checkSave(RowlEngine_GetCurrentNodeId(h) == 101, "V: narrow node != 101");
        RowlEngine_SetVariable(h, "v_key", "v_val");
        const uint64_t step = RowlEngine_GetCurrentStepId(h);

        checkSave(RowlEngine_LoadGameSlotResult(h, 1) == ROWL_RESULT_VALIDATION_ERROR,
                  "V: dangling-node load must report ValidationError");
        checkSessionIdentity(h, step, 101, "v_key", "v_val", "V");

        // Pozitif-kontrol: dar grafın kendi kaydı yüklenir.
        checkSave(RowlEngine_SaveGameSlotResult(h, 2) == ROWL_RESULT_OK,
                  "V: control save failed");
        const uint64_t savedStep = RowlEngine_GetCurrentStepId(h);
        RowlEngine_SetVariable(h, "v_key", "changed");
        checkSave(RowlEngine_LoadGameSlotResult(h, 2) == ROWL_RESULT_OK,
                  "V: control load failed");
        checkSessionIdentity(h, savedStep, 101, "v_key", "v_val", "V-ctrl");

        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    // ---- C (#45): koşul yan-etkisi saflığı ----------------------------------
    // Yan-etkili koşul (rowl.var_set + true) her sunumda çalışır ama oturuma
    // ve kayıt-dosyasına hiçbir şey yazmamalıdır; true-sonucu ise kapıyı
    // açık tutmalıdır (tıklama ilerler). 1920x1080 Init: PointerDown
    // koordinatları birebir eşlenir (düğme [680,1240]x[520,584]).
    {
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "C: Create failed");
        const std::string root = uniqueSaveRoot("c");
        RowlEngine_SetProjectDirectory(h, root.c_str());
        checkSave(RowlEngine_Init(h, 1920, 1080, 0) == 1, "C: Init failed");

        const std::string graphPath = root + "/choice_pois.json";
        {
            std::ofstream graph(graphPath);
            graph << R"JSON({"format_version":4,"start_node_id":101,"nodes":[
          {"id":101,"speaker":"Guide","dialogue":"Choose.","objects":[{"id":"choices","name":"Choices","is_active":true,"components":[
            {"type":"choice","id":"choice_main","enabled":true,"data":{"options":[
              {"option_id":"go_ahead","text":"Ahead","condition":"return ((function() rowl.var_set('c_pois','1') return true end)())"}]}}]}],"next_nodes":[
            {"id":102,"label":"Ahead","option_id":"go_ahead"}]},
          {"id":102,"speaker":"Guide","dialogue":"After."}]})JSON";
        }
        RowlEngine_LoadStoryGraph(h, graphPath.c_str());
        RowlEngine_Step(h, 0.0f);
        checkSave(RowlEngine_GetChoiceCount(h) == 1, "C: choice not presented");
        // Saflık: koşul çalıştı (kapı açık — aşağıda) ama değişken boş.
        const char* pois = RowlEngine_GetVariable(h, "c_pois");
        checkSave(pois != nullptr && std::strcmp(pois, "") == 0,
                  "C: condition var_set leaked into session");
        // Birikimsizlik: yeniden-sunum koşulu tekrar çalıştırır, hâlâ boş.
        RowlEngine_Step(h, 0.0f);
        pois = RowlEngine_GetVariable(h, "c_pois");
        checkSave(pois != nullptr && std::strcmp(pois, "") == 0,
                  "C: condition side-effect accumulated across Steps");
        // Kapı-sonucu korunur: true dönen koşul düğmeyi etkin bırakır —
        // hit-test engelli düğmeyi atlar, tıklama ilerlemelidir.
        checkSave(RowlEngine_PointerDown(h, 700.0f, 540.0f) == 1,
                  "C: enabled choice click not consumed");
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102,
                  "C: gated choice did not advance (condition result lost)");
        // Kayıt-temizliği: koşul yazımı sandbox'la sınırlıdır (m_gameState'e
        // commitlenmez), ama sonraki bir script-çalışması kirli anlık-görüntüyü
        // state'e taşır — bulgunun gerçek sapma-vektörü budur. Zararsız script
        // sonrası save dosyasında zehir aranır (kırmızı-bozda burada da kırılır).
        checkSave(RowlEngine_ExecuteScript(h, "return true") == 1,
                  "C: benign script failed");
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) == ROWL_RESULT_OK,
                  "C: save failed");
        checkSave(readFileBytes(saveDirOf(h) + "/save_slot_1.json").find(
                      "c_pois") == std::string::npos,
                  "C: condition side-effect entered the save file");

        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        std::error_code ecC;
        std::filesystem::remove_all(root, ecC);
    }

    // ---- T (#49): sahipsiz-pencerede claim-or-reject -------------------------
    // Create-sonrası/Init-öncesi handle sahipsizdir: ilk Save/Load/Step/
    // Rewind çağıran claim'ler, yabancılar WRONG_THREAD(14) damgasıyla
    // reddedilir (sessiz INVALID_HANDLE gömülmesi yok, state'e dokunulmaz).
    {
        // T1: ilk çağıran (main) claim'ler — StateError ama sahiplik main'de;
        // yabancı Load/Rewind/Step 14 damgalar, oturum aynı kalır.
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "T1: Create failed");
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) ==
                      ROWL_RESULT_STATE_ERROR,
                  "T1: pre-init save must report StateError (and claim)");
        int32_t foreignLoad = -1, foreignRewindCode = -1, foreignStepCode = -1;
        int foreignRewindRet = -1;
        std::thread foreign([&] {
            foreignLoad = static_cast<int32_t>(
                RowlEngine_LoadGameSlotResult(h, 1));
            foreignRewindRet = RowlEngine_Rewind(h, 1);
            foreignRewindCode =
                RowlEngine_GetLastResultCode(h);
            RowlEngine_Step(h, 0.016f);
            foreignStepCode = RowlEngine_GetLastResultCode(h);
        });
        foreign.join();
        checkSave(foreignLoad == ROWL_RESULT_WRONG_THREAD,
                  "T1: foreign load must report WrongThread, not InvalidHandle");
        checkSave(foreignRewindRet == 0,
                  "T1: foreign rewind must fail closed");
        checkSave(foreignRewindCode == ROWL_RESULT_WRONG_THREAD,
                  "T1: foreign rewind must stamp WrongThread");
        checkSave(foreignStepCode == ROWL_RESULT_WRONG_THREAD,
                  "T1: foreign step must stamp WrongThread");
        // Sahiplik main'de kaldı: tekrar Save yine StateError (14 değil).
        checkSave(RowlEngine_SaveGameSlotResult(h, 1) ==
                      ROWL_RESULT_STATE_ERROR,
                  "T1: owner save after foreign storm must stay StateError");
        RowlEngine_Destroy(h);

        // T2: tufan — sahipsiz handle'da 4 thread Save/Load/Rewind yarıştırır.
        // Crash yok; kazanan StateError alır, kaybedenler 14; OK/ölü-kod yok.
        // (Boz-duyarlılığı: claim kalkarsa kaybedenler sessiz INVALID_HANDLE
        // gömülmesine döner — tOther>0 → kırmızı. Paylaşımlı context'te damga
        // yarışı olabilir, o yüzden sayaçlar toplamda (>0/==0) okunur.)
        RowlEngineHandle t = RowlEngine_Create();
        checkSave(t != nullptr, "T2: Create failed");
        std::atomic<int> tStarted{0};
        std::atomic<int> tStateError{0};
        std::atomic<int> tWrongThread{0};
        std::atomic<int> tOther{0};
        auto hammerSave = [&] {
            tStarted.fetch_add(1);
            while (tStarted.load() < 4) std::this_thread::yield();
            for (int i = 0; i < 200; ++i) {
                const int32_t c = static_cast<int32_t>(
                    (i & 1) ? RowlEngine_SaveGameSlotResult(t, 1)
                            : RowlEngine_LoadGameSlotResult(t, 1));
                if (c == ROWL_RESULT_STATE_ERROR) tStateError.fetch_add(1);
                else if (c == ROWL_RESULT_WRONG_THREAD) tWrongThread.fetch_add(1);
                else tOther.fetch_add(1);
                if ((i & 7) == 0) (void)RowlEngine_Rewind(t, 1);
            }
        };
        std::thread pounders[4] = {std::thread(hammerSave),
                                   std::thread(hammerSave),
                                   std::thread(hammerSave),
                                   std::thread(hammerSave)};
        for (auto& p : pounders) p.join();
        checkSave(tStateError.load() > 0,
                  "T2: winner thread must observe StateError (hammer ran)");
        checkSave(tWrongThread.load() > 0,
                  "T2: loser threads must observe WrongThread (claim-or-reject)");
        checkSave(tOther.load() == 0,
                  "T2: no OK/dead codes in the storm");
        RowlEngine_Destroy(t);
    }

    // ---- D2 (#53): set-time yazılabilirlik yoklaması ------------------------
    // Yazılamaz saves/ ile SetProjectDirectory projeyi hiç mount etmeden
    // reddedilir (IoError(7) + op); override kurulmaz, getter varsayılan
    // yolu döndürmeye devam eder. Kaldırma-sonrası mount başarılıdır
    // (mekanizma-duyarlılık). Enjektör D1 ile aynı (chmod/yedek-bloker).
    {
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "D2: Create failed");
        const std::string root = uniqueSaveRoot("d2");
        const std::string savesPath = root + "/saves";
        std::error_code ecD;
        std::filesystem::create_directories(savesPath, ecD);
        std::filesystem::permissions(
            savesPath,
            std::filesystem::perms::owner_write |
                std::filesystem::perms::group_write |
                std::filesystem::perms::others_write,
            std::filesystem::perm_options::remove, ecD);
        bool readOnlyHolds = false;
        {
            std::ofstream probe(savesPath + "/probe.tmp", std::ios::binary);
            readOnlyHolds = !probe.is_open();
        }
        if (!readOnlyHolds) {
            std::filesystem::permissions(
                savesPath,
                std::filesystem::perms::owner_write |
                    std::filesystem::perms::group_write |
                    std::filesystem::perms::others_write,
                std::filesystem::perm_options::add, ecD);
            std::filesystem::remove_all(savesPath, ecD);
            std::ofstream blocker(savesPath, std::ios::binary);
            blocker << "blocked";
        }

        RowlEngine_SetProjectDirectory(h, root.c_str());
        checkSave(RowlEngine_GetLastResultCode(h) == ROWL_RESULT_IO_ERROR,
                  "D2: unwritable saves must report IoError");
        checkSave(std::string(RowlEngine_GetLastResultOperation(h)) ==
                      "set_project_directory",
                  "D2: refuse must stamp op=set_project_directory");
        // Override kurulmadı: getter reddedilen yolu değil, sözleşmeyi döner.
        const std::string dirAfterRefuse = saveDirOf(h);
        checkSave(!dirAfterRefuse.empty(),
                  "D2: save-directory getter must still return a path");
        checkSave(std::filesystem::path(dirAfterRefuse) !=
                      std::filesystem::path(savesPath),
                  "D2: refused override must not be installed");

        // Kaldırma: mount başarılı olur, override kurulur (duyarlılık).
        if (readOnlyHolds) {
            std::filesystem::permissions(
                savesPath,
                std::filesystem::perms::owner_write |
                    std::filesystem::perms::group_write |
                    std::filesystem::perms::others_write,
                std::filesystem::perm_options::add, ecD);
        } else {
            std::filesystem::remove(savesPath, ecD);
        }
        RowlEngine_SetProjectDirectory(h, root.c_str());
        checkSave(RowlEngine_GetLastResultCode(h) != ROWL_RESULT_IO_ERROR,
                  "D2: writable saves must not report IoError");
        checkSave(std::filesystem::path(saveDirOf(h)) ==
                      std::filesystem::path(savesPath),
                  "D2: successful mount must install the override");

        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        std::filesystem::remove_all(root, ecD);
    }

    // ---- G (#70): graf-kimliği kilidi --------------------------------------
    // Aynı node-id'li (101/102) ama farklı içerikli graftan kayıt yüklenemez:
    // ValidationError + oturum bit-bit donmuş + tekrarlanabilir. Yabancı
    // kaydın değişkeni Lua'ya bulaşmaz. Kimliksiz legacy kayıt WARN ile
    // yüklenir; aynı grafın kaydı yüklenir (duyarlılık).
    {
        RowlEngineHandle h = RowlEngine_Create();
        checkSave(h != nullptr, "G: Create failed");
        const std::string root = uniqueSaveRoot("g");
        RowlEngine_SetProjectDirectory(h, root.c_str());
        checkSave(RowlEngine_Init(h, 320, 180, 0) == 1, "G: Init failed");

        const std::string graphAPath = root + "/graph_a.json";
        writeTwoNodeGraphVariant(graphAPath, "First-A.", "Second-A.");
        RowlEngine_LoadStoryGraph(h, graphAPath.c_str());
        RowlEngine_Step(h, 0.0f);
        RowlEngine_SetVariable(h, "g_key", "from_a");
        checkSave(RowlEngine_SaveGameSlotResult(h, 5) == ROWL_RESULT_OK,
                  "G: graph-A save failed");
        // Mekanizma kanıtı: kimlik dosyaya damgalanır.
        checkSave(readFileBytes(saveDirOf(h) + "/save_slot_5.json").find(
                      "graph_id") != std::string::npos,
                  "G: save file must carry graph_id");

        // Aynı id'li ama farklı içerikli graf: V kilidi geçer (101 mevcut),
        // G kilidi reddeder. Freeze'i anlamlı kılmak için B'de 102'ye
        // ilerlenir (yabancı kayıt 101'dedir).
        const std::string graphBPath = root + "/graph_b.json";
        writeTwoNodeGraphVariant(graphBPath, "First-B.", "Second-B.");
        RowlEngine_LoadStoryGraph(h, graphBPath.c_str());
        RowlEngine_Step(h, 0.0f);
        RowlEngine_AdvanceNode(h, 0);
        RowlEngine_Step(h, 0.0f);
        checkSave(RowlEngine_GetCurrentNodeId(h) == 102,
                  "G: graph-B node != 102");
        RowlEngine_SetVariable(h, "g_key", "from_b");
        const uint64_t step = RowlEngine_GetCurrentStepId(h);

        checkSave(RowlEngine_LoadGameSlotResult(h, 5) ==
                      ROWL_RESULT_VALIDATION_ERROR,
                  "G: foreign-graph load must report ValidationError");
        // Oturum donmuş: B'nin adımı/düğümü/değişkeni aynen durur.
        checkSessionIdentity(h, step, 102, "g_key", "from_b", "G");
        checkSave(RowlEngine_LoadGameSlotResult(h, 5) ==
                      ROWL_RESULT_VALIDATION_ERROR,
                  "G: foreign-graph load must be repeatable");

        // Kontrolü kızıl-kaskattan ayır: B konumu açıkça yeniden kurulur
        // (boz'lu dünyada yabancı-yükleme 101'e çekmişti; yeşilde no-op).
        RowlEngine_LoadStoryGraph(h, graphBPath.c_str());
        RowlEngine_Step(h, 0.0f);
        RowlEngine_AdvanceNode(h, 0);
        RowlEngine_Step(h, 0.0f);
        RowlEngine_SetVariable(h, "g_key", "from_b");

        // Pozitif-kontrol: B grafının kendi kaydı yüklenir.
        checkSave(RowlEngine_SaveGameSlotResult(h, 6) == ROWL_RESULT_OK,
                  "G: control save failed");
        const uint64_t savedStep = RowlEngine_GetCurrentStepId(h);
        RowlEngine_SetVariable(h, "g_key", "changed");
        checkSave(RowlEngine_LoadGameSlotResult(h, 6) == ROWL_RESULT_OK,
                  "G: control load failed");
        checkSessionIdentity(h, savedStep, 102, "g_key", "from_b", "G-ctrl");

        // Legacy-kontrol: graph_id anahtarı silinmiş kayıt WARN ile yüklenir.
        {
            const std::string dir = saveDirOf(h);
            std::string legacy = readFileBytes(dir + "/save_slot_6.json");
            checkSave(!legacy.empty(), "G: control save file unreadable");
            const std::string::size_type keyPos = legacy.find("graph_id");
            checkSave(keyPos != std::string::npos,
                      "G: control save must carry graph_id");
            if (!legacy.empty() && keyPos != std::string::npos) {
                // Satır-başından satır-sonuna kadar sil (sondaki virgül dahil).
                std::string::size_type lineStart = legacy.rfind('\n', keyPos);
                lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
                std::string::size_type lineEnd = legacy.find('\n', keyPos);
                lineEnd = (lineEnd == std::string::npos) ? legacy.size()
                                                         : lineEnd + 1;
                legacy.erase(lineStart, lineEnd - lineStart);
                // graph_id son anahtarsa komşu satırda virgül kalır — temizle.
                const std::string::size_type brace = legacy.find_first_not_of(
                    " \t\r\n", lineStart);
                if (brace != std::string::npos && legacy[brace] == '}') {
                    const std::string::size_type comma =
                        legacy.find_last_of(',', lineStart);
                    if (comma != std::string::npos) legacy.erase(comma, 1);
                }
                std::ofstream out(dir + "/save_slot_7.json", std::ios::binary);
                out << legacy;
            }
            RowlEngine_SetVariable(h, "g_key", "changed_again");
            checkSave(RowlEngine_LoadGameSlotResult(h, 7) == ROWL_RESULT_OK,
                      "G: legacy save without graph_id must still load");
            checkSave(RowlEngine_GetCurrentNodeId(h) == 102,
                      "G: legacy load must apply node 102");
        }

        RowlEngine_Shutdown(h);
        RowlEngine_Destroy(h);
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    if (g_failures != 0) {
        std::cerr << "SAVE-LOCK: " << g_failures << " failure(s)" << std::endl;
        std::exit(1);
    }
    std::cout << "SAVE-LOCK: all sections green" << std::endl;
}
