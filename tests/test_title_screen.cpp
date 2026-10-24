/**
 * test_title_screen.cpp — D02: native title screen (Yeni Oyun / Devam Et /
 * Oyunu Yukle / Tercihler / Cikis), slot metadata on the slot pages and
 * preference persistence into the player profile.
 *
 * Kilit:
 *  1. OpenTitleScreen: mode 3, 5 satir, oyun adi basligi; Back/SetPaused(1)
 *     hikayeyi baslatmaz; Devam Et kayitsizken devre disi ve Confirm no-op.
 *  2. Yeni Oyun: baslangic dugumu, title kapali, AdvanceNode calisir.
 *  3. Devam Et en yeni GECERLI kaydi (saved_at) yukler; en yeni bozuksa
 *     satir "bozuk" olur ve bir sonrakine duser; hepsi bozuksa devre disi.
 *  4. Yukle sayfasi: bos yuva / bozuk yuva no-op + ipucu, gecerli yuva
 *     yukler ve title'dan cikar, Back title'a doner.
 *  5. Tercihler: metin hizi / ses degisimi player profile'a yazilir (title
 *     ve duraklatma menusu); sayfa Geri ile title'a doner.
 *  6. Yuva satirinda tarih + ozet (detail), secili yuvanin kucuk resmi
 *     (selected_has_thumbnail); "dolu"/"bos" degerleri korunur.
 *
 * Bilerek-boz: ensureSlotCache'te saved_at karsilastirmasini '<' yapmak 3'u,
 * titleBack'in kok sayfada setPaused(false) cagirmasi 1'i, Preferences
 * satirinda persistPauseMenuPreference'i kaldirmak 5'i kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/pause_menu.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;

constexpr const char* kSection = "title-screen";

constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "speaker": "A", "dialogue": "First line.", "next_nodes": [{"id": 2}]},
    {"id": 2, "speaker": "A", "dialogue": "Second line of the story.", "next_nodes": [{"id": 3}]},
    {"id": 3, "speaker": "A", "dialogue": "Third line.", "next_nodes": [{"id": 4}]},
    {"id": 4, "speaker": "A", "dialogue": "Fourth line."}
  ]
})json";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
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

json profile(RowlEngineHandle h) {
    json parsed = json::parse(capiText(h, RowlEngine_ProfileGetJson), nullptr, false);
    req(parsed.is_object(), "profile JSON did not parse");
    return parsed;
}

void cmd(RowlEngineHandle h, int command, int times = 1) {
    for (int i = 0; i < times; ++i) RowlEngine_PauseMenuCommand(h, command);
}

// Moves the selection to `row` (Down only; the page wraps).
void selectRow(RowlEngineHandle h, int row) {
    for (int i = 0; i < 16 && menu(h)["selected"].get<int>() != row; ++i) {
        cmd(h, ROWL_PAUSE_MENU_DOWN);
    }
    req(menu(h)["selected"].get<int>() == row, "could not select row " + std::to_string(row));
}

void expectNode(RowlEngineHandle h, uint64_t expected, const std::string& what) {
    const uint64_t actual = RowlEngine_GetCurrentNodeId(h);
    req(actual == expected, what + ": expected node " + std::to_string(expected) +
                                ", got " + std::to_string(actual));
}

bool titleActive(RowlEngineHandle h) {
    int32_t active = -1;
    req(RowlEngine_IsTitleScreenActive(h, &active) == ROWL_RESULT_OK, "IsTitleScreenActive failed");
    req(RowlEngine_IsTitleScreenActive(h, nullptr) == ROWL_RESULT_INVALID_ARGUMENT,
        "IsTitleScreenActive(NULL) must be INVALID_ARGUMENT");
    return active == 1;
}

void expectTitle(RowlEngineHandle h, int mode, const std::string& what) {
    const json m = menu(h);
    req(titleActive(h) && m["title_active"].get<bool>(), what + ": title not active");
    req(m["mode"].get<int>() == mode, what + ": mode " + std::to_string(m["mode"].get<int>()) +
                                          ", expected " + std::to_string(mode));
}

void expectClosed(RowlEngineHandle h, const std::string& what) {
    req(!titleActive(h), what + ": title still active");
    req(RowlEngine_IsPaused(h) == 0, what + ": still paused");
}

fs::path slotFile(const fs::path& saveDir, int slot) {
    return saveDir / ("save_slot_" + std::to_string(slot) + ".json");
}

void setSavedAt(const fs::path& file, const std::string& stamp) {
    json doc = json::parse(readText(file), nullptr, false);
    req(doc.is_object(), "slot file is not a JSON object: " + file.string());
    doc["saved_at"] = stamp;
    writeText(file, doc.dump());
}

void corrupt(const fs::path& file) {
    writeText(file, std::string("\x01\x02 not json at all \xff\xfe", 22));
}

}  // namespace

void test_title_screen() {
    TEST_SECTION("Native title screen (D02)");

    const fs::path root = fs::temp_directory_path() / "rowl_title_screen";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path game = root / "game";
    writeText(game / "project.rowlproj",
              R"({"project_uuid":"3f1e0a64-5b2c-4d7e-9a10-0c2b7d5e8f11","name":"Kayip Tablo Test"})");
    writeText(game / "Assets" / "json" / "full_story_graph.json", kGraph);

    const char* previousXdg = std::getenv("XDG_DATA_HOME");
    const std::string savedXdg = previousXdg ? previousXdg : "";
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", (root / "xdg").string().c_str());
    _putenv_s("LOCALAPPDATA", (root / "xdg").string().c_str());
#else
    ::setenv("XDG_DATA_HOME", (root / "xdg").string().c_str(), 1);
#endif

    RowlEngineHandle h = RowlEngine_Create();
    req(h && RowlEngine_Init(h, 320, 180, 0) == 1, "engine init failed");
    // Not initialized-engine guard is covered by the null-handle path.
    req(RowlEngine_OpenTitleScreen(nullptr) == ROWL_RESULT_INVALID_HANDLE, "null handle must fail");
    RowlEngine_SetProjectDirectory(h, game.string().c_str());
    const fs::path saveDir = capiText(h, RowlEngine_GetSaveDirectoryUtf8);
    fs::create_directories(saveDir);
    RowlEngine_SetPlayState(h, 1);
    RowlEngine_Step(h, 0.016f);

    uint64_t capabilities = 0;
    req(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK &&
            (capabilities & ROWL_ENGINE_CAPABILITY_TITLE_SCREEN) != 0 &&
            ROWL_ENGINE_CAPABILITY_TITLE_SCREEN == UINT64_C(33554432),
        "TITLE_SCREEN capability bit missing");

    // 1. Open: no saves yet.
    req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "OpenTitleScreen failed");
    {
        expectTitle(h, 3, "open");
        const json m = menu(h);
        req(m["rows"].size() == 5, "title must have 5 rows");
        req(m["title"] == "Kayip Tablo Test", "title row text is the game name: " + m["title"].dump());
        req(m["rows"][0]["label"] == "Yeni Oyun" && m["rows"][4]["label"] == "Cikis", "title labels");
        req(m["rows"][1]["enabled"] == false && m["rows"][1]["value"] == "kayit yok",
            "Continue must be disabled without saves");
        req(m["continue_slot"].get<int>() == -1, "continue_slot must be -1");
        req(m["selected"].get<int>() == 0, "New Game is preselected without saves");
    }
    TEST_PASS("Title opens on mode 3 with 5 rows, game name, Continue disabled");

    // Esc/Back and host pause never start the story.
    cmd(h, ROWL_PAUSE_MENU_BACK, 2);
    RowlEngine_SetPaused(h, 1);
    expectTitle(h, 3, "back / SetPaused(1)");
    expectNode(h, 1, "back must not advance");
    selectRow(h, 1);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectTitle(h, 3, "confirm on disabled Continue");
    req(!menu(h)["hint"].get<std::string>().empty(), "disabled Continue explains itself");
    selectRow(h, 4);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);  // arms the exit confirmation only
    req(menu(h)["confirm_quit"] == true, "first Exit press arms confirmation");
    cmd(h, ROWL_PAUSE_MENU_BACK);
    req(menu(h)["confirm_quit"] == false, "Back disarms exit confirmation");
    expectTitle(h, 3, "back on root after disarm");
    TEST_PASS("Back / pause toggle never start the story; disabled Continue is a no-op");

    // 2. New Game.
    selectRow(h, 0);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectClosed(h, "new game");
    expectNode(h, 1, "new game start node");
    RowlEngine_AdvanceNode(h, 0);
    expectNode(h, 2, "story advances after new game");
    TEST_PASS("New Game leaves the title on the start node and the story advances");

    // 3. Continue picks the newest valid save.
    for (int i = 0; i < 8; ++i) RowlEngine_Step(h, 0.25f);
    req(RowlEngine_SaveGameSlot(h, 3) == 1, "save slot 3");     // node 2
    RowlEngine_AdvanceNode(h, 0);
    for (int i = 0; i < 8; ++i) RowlEngine_Step(h, 0.25f);
    req(RowlEngine_SaveGameSlot(h, 5) == 1, "save slot 5");     // node 3
    RowlEngine_AdvanceNode(h, 0);
    expectNode(h, 4, "before reopening the title");
    setSavedAt(slotFile(saveDir, 3), "2026-01-01T10:00:00Z");
    setSavedAt(slotFile(saveDir, 5), "2026-02-02T09:30:00Z");

    RowlEngine_SetPaused(h, 1);  // host pause menu first: closing + reopening title
    req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title");
    {
        const json m = menu(h);
        req(m["continue_slot"].get<int>() == 5, "newest save is slot 5");
        req(m["rows"][1]["enabled"] == true, "Continue enabled with saves");
        const std::string value = m["rows"][1]["value"];
        req(value.find("Yuva 5") != std::string::npos && value.find("2026-02-02 09:30") != std::string::npos,
            "Continue value shows slot and date: " + value);
        req(m["selected"].get<int>() == 1, "Continue is preselected when a save exists");
    }
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectClosed(h, "continue");
    expectNode(h, 3, "Continue loaded slot 5");

    // Flip the ages: slot 3 becomes newest.
    setSavedAt(slotFile(saveDir, 3), "2026-03-03T08:00:00Z");
    req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title (flip)");
    req(menu(h)["continue_slot"].get<int>() == 3, "continue follows saved_at, not slot order");
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectNode(h, 2, "Continue loaded slot 3");

    // Same-second saves (saved_at has 1 s resolution): the slot file written
    // last wins — in either direction, so it is not just "lower/higher slot".
    {
        setSavedAt(slotFile(saveDir, 3), "2026-05-05T05:00:00Z");
        setSavedAt(slotFile(saveDir, 5), "2026-05-05T05:00:00Z");
        const auto now = fs::file_time_type::clock::now();
        fs::last_write_time(slotFile(saveDir, 3), now - std::chrono::seconds(10));
        fs::last_write_time(slotFile(saveDir, 5), now);
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title (same second, 5 newer)");
        req(menu(h)["continue_slot"].get<int>() == 5, "same-second tie: slot written last (5)");
        fs::last_write_time(slotFile(saveDir, 3), now + std::chrono::seconds(10));
        req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title (same second, 3 newer)");
        req(menu(h)["continue_slot"].get<int>() == 3, "same-second tie: slot written last (3)");
        RowlEngine_SetPaused(h, 0);
        setSavedAt(slotFile(saveDir, 3), "2026-03-03T08:00:00Z");
        setSavedAt(slotFile(saveDir, 5), "2026-02-02T09:30:00Z");
    }

    // Newest file corrupted: row "bozuk", Continue falls back to slot 5.
    corrupt(slotFile(saveDir, 3));
    req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title (corrupt newest)");
    req(menu(h)["continue_slot"].get<int>() == 5, "corrupt newest save is skipped");
    selectRow(h, 2);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);  // Load page
    expectTitle(h, 2, "load page from title");
    {
        const json m = menu(h);
        req(m["rows"].size() == 10, "load page lists 10 slots");
        req(m["rows"][3]["value"] == "bozuk", "corrupt slot value is bozuk");
        req(m["rows"][5]["value"] == "dolu", "valid slot value stays dolu");
        req(m["rows"][0]["value"] == "bos", "empty slot value stays bos");
    }
    // 4. Load page behaviours.
    selectRow(h, 0);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectTitle(h, 2, "empty slot");
    req(menu(h)["hint"] == "bos yuva", "empty slot hint");
    selectRow(h, 3);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectTitle(h, 2, "corrupt slot");
    req(menu(h)["hint"] == "bozuk kayit", "corrupt slot hint");
    expectNode(h, 2, "no load happened");
    cmd(h, ROWL_PAUSE_MENU_BACK);
    expectTitle(h, 3, "back from load page");
    req(menu(h)["selected"].get<int>() == 2, "Back returns to the Load row");
    TEST_PASS("Corrupt newest save is skipped; load page empty/corrupt slots are no-ops with hints");

    // Valid slot loads and leaves the title.
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    selectRow(h, 5);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectClosed(h, "load valid slot from title");
    expectNode(h, 3, "slot 5 restored from the load page");
    TEST_PASS("Valid slot loads from the title and closes it");

    // All corrupt: Continue disabled.
    corrupt(slotFile(saveDir, 5));
    req(RowlEngine_OpenTitleScreen(h) == ROWL_RESULT_OK, "reopen title (all corrupt)");
    {
        const json m = menu(h);
        req(m["continue_slot"].get<int>() == -1, "no valid save left");
        req(m["rows"][1]["enabled"] == false && m["rows"][1]["value"] == "kayit yok",
            "Continue disabled when every save is corrupt");
    }
    selectRow(h, 1);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectTitle(h, 3, "continue with only corrupt saves");
    TEST_PASS("All-corrupt saves disable Continue");

    // 5. Preferences page persists into the profile.
    selectRow(h, 3);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    expectTitle(h, 4, "preferences page");
    {
        const json m = menu(h);
        req(m["rows"].size() == 8 && m["rows"][4]["label"] == "Metin Hizi" &&
                m["rows"][5]["label"] == "Otomatik Hizi" && m["rows"][6]["label"] == "Atlama" &&
                m["rows"][7]["label"] == "Geri",
            "preferences rows");
    }
    selectRow(h, 4);
    cmd(h, ROWL_PAUSE_MENU_RIGHT);
    req(profile(h)["text_speed_multiplier"].get<double>() == 1.25, "title text speed persisted: " +
                                                                         profile(h).dump());
    req(menu(h)["rows"][4]["value"] == "1.25x", "row shows the new speed");
    selectRow(h, 0);
    cmd(h, ROWL_PAUSE_MENU_LEFT);
    {
        const double persisted = profile(h)["master_volume"].get<double>();
        const double live = RowlEngine_GetMasterVolume(h);
        req(std::abs(persisted - live) < 0.011, "master volume profile value follows the mixer");
    }
    cmd(h, ROWL_PAUSE_MENU_BACK);
    expectTitle(h, 3, "back from preferences");
    selectRow(h, 3);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);
    selectRow(h, 7);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);  // Geri
    expectTitle(h, 3, "Geri row");
    TEST_PASS("Title preferences persist volume/text speed; Back and Geri return to the title");

    // Leave the title (host override), then the pause menu persists too.
    RowlEngine_SetPaused(h, 0);
    expectClosed(h, "host override");
    RowlEngine_SetPaused(h, 1);
    selectRow(h, 7);
    cmd(h, ROWL_PAUSE_MENU_RIGHT);
    req(profile(h)["text_speed_multiplier"].get<double>() == 1.5, "pause menu text speed persisted");
    TEST_PASS("Pause-menu text speed also persists into the profile");

    // 6. Slot rows: date + summary detail, thumbnail of the selected slot.
    RowlEngine_SetPaused(h, 0);
    for (int i = 0; i < 8; ++i) RowlEngine_Step(h, 0.25f);
    req(RowlEngine_SaveGameSlot(h, 2) == 1, "save slot 2 (valid after corruption)");
    setSavedAt(slotFile(saveDir, 2), "2026-04-04T07:45:00Z");
    RowlEngine_SetPaused(h, 1);
    selectRow(h, 2);
    cmd(h, ROWL_PAUSE_MENU_CONFIRM);  // Load page of the pause menu
    {
        const json m = menu(h);
        req(m["mode"].get<int>() == 2 && m["title_active"] == false, "pause load page");
        const std::string detail = m["rows"][2]["detail"];
        req(detail.find("2026-04-04 07:45") != std::string::npos, "detail has the date: " + detail);
        req(m["rows"][2]["value"] == "dolu" && m["rows"][0]["detail"] == "", "empty slot has no detail");
        req(detail.size() > std::string("2026-04-04 07:45").size(), "detail also carries the summary: " + detail);
    }
    selectRow(h, 2);
    req(menu(h)["selected_has_thumbnail"] == true, "selected slot with a thumbnail reports it");
    selectRow(h, 0);
    req(menu(h)["selected_has_thumbnail"] == false, "empty slot has no thumbnail");
    TEST_PASS("Slot rows carry date + summary; selected_has_thumbnail follows the selection");

    // Offscreen overlay frames: the paused story frame used to be reused from
    // the frame cache WITH the previous overlay still on it, so every frame
    // dimmed again (the story faded to black) and a page switch left the old
    // page's text as a ghost. The same menu page must give the same pixels.
    {
        auto frame = [&](int steps) {
            for (int i = 0; i < steps; ++i) RowlEngine_Step(h, 0.016f);
            uint32_t w = 0, hh = 0;
            const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
            req(pixels != nullptr && w > 0 && hh > 0, "offscreen pixel buffer");
            return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
        };
        // The first paused frame still reuses the pre-pause cached frame;
        // compare re-rendered frames from the second one on.
        frame(1);
        const std::string first = frame(1);
        req(frame(5) == first, "paused overlay compounds over frames (offscreen)");
        cmd(h, ROWL_PAUSE_MENU_BACK);  // main pause page
        frame(1);
        selectRow(h, 2);
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);  // the same Load page again
        req(menu(h)["mode"].get<int>() == 2 && menu(h)["selected"].get<int>() == 0, "back on the load page");
        req(frame(1) == first, "previous menu page ghosts into the next one (offscreen)");
    }
    TEST_PASS("Offscreen pause overlay redraws on a clean story frame");

    // Pause menu "Ana Menu" (row 9): two-step back to the title. An exit
    // confirmation armed on row 10 must not carry over to a click on row 9
    // (it used to: one click then skipped the confirmation).
    {
        cmd(h, ROWL_PAUSE_MENU_BACK);  // load page -> main pause page
        req(menu(h)["mode"].get<int>() == 0 && menu(h)["rows"].size() == 11, "pause main has 11 rows");
        req(menu(h)["rows"][8]["label"] == "Gecmis" && menu(h)["rows"][9]["label"] == "Ana Menu" &&
                menu(h)["rows"][10]["label"] == "Cikis",
            "Gecmis, Ana Menu, Cikis close the main page");
        selectRow(h, 10);
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        req(menu(h)["confirm_quit"] == true && menu(h)["confirm_title"] == false, "exit armed");
        const float row9Y = Rowl::Core::PauseMenuLayout::kRowY0 +
                            9 * (Rowl::Core::PauseMenuLayout::kRowH + Rowl::Core::PauseMenuLayout::kRowGap) +
                            Rowl::Core::PauseMenuLayout::kRowH * 0.5f;
        // The engine surface is 320x180: map the 1920x1080 virtual point.
        constexpr float kToPhysical = 320.0f / 1920.0f;
        req(RowlEngine_PointerDown(h, 960.0f * kToPhysical, row9Y * kToPhysical) == 1,
            "paused click is consumed by the menu");
        req(!titleActive(h), "a click on Ana Menu used the exit row's armed confirmation");
        req(menu(h)["confirm_title"] == true && menu(h)["confirm_quit"] == false,
            "first click on Ana Menu only arms it");
        cmd(h, ROWL_PAUSE_MENU_BACK);
        req(menu(h)["confirm_title"] == false && RowlEngine_IsPaused(h) == 1, "Back disarms Ana Menu");
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        req(!titleActive(h), "Ana Menu needs a second confirm");
        cmd(h, ROWL_PAUSE_MENU_CONFIRM);
        expectTitle(h, 3, "Ana Menu confirmed");
        req(RowlEngine_IsRunning(h) == 1, "Ana Menu must not quit");
    }
    TEST_PASS("Pause menu Ana Menu returns to the title after a two-step confirm");

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
