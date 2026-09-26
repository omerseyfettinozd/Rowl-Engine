/**
 * test_e2b_character_fx_red_probe.cpp — E2b kilidi: karakter-tween +
 * konusan-vurgusu + dudak-senkronu + expression-harmani + C2 snapshot yardimcisi.
 *
 * Bagimsiz ikili: rowl_e2b_character_fx_red_probe (ctest -R e2b_character_fx_red_probe).
 * D15 konvansiyonu: yalnizca public C API + public Engine okumalari
 * (testEngineFromHandle()->getFxComposedCharacters) + dummy driver
 * (SDL_AUDIODRIVER=dummy, SDL_VIDEODRIVER=dummy). rowl_tests govdesine
 * gomulmez ki kirmizi-yesil dongusu tum suiti kosmadan kanitlansin.
 *
 * Kapsam (E2b — yeni kod; kilitler):
 *   engine/include/rowl/scene/character_fx.hpp (header-only matematik/durum)
 *   engine/src/core/engine.cpp (E2b blogu: updateCharacterFx/composeCharacterFx)
 *   engine/src/c_api_character_fx.cpp (9 yeni ROWL_API sempolu)
 *
 * DokunulMAYanlar (kilit-disi pinler — prob duserse inceleme disi degil,
 * dogrudan RED sayilir):
 *   window.cpp / renderVisualNovelFrame imzasi / frame-hash+reuse kilidi (C0/D2),
 *   editor/ (C3), tools/abi_baseline.txt (224 holds — yeni semboller eklemeli).
 *
 * Bacaklar (YESIL baz = ongorulen gozlem + exit 0):
 *   (D) tween: idx0 x 100->500 linear 0.5sn; Step(0) oynatmaz; Step(0.25)
 *       x~=300 + progress 0.5; Step(0.25) x==500 + inaktif; pin korunur.
 *   (A) vurgu+C2: SetSpeakerFocus(0, 0.5) -> sunulan idx1 opaklik 0.5,
 *       idx0 1.0; snapshot JSON tasir; OOB/NaN red; -1 kapatir.
 *   (E) dudak: focus+playing+acilir-diyalogda odakli y oynar (en az bir
 *       adimda |dy|>0); kapatinca/kapaninca y==taban.
 *   (C) harman: BeginBlend(0.5) + yeni sahne -> Step(0.25)'te sunulan
 *       eski+yeni (2+1=3 girdi, ~0.5 olcekli); bitince yalniz yeni.
 *   (B) guard: olu-handle INVALID_HANDLE; yabanci-thread WRONG_THREAD;
 *       bos-sahnede tween/harman INVALID_ARGUMENT; Cancel(-1) red.
 *
 * RED (bilerek-boz): vurgu-carpani terslenirse (odakli solar) A kirmiziya
 * doner + exit 1 — probun gercekten uygulanan davranisi ignelediginin kaniti
 * (yalnizca durum-depolamayi degil, sunulan kareyi okur).
 *
 * GREEN: tum bacak gozlemleri + exit 0.
 *
 * KIRMIZI-YESIL SOZLESMESI: bacak gozlemlerinden herhangi biri degisirse
 * fix TURU sayilir (max 3); kirmizida commit YOK (bu dilimde commit YOK).
 */
#include "rowl_test_harness.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace {

void probeFail(const std::string& message) {
    rowlLockFail("e2b-character-fx-probe", message);
}

void check(bool condition, const std::string& what) {
    if (!condition) probeFail(what);
}

bool near(float got, float want, float eps = 1e-3f) {
    return std::fabs(got - want) <= eps;
}

std::string makeProjectRoot(const std::string& tag) {
    static int counter = 0;
    std::ostringstream name;
    name << "rowl_e2b_probe_" << tag << "_" << (++counter);
    auto dir = std::filesystem::temp_directory_path() / name.str();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir.string();
}

