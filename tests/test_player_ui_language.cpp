/**
 * test_player_ui_language.cpp — D05-1: player UI language (Turkish with real
 * Turkish characters / English), UTF-8 safety of the menu path and the profile
 * "language not chosen" contract.
 *
 * Kilit:
 *  1. Yazi tablosu: her UiText id'si iki dilde dolu ve gecerli UTF-8; Turkce
 *     metinler dogru Turkce karakterle (bayt bayt "Geçmiş", "Duraklatıldı");
 *     dil secimi: locale bildirmeyen proje (manifestte locale anahtari yok)
 *     Turkce, "tr*" Turkce, diger her locale Ingilizce.
 *  2. Menu JSON: Turkce (locale'siz / tr) ve Ingilizce (en / de) tum sayfalar:
 *     duraklatma, kayit/yukleme yuvalari, baslik ekrani, Tercihler, Gecmis (bos
 *     dahil), degerler ("Kapalı"/"Off", "boş"/"empty", "2.0 sn"/"2.0 s"), ipuclari
 *     ve baslik ipucunun dil degisince yeni dilde cikmasi.
 *  3. Canli gecis: tr -> en -> tr, menu acikken bir sonraki karede yeni dil
 *     (bolge piksel farki; tr'ye donunce kare bit bit ayni, bayat kare yok).
 *  4. UTF-8 kesme: ozeti 48 bayt siniri cok baytli karakterin ortasina dusen
 *     yuva "detail" alani gecerli UTF-8 kalir; kontrol karakteri JSON'da \uXXXX.
 *  5. Profil: bos dil = "secilmedi" (yeni profil varsayilani), eski "en" acik
 *     secim, gecersiz etiket -> bos + uyari, "de-DE" / "pt_br" normalize, native
 *     yazdigi dosyayi geri okuyunca ayni; player'in uygulama karari
 *     (profileLanguageToApply): bos -> proje varsayilani, "en" acikca uygulanir,
 *     desteklenmeyen dil proje varsayilaninda birakir.
 *
 * Bilerek-boz: (a) uiLangForLocale'i her zaman Turkce yapmak 2-3'u, (b)
 * pauseSlotDetail'deki UTF-8 sinir dongusunu kaldirmak 4'u, (c)
 * profileLanguageToApply'in "en"i atlamasi 5'i, (d) normalizeProfileLanguage'i
 * yalniz en|tr kabul eder yapmak 5'i, (e) Engine'in dil onbellegi (m_titleHint
 * metin tutarsa) 2'deki ipucu denetimini kirmizi yapar.
 */
#include "rowl_test_harness.hpp"

#include "rowl/core/player_ui_strings.hpp"
#include "rowl/state/player_profile.hpp"
#include "rowl/state/profile_language.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Rowl::Core::UiLang;
using Rowl::Core::UiText;

constexpr const char* kSection = "player-ui-language";
constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 360;

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

bool validUtf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size()) return false;
        for (size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += length;
    }
    return true;
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

constexpr const char* kGraph = R"json({
  "format_version": 4,
  "start_node_id": 1,
  "nodes": [
    {"id": 1, "speaker": "A", "dialogue": "First line.", "next_nodes": [{"id": 2}]},
    {"id": 2, "speaker": "A", "dialogue": "Second line.", "next_nodes": [{"id": 3}]},
    {"id": 3, "speaker": "A", "dialogue": "Third line."}
  ]
})json";

struct Game {
    RowlEngineHandle h = nullptr;
    ~Game() {
        if (h) {
            RowlEngine_Shutdown(h);
            RowlEngine_Destroy(h);
        }
    }

