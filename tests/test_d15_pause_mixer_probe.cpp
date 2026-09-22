/**
 * test_d15_pause_mixer_probe.cpp — D15 pause-menu/mixer dörtlüsü çıkarma kilidi.
 *
 * Bağımsız ikili: rowl_d15_pause_mixer_probe (ctest -R d15_pause_mixer_probe).
 * D02 konvansiyonu: yalnızca public C API + public Engine okumaları
 * (getGameState) + dummy driver (SDL_AUDIODRIVER=dummy,
 * SDL_VIDEODRIVER=dummy). rowl_tests gövdesine gömülmez ki kırmızı-yeşil
 * döngüsü tüm süiti koşmadan saniyeler içinde kanıtlansın.
 *
 * Kapsam (engine.cpp dörtlüsü — güncel ağaçta teyitli satırlar):
 *   2992-3027: Engine::pauseMenuVolume / setPauseMenuVolume /
 *               pauseMenuAdjustSelected
 *   3634-3642: Engine::commitMixerVolumesToGameState
 * Yeni TU: engine/src/core/engine_pause_mixer.cpp (D02 deseni: kod birebir
 * taşınır, engine.cpp'de yalnızca silme; window.cpp / MainWindowViewModel /
 * EngineHost dosyalarına kod YOK; yeni RowlEngine_ sembolü YOK).
 * D14 guard + D13 kilit-sırası konvansiyonuyla çelişmez: taşınan kod kilit
 * almaz, TU-local kalır (kazanç setter/getter'ları AudioEngine'in kendi
 * kilidinde; withMixerVolumes yapısal-paylaşım, kilitsiz).
 *
 * Bacaklar (çıkarma-öncesi YEŞİL baz = öngörülen gözlem + exit 0):
 *   (D) setter-commit: C API setter'ları state'e damgalar, step ilerlemez
 *       (#86; commitMixerVolumesToGameState yolu);
 *   (A) okuma eşlemesi: menü JSON satırları 3=master 4=bgm 5=sfx 6=voice
 *       yüzdelerini taşır (pauseMenuVolume yolu);
 *   (B) clamp [0,1]: menü-üzerinden üst/alt doyurma tam 1.0f/0.0f tutar;
 *       değer-olmayan satırda (0) SOL/SAĞ mikseri oynatmaz;
 *   (C) yazma eşlemesi: 3/4/5/6 satırlarının her birinde SAĞ ~+0.05 artırır
 *       (setPauseMenuVolume + pauseMenuAdjustSelected yolu) ve state damgası
 *       menü-ayarını izler (set→commit zinciri).
 *   0.25 katları ikili-tamdır (float-tam eşitlik); 0.05 adımları epsilonla
 *   (1e-5) karşılaştırılır. Bu gözlemler çıkarma-sonrası birebir aynı
 *   olmalıdır; fark YEŞİL'den KIRMIZI'ya dönüş = davranış sızıntısı.
 *
 * RED (çıkarma-arası): dörtlü engine.cpp'den silinip yeni TU CMake'e
 * eklenmeden DERLENİRSE tanımsız-referans bağlanma hatası (exit != 0) —
 * probun gerçekten taşınan kodu iğnelediğinin kanıtı.
 *
 * GREEN (çıkarma-sonrası): bacak gözlemleri bayt-birebir aynı + exit 0.
 *
 * KIRMIZI-GREEN SÖZLEŞMESİ: bacak gözlemlerinden herhangi biri değişirse
 * fix TURU sayılır (max 3); kırmızıda commit YOK.
 */
#include "rowl_test_harness.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void probeFail(const std::string& message) {
    rowlLockFail("d15-pause-mixer-probe", message);
}

void check(bool condition, const std::string& what) {
    if (!condition) probeFail(what);
}

bool near(float got, float want, float eps = 1e-5f) {
    return std::fabs(got - want) <= eps;
}

