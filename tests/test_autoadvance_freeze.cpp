/**
 * test_autoadvance_freeze.cpp — Bulgu #61 [HIGH]: tekil uzun geçiş
 * auto-advance sayacını spam olmadan açlıktan öldürüyordu.
 *
 * Zemin: auto-advance kapısı kapalıyken (geçiş aktifken dahil) else-dalı
 * sayacı her karede SIFIRLARDI. 5 sn'lik tekil geçiş + 2 sn delay =
 * duvarda 7 sn; playtime aynı süre aktığı için saat/takvim ıraksardı.
 * Fix: geçiş TEK engelleyiciyken sayaç dondurulur (birikim korunur).
 *
 * RED: "Hi" (0.06 sn'de tamam), delay 2. 4 x 0.25 sn = 1.0 kredi biriktir,
 * 5 sn crossfade başlat, 20 x 0.25 sn bekle (geçiş biter), 4 x 0.25 sn.
 *   pre-fix: geçiş krediyi siler → final 1.0 < 2 → node 1 (kırmızı).
 *   post-fix: kredi 1.0 donar → final 2.0 >= 2 → node 2 (yeşil).
 * Bilerek-boz: dondurma dalı else-sıfırlamaya geri alınırsa bu test
 * "node 1"de kırmızılar. Geçişin headless açılmadığı sürücüde S6 emsali
 * SKIP (kurulum iddiası testin kendisini geçersiz kılar, ürünü değil).
 */
#include "rowl_test_harness.hpp"

#include <filesystem>
#include <fstream>

namespace {

struct FreezeCwd {
    std::filesystem::path saved;
    std::filesystem::path dir;
    bool ok = false;
    explicit FreezeCwd(const std::string& tag) {
        std::error_code ec;
        saved = std::filesystem::current_path(ec);
        if (ec) return;
        dir = std::filesystem::temp_directory_path() / tag;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        if (ec) return;
        std::filesystem::current_path(dir, ec);
        ok = !ec;
    }
    ~FreezeCwd() {
        std::error_code ec;
        if (ok) std::filesystem::current_path(saved, ec);
        std::filesystem::remove_all(dir, ec);
    }
};

void checkFreeze(bool condition, const char* what) {
    if (!condition) {
        rowlLockFail("autoadvance-freeze", what);
    }
}

}  // namespace

void test_autoadvance_freeze() {
    TEST_SECTION("Auto-advance Freeze (#61: transition pauses, not wipes)");

    FreezeCwd cwd("rowl_freeze_cwd_61");
    if (!cwd.ok) {
        std::cerr << "#61 setup: CWD pin failed" << std::endl;
        exit(1);
    }
    const auto project = cwd.dir / "proj";
    std::error_code ec;
    std::filesystem::create_directories(project / "Assets" / "json", ec);
    {
        std::ofstream graph(project / "Assets" / "json" / "full_story_graph.json");
        graph << R"({"format_version":4,"start_node_id":1,"nodes":[
            {"id":1,"title":"A","objects":[{"id":"d1","name":"D","is_active":true,
              "components":[{"type":"dialogue","id":"d1c","enabled":true,
                "data":{"speaker":"S","dialogue":"Hi","typewriter_enabled":true,
                  "text_speed":30,"auto_advance":true,"auto_advance_delay":2}}]}],
             "next_nodes":[{"id":2,"label":"go"}]},
            {"id":2,"title":"B","objects":[],"next_nodes":[]}]})";
    }
    RowlEngineHandle handle = RowlEngine_Create();
    checkFreeze(handle != nullptr, "setup: Create returned null");
    if (!RowlEngine_Init(handle, 320, 180, 0)) {
        std::cerr << "#61 setup: bare init failed" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_SetProjectDirectory(handle, project.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 1) {
        std::cerr << "#61 setup: freeze graph did not load" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_SetPlayState(handle, 1);
    // 4 x 0.25 s = 1.0 s kredi (gate açık: satır 0.06 sn'de tamam).
    for (int i = 0; i < 4; ++i) RowlEngine_Step(handle, 0.25f);
    checkFreeze(RowlEngine_GetCurrentNodeId(handle) == 1,
                "setup: must not advance before the transition (credit 1.0 < 2)");

    RowlEngine_StartTransition(handle, "crossfade", 5.0f, nullptr);
    if (!RowlEngine_IsTransitionActive(handle)) {
        std::cout << "  (#61 SKIP: transition does not engage on this driver; "
                     "freeze covered by review + probe mechanics)"
                  << std::endl;
        RowlEngine_Destroy(handle);
        TEST_PASS("#61 freeze skipped (headless, documented)");
        return;
    }
    // 10 x 0.25 s = 2.5 sn: geçiş sürüyor, advance yok.
    for (int i = 0; i < 10; ++i) RowlEngine_Step(handle, 0.25f);
    checkFreeze(RowlEngine_IsTransitionActive(handle) == 1,
                "#61: transition must still run mid-window");
    checkFreeze(RowlEngine_GetCurrentNodeId(handle) == 1,
                "#61: must not advance mid-transition");
    // 10 x 0.25 sn daha = 5.0 sn: geçiş biter.
    for (int i = 0; i < 10; ++i) RowlEngine_Step(handle, 0.25f);
    checkFreeze(RowlEngine_IsTransitionActive(handle) == 0,
                "#61: 5 s transition must complete after 5 s of steps");
    // Final 4 x 0.25 sn = +1.0: pre-fix 1.0 < 2 (kırmızı), post-fix 2.0 (yeşil).
    for (int i = 0; i < 4; ++i) RowlEngine_Step(handle, 0.25f);
    const uint64_t node = RowlEngine_GetCurrentNodeId(handle);
    RowlEngine_Destroy(handle);
    checkFreeze(node == 2,
                "#61: pre-transition credit must survive the transition "
                "(wiped credit needs a full second delay after every fade)");
    TEST_PASS("#61 transition freezes (not wipes) auto-advance credit");
}