    json menu() const {
        const char* text = RowlEngine_GetPauseMenuJson(h);
        const std::string raw = text ? text : "";
        req(validUtf8(raw), "pause menu JSON is not valid UTF-8: " + raw);
        json parsed = json::parse(raw, nullptr, false);
        req(parsed.is_object(), "pause menu JSON did not parse");
        return parsed;
    }
    void cmd(int command, int times = 1) const {
        for (int i = 0; i < times; ++i) RowlEngine_PauseMenuCommand(h, command);
    }
    void selectRow(int row) const {
        for (int i = 0; i < 16 && menu()["selected"].get<int>() != row; ++i) cmd(ROWL_PAUSE_MENU_DOWN);
        req(menu()["selected"].get<int>() == row, "could not select row " + std::to_string(row));
    }
    std::string frame(int steps = 2) const {
        for (int i = 0; i < steps; ++i) RowlEngine_Step(h, 0.016f);
        uint32_t w = 0, hh = 0;
        const uint8_t* pixels = RowlEngine_GetPixelBuffer(h, &w, &hh);
        req(pixels != nullptr && w == kWidth && hh == kHeight, "offscreen pixel buffer");
        return std::string(reinterpret_cast<const char*>(pixels), size_t(w) * hh * 4);
    }
    std::string locale() const { return capiText(h, RowlEngine_GetLocale); }
    void setLocale(const char* code) const {
        req(RowlEngine_SetLocale(h, code) == ROWL_RESULT_OK, std::string("SetLocale failed: ") + code);
    }
};

size_t differingPixels(const std::string& a, const std::string& b) {
    req(a.size() == b.size(), "frame size");
    size_t count = 0;
    for (size_t at = 0; at + 4 <= a.size(); at += 4) {
        if (a.compare(at, 4, b, at, 4) != 0) ++count;
    }
    return count;
}

// A project directory with a story, an optional locale declaration (raw
// manifest members) and one empty catalog per listed locale.
fs::path makeProject(const fs::path& root, const std::string& dir, const std::string& uuid,
                     const std::string& localeMembers, const std::vector<std::string>& catalogs) {
    const fs::path game = root / dir;
    writeText(game / "project.rowlproj",
              R"({"project_uuid":")" + uuid + R"(","name":"UI Lang )" + dir + R"(")" +
                  (localeMembers.empty() ? "" : "," + localeMembers) + "}");
    writeText(game / "Assets" / "json" / "full_story_graph.json", kGraph);
    for (const auto& locale : catalogs) {
        writeText(game / "Assets" / "locales" / (locale + ".json"),
                  R"({"schema_version":1,"locale":")" + locale + R"(","entries":{}})");
    }
    return game;
}

void openGame(Game& game, const fs::path& dir, bool play = true) {
    game.h = RowlEngine_Create();
    req(game.h && RowlEngine_Init(game.h, kWidth, kHeight, 0) == 1, "engine init failed");
    RowlEngine_SetProjectDirectory(game.h, dir.string().c_str());
    std::error_code ec;
    fs::create_directories(capiText(game.h, RowlEngine_GetSaveDirectoryUtf8), ec);
    if (play) {
        RowlEngine_SetPlayState(game.h, 1);
        RowlEngine_Step(game.h, 0.016f);
    }
}

// The player host's language step (engine/src/player/main.cpp), run against a
// handle: shared decision + SetLocale as the judge of "supported".
bool applyStoredLanguage(const Game& game, const std::string& stored) {
    const std::string target = Rowl::State::profileLanguageToApply(stored, game.locale());
    if (target.empty()) return false;
    return RowlEngine_SetLocale(game.h, target.c_str()) == ROWL_RESULT_OK;
}

struct Labels {
    const char* paused;
    const char* resume;
    const char* save;
    const char* load;
    const char* preferences;
    const char* history;
    const char* mainMenu;
    const char* quit;
};

// D05-2: the pause main page lists Resume, Save, Load, Preferences, History,
// Main Menu, Quit; mixer / speed rows live on the Preferences page.
const Labels kTr = {"Duraklatıldı", "Devam Et", "Oyunu Kaydet", "Oyunu Yükle", "Tercihler", "Geçmiş",
                    "Ana Menü", "Çıkış"};
const Labels kEn = {"Paused", "Resume", "Save Game", "Load Game", "Preferences", "History",
                    "Main Menu", "Quit"};

