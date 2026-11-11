/**
 * test_choice_keyboard.cpp — D-K3 (1): secim menusu klavye / fare ile oynanir.
 *
 * Gercek pencere bulgusu (D-K1 sonrasi): secim dugumunde Space/Enter "waiting for
 * a choice" diye yutuluyor, oklar yalniz duraklatilmis menude calisiyor, rakamlar
 * hizli-kayit yuvasini degistiriyordu — klavyeyle oynayan oyuncu ilk secimde
 * takiliyordu.
 *
 * Kilit (hepsi gercek giris yolundan: Window::mapKeyToRuntimeInput -> RuntimeInput
 * -> Engine::handleRuntimeInput):
 *  1. Secim acilinca odak YOK (sef karari: Space/Enter ile satirlari hizla gecen
 *     oyuncu secim belirdigi anda ayni tusla ilk secenegi farkinda olmadan secmesin);
 *     gozlem JSON'unda "focused" hepsi false; Enter/Space odak yokken eskisi gibi
 *     yok sayilir (dugum degismez, yazan satiri tamamlama aynen).
 *  2. Ilk Asagi ilk, ilk Yukari son SECILEBILIR secenege odak verir (devre disi /
 *     kosulu tutmayan atlanir); sonra gezer ve sarar.
 *  3. Enter ve Space odakli secenegi secer (dugum hedefe gider); yazan satir varsa
 *     ilk basis satiri tamamlar.
 *  4. 1-9 secim gorunurken n'inci SECILEBILIR secenegi secer; aralik disi rakam
 *     tuketilir (hizli-kayit yuvasi degismez); secim yokken rakam yuva degistirir.
 *  5. Fare hareketi ustundeki secilebilir secenege odagi tasir; dugme disina cikinca
 *     odak kalir (karar); hicbir sey ilerletmez.
 *  6. Duraklatilmisken oklar duraklatma menusunu gezer (secim odagi oynamaz).
 *  7. Odak secim kapaninca ve geri sarma sonrasi yok; secilebilirligini yitiren odak
 *     duser (yeni Asagi ilk secilebiliri secer).
 *  8. Odaksiz karede hicbir dugme vurgulanmaz; ok tusundan sonra odaklanan dugme
 *     ekran disi karede gorsel olarak ayrisir, digerleri degismez.
 *
 * Bilerek-boz: focusedChoiceIndex'in odak yokken ilk secilebiliri donmesi (eski
 * "ilk odak" davranisi) 1'i, handleRuntimeInput'ta Advance'in activateFocusedChoice'a gitmemesi
 * 3'u, SelectSlot'ta selectChoiceByNumber'in kaldirilmasi 4'u, PointerMotion'in
 * focusChoiceAt'i cagirmamasi 5'i, window.cpp'de focused dolgusunun kaldirilmasi
 * 8'i kirmizi yapar.
 */
#include "rowl_test_harness.hpp"
#include "test_input_host.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using InType = Rowl::Platform::RuntimeInputEvent::Type;

constexpr const char* kSection = "choice-keyboard";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