std::string queryFxSnapshot(RowlEngineHandle h) {
    uint32_t required = 0;
    if (RowlEngine_GetCharacterFxSnapshotJson(h, nullptr, 0, &required) !=
            ROWL_RESULT_OK ||
        required < 1) {
        probeFail("snapshot size query failed");
    }
    if (required > 1) {
        std::vector<char> small(required - 1, 'x');
        uint32_t repeated = 0;
        if (RowlEngine_GetCharacterFxSnapshotJson(h, small.data(),
                                                  static_cast<uint32_t>(small.size()),
                                                  &repeated) !=
                ROWL_RESULT_BUFFER_TOO_SMALL ||
            repeated != required || small.front() != '\0') {
            probeFail("snapshot undersized contract failed");
        }
    }
    std::vector<char> buffer(required, '\0');
    uint32_t repeated = 0;
    if (RowlEngine_GetCharacterFxSnapshotJson(h, buffer.data(),
                                              static_cast<uint32_t>(buffer.size()),
                                              &repeated) != ROWL_RESULT_OK ||
        repeated != required || buffer.back() != '\0') {
        probeFail("snapshot exact-size copy failed");
    }
    return std::string(buffer.data());
}

// 2 karakter + uzun (acilir) diyalog. x: a=100, b=1400; y=340; 360x540.
const char* kTwoCharScene = R"json([
    {"type":"dialogue","enabled":true,"data":{"speaker":"Efe","dialogue":"Bu uzun bir repliktir; typewriter adim adim acar, dudak-senkronu gozlemi icin bilerek uzun tutuldu. Yuzlerce milisaniye surmeli."}},
    {"type":"character","enabled":true,"data":{"sprite":"a.png","x":100,"y":340,"width":360,"height":540}},
    {"type":"character","enabled":true,"data":{"sprite":"b.png","x":1400,"y":340,"width":360,"height":540}}
])json";

const Rowl::Render::CharacterRenderData& composedAt(Rowl::Core::Engine* engine,
                                                    std::size_t index) {
    const auto& chars = engine->getFxComposedCharacters();
    if (index >= chars.size()) {
        probeFail("composed character index out of range");
    }
    return chars[index];
}

} // namespace

