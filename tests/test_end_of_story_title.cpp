/**
 * test_end_of_story_title.cpp — D-K3 (6): hikaye bitince oyuncu basliga doner.
 *
 * Gercek pencere bulgusu: son dugumde (ornek "SON: ...") ilerleme her basista
 * "End of story chain reached" yazip hicbir sey yapmiyordu; oyuncu basliga
 * ancak Esc -> Ana Menu -> onay ile donebiliyordu.
 *
 * Karar (kapsam): davranis yalniz BASLIK EKRANI GOSTERILMIS oturumlarda (paketli
 * oyuncu varsayilan olarak acar; `--title-state story`, editor onizlemesi ve baslik
 * hic acmayan C API host'lari bunu gormez) ve yalniz gercek oyuncu GIRISINDE
 * (Enter / Space / tik / kaydirma = handleRuntimeInput) gecerlidir; RowlEngine_AdvanceNode /
 * Step'in kendisi eskisi gibi son karede kalir. Baslik acilis yolu, duraklatma
 * menusunun "Ana Menu" yoludur (openTitleScreen), onay sorulmaz.
 *
 * Kilit:
 *  1. Baslik acilmis oturumda son dugumde Enter / Space / tik / kaydirma baslik
 *     ekranini acar (mod 3, duraklatilmis); Yeni Oyun tekrar baslatir.
 *  2. Dugum girisinde yazilan basarim bu noktada zaten profilde: baslik acilinca
 *     kaybolmaz, yeniden baslatinca da durur.
 *  3. Yazan son satirda ilk basis yalniz satiri tamamlar, ikincisi basligi acar.
 *  4. Auto ve Skip sonda durur: hic baslik acmazlar.
 *  5. Baslik hic acilmamis oturumda (editor / --title-state story) son dugumde hicbir
 *     girdi baslik acmaz; RowlEngine_AdvanceNode baslik oturumunda da eskisi gibi no-op.
 *  6. Sonda olmayan dugumde ilerleme eskisi gibi ilerler (baslik yok).
 *
 * Bilerek-boz: tryEndOfStoryTitle'in handleRuntimeInput'taki cagrilarini
 * kaldirmak 1'i, m_titleSessionSeen kapisini kaldirmak 5'i, Auto/Skip'i
 * sonda ilerletmeye zorlamak 4'u kirmizi yapar.
 */
#include "rowl_test_harness.hpp"
#include "test_input_host.hpp"

#include "rowl/core/engine.hpp"
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
using InType = Rowl::Platform::RuntimeInputEvent::Type;

constexpr const char* kSection = "end-of-story-title";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string capiText(RowlEngineHandle h,
                     RowlEngine_ResultCode (*query)(RowlEngineHandle, char*, uint32_t, uint32_t*)) {
    uint32_t required = 0;
    req(query(h, nullptr, 0, &required) == ROWL_RESULT_OK, "size query failed");
    std::string buffer(required, '\0');
    req(query(h, buffer.data(), required, &required) == ROWL_RESULT_OK, "copy failed");
    buffer.resize(required - 1);
    return buffer;
}

constexpr const char* kCatalog = R"({
  "format": 1,
  "achievements": [{"id": "a_end", "title": "The End"}]
})";