void expectPauseMain(const Game& game, const Labels& L, const std::string& what) {
    const json m = game.menu();
    req(m["mode"].get<int>() == 0 && m["rows"].size() == 7, what + ": pause main page shape");
    req(m["title"] == L.paused, what + ": title " + m["title"].dump());
    const char* expected[] = {L.resume, L.save, L.load, L.preferences, L.history, L.mainMenu, L.quit};
    for (int i = 0; i < 7; ++i) {
        req(m["rows"][i]["label"] == expected[i],
            what + ": row " + std::to_string(i) + " label " + m["rows"][i]["label"].dump());
    }
}

void checkTable() {
    using namespace Rowl::Core;
    for (unsigned id = 1; id < static_cast<unsigned>(UiText::Count); ++id) {
        const std::string tr = uiText(static_cast<UiText>(id), UiLang::Turkish);
        const std::string en = uiText(static_cast<UiText>(id), UiLang::English);
        req(!tr.empty() && !en.empty(), "UiText " + std::to_string(id) + " has an empty text");
        req(validUtf8(tr) && validUtf8(en), "UiText " + std::to_string(id) + " is not valid UTF-8");
    }
    req(std::string(uiText(UiText::None, UiLang::Turkish)).empty() &&
            std::string(uiText(UiText::None, UiLang::English)).empty(),
        "None is empty");
    req(std::string(uiText(UiText::Count, UiLang::Turkish)).empty(), "out-of-range id yields an empty string");
    // Byte-exact Turkish: ç ğ ı İ ş ü ö are real two-byte UTF-8 letters.
    req(std::string(uiText(UiText::History, UiLang::Turkish)) == "Ge\xC3\xA7mi\xC5\x9F", "Geçmiş bytes");
    req(std::string(uiText(UiText::Paused, UiLang::Turkish)) == "Duraklat\xC4\xB1ld\xC4\xB1", "Duraklatıldı bytes");
    req(std::string(uiText(UiText::Quit, UiLang::Turkish)) == "\xC3\x87\xC4\xB1k\xC4\xB1\xC5\x9F", "Çıkış bytes");
    req(std::string(uiText(UiText::SaveSlotTitle, UiLang::Turkish)) ==
            "Kay\xC4\xB1t Yuvas\xC4\xB1 Se\xC3\xA7",
        "Kayıt Yuvası Seç bytes");
    req(std::string(uiText(UiText::Off, UiLang::Turkish)) == "Kapal\xC4\xB1", "Kapalı bytes");
    req(std::string(uiText(UiText::Resume, UiLang::English)) == "Resume" &&
            std::string(uiText(UiText::Continue, UiLang::English)) == "Continue",
        "English Resume vs Continue differ");
    // Language choice.
    req(uiLangForLocale("en", false) == UiLang::Turkish, "no locale declaration keeps Turkish");
    req(uiLangForLocale("", false) == UiLang::Turkish, "no locale declaration, empty locale");
    req(uiLangForLocale("tr", true) == UiLang::Turkish && uiLangForLocale("tr-TR", true) == UiLang::Turkish &&
            uiLangForLocale("tr_TR", true) == UiLang::Turkish,
        "tr* is Turkish");
    for (const char* other : {"en", "en-us", "de", "ja", "trk", "t", ""}) {
        req(uiLangForLocale(other, true) == UiLang::English, std::string("'") + other + "' is English");
    }
}