size_t countOccurrences(const std::string& haystack, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::string makeProjectRoot() {
    static int counter = 0;
    std::ostringstream name;
    name << "rowl_d15_probe_" << (++counter);
    auto dir = std::filesystem::temp_directory_path() / name.str();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

void down(RowlEngineHandle h, int n) {
    for (int i = 0; i < n; ++i) RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_DOWN);
}

void up(RowlEngineHandle h, int n) {
    for (int i = 0; i < n; ++i) RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_UP);
}

}  // namespace

int main() {
    TEST_SECTION("D15 pause-menu/mixer dortlusu cikarma kilidi");

    const std::string root = makeProjectRoot();
    RowlEngineHandle h = RowlEngine_Create();
    check(h != nullptr, "setup: RowlEngine_Create returned null");
    RowlEngine_SetProjectDirectory(h, root.c_str());
    check(RowlEngine_Init(h, 320, 180, 0) == 1,
          "setup: RowlEngine_Init(320x180) must succeed under dummy drivers");

    const std::string graphPath = root + "/two_node.json";
    {
        std::ofstream graph(graphPath);
        graph << R"({"format_version":4,"start_node_id":101,"nodes":[)"
                 R"({"id":101,"speaker":"Guide","dialogue":"First.","next_nodes":[{"id":102}]},)"
                 R"({"id":102,"speaker":"Guide","dialogue":"Second."}]})";
    }
    RowlEngine_LoadStoryGraph(h, graphPath.c_str());
    RowlEngine_Step(h, 0.0f);
    RowlEngine_AdvanceNode(h, 0);
    RowlEngine_Step(h, 0.0f);
    check(RowlEngine_GetCurrentNodeId(h) == 102, "setup: node != 102");

    Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(h);
    check(engine != nullptr, "setup: test bridge null");

    std::cout << std::fixed;
    std::cout.precision(9);

    // Kanonik ayırdedici kazançlar (0.25 katları ikili-tam).
    RowlEngine_SetMasterVolume(h, 0.5f);
    RowlEngine_SetBgmVolume(h, 0.25f);
    RowlEngine_SetSfxVolume(h, 0.75f);
    RowlEngine_SetVoiceVolume(h, 0.125f);

    // ── Bacak D: setter-commit (#86) — state damgası, step ilerlemez.
    {
        const uint64_t step0 = RowlEngine_GetCurrentStepId(h);
        const auto state = engine->getGameState();
        check(state != nullptr, "D: gameState null");
        std::cout << "  commit -> step=" << state->stepId
                  << " master=" << state->masterVolume
                  << " bgm=" << state->bgmVolume << " sfx=" << state->sfxVolume
                  << " voice=" << state->voiceVolume << std::endl;
        check(state->masterVolume == 0.5f && state->bgmVolume == 0.25f &&
                  state->sfxVolume == 0.75f && state->voiceVolume == 0.125f,
              "D: withMixerVolumes stamp mismatch");
        check(RowlEngine_GetCurrentStepId(h) == step0, "D: stepId moved");
    }
    TEST_PASS("D — commit damgasi + step-sabit (#86)");

    RowlEngine_SetPaused(h, 1);
    check(RowlEngine_IsPaused(h) == 1, "setup: SetPaused did not open menu");

    // ── Bacak A: okuma eşlemesi — JSON satır yüzdeleri kanalları tutar.
    {
        const std::string json = RowlEngine_GetPauseMenuJson(h);
        std::cout << "  menu json=" << json << std::endl;
        check(json.find("\"open\":true") != std::string::npos, "A: menu not open in JSON");
        check(json.find("50%") != std::string::npos, "A: master row != 50%");
        check(json.find("25%") != std::string::npos, "A: bgm row != 25%");
        check(json.find("75%") != std::string::npos, "A: sfx row != 75%");
        check(json.find("13%") != std::string::npos, "A: voice row != 13% (0.125)");
    }
    TEST_PASS("A — okuma eslemesi 3/4/5/6 (JSON yuzdeleri)");

    // ── Bacak B: clamp [0,1] + değer-olmayan satır no-op.
    RowlEngine_SetMasterVolume(h, 1.0f);
    down(h, 3);  // satır 3 (Ana Ses)
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_RIGHT);
    check(RowlEngine_GetMasterVolume(h) == 1.0f, "B: upper clamp lost (master)");
    RowlEngine_SetSfxVolume(h, 0.0f);
    down(h, 2);  // satır 5 (SFX)
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_LEFT);
    check(RowlEngine_GetSfxVolume(h) == 0.0f, "B: lower clamp lost (sfx)");
    up(h, 5);  // satır 0 (Devam Et) — değer satırı değil
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_LEFT);
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_RIGHT);
    check(RowlEngine_GetMasterVolume(h) == 1.0f, "B: row-0 LEFT/RIGHT moved master");
    check(RowlEngine_GetSfxVolume(h) == 0.0f, "B: row-0 LEFT/RIGHT moved sfx");
    check(RowlEngine_GetBgmVolume(h) == 0.25f, "B: row-0 LEFT/RIGHT moved bgm");
    check(RowlEngine_GetVoiceVolume(h) == 0.125f, "B: row-0 LEFT/RIGHT moved voice");
    std::cout << "  clamp/invalid -> master=" << RowlEngine_GetMasterVolume(h)
              << " sfx=" << RowlEngine_GetSfxVolume(h) << std::endl;
    TEST_PASS("B — clamp + deger-olmayan satir no-op");

    // ── Bacak C: yazma eşlemesi — 4 satırın her birinde SAĞ ~+0.05.
    RowlEngine_SetMasterVolume(h, 0.5f);
    RowlEngine_SetBgmVolume(h, 0.5f);
    RowlEngine_SetSfxVolume(h, 0.5f);
    RowlEngine_SetVoiceVolume(h, 0.5f);
    down(h, 3);  // satır 0 -> 3
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_RIGHT);
    check(near(RowlEngine_GetMasterVolume(h), 0.55f), "C: row3 RIGHT != ~0.55");
    down(h, 1);  // satır 4
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_RIGHT);
    check(near(RowlEngine_GetBgmVolume(h), 0.55f), "C: row4 RIGHT != ~0.55");
    down(h, 1);  // satır 5
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_RIGHT);
    check(near(RowlEngine_GetSfxVolume(h), 0.55f), "C: row5 RIGHT != ~0.55");
    down(h, 1);  // satır 6
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_RIGHT);
    check(near(RowlEngine_GetVoiceVolume(h), 0.55f), "C: row6 RIGHT != ~0.55");
    {
        const std::string json = RowlEngine_GetPauseMenuJson(h);
        std::cout << "  menu adjust -> master=" << RowlEngine_GetMasterVolume(h)
                  << " bgm=" << RowlEngine_GetBgmVolume(h)
                  << " sfx=" << RowlEngine_GetSfxVolume(h)
                  << " voice=" << RowlEngine_GetVoiceVolume(h) << std::endl;
        check(countOccurrences(json, "55%") == 4, "C: JSON 55% count != 4");
        // Menü-ayarı state damgasını izler (set→commit zinciri).
        const auto state = engine->getGameState();
        check(state != nullptr, "C: gameState null");
        check(near(state->masterVolume, 0.55f) && near(state->bgmVolume, 0.55f) &&
                  near(state->sfxVolume, 0.55f) && near(state->voiceVolume, 0.55f),
              "C: menu adjust did not commit to state");
    }
    RowlEngine_PauseMenuCommand(h, ROWL_PAUSE_MENU_LEFT);
    check(near(RowlEngine_GetVoiceVolume(h), 0.50f), "C: row6 LEFT != ~0.50");
    RowlEngine_SetPaused(h, 0);
    check(RowlEngine_IsPaused(h) == 0, "C: SetPaused(0) did not close menu");
    TEST_PASS("C — yazma eslemesi 3/4/5/6 + state-takibi");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("d15 pause-menu/mixer parite kilidi yesil");
    return 0;
}