// Node 1 -> 2 (the end: unlocks a_end). `typed` makes the last line a typewriter line.
std::string graph(bool typed) {
    const std::string typing = typed ? R"("typewriter_enabled": true, "text_speed": 30)"
                                     : R"("typewriter_enabled": false)";
    return R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "objects": [{"id": "o1", "name": "D", "is_active": true, "components": [
       {"type": "dialogue", "id": "d1", "enabled": true,
        "data": {"speaker": "A", "dialogue": "First.", "typewriter_enabled": false}}]}],
     "next_nodes": [{"id": 2}]},
    {"id": 2, "objects": [{"id": "o2", "name": "D", "is_active": true, "components": [
       {"type": "unlock", "id": "u2", "enabled": true, "data": {"kind": "achievement", "id": "a_end"}},
       {"type": "dialogue", "id": "d2", "enabled": true,
        "data": {"speaker": "A", "dialogue": "SON: the story ends here, with a line long enough to type.", )json" +
           typing + R"json(}}]}]}
  ]
})json";
}

int g_counter = 0;

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
    void open(const fs::path& root, bool typed) {
        const fs::path game = root / ("game" + std::to_string(++g_counter));
        char uuid[64];
        std::snprintf(uuid, sizeof(uuid), "6e1c2a64-3b2c-4d7e-9a10-%012d", g_counter);
        writeText(game / "project.rowlproj",
                  std::string(R"({"project_uuid":")") + uuid + R"(","name":"End Of Story Test"})");
        writeText(game / "Assets" / "json" / "full_story_graph.json", graph(typed));
        writeText(game / "Assets" / "json" / "extras.json", kCatalog);
        h = RowlEngine_Create();
        req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
        RowlEngine_SetProjectDirectory(h, game.string().c_str());
        fs::create_directories(capiText(h, RowlEngine_GetSaveDirectoryUtf8));
        engine = Rowl::Core::testEngineFromHandle(h);
        req(engine != nullptr, "no engine behind the handle");
        const auto context = engine->getContext();
        req(context != nullptr, "engine has no runtime context");
        host = std::make_shared<RowlTest::InputHost>(context->getPlatformHost());
        context->setPlatformHost(host);
        RowlEngine_SetPlayState(h, 1);
        settle();
    }
    void settle(int steps = 24, float dt = 0.25f) const {
        for (int i = 0; i < steps; ++i) RowlEngine_Step(h, dt);
    }
    void key(uint32_t sdlKey) {
        Rowl::Platform::RuntimeInputEvent event{};
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    void input(InType type, float x = 0.0f, float y = 0.0f) {
        Rowl::Platform::RuntimeInputEvent event{};
        event.type = type;
        event.x = x;
        event.y = y;
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    uint64_t node() const { return RowlEngine_GetCurrentNodeId(h); }
    bool title() const {
        int32_t active = -1;
        req(RowlEngine_IsTitleScreenActive(h, &active) == ROWL_RESULT_OK, "IsTitleScreenActive failed");
        return active == 1;
    }
    bool unlocked() const {
        int32_t value = -1;
        req(RowlEngine_ProfileIsUnlocked(h, "achievement", "a_end", &value) == ROWL_RESULT_OK, "IsUnlocked failed");
        return value == 1;
    }
    // Title (as the packaged player does) -> New Game -> the story's last node.
    void startFromTitle() {
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "OpenTitleScreen failed");
        key(SDLK_RETURN);  // Yeni Oyun is the first row
        req(!title() && node() == 1, "New Game did not start the story");
        settle(2);
    }
    void toEnd() {
        key(SDLK_RETURN);
        settle();
        req(node() == 2, "could not reach the last node, at " + std::to_string(node()));
    }
};

}  // namespace

void test_end_of_story_title() {
    TEST_SECTION("End of story returns to the title screen (D-K3)");

    const fs::path root = fs::temp_directory_path() / "rowl_end_of_story_title";
    std::error_code ec;
    fs::remove_all(root, ec);
    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    // 1+2+6. Title session: every manual advance form returns to the title; the
    // achievement written on entry survives; mid-story advance is unchanged.
    for (int form = 0; form < 4; ++form) {
        Rig rig;
        rig.open(root, false);
        rig.startFromTitle();
        rig.key(SDLK_RETURN);
        rig.settle(2);
        req(rig.node() == 2 && !rig.title(), "mid-story advance moves on without a title");
        req(rig.unlocked(), "the achievement is written on node entry");
        switch (form) {
            case 0: rig.key(SDLK_RETURN); break;
            case 1: rig.key(SDLK_SPACE); break;
            case 2: rig.input(InType::PointerDown, 160.0f, 90.0f); break;
            default: rig.input(InType::SwipeForward, 160.0f, 90.0f); break;
        }
        req(rig.title() && RowlEngine_IsPaused(rig.h) == 1,
            "form " + std::to_string(form) + ": the finished last node did not open the title");
        req(rig.unlocked(), "the achievement is still there on the title");
        // F2 (D-O1): the title that the finished story opened ignores confirm input for
        // ~0.6 s, so mashing the advance key on the last line cannot start a new game
        // by accident; afterwards Enter confirms New Game as before.
        rig.key(SDLK_RETURN);
        rig.key(SDLK_SPACE);
        rig.input(InType::PointerDown, 960.0f, 540.0f);
        req(rig.title() && rig.node() == 2, "form " + std::to_string(form) + ": input during the lock confirmed a row");
        rig.settle(3, 0.25f);
        // New Game starts over; the unlock stays.
        rig.key(SDLK_RETURN);
        req(!rig.title() && rig.node() == 1, "New Game after the end");
        req(rig.unlocked(), "the achievement survives the restart");
    }
    TEST_PASS("Enter / Space / click / swipe on the finished last node open the title; unlock kept; New Game restarts");

    // 3. A typing last line: first press completes it, the second opens the title.
    {
        Rig rig;
        rig.open(root, true);
        rig.startFromTitle();
        rig.key(SDLK_RETURN);  // to node 2, line typing
        RowlEngine_Step(rig.h, 0.05f);
        req(rig.node() == 2, "reached the typed last node");
        rig.key(SDLK_RETURN);
        req(!rig.title() && rig.node() == 2, "the first press only completes the typing line");
        rig.key(SDLK_RETURN);
        req(rig.title(), "the second press opens the title");
        rig.key(SDLK_RETURN);  // F2: ...and the very next press (the mashing player) is ignored
        req(rig.title(), "a third press inside the input lock must not confirm New Game");
    }
    TEST_PASS("A typing last line is completed first, then the title opens");

    // 4. Auto and Skip stop at the end.
    {
        Rig rig;
        rig.open(root, false);
        rig.startFromTitle();
        rig.toEnd();
        rig.key(SDLK_A);  // Auto on
        rig.settle(80, 0.25f);
        req(!rig.title() && rig.node() == 2, "Auto must stop at the last node, not open the title");
        rig.key(SDLK_A);  // Auto off
        rig.key(SDLK_S);  // Skip on
        rig.settle(80, 0.25f);
        req(!rig.title() && rig.node() == 2, "Skip must stop at the last node, not open the title");
    }
    TEST_PASS("Auto and Skip stop at the end and never open the title");

    // 5. No title shown in the session (editor preview / --title-state story): old behaviour.
    {
        Rig rig;
        rig.open(root, false);
        rig.key(SDLK_RETURN);
        rig.settle(2);
        req(rig.node() == 2, "no-title session reached the last node");
        rig.key(SDLK_RETURN);
        rig.key(SDLK_SPACE);
        rig.input(InType::PointerDown, 160.0f, 90.0f);
        rig.input(InType::SwipeForward, 160.0f, 90.0f);
        req(!rig.title() && RowlEngine_IsPaused(rig.h) == 0 && rig.node() == 2,
            "a session that never showed the title must stay on the last frame");
    }
    // The host API never navigates: AdvanceNode at the end stays a no-op, also in a title session.
    {
        Rig rig;
        rig.open(root, false);
        rig.startFromTitle();
        rig.toEnd();
        RowlEngine_AdvanceNode(rig.h, 0);
        RowlEngine_Step(rig.h, 0.016f);
        req(!rig.title() && rig.node() == 2, "RowlEngine_AdvanceNode at the end must stay a no-op");
    }
    TEST_PASS("Sessions without a title (and the host AdvanceNode API) keep the old end-of-story behaviour");

#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(root, ec);
}