void checkProfileRules(const fs::path& root) {
    using namespace Rowl::State;
    // Pure rule.
    struct Case {
        const char* in;
        const char* out;
        bool valid;
    };
    const Case cases[] = {
        {"", "", true},          {"   ", "", true},        {"en", "en", true},        {" EN ", "en", true},
        {"tr", "tr", true},      {"de-DE", "de-de", true}, {"pt_br", "pt-br", true},  {"zh-Hans-CN", "zh-hans-cn", true},
        {"fil", "fil", true},    {"es-419", "es-419", true},
        {"e", "", false},        {"english", "", false},   {"en-", "", false},        {"-en", "", false},
        {"en--us", "", false},   {"e1", "", false},        {"en-us!", "", false},     {"en us", "", false},
        {"abcdefgh-ijklmnop", "", false}, {"en-abcdefghi", "", false}, {"\xC3\xA7\xC3\xA7", "", false},
    };
    for (const auto& c : cases) {
        bool valid = false;
        const std::string out = normalizeProfileLanguage(c.in, &valid);
        req(out == c.out && valid == c.valid,
            std::string("normalizeProfileLanguage('") + c.in + "') -> '" + out + "' valid=" + (valid ? "1" : "0"));
    }
    // What the player does with the stored value.
    req(profileLanguageToApply("", "tr").empty(), "never chosen: nothing to apply");
    req(profileLanguageToApply("en", "tr") == "en", "explicit en is applied over a tr default");
    req(profileLanguageToApply("EN", "tr") == "en", "stored value is normalized first");
    req(profileLanguageToApply("tr", "tr").empty(), "already active: nothing to apply");
    req(profileLanguageToApply("not a tag!", "tr").empty(), "invalid: nothing to apply");

    // Store: defaults, old explicit choice, invalid, merge, round trip.
    const std::string id = "5c1d0a7e-2b3f-4a49-8d2e-1f6b9c0e4a11";
    {
        PlayerProfileStore fresh;
        fresh.open(root / "p_fresh", id);
        req(fresh.preferences().language.empty(), "a new profile starts with no language chosen");
        std::string error;
        req(fresh.setPreferencesJson(R"({"language":"de-DE"})", &error) == ProfileMutation::Ok &&
                fresh.preferences().language == "de-de",
            "merge normalizes de-DE: " + error);
        req(fresh.setPreferencesJson(R"({"language":"pt_br"})", &error) == ProfileMutation::Ok &&
                fresh.preferences().language == "pt-br",
            "merge normalizes pt_br");
        req(fresh.setPreferencesJson(R"({"language":"???"})", &error) == ProfileMutation::Ok &&
                fresh.preferences().language.empty(),
            "an invalid tag resets to not chosen");
        req(fresh.setPreferencesJson(R"({"language":""})", &error) == ProfileMutation::Unchanged &&
                fresh.preferences().language.empty(),
            "an empty language is valid and stays empty");
        req(fresh.setPreferencesJson(R"({"language":7})", &error) == ProfileMutation::Invalid, "non-string rejected");
    }
    {
        writeText(root / "p_old_en" / "player-profile.json", R"({"version":1,"language":"en"})");
        PlayerProfileStore store;
        store.open(root / "p_old_en", id);
        req(store.status() == PlayerProfileStatus::Loaded && store.preferences().language == "en" &&
                store.diagnostic().empty(),
            "an old file with en is an explicit choice: " + store.diagnostic());
        writeText(root / "p_bad" / "player-profile.json", R"({"version":1,"language":"Klingon Warrior"})");
        PlayerProfileStore bad;
        bad.open(root / "p_bad", id);
        req(bad.status() == PlayerProfileStatus::Loaded && bad.preferences().language.empty() &&
                bad.diagnostic().find("Invalid language") != std::string::npos,
            "an invalid tag loads as not chosen with a warning: " + bad.diagnostic());
        writeText(root / "p_other" / "player-profile.json", R"({"version":1,"language":"ja"})");
        PlayerProfileStore other;
        other.open(root / "p_other", id);
        req(other.preferences().language == "ja", "a tag outside en|tr is kept (the game decides support)");
    }
    {
        // Native writes, native reads: the value survives, including "".
        for (const char* value : {"", "en", "tr", "de-de", "pt-br"}) {
            const fs::path dir = root / (std::string("p_rt_") + (*value ? value : "empty"));
            PlayerProfileStore writer;
            writer.open(dir, id);
            std::string error;
            if (*value) {
                req(writer.setPreferencesJson(std::string(R"({"language":")") + value + R"("})", &error) ==
                        ProfileMutation::Ok,
                    "set language");
            } else {
                req(writer.setPreferencesJson(R"({"master_volume":0.5})", &error) == ProfileMutation::Ok, "set volume");
            }
            req(writer.flush(&error), "flush: " + error);
            const json written = json::parse(readText(dir / "player-profile.json"), nullptr, false);
            req(written.is_object() && written.value("language", "?") == value,
                std::string("language is written as-is: ") + written.dump());
            PlayerProfileStore reader;
            reader.open(dir, id);
            req(reader.preferences().language == value && reader.toJson() == writer.toJson(),
                std::string("round trip keeps '") + value + "'");
        }
    }
}

}  // namespace

