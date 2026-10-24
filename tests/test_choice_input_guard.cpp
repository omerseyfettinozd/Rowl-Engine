/**
 * test_choice_input_guard.cpp — sunulan seçim menüsü yalnız seçilebilir
 * bir seçenekle yanıtlanır.
 *
 * Hata (2026-10-23, "Kayıp Tablo" ajan bulgusu): seçim düğümünde Space /
 * Enter / boş tuvale tıklama `advanceToNextNode(0)` ile 0. kenarı izliyordu —
 * o seçeneğin koşulu sağlanmasa bile. RowlEngine_SelectChoice da devre dışı
 * ya da koşulu tutmayan seçeneği kabul ediyordu.
 *
 * Kilit:
 *  1. Düz ilerleme (AdvanceNode = Space/Enter/kaydırma) seçim düğümünde
 *     düğümü değiştirmez.
 *  2. Boş tuvale tıklama PointerDown'da tüketilir (1 döner); host'un
 *     PointerDown+AdvanceNode birleşimi de düğümü değiştirmez.
 *  3. Koşulu tutmayan / yazar tarafından kapatılmış seçenek SelectChoice'ta
 *     ve düğmesine tıklamada reddedilir.
 *  4. Koşul seçim anında yeniden değerlendirilir: değişken sonradan
 *     sağlanınca seçenek seçilebilir.
 *  5. Seçim düğümünde yazan satırı ilk ilerleme yine tamamlar (MS-6).
 *  6. Seçimsiz düğümde düz ilerleme eskisi gibi çalışır.
 *
 * Bilerek-boz: advanceStory'deki `!viaChoice && !m_activeChoiceButtons
 * .empty()` koruması kaldırılırsa 1 ve 2 kırmızı; advanceToChoice'taki
 * choiceButtonSelectable kapısı kaldırılırsa 3 kırmızı.
 */
#include "rowl_test_harness.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

constexpr const char* kSection = "choice-input-guard";

// Node 1: locked (condition) is option 0 — the edge the old advance took;
// disabled (author flag) is option 1; open is option 2.
constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "speaker": "Guide", "dialogue": "Choose.",
     "objects": [{"id": "choices", "name": "Choices", "is_active": true, "components": [
       {"type": "choice", "id": "choice_main", "enabled": true, "data": {"options": [
         {"option_id": "locked", "text": "Locked", "condition": "has_key == 'yes'",
          "x": 680, "y": 440, "width": 560, "height": 64},
         {"option_id": "disabled", "text": "Disabled", "enabled": false,
          "x": 680, "y": 520, "width": 560, "height": 64},
         {"option_id": "open", "text": "Open",
          "x": 680, "y": 600, "width": 560, "height": 64}]}}]}],
     "next_nodes": [
       {"id": 2, "label": "Locked", "option_id": "locked"},
       {"id": 3, "label": "Disabled", "option_id": "disabled"},
       {"id": 4, "label": "Open", "option_id": "open"}]},
    {"id": 2, "speaker": "Guide", "dialogue": "Locked path."},
    {"id": 3, "speaker": "Guide", "dialogue": "Disabled path."},
    {"id": 4, "speaker": "Guide", "dialogue": "Open path.", "next_nodes": [{"id": 5}]},
    {"id": 5, "speaker": "Guide", "dialogue": "After."}
  ]
})json";

// A choice node whose own line is still typing.
constexpr const char* kTypingGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "speaker": "Guide", "dialogue": "Typed.",
     "objects": [{"id": "o", "name": "O", "is_active": true, "components": [
       {"type": "dialogue", "id": "d", "enabled": true, "data": {
         "speaker": "Guide",
         "dialogue": "This line is long enough that a short step leaves it mid-typing.",
         "typewriter_enabled": true, "text_speed": 30}},
       {"type": "choice", "id": "c", "enabled": true, "data": {"options": [
         {"option_id": "a", "text": "A", "x": 680, "y": 520, "width": 560, "height": 64}]}}]}],
     "next_nodes": [{"id": 2, "label": "A", "option_id": "a"}]},
    {"id": 2, "speaker": "Guide", "dialogue": "A path."}
  ]
})json";

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) rowlLockFail(kSection, "could not stage " + path.string());
    out << content;
    out.close();
    if (!out) rowlLockFail(kSection, "could not flush " + path.string());
}

