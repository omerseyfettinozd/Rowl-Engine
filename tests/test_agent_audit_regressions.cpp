/**
 * test_agent_audit_regressions.cpp — denetim duzeltmelerinin regresyon kilidi.
 *
 * Kilitlenen duzeltmeler:
 *  A. parseUiTheme: 200000 kez "[" iceren belge cokmeden usable=false + "nesting too deep".
 *  B. uiThemeWithoutSkins cursor.image'i temizler; uiThemeHasSkins yalniz imlec gorseli
 *     olan temada true doner.
 *  C. GameState::decodeJson: "-7" step_id reddedilir; "version": 4294967297 UnsupportedVersion.
 *  D. LuaSandbox::setVariable: gomulu NUL'lu deger false doner.
 *  E. LuaSandbox::loadModule: modul govdesinde _G[1] sayi anahtari calisir.
 *  F. LuaSandbox::clearVariables: _G[5] sayi anahtarli kacak global oturum sinirinda silinir.
 *  G. ChapterLoader: index "order" tasmasi reddedilir; ayni chapter iki kez beslenince
 *     toplam dugum sayisi degismez; dugumu silinmis surum silinen dugumu dusurur.
 *  H. StoryGraphParser: chapters[].order 4294967296 reddedilir ("out of range").
 *  I. PlayerProfileStore: 200000 derinlikte dosya cokmeden Corrupt durumu verir.
 *
 * Ses aygiti / pencere / ag / gercek kullanici dizini gerekmez; profil bolumu
 * test_native_player_profile.cpp'nin gecici dizin kalibini izler.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/chapter_loader.hpp"
#include "rowl/core/story_graph_parser.hpp"
#include "rowl/state/player_profile.hpp"
#include "rowl/state/ui_theme.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace Rowl::State;

constexpr const char* kSection = "agent-audit-regressions";

void req(bool condition, const std::string& message) {
    if (!condition) rowlLockFail(kSection, message);
}

bool hasDiagnostic(const UiThemeLoad& load, const std::string& needle) {
    for (const auto& line : load.diagnostics) {
        if (line.find(needle) != std::string::npos) return true;
    }
    return false;
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// ── A ───────────────────────────────────────────────────────────────────────
void themeNestingDepthTest() {
    const std::string deeplyNested(200000, '[');
    const UiThemeLoad load = parseUiTheme(deeplyNested);
    req(!load.usable, "a 200000-deep ui_theme.json was accepted");
    req(hasDiagnostic(load, "nesting too deep"),
        "the refusal does not name the nesting depth");
    req(load.theme == builtinUiTheme(),
        "a refused theme must fall back to the built-in one");
    TEST_PASS("A: 200000-deep ui_theme.json -> usable=false + 'nesting too deep' (no crash)");
}

// ── B ───────────────────────────────────────────────────────────────────────
void themeCursorSkinTest() {
    req(!uiThemeHasSkins(builtinUiTheme()), "the built-in theme reports skins");

    UiTheme cursorOnly = builtinUiTheme();
    cursorOnly.cursor.image = "cursors/audit.png";
    req(uiThemeHasSkins(cursorOnly), "a cursor-only theme must report skins");
    const UiTheme strippedCursor = uiThemeWithoutSkins(cursorOnly);
    req(strippedCursor.cursor.image.empty(), "uiThemeWithoutSkins kept cursor.image");
    req(!uiThemeHasSkins(strippedCursor),
        "the cursor image still counts after uiThemeWithoutSkins");

    UiTheme skinned = builtinUiTheme();
    skinned.menu.panelSkin.normal.image = "ui/audit_panel.png";
    req(uiThemeHasSkins(skinned), "a panel-skin theme must report skins");
    const UiTheme strippedSkin = uiThemeWithoutSkins(skinned);
    req(strippedSkin.menu.panelSkin.normal.image.empty(),
        "uiThemeWithoutSkins kept the panel image");
    req(!uiThemeHasSkins(strippedSkin),
        "a skin image still counts after uiThemeWithoutSkins");
    TEST_PASS("B: cursor.image cleared by uiThemeWithoutSkins; hasSkins tracks images only");
}

// ── C ───────────────────────────────────────────────────────────────────────
void gameStateDecodeGuardTest() {
    using Rowl::State::GameState;
    using Rowl::State::GameStateDecodeStatus;

    const auto valid = GameState::decodeJson(R"({"version":4,"step_id":5,"active_node_id":1})");
    req(valid.state != nullptr && valid.status == GameStateDecodeStatus::Loaded &&
            valid.state->stepId == 5,
        "control: a valid v4 save no longer decodes");

    const auto negative = GameState::decodeJson(
        R"({"version":4,"step_id":-7,"active_node_id":1})");
    req(negative.state == nullptr, "a negative step_id decoded into a state");
    req(negative.status == GameStateDecodeStatus::InvalidData,
        "a negative step_id must answer InvalidData");

    const auto wrapped = GameState::decodeJson(
        R"({"version":4294967297,"step_id":1,"active_node_id":1})");
    req(wrapped.state == nullptr, "version 4294967297 wrapped onto an accepted version");
    req(wrapped.status == GameStateDecodeStatus::UnsupportedVersion,
        "version 4294967297 must answer UnsupportedVersion");
    TEST_PASS("C: negative step_id refused; version 4294967297 is UnsupportedVersion");
}

// ── D ───────────────────────────────────────────────────────────────────────
void luaEmbeddedNulTest() {
    Rowl::Scripting::LuaSandbox sandbox;
    req(sandbox.initialize() && sandbox.isInitialized(), "lua sandbox init failed");

    const std::string embedded("a\0b", 3);
    req(!sandbox.setVariable("audit_nul", embedded),
        "a value with an embedded NUL was accepted");
    req(sandbox.getVariable("audit_nul").empty(),
        "the rejected NUL value still entered the variable map");
    req(sandbox.setVariable("audit_plain", "a-b"), "a plain value was rejected");
    req(sandbox.getVariable("audit_plain") == "a-b", "a plain value did not round-trip");

    sandbox.shutdown();
    TEST_PASS("D: setVariable rejects an embedded-NUL value and keeps the map clean");
}

// ── E ───────────────────────────────────────────────────────────────────────
void luaModuleNumericGlobalTest() {
    Rowl::Scripting::LuaSandbox sandbox;
    req(sandbox.initialize() && sandbox.isInitialized(), "lua sandbox init failed");

    req(sandbox.loadModule("audit_numeric_key",
                           R"(_G[1] = "deger"; assert(_G[1] == "deger"))"),
        "module body with a numeric global key failed: " + sandbox.getLastError());
    req(sandbox.getModuleCount() == 1, "the module was not registered");
    req(sandbox.unloadModule("audit_numeric_key"), "the module could not be unloaded");

    sandbox.shutdown();
    TEST_PASS("E: loadModule keeps a numeric _G key numeric inside the module environment");
}

// ── F ───────────────────────────────────────────────────────────────────────
void luaClearVariablesNumericStrayTest() {
    Rowl::Scripting::LuaSandbox sandbox;
    req(sandbox.initialize() && sandbox.isInitialized(), "lua sandbox init failed");

    req(sandbox.executeString(R"(_G[5] = "x")"),
        "planting _G[5] failed: " + sandbox.getLastError());
    sandbox.clearVariables();
    req(sandbox.executeString(
            R"(assert(_G[5] == nil, "numeric-keyed stray survived clearVariables"))"),
        "a numeric-keyed stray global survived clearVariables: " + sandbox.getLastError());

    sandbox.shutdown();
    TEST_PASS("F: clearVariables sweeps numeric-keyed stray globals too");
}

// ── G ───────────────────────────────────────────────────────────────────────
void chapterLoaderOrderAndReplaceTest() {
    {
        Rowl::Core::ChapterLoader indexLoader;
        std::string error;
        req(!indexLoader.loadIndexJson(R"({"chapters":[{"id":"c1","order":4294967296}]})",
                                       error),
            "chapter index order 4294967296 was accepted");
        req(error.find("out of range") != std::string::npos,
            "chapter index order rejection has the wrong diagnostic: " + error);
    }

    const std::string chapter = R"({"chapter_id":"ch1","nodes":[{"id":1},{"id":2},{"id":3}]})";
    const std::string withoutMiddle = R"({"chapter_id":"ch1","nodes":[{"id":1},{"id":3}]})";

    Rowl::Core::ChapterLoader loader;
    std::string error;
    req(loader.appendChapterFileJson(chapter, error), "first append failed: " + error);
    const std::size_t total = loader.totalNodeCount();
    req(total == 3, "first append stored " + std::to_string(total) + " nodes");

    req(loader.appendChapterFileJson(chapter, error),
        "re-appending the same chapter failed: " + error);
    req(loader.totalNodeCount() == total,
        "re-appending the same chapter changed the total node count");
    req(loader.node(2) != nullptr, "node 2 vanished after the idempotent re-append");

    req(loader.appendChapterFileJson(withoutMiddle, error),
        "append without node 2 failed: " + error);
    req(loader.totalNodeCount() == 2,
        "the deleted node still counts: " + std::to_string(loader.totalNodeCount()));
    req(loader.node(2) == nullptr, "the deleted node is still reachable");
    req(loader.node(1) != nullptr && loader.node(3) != nullptr,
        "surviving nodes vanished");
    TEST_PASS("G: order overflow rejected; same chapter idempotent; deleted node gone");
}

// ── H ───────────────────────────────────────────────────────────────────────
void storyGraphChapterOrderTest() {
    using Rowl::Core::StoryGraphParseErrorKind;
    using Rowl::Core::StoryGraphParser;

    const auto accepted = StoryGraphParser::parse(R"({"format_version":5,"start_node_id":1,
        "nodes":[{"id":1,"chapter_id":"c1"}],"chapters":[{"id":"c1","order":1}]})");
    req(accepted.succeeded(),
        "control: a valid chapter order was rejected: " + accepted.message);

    const auto rejected = StoryGraphParser::parse(R"({"format_version":5,"start_node_id":1,
        "nodes":[{"id":1,"chapter_id":"c1"}],"chapters":[{"id":"c1","order":4294967296}]})");
    req(!rejected.succeeded(), "chapter order 4294967296 was accepted");
    req(rejected.errorKind == StoryGraphParseErrorKind::Validation,
        "the chapter order rejection is not a validation error");
    req(rejected.message.find("out of range") != std::string::npos,
        "chapter order rejection has the wrong diagnostic: " + rejected.message);
    TEST_PASS("H: chapters[].order 4294967296 -> Validation + 'out of range'");
}

// ── I ───────────────────────────────────────────────────────────────────────
void playerProfileDeepNestingTest() {
    const fs::path base = fs::temp_directory_path() / "rowl_agent_audit_regressions_profile";
    std::error_code ec;
    fs::remove_all(base, ec);
    fs::create_directories(base);
    writeText(base / kPlayerProfileFileName, std::string(200000, '['));

    PlayerProfileStore store;
    store.open(base, "");
    req(store.status() == PlayerProfileStatus::Corrupt,
        std::string("deeply nested profile status is not Corrupt: ") +
            playerProfileStatusName(store.status()));
    req(store.diagnostic().find("nested too deeply") != std::string::npos,
        "deeply nested profile diagnostic is wrong: " + store.diagnostic());
    store.close();
    TEST_PASS("I: 200000-deep player-profile.json -> Corrupt 'nested too deeply' (no crash)");
}

} // namespace

void test_agent_audit_regressions() {
    TEST_SECTION("Agent Audit Regressions (A-I)");

    themeNestingDepthTest();
    themeCursorSkinTest();
    gameStateDecodeGuardTest();
    luaEmbeddedNulTest();
    luaModuleNumericGlobalTest();
    luaClearVariablesNumericStrayTest();
    chapterLoaderOrderAndReplaceTest();
    storyGraphChapterOrderTest();
    playerProfileDeepNestingTest();
}