int main() {
    TEST_SECTION("E2b karakter-tween + konusan-vurgusu + dudak + harman kilidi");

    const std::string root = makeProjectRoot("main");
    RowlEngineHandle h = RowlEngine_Create();
    check(h != nullptr, "setup: RowlEngine_Create returned null");
    RowlEngine_SetProjectDirectory(h, root.c_str());
    check(RowlEngine_Init(h, 320, 180, 0) == 1,
          "setup: RowlEngine_Init(320x180) must succeed under dummy drivers");
    RowlEngine_SetPlayState(h, 1);
    RowlEngine_UpdateSceneFromJson(h, kTwoCharScene);
    RowlEngine_Step(h, 0.0f);

    Rowl::Core::Engine* engine = Rowl::Core::testEngineFromHandle(h);
    check(engine != nullptr, "setup: test bridge null");
    check(engine->getActiveCharacters().size() == 2,
          "setup: live scene must carry 2 characters");

    // Capability biti yanmali (alt bitler test_character_layers kilidinde).
    {
        uint64_t capabilities = 0;
        check(RowlEngine_GetCapabilities(&capabilities) == ROWL_RESULT_OK,
              "setup: GetCapabilities failed");
        check((capabilities & ROWL_ENGINE_CAPABILITY_CHARACTER_FX) != 0,
              "setup: capability bit 524288 missing");
    }

    // ── Bacak D: tween (linear 100->500, 0.5sn; Step 0.25 clamp'ine uygun) ──
    check(RowlEngine_CharacterTweenTo(h, 0, 500.0f, 340.0f, 360.0f, 540.0f,
                                      1.0f, 0.5f, 0) == ROWL_RESULT_OK,
          "D: CharacterTweenTo rejected");
    check(RowlEngine_IsCharacterTweenActive(h) == 1, "D: tween not active");
    RowlEngine_Step(h, 0.0f);
    check(near(composedAt(engine, 0).x, 100.0f), "D: Step(0) advanced tween");
    RowlEngine_Step(h, 0.25f);
    {
        const float x = composedAt(engine, 0).x;
        std::cout << "  tween mid x=" << x << std::endl;
        check(near(x, 300.0f), "D: tween mid x != ~300");
        const auto snap = nlohmann::json::parse(queryFxSnapshot(h));
        check(snap["tweens"].size() == 1 && snap["tweens"][0]["index"] == 0,
              "D: snapshot tween entry missing");
        check(near(snap["tweens"][0]["progress"].get<float>(), 0.5f),
              "D: snapshot progress != 0.5");
    }
    RowlEngine_Step(h, 0.25f);
    check(near(composedAt(engine, 0).x, 500.0f, 1e-4f), "D: tween end x != 500");
    check(RowlEngine_IsCharacterTweenActive(h) == 0, "D: tween still active");
    RowlEngine_Step(h, 0.25f);
    check(near(composedAt(engine, 0).x, 500.0f, 1e-4f), "D: pinned target lost");
    // Bilinmeyen easing + negatif sure red; durum korunur (pin yerinde).
    check(RowlEngine_CharacterTweenTo(h, 0, 0, 0, 0, 0, 1, 1.0f, 99) ==
              ROWL_RESULT_INVALID_ARGUMENT,
          "D: bogus easing accepted");
    check(RowlEngine_CharacterTweenTo(h, 0, 0, 0, 0, 0, 1, -1.0f, 0) ==
              ROWL_RESULT_INVALID_ARGUMENT,
          "D: negative duration accepted");
    check(near(composedAt(engine, 0).x, 500.0f, 1e-4f), "D: red input moved pin");
    check(RowlEngine_CancelCharacterTween(h, 0) == ROWL_RESULT_OK,
          "D: cancel rejected");
    RowlEngine_Step(h, 0.0f);
    check(near(composedAt(engine, 0).x, 100.0f), "D: cancel did not restore live");
    TEST_PASS("D — tween ilerleme + pin + red-girdiler");

    // ── Bacak A: konusan-vurgusu + C2 snapshot ──
    check(RowlEngine_SetSpeakerFocus(h, 0, 0.5f) == ROWL_RESULT_OK,
          "A: SetSpeakerFocus rejected");
    {
        int index = -9;
        float dim = 0.0f;
        check(RowlEngine_GetSpeakerFocus(h, &index, &dim) == ROWL_RESULT_OK,
              "A: GetSpeakerFocus failed");
        check(index == 0 && near(dim, 0.5f), "A: focus readback mismatch");
    }
    RowlEngine_Step(h, 0.0f);
    {
        const float o0 = composedAt(engine, 0).opacity;
        const float o1 = composedAt(engine, 1).opacity;
        std::cout << "  focus opacities: [" << o0 << ", " << o1 << "]" << std::endl;
        check(near(o0, 1.0f), "A: focused character dimmed (must stay 1.0)");
        check(near(o1, 0.5f), "A: unfocused character not dimmed to 0.5");
    }
    {
        const auto snap = nlohmann::json::parse(queryFxSnapshot(h));
        check(snap["focus"]["enabled"] == true && snap["focus"]["index"] == 0 &&
                  near(snap["focus"]["dim"].get<float>(), 0.5f),
              "A: snapshot focus mismatch: " + snap.dump());
        check(snap["characters"] == 2, "A: snapshot character count != 2");
    }
    check(RowlEngine_SetSpeakerFocus(h, 7, 0.5f) == ROWL_RESULT_INVALID_ARGUMENT,
          "A: out-of-range focus accepted");
    check(RowlEngine_SetSpeakerFocus(h, 0, std::nanf("")) ==
              ROWL_RESULT_INVALID_ARGUMENT,
          "A: NaN dim accepted");
    check(RowlEngine_SetSpeakerFocus(h, -2, 0.5f) == ROWL_RESULT_INVALID_ARGUMENT,
          "A: focus index -2 accepted");
    check(RowlEngine_SetSpeakerFocus(h, -1, 0.5f) == ROWL_RESULT_OK,
          "A: focus-off rejected");
    RowlEngine_Step(h, 0.0f);
    check(near(composedAt(engine, 1).opacity, 1.0f),
          "A: focus-off did not restore opacity");
    check(RowlEngine_SetSpeakerFocus(h, 0, 0.5f) == ROWL_RESULT_OK,
          "A: focus re-enable rejected");
    TEST_PASS("A — konusan-vurgusu + C2 snapshot + red-girdiler");

    // ── Bacak E: dudak-senkronu (odakli + acilir diyalogda y oynar) ──
    check(RowlEngine_SetLipSyncEnabled(h, 1) == ROWL_RESULT_OK,
          "E: SetLipSyncEnabled rejected");
    check(RowlEngine_IsLipSyncEnabled(h) == 1, "E: lip-sync not enabled");
    {
        bool moved = false;
        for (int i = 0; i < 10; ++i) {
            RowlEngine_Step(h, 0.05f);
            if (std::fabs(composedAt(engine, 0).y - 340.0f) > 1e-6f) {
                moved = true;
                break;
            }
        }
        std::cout << "  lipsync moved=" << (moved ? "yes" : "no") << std::endl;
        check(moved, "E: focused y never moved while dialogue reveals");
        check(near(composedAt(engine, 1).y, 340.0f),
              "E: unfocused character bobbed");
    }
    check(RowlEngine_SetLipSyncEnabled(h, 0) == ROWL_RESULT_OK,
          "E: lip-sync disable rejected");
    check(RowlEngine_IsLipSyncEnabled(h) == 0, "E: lip-sync still enabled");
    RowlEngine_Step(h, 0.05f);
    check(near(composedAt(engine, 0).y, 340.0f),
          "E: y != base after lip-sync off");
    TEST_PASS("E — dudak-senkronu secici-salinin + kapatma");

    // ── Bacak C: expression-harmani (2 canli -> 1 yeni, 0.5sn; Step kelepcesi: 0.25'lik adimlar) ──
    check(RowlEngine_BeginCharacterExpressionBlend(h, 0.5f) == ROWL_RESULT_OK,
          "C: BeginBlend rejected");
    check(RowlEngine_BeginCharacterExpressionBlend(h, 0.0f) ==
              ROWL_RESULT_INVALID_ARGUMENT,
          "C: zero-duration blend accepted");
    RowlEngine_UpdateSceneFromJson(h, R"json([
        {"type":"dialogue","enabled":true,"data":{"speaker":"Efe","dialogue":"Yeni sahne."}},
        {"type":"character","enabled":true,"data":{"sprite":"c.png","x":700,"y":340,"width":360,"height":540}}
    ])json");
    RowlEngine_Step(h, 0.25f);
    {
        const auto& chars = engine->getFxComposedCharacters();
        std::cout << "  blend mid count=" << chars.size() << std::endl;
        check(chars.size() == 3, "C: blended count != old(2)+new(1)");
        check(near(chars[0].opacity, 0.5f) && near(chars[1].opacity, 0.5f) &&
                  near(chars[2].opacity, 0.5f),
              "C: blend mid opacities != ~0.5");
        const auto snap = nlohmann::json::parse(queryFxSnapshot(h));
        check(snap["blend"]["active"] == true &&
                  near(snap["blend"]["progress"].get<float>(), 0.5f) &&
                  snap["blend"]["old_count"] == 2 &&
                  snap["blend"]["blended_count"] == 3,
              "C: snapshot blend mismatch: " + snap.dump());
    }
    RowlEngine_Step(h, 0.25f);
    {
        const auto& chars = engine->getFxComposedCharacters();
        check(chars.size() == 1 && chars[0].sprite == "c.png",
              "C: blend did not settle to live scene");
        check(near(chars[0].opacity, 1.0f), "C: settled opacity != 1.0");
    }
    TEST_PASS("C — expression-harmani + C2 gozlemi");

    // ── Bacak B: guard'lar (olu / yabanci / bos-sahne) ──
    {
        RowlEngineHandle dead = RowlEngine_Create();
        check(dead != nullptr, "B: second handle null");
        RowlEngine_Destroy(dead);
        check(RowlEngine_CharacterTweenTo(dead, 0, 0, 0, 0, 0, 1, 1.0f, 0) ==
                  ROWL_RESULT_INVALID_HANDLE,
              "B: dead tween not INVALID_HANDLE");
        check(RowlEngine_SetSpeakerFocus(dead, 0, 0.5f) ==
                  ROWL_RESULT_INVALID_HANDLE,
              "B: dead focus not INVALID_HANDLE");
        check(RowlEngine_BeginCharacterExpressionBlend(dead, 1.0f) ==
                  ROWL_RESULT_INVALID_HANDLE,
              "B: dead blend not INVALID_HANDLE");
        uint32_t required = 0;
        check(RowlEngine_GetCharacterFxSnapshotJson(dead, nullptr, 0,
                                                    &required) ==
                  ROWL_RESULT_INVALID_HANDLE,
              "B: dead snapshot not INVALID_HANDLE");
        check(RowlEngine_IsCharacterTweenActive(dead) == 0,
              "B: dead IsActive != 0");
        check(RowlEngine_IsLipSyncEnabled(dead) == 0, "B: dead lip != 0");
    }
    {
        RowlEngine_ResultCode foreignRc = ROWL_RESULT_OK;
        std::thread foreign([&] {
            foreignRc = RowlEngine_CancelCharacterTween(h, 0);
        });
        foreign.join();
        check(foreignRc == ROWL_RESULT_WRONG_THREAD,
              "B: foreign-thread cancel not WRONG_THREAD");
    }
    {
        const std::string root2 = makeProjectRoot("empty");
        RowlEngineHandle h2 = RowlEngine_Create();
        check(h2 != nullptr, "B: empty handle null");
        RowlEngine_SetProjectDirectory(h2, root2.c_str());
        check(RowlEngine_Init(h2, 320, 180, 0) == 1, "B: empty Init failed");
        // cwd'deki story-graph yedegine dusmeyi engellemek icin sahneyi
        // acikca bosalt (ctest WORKING_DIRECTORY kaynaktir).
        RowlEngine_UpdateSceneFromJson(h2, "[]");
        RowlEngine_Step(h2, 0.0f);
        check(RowlEngine_CharacterTweenTo(h2, 0, 0, 0, 0, 0, 1, 1.0f, 0) ==
                  ROWL_RESULT_INVALID_ARGUMENT,
              "B: empty-scene tween accepted");
        check(RowlEngine_BeginCharacterExpressionBlend(h2, 1.0f) ==
                  ROWL_RESULT_INVALID_ARGUMENT,
              "B: empty-scene blend accepted");
        check(RowlEngine_CancelCharacterTween(h2, -1) ==
                  ROWL_RESULT_INVALID_ARGUMENT,
              "B: Cancel(-1) accepted");
        check(RowlEngine_GetSpeakerFocus(h2, nullptr, nullptr) ==
                  ROWL_RESULT_INVALID_ARGUMENT,
              "B: null out-params accepted");
        RowlEngine_Shutdown(h2);
        RowlEngine_Destroy(h2);
    }
    TEST_PASS("B — olu/yabanci/bos-sahne guard'lari");

    RowlEngine_Shutdown(h);
    RowlEngine_Destroy(h);

    TEST_PASS("e2b karakter-tween + konusan-vurgusu parite kilidi yesil");
    return 0;
}