void expectNode(RowlEngineHandle h, uint64_t expected, const std::string& what) {
    const uint64_t actual = RowlEngine_GetCurrentNodeId(h);
    if (actual != expected) {
        rowlLockFail(kSection, what + ": expected node " + std::to_string(expected) +
                                   ", got " + std::to_string(actual));
    }
}

// Lets any typewriter finish so the checks below verify story flow.
void settle(RowlEngineHandle h) {
    for (int i = 0; i < 24; ++i) RowlEngine_Step(h, 0.25f);
}

}  // namespace

void test_choice_input_guard() {
    TEST_SECTION("Choice input guard (plain advance never answers a choice menu)");

    const fs::path dir = fs::temp_directory_path() / "rowl_choice_input_guard";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path graphPath = dir / "graph.json";
    const fs::path typingPath = dir / "typing.json";
    writeFile(graphPath, kGraph);
    writeFile(typingPath, kTypingGraph);

    RowlEngineHandle h = RowlEngine_Create();
    if (!h || RowlEngine_Init(h, 1920, 1080, 0) != 1) rowlLockFail(kSection, "engine init failed");

    RowlEngine_LoadStoryGraph(h, graphPath.string().c_str());
    RowlEngine_SetPlayState(h, 1);
    settle(h);
    expectNode(h, 1, "fixture start");

    // 1. Space/Enter/swipe funnel.
    RowlEngine_AdvanceNode(h, 0);
    RowlEngine_AdvanceNode(h, 0);
    expectNode(h, 1, "plain advance on a choice node");
    TEST_PASS("Plain advance does not pick choice 0 on a choice node");

    // 2. Empty-canvas click, composed the way the player and editor do.
    if (RowlEngine_PointerDown(h, 960.0f, 100.0f) != 1) {
        rowlLockFail(kSection, "click beside the choice buttons was not consumed");
    }
    if (RowlEngine_PointerDown(h, 960.0f, 100.0f) == 0) RowlEngine_AdvanceNode(h, 0);
    expectNode(h, 1, "empty-canvas click on a choice node");
    TEST_PASS("Empty-canvas click is consumed on a choice node");

    // 3. Locked and author-disabled options are refused, by id and by click.
    if (RowlEngine_SelectChoice(h, "locked") != 0) rowlLockFail(kSection, "locked option was accepted");
    if (RowlEngine_SelectChoice(h, "disabled") != 0) rowlLockFail(kSection, "disabled option was accepted");
    if (RowlEngine_PointerDown(h, 960.0f, 470.0f) != 1) rowlLockFail(kSection, "click on locked button not consumed");
    if (RowlEngine_PointerDown(h, 960.0f, 550.0f) != 1) rowlLockFail(kSection, "click on disabled button not consumed");
    expectNode(h, 1, "locked/disabled options");
    TEST_PASS("Locked and disabled options are refused");

    // 4. The condition is checked at selection time.
    RowlEngine_SetVariable(h, "has_key", "yes");
    if (RowlEngine_SelectChoice(h, "locked") != 1) rowlLockFail(kSection, "met condition still refused");
    expectNode(h, 2, "locked option after its condition holds");
    RowlEngine_LoadStoryGraph(h, graphPath.string().c_str());
    settle(h);
    RowlEngine_SetVariable(h, "has_key", "no");
    if (RowlEngine_PointerDown(h, 960.0f, 630.0f) != 1) rowlLockFail(kSection, "open button click not consumed");
    expectNode(h, 4, "open option by click");
    TEST_PASS("Condition is re-checked at selection; open option works");

    // 6. A node without choices still advances.
    settle(h);
    RowlEngine_AdvanceNode(h, 0);
    expectNode(h, 5, "plain node advance");
    TEST_PASS("Plain node still advances");

    // 5. Typing line on a choice node: first advance completes it, nothing moves.
    RowlEngine_LoadStoryGraph(h, typingPath.string().c_str());
    RowlEngine_Step(h, 0.0f);
    RowlEngine_Step(h, 0.2f);
    RowlEngine_AdvanceNode(h, 0);
    expectNode(h, 1, "typing choice node");
    RowlEngine_Step(h, 0.0f);
    if (RowlEngine_SelectChoice(h, "a") != 1) rowlLockFail(kSection, "choice after completed line refused");
    expectNode(h, 2, "choice after completed line");
    TEST_PASS("Typing line on a choice node completes, then the choice works");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);
    fs::remove_all(dir, ec);
}
