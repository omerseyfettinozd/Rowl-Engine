/**
 * test_backlog_screen.cpp — D03-2: the player's native backlog ("Gecmis")
 * screen: entry points, navigation, voice replay and the read-only contract.
 *
 * Kilit:
 *  1. Giris: hikayede B ve tekerlek-yukari gecmisi acar (secili = en yeni),
 *     tekerlek-asagi/yatay tekerlek acmaz; baslik ekraninda B/tekerlek acmaz;
 *     duraklatma menusu aciksken tekerlek/B eskisi gibi tuketilir (mod, secim
 *     ayni); B anahtari Type::Backlog'a eslenir.
 *  2. Cikis: B, sag tik, Esc (PauseToggle), Geri, en yeni satirda tekerlek-asagi
 *     kapatir; hikayeden acilan hikayeye, duraklatma menusundeki "Gecmis"
 *     satirindan acilan duraklatma ana menusune (satir 8) doner.
 *  3. Gezinme: Yukari/Asagi sarmaz, sinirda durur; kaydirma ofseti secimi
 *     gorunur tutar; tekerlek-yukari/asagi secimi tasir; girdiler sutuna
 *     cakismadan dizilir; cok satirli girdi daha cok yer kaplar; sol tik
 *     girdiyi secer + sesi calar, bosluk/alan disi tik hicbir sey yapmaz.
 *  4. Ses: Enter/Space/tik sesli satirda yalniz o satirin klibini calar,
 *     sessiz satirda hicbir sey (klip sayaci/yol ayni); Advance acikken
 *     hikayeyi ILERLETMEZ.
 *  5. Salt-okunur: ac-gez-tekrar-oynat-kapat boyunca degiskenler, stepId, mevcut
 *     dugum, gecmis, okundu profili, yuva dosyalari ve SFX sayaci degismez
 *     (D03 kabulunun ucten uca kilidi); acikken zaman ilerlemez.
 *  6. Bos gecmis: "Gecmis bos" satiri, Enter/Yukari/Asagi etkisiz, tekerlek-asagi
 *     kapatir.
 *  7. C API: RowlEngine_OpenBacklog (Init oncesi STATE_ERROR, null INVALID_HANDLE,
 *     baslikta STATE_ERROR, zaten aciksa OK); JSON backlog nesnesi (total,
 *     selected, scroll, entries[speaker, dialogue, has_voice]) Turkce metni
 *     bozmadan tasir.
 *  8. Cizim: gecmis acikken kare kararli (dim uste binmez); acilis/kapanis
 *     sonrasi hikaye karesi geri gelir.
 *
 * Bilerek-boz: Scroll'u yeniden no-op yapmak 1'i ve 3'u, backlogActivateSelected'in
 * sessiz satirda da replay cagirmasi 4'u, backlogClose'un duraklatma kolunu
 * setPaused(false)'a cevirmek 2'yi, backlogMoveSelection'i modulo-sarmaya
 * cevirmek 3'u kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/audio/audio_engine.hpp"
#include "rowl/core/engine.hpp"
#include "rowl/core/pause_menu.hpp"
#include "rowl/core/runtime_context.hpp"
#include "rowl/render/window.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Platform::RuntimeInputEvent;
using InType = RuntimeInputEvent::Type;
using LM = Rowl::Core::PauseMenuLayout;

constexpr const char* kSection = "backlog-screen";
constexpr int kNodes = 30;

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeBytes(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// 16-bit mono PCM silence, 3 s: a clip is still queued when the test looks.
std::string makeWav(uint32_t seconds) {
    constexpr uint32_t kRate = 22050;
    const uint32_t dataBytes = kRate * 2u * seconds;
    std::string out;
    const auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    const auto u16 = [&](uint16_t v) { for (int i = 0; i < 2; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF)); };
    out += "RIFF"; u32(36u + dataBytes); out += "WAVE";
    out += "fmt "; u32(16); u16(1); u16(1); u32(kRate); u32(kRate * 2u); u16(2); u16(16);
    out += "data"; u32(dataBytes);
    out.append(dataBytes, '\0');
    return out;
}

bool voicedNode(int id) { return id % 5 == 2; }  // 2, 7, 12, 17, 22, 27

const char* kLongText =
    "Bu cok uzun bir satirdir: gecmis ekraninda birden fazla satira sarilmasi gerekir. "
    "Turkce karakterler de var: ğüşiöç ĞÜŞİÖÇ ı İ. Kayip tablonun arkasindaki kapi, "
    "sabaha kadar acik kaldi ve kimse neden oldugunu soramadi; herkes sessizce bekledi, "
    "cunku bekleyecek baska bir sey kalmamisti.";

std::string buildGraph() {
    json nodes = json::array();
    for (int id = 1; id <= kNodes; ++id) {
        json components = json::array();
        if (id == 2 || id == 7) {
            components.push_back({{"type", "variable"}, {"id", "v" + std::to_string(id)}, {"enabled", true},
                                  {"data", {{"key", "score"}, {"value", "5"}, {"operation", "add"}}}});
        }
        if (id == 2) {
            components.push_back({{"type", "variable"}, {"id", "vm"}, {"enabled", true},
                                  {"data", {{"key", "mood"}, {"value", "calm"}, {"operation", "set"}}}});
        }
        if (voicedNode(id)) {
            components.push_back({{"type", "audio"}, {"id", "a" + std::to_string(id)}, {"enabled", true},
                                  {"data", {{"sfx_track", "s.wav"},
                                            {"voice_track", "v" + std::to_string(id) + ".wav"}}}});
        }
        const std::string text = id == 10 ? std::string(kLongText)
                                          : "Satir " + std::to_string(id) + (id % 3 == 0 ? " şöyle ığdır." : ".");
        components.push_back({{"type", "dialogue"}, {"id", "d" + std::to_string(id)}, {"enabled", true},
                              {"data", {{"speaker", id % 2 ? "Ayşe" : "Çağrı"}, {"dialogue", text},
                                        {"typewriter_enabled", false}}}});
        json node = {{"id", id}, {"title", "N" + std::to_string(id)},
                     {"objects", json::array({{{"id", "o" + std::to_string(id)}, {"name", "D"},
                                               {"is_active", true}, {"components", components}}})}};
        if (id < kNodes) node["next_nodes"] = json::array({{{"id", id + 1}}});
        nodes.push_back(node);
    }
    return json({{"format_version", 4}, {"start_node_id", 1}, {"nodes", nodes}}).dump();
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

json menu(RowlEngineHandle h) {
    const char* text = RowlEngine_GetPauseMenuJson(h);
    json parsed = json::parse(text ? text : "", nullptr, false);
    req(parsed.is_object(), "pause menu JSON did not parse");
    return parsed;
}

json history(RowlEngineHandle h) {
    json parsed = json::parse(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8), nullptr, false);
    req(parsed.is_array(), "history JSON did not parse");
    return parsed;
}

std::string variable(RowlEngineHandle h, const char* key) {
    const char* value = RowlEngine_GetVariable(h, key);
    return value ? value : "";
}

void cmd(RowlEngineHandle h, int command, int times = 1) {
    for (int i = 0; i < times; ++i) RowlEngine_PauseMenuCommand(h, command);
}

std::map<std::string, std::string> snapshotDir(const fs::path& dir) {
    std::map<std::string, std::string> files;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (entry.is_regular_file()) files[fs::relative(entry.path(), dir).string()] = readBytes(entry.path());
    }
    return files;
}

// Forwards everything to the host the engine already runs with and adds an
// injectable input queue (the same channel a native host uses), so B / wheel /
// right-click travel through Engine::handleRuntimeInput like real input.
class InputHost final : public Rowl::Platform::PlatformHost {
public:
    explicit InputHost(std::shared_ptr<Rowl::Platform::PlatformHost> inner) : m_inner(std::move(inner)) {}

    std::unique_ptr<std::istream> openAssetStream(const std::string& path) override {
        return m_inner ? m_inner->openAssetStream(path) : nullptr;
    }
    std::filesystem::path writableSavePath() const override {
        return m_inner ? m_inner->writableSavePath() : std::filesystem::path();
    }
    std::filesystem::path writableProfilePath() const override {
        return m_inner ? m_inner->writableProfilePath() : std::filesystem::path("profiles");
    }
    Rowl::Platform::LifecycleState lifecycleState() const override {
        return m_inner ? m_inner->lifecycleState() : Rowl::Platform::LifecycleState::Active;
    }
    std::vector<RuntimeInputEvent> takeInputEvents() override {
        std::vector<RuntimeInputEvent> result(input.begin(), input.end());
        input.clear();
        return result;
    }
    Rowl::Platform::RenderSurface renderSurface() const override {
        return m_inner ? m_inner->renderSurface() : Rowl::Platform::RenderSurface{};
    }
    Rowl::Platform::AudioFocus audioFocus() const override {
        return m_inner ? m_inner->audioFocus() : Rowl::Platform::AudioFocus::Granted;
    }

    std::deque<RuntimeInputEvent> input;

private:
    std::shared_ptr<Rowl::Platform::PlatformHost> m_inner;
};

struct Rig {
    RowlEngineHandle h = nullptr;
    Rowl::Core::Engine* engine = nullptr;
    Rowl::Audio::AudioEngine* audio = nullptr;
    std::shared_ptr<InputHost> host;
    fs::path saveDir;

    // Queues one input event and runs one frame so the engine consumes it.
    void send(InType type, float x = 0.0f, float y = 0.0f) {
        RuntimeInputEvent event{};
        event.type = type;
        event.x = x;
        event.y = y;
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    void key(uint32_t sdlKey) {
        RuntimeInputEvent event{};
        event.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(sdlKey, event), "unmapped test key");
        host->input.push_back(event);
        RowlEngine_Step(h, 0.016f);
    }
    void wheelUp() { send(InType::Scroll, 0.0f, 1.0f); }
    void wheelDown() { send(InType::Scroll, 0.0f, -1.0f); }

    json backlog() const {
        const json m = menu(h);
        req(m.contains("backlog"), "pause JSON has no backlog object: " + m.dump());
        return m["backlog"];
    }
    int mode() const { return menu(h)["mode"].get<int>(); }
    bool paused() const { return RowlEngine_IsPaused(h) == 1; }
    bool open() const { return paused() && mode() == 5; }
    int selected() const { return backlog()["selected"].get<int>(); }
    int total() const { return backlog()["total"].get<int>(); }
    void selectIndex(int index) {
        for (int i = 0; i < 200 && selected() != index; ++i) {
            cmd(h, selected() < index ? ROWL_PAUSE_MENU_DOWN : ROWL_PAUSE_MENU_UP);
        }
        req(selected() == index, "could not select backlog row " + std::to_string(index));
    }
    // Virtual centre of the visible entry for history row `index` -> click.
    bool clickEntry(int index) {
        const auto view = engine->getPauseMenuView();
        for (const auto& entry : view.backlog.entries) {
            if (entry.index != index) continue;
            constexpr float kToPhysical = 320.0f / 1920.0f;
            return RowlEngine_PointerDown(h, (LM::kRowX + LM::kRowW * 0.5f) * kToPhysical,
                                          (entry.y + entry.h * 0.5f) * kToPhysical) == 1;
        }
        req(false, "history row " + std::to_string(index) + " is not visible");
        return false;
    }
    void closeToStory() {
        if (paused()) RowlEngine_SetPaused(h, 0);
        req(!paused(), "could not return to the story");
    }
};

}  // namespace

void test_backlog_screen() {
    TEST_SECTION("Native backlog screen (D03-2)");

    const fs::path root = fs::temp_directory_path() / "rowl_backlog_screen";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "game";
    writeBytes(game / "project.rowlproj",
               R"({"project_uuid":"5b1e0a64-5b2c-4d7e-9a10-0c2b7d5e8f33","name":"Backlog Screen Test"})");
    writeBytes(game / "Assets" / "json" / "full_story_graph.json", buildGraph());
    const std::string wav = makeWav(3);
    writeBytes(game / "Assets" / "audio" / "s.wav", wav);
    for (int id = 1; id <= kNodes; ++id) {
        if (voicedNode(id)) writeBytes(game / "Assets" / "audio" / ("v" + std::to_string(id) + ".wav"), wav);
    }

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    // C API contract before the engine exists.
    {
        RowlEngineHandle early = RowlEngine_Create();
        req(early != nullptr, "create failed");
        req(RowlEngine_OpenBacklog(early) == ROWL_RESULT_STATE_ERROR, "OpenBacklog before Init must be STATE_ERROR");
        RowlEngine_Destroy(early);
        req(RowlEngine_OpenBacklog(nullptr) == ROWL_RESULT_INVALID_HANDLE, "OpenBacklog(null) must be INVALID_HANDLE");
    }

    Rig rig;
    rig.h = RowlEngine_Create();
    req(rig.h && RowlEngine_Init(rig.h, 320, 180, 0) == 1, "engine init failed");
    RowlEngine_SetProjectDirectory(rig.h, game.string().c_str());
    rig.saveDir = capiText(rig.h, RowlEngine_GetSaveDirectoryUtf8);
    fs::create_directories(rig.saveDir);
    RowlEngine_SetPlayState(rig.h, 1);
    RowlEngine_Step(rig.h, 0.016f);
    rig.engine = Rowl::Core::testEngineFromHandle(rig.h);
    req(rig.engine != nullptr, "no engine behind the handle");
    rig.audio = rig.engine->getAudio();
    req(rig.audio != nullptr, "engine has no audio engine");
    const auto context = rig.engine->getContext();
    req(context != nullptr, "engine has no runtime context");
    rig.host = std::make_shared<InputHost>(context->getPlatformHost());
    context->setPlatformHost(rig.host);
    RowlEngineHandle h = rig.h;

    uint64_t capabilities = 0;
    req(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK &&
            (capabilities & ROWL_ENGINE_CAPABILITY_BACKLOG_REPLAY) != 0,
        "BACKLOG_REPLAY capability must cover the backlog screen");
    req(RowlEngine_GetCurrentNodeId(h) == 1, "story starts at node 1");

    // 6. Empty history (before any node was presented to the history).
    req(history(h).empty(), "history must start empty");
    rig.key(SDLK_B);
    {
        req(rig.open(), "B opens the backlog even when it is empty");
        const json m = menu(h);
        req(m["title"] == "Gecmis" && m["rows"].size() == 1 && m["rows"][0]["label"] == "Gecmis bos" &&
                m["rows"][0]["enabled"] == false,
            "empty backlog shows a single dim 'Gecmis bos' row: " + m.dump());
        req(m["backlog"]["total"] == 0 && m["backlog"]["entries"].empty() && m["backlog"]["selected"] == -1,
            "empty backlog JSON");
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        cmd(h, ROWL_PAUSE_MENU_DOWN);
        cmd(h, ROWL_PAUSE_MENU_UP);
        rig.wheelUp();
        req(rig.open() && rig.audio->getVoiceClipCount() == 0, "Enter/Up/Down/wheel-up are inert on an empty backlog");
        rig.wheelDown();
        req(!rig.paused(), "wheel down closes an empty backlog");
    }
    TEST_PASS("Empty history: 'Gecmis bos', Enter inert, wheel down closes");

    // Present every node: history rows for nodes 2..30.
    for (int i = 1; i < kNodes; ++i) {
        RowlEngine_AdvanceNode(h, 0);
        RowlEngine_Step(h, 0.016f);
    }
    req(RowlEngine_GetCurrentNodeId(h) == static_cast<uint64_t>(kNodes), "advanced to the last node");
    const json rows = history(h);
    const int total = static_cast<int>(rows.size());
    req(total >= 25 && total <= kNodes, "history size " + std::to_string(total));
    std::vector<int> voicedRows, silentRows;
    for (int i = 0; i < total; ++i) (rows[i]["voice"] == "" ? silentRows : voicedRows).push_back(i);
    req(voicedRows.size() >= 4 && silentRows.size() >= 4, "graph must give both voiced and silent rows");

    // 1. Entry points.
    {
        RuntimeInputEvent mapped{};
        mapped.type = InType::Advance;
        req(Rowl::Render::Window::mapKeyToRuntimeInput(SDLK_B, mapped) && mapped.type == InType::Backlog,
            "B must map to Type::Backlog");

        // Not-opening inputs in the story.
        rig.wheelDown();
        rig.send(InType::Scroll, 3.0f, 0.0f);  // horizontal only
        rig.send(InType::Scroll, 0.0f, 0.0f);
        rig.send(InType::PointerMotion, 10.0f, 10.0f);
        rig.send(InType::SecondaryClick, 50.0f, 50.0f);
        req(!rig.paused(), "wheel down / horizontal wheel / right click in the story must not open the backlog");
        req(RowlEngine_GetCurrentNodeId(h) == static_cast<uint64_t>(kNodes), "those inputs do not advance the story");

        rig.wheelUp();
        req(rig.open(), "wheel up opens the backlog");
        req(rig.selected() == total - 1 && rig.total() == total, "opens on the newest row");
        rig.wheelDown();
        req(!rig.paused(), "wheel down on the newest row closes straight to the story");

        rig.key(SDLK_B);
        req(rig.open() && rig.selected() == total - 1, "B opens the backlog");
        rig.key(SDLK_B);
        req(!rig.paused(), "B closes it again");
    }
    TEST_PASS("B and wheel up open the backlog in the story; B / wheel down close it");

    // Title screen never opens the backlog; an open pause menu keeps eating the wheel.
    {
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "open title");
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_STATE_ERROR, "OpenBacklog on the title must be STATE_ERROR");
        rig.key(SDLK_B);
        rig.wheelUp();
        rig.wheelDown();
        rig.send(InType::SecondaryClick, 50.0f, 50.0f);
        {
            const json m = menu(h);
            req(m["title_active"] == true && m["mode"] == 3 && !m.contains("backlog"),
                "title screen must ignore B / wheel / right click");
        }
        RowlEngine_SetPaused(h, 0);

        RowlEngine_SetPaused(h, 1);
        cmd(h, ROWL_PAUSE_MENU_DOWN, 2);
        const json before = menu(h);
        rig.wheelUp();
        rig.wheelDown();
        rig.key(SDLK_B);
        rig.send(InType::SecondaryClick, 50.0f, 50.0f);
        const json after = menu(h);
        req(after["mode"] == 0 && after["selected"] == before["selected"] && rig.paused() &&
                !after.contains("backlog"),
            "an open pause menu consumes wheel / B / right click as before");
        RowlEngine_SetPaused(h, 0);
    }
    TEST_PASS("Title ignores B/wheel; pause menu still consumes the wheel (no backlog)");

    // 2. Pause-menu entry: Geri -> main page (row 8); direct entry: Geri -> story.
    {
        RowlEngine_SetPaused(h, 1);
        {
            const json m = menu(h);
            req(m["rows"].size() == 11 && m["rows"][8]["label"] == "Gecmis" && m["rows"][9]["label"] == "Ana Menu",
                "main page lists Gecmis before Ana Menu");
        }
        cmd(h, ROWL_PAUSE_MENU_DOWN, 8);
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        req(rig.open() && rig.selected() == total - 1, "pause menu 'Gecmis' row opens the backlog on the newest row");
        cmd(h, ROWL_PAUSE_MENU_BACK);
        {
            const json m = menu(h);
            req(rig.paused() && m["mode"] == 0 && m["selected"] == 8 && !m.contains("backlog"),
                "Geri returns to the pause main page on the Gecmis row");
        }
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        req(rig.open(), "reopen from the pause menu");
        rig.key(SDLK_B);
        req(rig.paused() && rig.mode() == 0, "B returns to the pause main page when opened from it");
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        rig.send(InType::SecondaryClick, 5.0f, 5.0f);
        req(rig.paused() && rig.mode() == 0, "right click returns to the pause main page when opened from it");
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        rig.key(SDLK_ESCAPE);
        req(rig.paused() && rig.mode() == 0, "Esc on the backlog is Back (pause main page)");
        cmd(h, ROWL_PAUSE_MENU_BACK);
        req(!rig.paused(), "Back on the pause main page resumes");

        // Opened over a pause sub-page the same way (host call while paused).
        RowlEngine_SetPaused(h, 1);
        cmd(h, ROWL_PAUSE_MENU_DOWN);
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);  // Oyunu Kaydet page
        req(rig.mode() == 1, "save slot page");
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK && rig.open(), "OpenBacklog while the menu is up");
        cmd(h, ROWL_PAUSE_MENU_BACK);
        req(rig.paused() && rig.mode() == 0, "Back from an API-opened backlog over a menu returns to the main page");
        RowlEngine_SetPaused(h, 0);

        // Direct (story) entries return to the story with every close path.
        for (int path = 0; path < 4; ++path) {
            req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK && rig.open(), "OpenBacklog from the story");
            req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK && rig.open(), "OpenBacklog twice stays open");
            if (path == 0) cmd(h, ROWL_PAUSE_MENU_BACK);
            if (path == 1) rig.send(InType::SecondaryClick, 5.0f, 5.0f);
            if (path == 2) rig.key(SDLK_ESCAPE);
            if (path == 3) rig.key(SDLK_P);
            req(!rig.paused(), "closing a story-opened backlog returns straight to the story (path " +
                                   std::to_string(path) + ")");
        }
    }
    TEST_PASS("Geri/B/right click/Esc: pause-opened -> pause main page (row 8), story-opened -> story");

    // 3. Navigation, scroll window and layout.
    {
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open for navigation");
        const auto geometryOk = [&](const std::string& what) {
            const auto view = rig.engine->getPauseMenuView();
            const auto& b = view.backlog;
            req(b.active && !b.entries.empty(), what + ": no entries");
            bool selectedVisible = false;
            float edge = LM::kBacklogY0;
            for (size_t i = 0; i < b.entries.size(); ++i) {
                const auto& e = b.entries[i];
                req(e.index == b.scroll + static_cast<int>(i), what + ": entries must be consecutive from the scroll offset");
                req(e.y >= edge - 0.01f && e.y + e.h <= LM::kBacklogY1 + 0.01f,
                    what + ": entry " + std::to_string(e.index) + " leaves the list area");
                req(!e.lines.empty() && e.h > 0.0f, what + ": entry without text lines");
                edge = e.y + e.h + LM::kBacklogGap;
                if (e.selected) {
                    selectedVisible = true;
                    req(e.index == b.selected, what + ": selected flag disagrees");
                }
            }
            req(selectedVisible, what + ": selected row " + std::to_string(b.selected) + " is not visible");
            req(view.backlog.counter == std::to_string(b.selected + 1) + " / " + std::to_string(total),
                what + ": counter " + view.backlog.counter);
        };
        geometryOk("open");
        int scrollAtOpen = rig.backlog()["scroll"].get<int>();
        req(scrollAtOpen > 0 && scrollAtOpen < total - 1, "a long history opens scrolled to its end");
        // Newest row last: the last visible entry is the newest one.
        {
            const auto view = rig.engine->getPauseMenuView();
            req(view.backlog.entries.back().index == total - 1, "newest row is the last visible entry");
        }

        // Down at the end does not wrap.
        cmd(h, ROWL_PAUSE_MENU_DOWN, 3);
        req(rig.selected() == total - 1 && rig.mode() == 5, "Down stops at the newest row (no wrap)");

        // Up all the way: the window follows; the oldest row is the stop.
        int lastScroll = scrollAtOpen;
        for (int step = 1; step <= total + 3; ++step) {
            cmd(h, ROWL_PAUSE_MENU_UP);
            const int expected = std::max(0, total - 1 - step);
            req(rig.selected() == expected, "Up step " + std::to_string(step) + " selection");
            const int scroll = rig.backlog()["scroll"].get<int>();
            req(scroll <= lastScroll && scroll <= rig.selected(), "scroll offset follows the selection upwards");
            lastScroll = scroll;
            geometryOk("up " + std::to_string(step));
        }
        req(rig.selected() == 0 && rig.backlog()["scroll"] == 0, "Up stops at the oldest row (no wrap)");

        // Down again: selection leaves the bottom edge, the list slides.
        int prevScroll = 0;
        for (int step = 1; step < total; ++step) {
            cmd(h, ROWL_PAUSE_MENU_DOWN);
            req(rig.selected() == step, "Down step selection");
            const int scroll = rig.backlog()["scroll"].get<int>();
            req(scroll >= prevScroll, "scroll offset never moves back while going down");
            prevScroll = scroll;
            geometryOk("down " + std::to_string(step));
        }
        req(rig.selected() == total - 1, "Down reaches the newest row");

        // Wheel moves the selection; wheel down at the newest row closes.
        rig.wheelUp();
        rig.wheelUp();
        req(rig.open() && rig.selected() == total - 3, "wheel up moves two rows older");
        rig.wheelDown();
        req(rig.open() && rig.selected() == total - 2, "wheel down moves one row newer");
        rig.wheelDown();
        req(rig.open() && rig.selected() == total - 1, "wheel down reaches the newest row");
        rig.wheelDown();
        req(!rig.paused(), "wheel down on the newest row closes the backlog");

        // Multi-line entries take more room than one-liners.
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open for layout");
        int longRow = -1;
        for (int i = 0; i < total; ++i) {
            if (rows[i]["dialogue"].get<std::string>().rfind("Bu cok uzun", 0) == 0) longRow = i;
        }
        req(longRow >= 0, "history holds the long line");
        rig.selectIndex(longRow);
        geometryOk("long row");
        {
            const auto view = rig.engine->getPauseMenuView();
            float longH = 0.0f, shortH = 1e9f;
            size_t longLines = 0;
            for (const auto& e : view.backlog.entries) {
                if (e.index == longRow) { longH = e.h; longLines = e.lines.size(); }
                else shortH = std::min(shortH, e.h);
            }
            req(longLines > 1 && longH > shortH, "the long line wraps to several lines and takes more height");
            req(longLines <= static_cast<size_t>(LM::kBacklogMaxLines), "wrapped lines are capped");
        }
        rig.closeToStory();
    }
    TEST_PASS("Navigation clamps at both ends, the window follows the selection, wheel moves/closes, entries tile");

    // JSON content: speaker / dialogue / has_voice, Turkish text intact.
    {
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open for JSON");
        const json b = rig.backlog();
        req(b["total"] == total && b["selected"] == total - 1 && b["scroll"].is_number_integer(),
            "backlog JSON header");
        for (const auto& entry : b["entries"]) {
            const int index = entry["index"].get<int>();
            req(entry["speaker"] == rows[index]["speaker"] && entry["dialogue"] == rows[index]["dialogue"],
                "entry " + std::to_string(index) + " speaker/dialogue differ from the history");
            req(entry["has_voice"].get<bool>() == (rows[index]["voice"] != ""), "has_voice flag");
            req(entry["selected"].get<bool>() == (index == total - 1), "entry selected flag");
        }
        bool turkish = false;
        for (const auto& entry : b["entries"]) {
            if (entry["speaker"] == "Çağrı" || entry["speaker"] == "Ayşe") turkish = true;
        }
        req(turkish, "Turkish speaker names survive the JSON");
        rig.closeToStory();
    }
    TEST_PASS("JSON backlog object carries total/selected/scroll and entries with speaker/dialogue/has_voice");

    // 4 + 5. Voice replay and the read-only contract.
    {
        const std::string moodBefore = variable(h, "mood");
        const std::string scoreBefore = variable(h, "score");
        req(moodBefore == "calm" && scoreBefore != "", "variable precondition");
        const uint64_t stepBefore = RowlEngine_GetCurrentStepId(h);
        const uint64_t nodeBefore = RowlEngine_GetCurrentNodeId(h);
        const std::string historyBefore = capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8);
        const std::string profileBefore = capiText(h, RowlEngine_ProfileGetJson);
        req(RowlEngine_SaveGameSlot(h, 1) == 1, "save slot 1 for the invariance check");
        const auto dirBefore = snapshotDir(rig.saveDir);
        const uint64_t sfxBefore = rig.audio->sfxStartedCount();

        const auto untouched = [&](const std::string& what) {
            req(variable(h, "mood") == moodBefore && variable(h, "score") == scoreBefore, what + ": variables changed");
            req(RowlEngine_GetCurrentStepId(h) == stepBefore, what + ": stepId changed");
            req(RowlEngine_GetCurrentNodeId(h) == nodeBefore, what + ": current node changed");
            req(capiText(h, RowlEngine_GetDialogueHistoryJsonUtf8) == historyBefore, what + ": history changed");
            req(capiText(h, RowlEngine_ProfileGetJson) == profileBefore, what + ": read profile changed");
            req(snapshotDir(rig.saveDir) == dirBefore, what + ": slot files changed");
            req(rig.audio->sfxStartedCount() == sfxBefore, what + ": sfx was triggered");
        };

        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open for replay");
        untouched("open");

        // Voiced row: Enter plays exactly that clip.
        const int voicedA = voicedRows[1], voicedB = voicedRows[2], silentA = silentRows[1];
        rig.selectIndex(voicedA);
        const uint32_t clips0 = rig.audio->getVoiceClipCount();
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        req(rig.audio->getCurrentVoicePath() == rows[voicedA]["voice"].get<std::string>() &&
                rig.audio->getVoiceClipCount() == clips0 + 1,
            "Enter on a voiced row replays that row's clip, path=" + rig.audio->getCurrentVoicePath());
        untouched("Enter on a voiced row");

        // Space (Advance) while the backlog is open replays too and never advances the story.
        rig.selectIndex(voicedB);
        rig.key(SDLK_SPACE);
        req(rig.audio->getCurrentVoicePath() == rows[voicedB]["voice"].get<std::string>() &&
                rig.audio->getVoiceClipCount() == clips0 + 2,
            "Space on a voiced row replays that row's clip");
        untouched("Space on a voiced row (no story advance)");

        // Silent row: nothing happens at all.
        rig.selectIndex(silentA);
        const uint32_t clips1 = rig.audio->getVoiceClipCount();
        const std::string path1 = rig.audio->getCurrentVoicePath();
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        rig.key(SDLK_RETURN);
        req(rig.audio->getVoiceClipCount() == clips1 && rig.audio->getCurrentVoicePath() == path1,
            "Enter on a silent row does nothing (clip count and path unchanged)");
        req(rig.open() && rig.selected() == silentA, "silent-row Enter keeps the page and the selection");
        untouched("Enter on a silent row");

        // Clicks: on a voiced row selects + replays; on a silent row only selects;
        // gaps / outside the list do nothing.
        rig.selectIndex(total - 1);
        {
            const auto view = rig.engine->getPauseMenuView();
            int clickVoiced = -1, clickSilent = -1;
            for (const auto& e : view.backlog.entries) {
                if (e.hasVoice && clickVoiced < 0) clickVoiced = e.index;
                if (!e.hasVoice && e.index != total - 1 && clickSilent < 0) clickSilent = e.index;
            }
            req(clickVoiced >= 0 && clickSilent >= 0, "visible window holds a voiced and a silent row");
            const uint32_t clips2 = rig.audio->getVoiceClipCount();
            req(rig.clickEntry(clickVoiced), "click consumed");
            req(rig.selected() == clickVoiced &&
                    rig.audio->getCurrentVoicePath() == rows[clickVoiced]["voice"].get<std::string>() &&
                    rig.audio->getVoiceClipCount() == clips2 + 1,
                "left click selects the row and replays its voice");
            const std::string path2 = rig.audio->getCurrentVoicePath();
            req(rig.clickEntry(clickSilent), "click consumed");
            req(rig.selected() == clickSilent && rig.audio->getVoiceClipCount() == clips2 + 1 &&
                    rig.audio->getCurrentVoicePath() == path2,
                "left click on a silent row selects it and plays nothing");
            // Outside the list column and in the title area: selection and audio stay.
            constexpr float kToPhysical = 320.0f / 1920.0f;
            const int keep = rig.selected();
            req(RowlEngine_PointerDown(h, 100.0f * kToPhysical, 500.0f * kToPhysical) == 1, "outside click consumed");
            req(RowlEngine_PointerDown(h, 960.0f * kToPhysical, 150.0f * kToPhysical) == 1, "title-area click consumed");
            req(rig.selected() == keep && rig.audio->getVoiceClipCount() == clips2 + 1, "outside clicks do nothing");
            req(rig.open(), "outside clicks never close the page");
        }
        untouched("clicks");

        // Frozen: stepping while open advances no time/story, a story advance call is blocked as in the menu.
        const uint64_t stepsInBacklog = RowlEngine_GetCurrentStepId(h);
        for (int i = 0; i < 20; ++i) RowlEngine_Step(h, 0.1f);
        req(RowlEngine_GetCurrentStepId(h) == stepsInBacklog && RowlEngine_GetCurrentNodeId(h) == nodeBefore,
            "the story is frozen while the backlog is open");

        // Close and compare again: every path left the session identical.
        rig.key(SDLK_B);
        req(!rig.paused(), "closed");
        untouched("open-navigate-replay-close");
        // The story still plays on afterwards.
        req(RowlEngine_GetCurrentNodeId(h) == nodeBefore, "same node after closing");
    }
    TEST_PASS("Voice replay: only the selected row's clip; silent rows inert; variables/step/node/history/profile/slots/sfx untouched");

    // 8. Rendering: the frame is stable while open and the story frame returns.
    {
        const auto frame = [&](int count) {
            for (int i = 0; i < count; ++i) RowlEngine_Step(h, 0.016f);
            uint32_t w = 0, hh = 0;
            const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
            req(pixels != nullptr && w > 0 && hh > 0, "offscreen pixel buffer");
            return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
        };
        const std::string story = frame(2);
        req(RowlEngine_OpenBacklog(h) == ROWL_RESULT_OK, "open for rendering");
        frame(1);
        const std::string first = frame(1);
        req(first != story, "the backlog overlay changes the frame");
        req(frame(5) == first, "backlog overlay compounds over frames (offscreen)");
        rig.selectIndex(rig.total() / 2);
        const std::string moved = frame(2);
        req(moved != first, "moving the selection redraws the list");
        rig.closeToStory();
        const std::string back = frame(2);
        req(back == story, "the story frame returns unchanged after closing the backlog");
    }
    TEST_PASS("Backlog overlay redraws on a clean story frame and leaves no ghost behind");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
    fs::remove_all(root, ec);
}
