/**
 * test_pause_subpage_back.cpp — D-K3 (4): Esc, duraklatma alt sayfalarinda Geri'dir.
 *
 * Gercek pencere bulgusu: Kayit / Yukle yuva sayfasinin alt bilgisi "ESC: geri"
 * diyor ama PauseToggle yalniz Tercihler / Gecmis icin menuBack'e gidiyor,
 * yuva sayfalarinda togglePause ile oyuna donuyordu.
 *
 * Kilit (gercek giris yolu: Window::mapKeyToRuntimeInput -> handleRuntimeInput):
 *  1. Kayit sayfasinda Esc (ve P) ana duraklatma menusune doner (Esc = menuBack:
 *     secim sifirlanir, MS-6 kilidi test_native_c_api "Back -> main page,
 *     selection reset" ile ayni); menu hala aciktir; ikinci Esc oyuna doner.
 *  2. Yukle sayfasi ayni.
 *  3. Ana duraklatma menusunde Esc eskisi gibi devam ettirir; onay beklerken
 *     (Ana Menu satirinda "emin misin?") de bugunku davranis: menu kapanir ve
 *     onay silinir (bozulmadi).
 *  4. Tercihler / Gecmis geri yollari etkilenmedi (secili satir 3 / 4).
 *  5. Baslik ekraninda Yukle sayfasi Esc ile basliga doner (hikaye baslamaz).
 *
 * Bilerek-boz: handleRuntimeInput PauseToggle kosulunu `m_pauseMode ==
 * Preferences`e geri almak 1 ve 2'yi kirmizi yapar.
 */
#include "rowl_test_harness.hpp"
#include "test_input_host.hpp"

#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using LM = Rowl::Core::PauseMenuLayout;

constexpr const char* kSection = "pause-subpage-back";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "speaker": "A", "dialogue": "First.", "next_nodes": [{"id": 2}]},
    {"id": 2, "speaker": "A", "dialogue": "Second.", "next_nodes": [{"id": 3}]},
    {"id": 3, "speaker": "A", "dialogue": "Third."}
  ]
})json";

struct Rig {
    RowlEngineHandle h = nullptr;
    std::shared_ptr<RowlTest::InputHost> host;

    ~Rig() {
        if (h) {
            RowlEngine_Shutdown(h);
            RowlEngine_Destroy(h);
        }
    }
    void open(const fs::path& graph) {
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 640, 360, 0) == 1, "engine init failed");
        RowlEngine_LoadStoryGraph(h, graph.string().c_str());
        auto* engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<RowlTest::InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
        RowlEngine_SetPlayState(h, 1);
        for (int i = 0; i < 8; ++i) RowlEngine_Step(h, 0.25f);
    }
    void key(uint32_t sdlKey) {
        Rowl::Platform::RuntimeInputEvent event{};
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    json menu() const {
        const char* text = RowlEngine_GetPauseMenuJson(h);
        json parsed = json::parse(text ? text : "", nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    bool paused() const { return RowlEngine_IsPaused(h) == 1; }
    int mode() const { return menu()["mode"].get<int>(); }
    int selected() const { return menu()["selected"].get<int>(); }
    void expectPage(int mode, int selected, const std::string& what) const {
        const json m = menu();
        req(paused() && m["mode"].get<int>() == mode && m["selected"].get<int>() == selected,
            what + ": " + m.dump());
    }
    // Opens the pause menu with Esc and walks to `row`, then confirms.
    void openRow(int row) {
        key(SDLK_ESCAPE);
        req(paused() && mode() == 0, "Esc did not open the pause menu");
        for (int i = 0; i < 8 && selected() != row; ++i) key(SDLK_DOWN);
        req(selected() == row, "could not reach row " + std::to_string(row));
        key(SDLK_RETURN);
    }
};

}  // namespace

void test_pause_subpage_back() {
    TEST_SECTION("Pause sub-page back (Esc leaves the page, not the menu)");

    const fs::path dir = fs::temp_directory_path() / "rowl_pause_subpage_back";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path graphPath = dir / "graph.json";
    {
        std::ofstream out(graphPath, std::ios::binary | std::ios::trunc);
        out << kGraph;
    }

    {
        Rig rig;
        rig.open(graphPath);
        rig.openRow(1);
        rig.expectPage(1, 0, "Kaydet page open");
        rig.key(SDLK_ESCAPE);
        rig.expectPage(0, 0, "Esc on the save page returns to the main page (selection reset, like the menu Back)");
        rig.key(SDLK_ESCAPE);
        req(!rig.paused(), "Esc on the main page resumes");
        TEST_PASS("Esc on the save page goes back to the pause menu, a second Esc resumes");

        // P maps to the same toggle.
        rig.openRow(1);
        rig.key(SDLK_P);
        rig.expectPage(0, 0, "P on the save page");
        TEST_PASS("P behaves like Esc on the save page");
    }
    {
        Rig rig;
        rig.open(graphPath);
        rig.openRow(2);
        rig.expectPage(2, 0, "Yukle page open");
        rig.key(SDLK_ESCAPE);
        rig.expectPage(0, 0, "Esc on the load page returns to the main page (selection reset, like the menu Back)");
        TEST_PASS("Esc on the load page goes back to the pause menu");
    }

    // 3. Main page: resume, also while the exit / title confirmation is armed.
    {
        Rig rig;
        rig.open(graphPath);
        rig.openRow(LM::kMainTitleRow);  // arms the "emin misin?" confirmation
        req(rig.paused() && rig.menu()["confirm_title"].get<bool>(), "confirmation not armed: " + rig.menu().dump());
        rig.key(SDLK_ESCAPE);
        req(!rig.paused(), "Esc with an armed confirmation (today's behaviour: resume)");
        rig.key(SDLK_ESCAPE);
        req(rig.paused() && !rig.menu()["confirm_title"].get<bool>(), "reopened menu must not carry the confirmation");
        TEST_PASS("Main page Esc still resumes, also over an armed confirmation");
    }

    // 4. Preferences / backlog back paths are untouched.
    {
        Rig rig;
        rig.open(graphPath);
        rig.openRow(LM::kMainPrefsRow);
        rig.expectPage(4, 0, "Tercihler open");
        rig.key(SDLK_ESCAPE);
        rig.expectPage(0, LM::kMainPrefsRow, "Esc on Tercihler returns to its row");
        rig.key(SDLK_ESCAPE);
        req(!rig.paused(), "resume");
        TEST_PASS("Preferences back path unchanged");
    }

    // 5. Title screen: the Load page returns to the title, never the story.
    {
        Rig rig;
        rig.open(graphPath);
        req(RowlEngine_OpenTitleScreen(rig.h) == ROWL_RESULT_OK, "OpenTitleScreen failed");
        rig.key(SDLK_DOWN);
        rig.key(SDLK_DOWN);
        rig.key(SDLK_RETURN);
        req(rig.mode() == 2, "title Load page did not open: " + rig.menu().dump());
        rig.key(SDLK_ESCAPE);
        req(rig.paused() && rig.mode() == 3, "Esc on the title Load page returns to the title: " + rig.menu().dump());
        TEST_PASS("Title Load page Esc returns to the title");
    }

    fs::remove_all(dir, ec);
}