void test_player_ui_language() {
    TEST_SECTION("Player UI language + UTF-8 menu path + profile language (D05-1)");

    const fs::path root = fs::temp_directory_path() / "rowl_player_ui_language";
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

    // 1. Table + language choice.
    checkTable();
    TEST_PASS("String table: every id filled in both languages, valid UTF-8, byte-exact Turkish; language choice rule");

    // 5 (first, no engine). Profile rules.
    checkProfileRules(root / "profiles");
    TEST_PASS("Profile language: empty = not chosen, old en explicit, invalid -> empty + warning, normalized tags, round trip");

    const fs::path noLocale = makeProject(root, "no_locale", "6a0f1d52-9c3e-4b7a-8e21-0d4c7b9a1f01", "", {});
    const fs::path trGame = makeProject(root, "tr_game", "6a0f1d52-9c3e-4b7a-8e21-0d4c7b9a1f02",
                                        R"("default_locale":"tr","supported_locales":["tr","en"])", {"tr", "en"});
    const fs::path enGame = makeProject(root, "en_game", "6a0f1d52-9c3e-4b7a-8e21-0d4c7b9a1f03",
                                        R"("default_locale":"en","supported_locales":["en","tr"])", {"en", "tr"});
    const fs::path deGame = makeProject(root, "de_game", "6a0f1d52-9c3e-4b7a-8e21-0d4c7b9a1f04",
                                        R"("default_locale":"de","supported_locales":["de","en"])", {"de", "en"});

    // 2a. A project without any locale declaration keeps the Turkish menus,
    //     even after a (legacy-accepted) SetLocale("en").
    {
        Game g;
        openGame(g, noLocale);
        RowlEngine_SetPaused(g.h, 1);
        expectPauseMain(g, kTr, "no-locale project");
        req(RowlEngine_SetLocale(g.h, "en") == ROWL_RESULT_OK, "SetLocale(en) on a locale-free project");
        expectPauseMain(g, kTr, "no-locale project after SetLocale(en)");
    }
    TEST_PASS("Locale-free project: Turkish pause menu with real Turkish characters");

    // 2b. Turkish default project: every page in both languages, live switch.
    {
        Game g;
        openGame(g, trGame);
        req(g.locale() == "tr", "project default locale is tr");
        RowlEngine_SetPaused(g.h, 1);
        expectPauseMain(g, kTr, "tr game");
        {
            const json m = g.menu();
            req(m["rows"][1]["value"] == "yuva seç >" && m["rows"][4]["value"] == "satırlar >" &&
                    m["rows"][3]["value"] == ">",
                "tr values");
            req(m["hint"] == "YUKARI/AŞAĞI: seç   ENTER: tamam   ESC: devam et", "tr hint");
        }

        // Two-step confirm texts follow the language too.
        g.selectRow(5);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(g.menu()["rows"][5]["value"] == "emin misin?" &&
                g.menu()["hint"].get<std::string>().find("ana menüye dön") != std::string::npos,
            "tr confirm texts");
        g.setLocale("en");
        req(g.menu()["rows"][5]["value"] == "are you sure?" &&
                g.menu()["hint"].get<std::string>().find("back to main menu") != std::string::npos,
            "en confirm texts follow a live switch");
        g.cmd(ROWL_PAUSE_MENU_BACK);

        // Live switch with the menu open: pixels change, and going back is bit-identical.
        g.setLocale("tr");
        g.selectRow(0);
        const std::string trFrame = g.frame(3);
        g.setLocale("en");
        const std::string enFrame = g.frame(1);
        req(differingPixels(trFrame, enFrame) > 200,
            "the open menu is redrawn in English on the next frame (" +
                std::to_string(differingPixels(trFrame, enFrame)) + " px differ)");
        expectPauseMain(g, kEn, "after live switch to en");
        req(g.frame(3) == enFrame, "English frame is stable");
        g.setLocale("tr");
        req(g.frame(1) == trFrame, "switching back restores the Turkish frame bit for bit (no stale frame)");
        g.setLocale("en");
        req(g.frame(1) == enFrame, "and the English frame again");

        // English pages.
        {
            const json m = g.menu();
            req(m["rows"][1]["value"] == "pick slot >" && m["rows"][4]["value"] == "lines >", "en values");
            req(m["hint"] == "UP/DOWN: select   ENTER: OK   ESC: resume", "en hint");
        }
        g.selectRow(1);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);  // save page
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 1 && m["title"] == "Choose Save Slot", "en save page title");
            req(m["rows"][0]["label"] == "Slot 0" && m["rows"][0]["value"] == "empty", "en slot row");
            req(m["hint"] == "ENTER/click: choose   0-9: pick directly   ESC: back", "en slot hint");
        }
        g.setLocale("tr");
        {
            const json m = g.menu();
            req(m["title"] == "Kayıt Yuvası Seç" && m["rows"][0]["label"] == "Yuva 0" && m["rows"][0]["value"] == "boş",
                "the open slot page switches live to Turkish");
            req(m["hint"] == "ENTER/tık: seç   0-9: doğrudan seç   ESC: geri", "tr slot hint");
        }
        g.cmd(ROWL_PAUSE_MENU_BACK);
        g.selectRow(2);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        req(g.menu()["title"] == "Yükleme Yuvası Seç", "tr load page title");
        g.setLocale("en");
        req(g.menu()["title"] == "Choose Load Slot", "en load page title");
        g.cmd(ROWL_PAUSE_MENU_BACK);

        // Backlog: empty and with rows.
        g.selectRow(4);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 5 && m["title"] == "History", "en backlog title");
        }
        g.setLocale("tr");
        req(g.menu()["title"] == "Geçmiş" && g.menu()["hint"] == "YUKARI/AŞAĞI: gez   ESC/B: kapat",
            "tr backlog title + hint");
        g.cmd(ROWL_PAUSE_MENU_BACK);
        RowlEngine_SetPaused(g.h, 0);

        // Title screen + Preferences.
        g.setLocale("tr");
        req(RowlEngine_OpenTitleScreen(g.h) == ROWL_RESULT_OK, "open title");
        {
            const json m = g.menu();
            req(m["rows"][0]["label"] == "Yeni Oyun" && m["rows"][1]["label"] == "Devam Et" &&
                    m["rows"][1]["value"] == "kayıt yok" && m["rows"][2]["label"] == "Oyunu Yükle" &&
                    m["rows"][3]["label"] == "Tercihler" && m["rows"][4]["label"] == "Çıkış",
                "tr title rows: " + m.dump());
            req(m["hint"] == "YUKARI/AŞAĞI: seç   ENTER: tamam", "tr title hint");
        }
        // A title hint is a key, not a frozen text: it follows the language.
        g.selectRow(1);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);  // Continue without saves
        req(g.menu()["hint"] == "devam için kayıt yok", "tr hint: " + g.menu()["hint"].dump());
        g.setLocale("en");
        req(g.menu()["hint"] == "no save to continue", "the title hint switches language with the locale");
        {
            const json m = g.menu();
            req(m["rows"][0]["label"] == "New Game" && m["rows"][1]["label"] == "Continue" &&
                    m["rows"][1]["value"] == "no save" && m["rows"][2]["label"] == "Load Game" &&
                    m["rows"][3]["label"] == "Preferences" && m["rows"][4]["label"] == "Quit",
                "en title rows: " + m.dump());
        }
        g.selectRow(3);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);
        {
            const json m = g.menu();
            req(m["mode"].get<int>() == 4 && m["title"] == "Preferences" && m["rows"].size() == 12, "en preferences");
            const char* en[] = {"Master Volume", "Music", "SFX", "Voice", "Text Speed", "Auto Speed", "Skip",
                                "Language", "Text Size", "High Contrast", "Reduce Motion", "Back"};
            for (int i = 0; i < 12; ++i) req(m["rows"][i]["label"] == en[i], std::string("en pref row ") + en[i]);
            req(m["rows"][8]["value"] == "100%" && m["rows"][9]["value"] == "Off" && m["rows"][10]["value"] == "Off",
                "en pref accessibility values");
            req(m["rows"][5]["value"] == "2.0 s" && m["rows"][6]["value"] == "Read only", "en pref values");
            req(m["hint"] == "UP/DOWN: select   LEFT/RIGHT: adjust   ESC: back", "en pref hint");
        }
        g.selectRow(6);
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(g.menu()["rows"][6]["value"] == "All", "en Skip All");
        g.cmd(ROWL_PAUSE_MENU_RIGHT);
        req(g.menu()["rows"][6]["value"] == "Off", "en Skip Off");
        g.setLocale("tr");
        {
            const json m = g.menu();
            req(m["title"] == "Tercihler" && m["rows"][4]["label"] == "Metin Hızı" &&
                    m["rows"][5]["label"] == "Otomatik Hızı" && m["rows"][5]["value"] == "2.0 sn" &&
                    m["rows"][6]["label"] == "Atlama" && m["rows"][6]["value"] == "Kapalı" &&
                    m["rows"][7]["label"] == "Dil" && m["rows"][8]["label"] == "Metin Boyutu" &&
                    m["rows"][9]["label"] == "Yüksek Kontrast" && m["rows"][9]["value"] == "Kapalı" &&
                    m["rows"][10]["label"] == "Hareketi Azalt" && m["rows"][11]["label"] == "Geri",
                "tr preferences after a live switch: " + m.dump());
        }
    }
    TEST_PASS("tr game: all pages Turkish, English after SetLocale(en), live switch redraws the next frame bit-exactly");

    // 2c. Empty backlog + English-default and German-default games.
    {
        Game g;
        openGame(g, enGame, /*play=*/false);
        req(g.locale() == "en", "en game default locale");
        req(RowlEngine_OpenBacklog(g.h) == ROWL_RESULT_OK, "open empty backlog");
        {
            const json m = g.menu();
            req(m["title"] == "History" && m["rows"].size() == 1 && m["rows"][0]["label"] == "History is empty",
                "en empty backlog: " + m.dump());
        }
        g.setLocale("tr");
        {
            const json m = g.menu();
            req(m["title"] == "Geçmiş" && m["rows"][0]["label"] == "Geçmiş boş", "tr empty backlog: " + m.dump());
        }
        g.cmd(ROWL_PAUSE_MENU_BACK);
        RowlEngine_SetPaused(g.h, 0);
        g.setLocale("en");
        RowlEngine_SetPlayState(g.h, 1);
        RowlEngine_Step(g.h, 0.016f);
        RowlEngine_SetPaused(g.h, 1);
        expectPauseMain(g, kEn, "en game");
    }
    {
        Game g;
        openGame(g, deGame);
        req(g.locale() == "de", "de game default locale");
        RowlEngine_SetPaused(g.h, 1);
        expectPauseMain(g, kEn, "de game uses the English menu");
    }
    TEST_PASS("en / de default games: English menu, empty backlog texts follow the locale");

    // 3. Stored profile language as the player applies it.
    {
        Game g;
        openGame(g, trGame);
        RowlEngine_SetPaused(g.h, 1);
        req(!applyStoredLanguage(g, ""), "not chosen: the game's tr stays");
        expectPauseMain(g, kTr, "not chosen");
        req(applyStoredLanguage(g, "en"), "an explicit en is applied over a tr default");
        req(g.locale() == "en", "locale is en");
        expectPauseMain(g, kEn, "explicit en");
        req(!applyStoredLanguage(g, "en"), "applied once: nothing left to do");
        req(applyStoredLanguage(g, "tr"), "explicit tr applied");
        expectPauseMain(g, kTr, "explicit tr");
        req(applyStoredLanguage(g, "EN-us") && g.locale() == "en", "en-US resolves to the game's en");
        g.setLocale("tr");
        req(RowlEngine_SetLocale(g.h, "ja") != ROWL_RESULT_OK, "ja is not a supported locale of this game");
        req(!applyStoredLanguage(g, "ja") && g.locale() == "tr", "an unsupported stored language keeps the project default");
        expectPauseMain(g, kTr, "unsupported ja");
    }
    TEST_PASS("Stored language: empty keeps the game default, explicit en applies, unsupported keeps the default");

    // 4. UTF-8 safe slot summary cut + control characters in JSON.
    {
        Game g;
        openGame(g, trGame);
        RowlEngine_Step(g.h, 0.016f);
        const fs::path saveDir = capiText(g.h, RowlEngine_GetSaveDirectoryUtf8);
        struct Summary {
            std::string text;
            const char* what;
        };
        const std::string ascii47(47, 'a');
        const std::string ascii46(46, 'a');
        const std::string ascii45(45, 'a');
        const std::vector<Summary> summaries = {
            {ascii47 + "\xC4\x9F" + "tail", "2-byte letter straddles byte 48"},
            {ascii46 + "\xE6\x97\xA5" + "tail", "3-byte letter straddles byte 48"},
            {ascii45 + "\xF0\x9F\x99\x82" + "tail", "4-byte letter straddles byte 48"},
            {std::string(30, 'x') + "\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7\xC3\xA7",
             "ends inside a run of 2-byte letters"},
        };
        for (size_t i = 0; i < summaries.size(); ++i) {
            const int slot = static_cast<int>(i) + 1;
            req(RowlEngine_SaveGameSlot(g.h, slot) == 1, "save slot");
            const fs::path file = saveDir / ("save_slot_" + std::to_string(slot) + ".json");
            json doc = json::parse(readText(file), nullptr, false);
            req(doc.is_object(), "slot file parse");
            doc["summary"] = summaries[i].text;
            doc["chapter_title"] = std::string("Ch\x01") + "ap\xC3\xA7";
            writeText(file, doc.dump());
        }
        RowlEngine_SetPaused(g.h, 1);
        g.selectRow(2);
        g.cmd(ROWL_PAUSE_MENU_CONFIRM);  // load page
        const char* raw = RowlEngine_GetPauseMenuJson(g.h);
        req(raw != nullptr && validUtf8(raw), "load page JSON is valid UTF-8");
        req(std::string(raw).find("Ch\\u0001ap\xC3\xA7") != std::string::npos,
            "a control character in a detail field is written as \\u0001");
        const json m = g.menu();
        for (size_t i = 0; i < summaries.size(); ++i) {
            const std::string detail = m["rows"][i + 1]["detail"].get<std::string>();
            req(validUtf8(detail), std::string("detail stays valid UTF-8 when the cut hits a multi-byte letter: ") +
                                       summaries[i].what);
            req(detail.size() >= 3 && detail.compare(detail.size() - 3, 3, "...") == 0,
                std::string("long summary is shortened with an ellipsis: ") + summaries[i].what);
        }
    }
    TEST_PASS("UTF-8: slot summary cut lands on a character boundary; control characters escape as \\uXXXX");

    RowlEngine_SetLocale(nullptr, "en");  // no-op sanity: null handle never throws
#ifdef _WIN32
    _putenv_s("XDG_DATA_HOME", savedXdg.c_str());
#else
    if (previousXdg) ::setenv("XDG_DATA_HOME", savedXdg.c_str(), 1);
    else ::unsetenv("XDG_DATA_HOME");
#endif
}