// Options: first (open, 0), off (author-disabled), locked (condition), second (open),
// third (open). Selectable order: first, second, third.
constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "speaker": "Guide", "dialogue": "Choose.",
     "objects": [{"id": "choices", "name": "Choices", "is_active": true, "components": [
       {"type": "choice", "id": "choice_main", "enabled": true, "data": {"options": [
         {"option_id": "first", "text": "First", "x": 600, "y": 300, "width": 600, "height": 70},
         {"option_id": "off", "text": "Off", "enabled": false,
          "x": 600, "y": 400, "width": 600, "height": 70},
         {"option_id": "locked", "text": "Locked", "condition": "has_key == 'yes'",
          "x": 600, "y": 500, "width": 600, "height": 70},
         {"option_id": "second", "text": "Second", "x": 600, "y": 600, "width": 600, "height": 70},
         {"option_id": "third", "text": "Third", "x": 600, "y": 700, "width": 600, "height": 70}]}}]}],
     "next_nodes": [
       {"id": 2, "label": "First", "option_id": "first"},
       {"id": 3, "label": "Off", "option_id": "off"},
       {"id": 4, "label": "Locked", "option_id": "locked"},
       {"id": 5, "label": "Second", "option_id": "second"},
       {"id": 6, "label": "Third", "option_id": "third"}]},
    {"id": 2, "speaker": "Guide", "dialogue": "Went first.", "next_nodes": [{"id": 7}]},
    {"id": 3, "speaker": "Guide", "dialogue": "Went off."},
    {"id": 4, "speaker": "Guide", "dialogue": "Went locked."},
    {"id": 5, "speaker": "Guide", "dialogue": "Went second."},
    {"id": 6, "speaker": "Guide", "dialogue": "Went third."},
    {"id": 7, "speaker": "Guide", "dialogue": "After first."}
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
         {"option_id": "a", "text": "A", "x": 600, "y": 400, "width": 600, "height": 70}]}}]}],
     "next_nodes": [{"id": 2, "label": "A", "option_id": "a"}]},
    {"id": 2, "speaker": "Guide", "dialogue": "A path."}
  ]
})json";

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    std::shared_ptr<RowlTest::InputHost> host;

    ~Rig() {
        if (h) {
            RowlEngine_Shutdown(h);
            RowlEngine_Destroy(h);
        }
    }

    void open(const fs::path& graph) {
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 1920, 1080, 0) == 1, "engine init failed");
        RowlEngine_LoadStoryGraph(h, graph.string().c_str());
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<RowlTest::InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
        RowlEngine_SetPlayState(h, 1);
        settle();
    }
    // Lets any typewriter finish so the checks below verify story flow.
    void settle() const {
        for (int i = 0; i < 24; ++i) RowlEngine_Step(h, 0.25f);
    }
    void key(uint32_t sdlKey) {
        Rowl::Platform::RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    void hover(float x, float y) {
        Rowl::Platform::RuntimeInputEvent event{};
        event.type = InType::PointerMotion;
        event.x = x;
        event.y = y;
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    // Option id holding the focus ("" = none).
    std::string focused() const {
        std::string id;
        int count = 0;
        for (const auto& button : engine->getLaidOutChoiceButtons()) {
            if (button.focused) {
                id = button.optionId;
                ++count;
            }
        }
        req(count <= 1, "more than one focused choice");
        return id;
    }
    void expectFocus(const std::string& id, const std::string& what) const {
        const std::string actual = focused();
        req(actual == id, what + ": focus '" + actual + "', expected '" + id + "'");
    }
    void expectNode(uint64_t id, const std::string& what) const {
        req(node() == id, what + ": node " + std::to_string(node()) + ", expected " + std::to_string(id));
    }
};

void writeFile(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    req(static_cast<bool>(out), "could not stage " + path.string());
    out << content;
}

}  // namespace

