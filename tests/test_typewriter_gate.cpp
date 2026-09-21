/**
 * test_typewriter_gate.cpp — Bulgu #40 [HIGH]: naive tamamlanma-kapısı
 * render'dan farklı formülle ölçüyordu.
 *
 * Zemin: areActiveDialoguesComplete() ham dialogue üzerinde markup-tag'leri
 * dahil codepoint sayıp lineer elapsed*1000/textSpeed uygularken, step/render
 * aynı satırı shapeDialogue+evaluateReveal (birim-bazı pauseBefore/speed,
 * trailingPause, markup-strip) ile çizerdi. Markup-ağır satır geç, duraklamalı
 * satır erken okunur; auto-advance ve isPreviewFrameStatic kayardı.
 *
 * RED: "<b>Hi</b>" (9 ham codepoint, 2 reveal birimi), text_speed 30
 * (30 ms/birim), auto_advance_delay 0, 3 x 0.05 sn adım.
 *   pre-fix: gate 0.27 sn'de açılır → 3 adımda (0.15 sn) node 1 (kırmızı).
 *   post-fix: reveal 0.06 sn'de tamamlanır → 2. adımda advance → node 2.
 * Bilerek-boz: gate'teki evaluateReveal sua satırı naive formüle geri
 * alınırsa bu test "node 1"de kırmızılar.
 */
#include "rowl_test_harness.hpp"

#include <filesystem>
#include <fstream>

namespace {

struct GateCwd {
    std::filesystem::path saved;
    std::filesystem::path dir;
    bool ok = false;
    explicit GateCwd(const std::string& tag) {
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
    ~GateCwd() {
        std::error_code ec;
        if (ok) std::filesystem::current_path(saved, ec);
        std::filesystem::remove_all(dir, ec);
    }
};

void checkGate(bool condition, const char* what) {
    if (!condition) {
        rowlLockFail("typewriter-gate", what);
    }
}

}  // namespace

void test_typewriter_gate() {
    TEST_SECTION("Typewriter Gate (#40: reveal-based completion)");

    GateCwd cwd("rowl_gate_cwd_40");
    if (!cwd.ok) {
        std::cerr << "#40 setup: CWD pin failed" << std::endl;
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
                "data":{"speaker":"S","dialogue":"<b>Hi</b>","typewriter_enabled":true,
                  "text_speed":30,"auto_advance":true,"auto_advance_delay":0}}]}],
             "next_nodes":[{"id":2,"label":"go"}]},
            {"id":2,"title":"B","objects":[],"next_nodes":[]}]})";
    }
    RowlEngineHandle handle = RowlEngine_Create();
    checkGate(handle != nullptr, "setup: Create returned null");
    if (!RowlEngine_Init(handle, 320, 180, 0)) {
        std::cerr << "#40 setup: bare init failed" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_SetProjectDirectory(handle, project.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 1) {
        std::cerr << "#40 setup: gate graph did not load" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_SetPlayState(handle, 1);
    // 3 x 0.05 s = 0.15 s elapsed: reveal (0.06 s) tamam, naive (0.27 s) degil.
    for (int i = 0; i < 3; ++i) RowlEngine_Step(handle, 0.05f);
    const uint64_t node = RowlEngine_GetCurrentNodeId(handle);
    RowlEngine_Destroy(handle);
    checkGate(node == 2,
              "#40: markup-heavy line must advance on reveal completion "
              "(naive gate holds to 0.27 s, reveal completes at 0.06 s)");
    TEST_PASS("#40 reveal-based gate advances a markup-heavy line on time");
}