void test_choice_keyboard() {
    TEST_SECTION("Choice keyboard (arrows / Enter / digits / hover on a choice menu)");

    const fs::path dir = fs::temp_directory_path() / "rowl_choice_keyboard";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path graphPath = dir / "graph.json";
    const fs::path typingPath = dir / "typing.json";
    writeFile(graphPath, kGraph);
    writeFile(typingPath, kTypingGraph);

    // 1-3. No focus on open, arrow walk with wrap, Enter / Space pick.
    {
        Rig rig;
        rig.open(graphPath);
        rig.expectNode(1, "fixture start");
        rig.expectFocus("", "a choice menu opens without a focus");
        {
            const int required = RowlEngine_QueryWorldJson(rig.h, nullptr, nullptr, 0);
            req(required != 0, "world JSON size query failed");
            std::string text(static_cast<size_t>(required < 0 ? -required : required) + 1, '\0');
            const int written = RowlEngine_QueryWorldJson(rig.h, nullptr, text.data(), static_cast<int>(text.size()));
            req(written > 0, "world JSON copy failed");
            text.resize(static_cast<size_t>(written));
            req(text.find("\"focused\":true") == std::string::npos, "world JSON shows a focus on a fresh menu: " + text);
            req(text.find("\"focused\":false") != std::string::npos, "world JSON lacks the focused field: " + text);
        }
        rig.key(SDLK_RETURN);
        rig.key(SDLK_SPACE);
        rig.expectNode(1, "Enter / Space without a focus must not answer the menu");
        rig.expectFocus("", "mashing Enter / Space does not create a focus");
        TEST_PASS("A fresh choice menu has no focus; Enter / Space without a focus are ignored");

        rig.key(SDLK_DOWN);
        rig.expectFocus("first", "first Down -> first selectable option");
        rig.key(SDLK_DOWN);
        rig.expectFocus("second", "Down skips the disabled and the locked option");
        rig.key(SDLK_DOWN);
        rig.expectFocus("third", "Down again");
        rig.key(SDLK_DOWN);
        rig.expectFocus("first", "Down wraps to the first");
        rig.key(SDLK_UP);
        rig.expectFocus("third", "Up wraps to the last");
        rig.key(SDLK_UP);
        rig.expectFocus("second", "Up");
        rig.expectNode(1, "arrows never advance the story");
        TEST_PASS("Up/Down walk the selectable options and wrap");

        rig.key(SDLK_RETURN);
        rig.expectNode(5, "Enter picks the focused option");
        rig.expectFocus("", "no focus once the menu is gone");
        TEST_PASS("Enter picks the focused option; focus resets with the menu");
    }
    {
        Rig rig;
        rig.open(graphPath);
        rig.key(SDLK_UP);
        rig.expectFocus("third", "first Up -> last selectable option");
        TEST_PASS("First Up focuses the last selectable option");
    }
    {
        Rig rig;
        rig.open(graphPath);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_SPACE);
        rig.expectNode(6, "Space picks the focused option");
        TEST_PASS("Space picks the focused option");
    }

    // 3b. A typing line: first press completes it, the second picks.
    {
        Rig rig;
        rig.open(typingPath);
        // Re-arm the typewriter, then step a hair so the line is mid-typing.
        RowlEngine_ResetToStartNode(rig.h);
        RowlEngine_Step(rig.h, 0.05f);
        rig.key(SDLK_RETURN);
        rig.expectNode(1, "first Enter only completes the typing line");
        rig.key(SDLK_RETURN);
        rig.expectNode(1, "without a focus the second Enter is ignored");
        rig.key(SDLK_DOWN);
        rig.key(SDLK_RETURN);
        rig.expectNode(2, "with a focus Enter picks");
        // A focus on a line that is still typing: Enter completes the line first.
        RowlEngine_ResetToStartNode(rig.h);
        RowlEngine_Step(rig.h, 0.05f);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_RETURN);
        rig.expectNode(1, "Enter with a focus still completes a typing line first");
        rig.key(SDLK_RETURN);
        rig.expectNode(2, "then picks");
        TEST_PASS("Enter completes a typing line first; it picks only with a focus");
    }

    // 4. Digits.
    {
        Rig rig;
        rig.open(graphPath);
        const int32_t slotBefore = RowlEngine_GetQuickSaveSlot(rig.h);
        rig.key(SDLK_9);
        rig.key(SDLK_0);
        rig.expectNode(1, "out-of-range digits are consumed");
        req(RowlEngine_GetQuickSaveSlot(rig.h) == slotBefore, "a digit on a choice menu changed the quick-save slot");
        rig.key(SDLK_2);
        rig.expectNode(5, "2 picks the second SELECTABLE option (second), not the second listed");
        TEST_PASS("Digits pick the n-th selectable option; out-of-range ones are consumed");
    }
    {
        Rig rig;
        rig.open(graphPath);
        rig.key(SDLK_1);
        rig.expectNode(2, "1 picks the first selectable option");
        // Node 2 has no choice menu: the digit is a quick-save slot again.
        rig.key(SDLK_4);
        req(RowlEngine_GetQuickSaveSlot(rig.h) == 4, "digit without a choice menu must pick the quick-save slot");
        rig.expectNode(2, "slot digit does not advance");
        TEST_PASS("Without a choice menu a digit still picks the quick-save slot");
    }

    // 5. Hover.
    {
        Rig rig;
        rig.open(graphPath);
        rig.hover(900.0f, 640.0f);  // inside "second" (600..1200 x 600..670)
        rig.expectFocus("second", "hover moves focus to the option under the pointer");
        rig.hover(900.0f, 440.0f);  // "off" is disabled
        rig.expectFocus("second", "hover over a disabled option keeps the focus");
        rig.hover(100.0f, 100.0f);  // empty canvas
        rig.expectFocus("second", "hover over empty canvas keeps the focus");
        rig.hover(900.0f, 740.0f);
        rig.expectFocus("third", "hover again");
        rig.expectNode(1, "hover never advances the story");
        rig.key(SDLK_RETURN);
        rig.expectNode(6, "Enter after hover picks the hovered option");
        TEST_PASS("Hover moves the focus to the selectable option under the pointer");
    }

    // 6. Paused: arrows belong to the pause menu.
    {
        Rig rig;
        rig.open(graphPath);
        rig.key(SDLK_DOWN);
        rig.expectFocus("first", "setup");
        RowlEngine_SetPaused(rig.h, 1);
        rig.key(SDLK_DOWN);
        rig.expectFocus("", "no focus highlight under the pause menu");
        RowlEngine_SetPaused(rig.h, 0);
        rig.expectFocus("first", "focus is kept across a pause (the arrow went to the pause menu)");
        rig.expectNode(1, "pause + arrows leave the story alone");
        TEST_PASS("While paused the arrows drive the pause menu, not the choice focus");
    }

    // 7. Rewind resets, a condition turning true adds an option to the walk.
    {
        Rig rig;
        rig.open(graphPath);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_RETURN);
        rig.expectNode(5, "setup: picked second");
        req(RowlEngine_Rewind(rig.h, 1) == 1, "rewind failed");
        rig.settle();
        rig.expectNode(1, "rewound to the choice node");
        rig.expectFocus("", "after a rewind there is no focus");
        TEST_PASS("Rewind leaves the menu without a focus");

        // A condition that holds is part of the walk; one that stops holding
        // while the menu is up drops out of it (selectability is re-evaluated).
        // Variant graph: "locked" is open until has_key becomes "no".
        std::string openGraph = kGraph;
        const std::string lockedCondition = "has_key == 'yes'";
        openGraph.replace(openGraph.find(lockedCondition), lockedCondition.size(), "has_key ~= 'no'");
        const fs::path openPath = dir / "open.json";
        writeFile(openPath, openGraph);
        RowlEngine_LoadStoryGraph(rig.h, openPath.string().c_str());
        rig.settle();
        rig.expectFocus("", "fresh menu");
        rig.key(SDLK_DOWN);
        rig.key(SDLK_DOWN);
        rig.expectFocus("locked", "an option whose condition holds is in the walk");
        RowlEngine_SetVariable(rig.h, "has_key", "no");
        rig.expectFocus("", "the focus of an option that got locked drops");
        rig.key(SDLK_DOWN);
        rig.expectFocus("first", "an option whose condition stopped holding is skipped; Down starts at the first");
        rig.key(SDLK_DOWN);
        rig.expectFocus("second", "and the walk skips the locked one");
        TEST_PASS("Selectability is re-evaluated: a locked option leaves the walk");
    }

    // 8. Visual: the focused button differs from the unfocused one on screen.
    {
        Rig rig;
        rig.open(graphPath);
        auto frame = [&rig]() {
            for (int i = 0; i < 3; ++i) RowlEngine_Step(rig.h, 0.016f);
            uint32_t w = 0, hgt = 0;
            const uint8_t* pixels = RowlEngine_GetPixelBuffer(rig.h, &w, &hgt);
            req(pixels != nullptr && w == 1920 && hgt == 1080, "offscreen pixel buffer");
            return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hgt * 4);
        };
        auto region = [](const std::string& px, int x0, int y0, int x1, int y1) {
            std::string out;
            for (int y = y0; y < y1; ++y) out.append(px, (size_t(y) * 1920 + x0) * 4, size_t(x1 - x0) * 4);
            return out;
        };
        const std::string noFocus = frame();
        rig.key(SDLK_RETURN);  // ignored without a focus: still the same picture
        req(noFocus == frame(), "Enter without a focus changed the picture");
        rig.key(SDLK_DOWN);
        const std::string firstFocused = frame();
        rig.key(SDLK_DOWN);
        const std::string secondFocused = frame();
        // "first" box 600..1200 x 300..370, "second" 600..1200 x 600..670, "third" y 700..770.
        req(region(noFocus, 600, 300, 1200, 370) != region(firstFocused, 600, 300, 1200, 370),
            "the first button looks the same focused and unfocused");
        req(region(noFocus, 600, 600, 1200, 770) == region(firstFocused, 600, 600, 1200, 770),
            "an unfocused button changed when the first got the focus");
        req(region(firstFocused, 600, 300, 1200, 370) != region(secondFocused, 600, 300, 1200, 370),
            "the first button keeps its highlight after the focus left it");
        req(region(firstFocused, 600, 600, 1200, 670) != region(secondFocused, 600, 600, 1200, 670),
            "the second button looks the same focused and unfocused");
        req(region(firstFocused, 600, 700, 1200, 770) == region(secondFocused, 600, 700, 1200, 770),
            "an uninvolved button changed");
        TEST_PASS("No focus draws no highlight; the arrow-focused button is drawn differently from the others");
    }
}
