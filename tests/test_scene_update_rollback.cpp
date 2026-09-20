/**
 * test_scene_update_rollback.cpp — Bulgular #20+#21 [HIGH] hybrid-state probe.
 *
 * updateSceneFromComponents hata-kurtarmasi: veri geri alinirken try-ici
 * uygulanmis kamera/FX/ses/script yan-etkileri geri alinmiyordu (#20) ve
 * ozelinde script kumes + teshisler geri gelmiyordu — sessiz olu script
 * (#21). Ayrica catch yalniz LOG basar, gozlemlenebilir sinyal yoktu.
 *
 * Kirmizi-kanit imzasi (fix'siz): somut tetik comp listesi UpdateScene
 * girisinden surulur (once gecerli kamera/FX/(ses) uygulamasi + sonra
 * firlatan bilesen); catch sonrasi HIBRIT damga gozlenir — sahne-vektor
 * restore + kamera/ses restore, ama script kumes OLU (status bos, teshis
 * bos), FX GERI ALINMAMIS (flash aktif / tint / vignette), parallax/opaklik
 * sapmis, sonuc kodu bayat OK.
 *
 * Yesil-kriter (fix'li): ya tam-restore ya hic-uygulama (atomik faz) +
 * gozlemlenebilir hata sinyali (GetLastResultCode == VALIDATION_ERROR).
 *
 * Tetikler (ikisi de isSafeComponentData'dan gecer, try-ici mutasyondan
 * SONRA firlatir):
 *   A. [camera-gecerli + screen_fx-gecerli + background + speaker +
 *       variable-add value:"abc"] — std::stod throw'u.
 *   B. [audio-gecerli + script code:123] — script.value("code","")
 *       type_error'i (integer, kisa sayi diye gecer).
 */
#include "rowl_test_harness.hpp"

#include <filesystem>
#include <string>

namespace {

struct RollbackCwd {
    std::filesystem::path saved;
    std::filesystem::path dir;
    bool ok = false;
    explicit RollbackCwd(const std::string& tag) {
        std::error_code ec;
        saved = std::filesystem::current_path(ec);
        if (ec) return;
        dir = std::filesystem::temp_directory_path() / tag;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
        if (ec) return;
        std::filesystem::current_path(dir, ec);
        ok = !ec;
    }
    ~RollbackCwd() {
        std::error_code ec;
        if (ok) std::filesystem::current_path(saved, ec);
        std::filesystem::remove_all(dir, ec);
    }
};

void rollbackCheck(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "#20/#21: " << what << std::endl;
        std::exit(1);
    }
}

RowlEngineHandle rollbackCreateEngine(const std::string& root) {
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) {
        std::cerr << "#20/#21: Create failed" << std::endl;
        std::exit(1);
    }
    RowlEngine_SetProjectDirectory(handle, root.c_str());
    if (!RowlEngine_Init(handle, 320, 180, 0)) {
        std::cerr << "#20/#21: Init failed" << std::endl;
        std::exit(1);
    }
    return handle;
}

} // namespace

void test_scene_update_rollback() {
    TEST_SECTION("Scene-Update Atomic Rollback (#20/#21)");
    RollbackCwd cwd("rowl_scene_update_rollback");
    if (!cwd.ok) {
        std::cerr << "#20/#21: CWD pin failed" << std::endl;
        std::exit(1);
    }

    RowlEngineHandle handle = rollbackCreateEngine(cwd.dir.string());
    auto* engine = Rowl::Core::testEngineFromHandle(handle);
    rollbackCheck(engine != nullptr && engine->getAudio() != nullptr &&
                      engine->getCamera() != nullptr && engine->getWindow() != nullptr,
                  "engine/audio/camera/window missing");
    auto* window = engine->getWindow();
    auto* camera = engine->getCamera();
    auto* audio = engine->getAudio();

    // Tohum: bilinen sahne + kamera + FX-sessiz + calisan inline script.
    // (Kod iddiasi yok: basari-sinyali asagida re-seed ile pinlenir.)
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"background","id":"bg","enabled":true,
         "data":{"texture":"bg_seed.png","opacity":0.9,"parallax_x":2.0,"parallax_y":3.0}},
        {"type":"camera","id":"cam","enabled":true,
         "data":{"x":100.0,"y":200.0,"zoom":2.0}},
        {"type":"dialogue","id":"dlg","enabled":true,
         "data":{"speaker":"Seed","dialogue":"Seed line."}},
        {"type":"script","id":"s1","enabled":true,
         "data":{"code":"return {}"}}
    ])");
    rollbackCheck(engine->getActiveBackground() == "bg_seed.png", "seed bg");
    rollbackCheck(engine->getActiveBackgroundOpacity() == 0.9f, "seed opacity");
    rollbackCheck(engine->getActiveBackgroundParallaxX() == 2.0f &&
                      engine->getActiveBackgroundParallaxY() == 3.0f,
                  "seed parallax");
    rollbackCheck(camera->getPositionX() == 100.0f && camera->getPositionY() == 200.0f,
                  "seed camera pos");
    rollbackCheck(camera->getZoom() == 2.0f, "seed camera zoom");
    rollbackCheck(!window->isScreenFlashActive() && !window->hasScreenTint() &&
                      !window->isVignetteActive(),
                  "seed must leave FX quiet");
    rollbackCheck(engine->getScriptRuntimeStatuses().size() == 1 &&
                      engine->getScriptRuntimeStatuses()[0].state == "running" &&
                      engine->getScriptRuntimeStatuses()[0].moduleId == "inline#0",
                  "seed script must be running as inline#0");
    rollbackCheck(audio->getActiveFilter() == Rowl::Audio::DSPFilterType::Normal,
                  "seed audio filter");

    // Prob A: kamera + FX + background + speaker gecerli, SONRA
    // variable-add value:"abc" stod throw'u.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"camera","id":"cam","enabled":true,
         "data":{"x":500.0,"y":500.0,"zoom":3.0,"shake_preset":"explosion"}},
        {"type":"screen_fx","id":"fx","enabled":true,
         "data":{"flash_enabled":true,"flash_color":"#FF0000","flash_duration":5.0,
                 "flash_intensity":1.0,"tint_enabled":true,"tint_color":"#00FF00",
                 "tint_opacity":0.5,"vignette_enabled":true,"vignette_intensity":0.6}},
        {"type":"background","id":"bg","enabled":true,
         "data":{"texture":"bg_poison.png","opacity":0.2,"parallax_x":-2.0,"parallax_y":-3.0}},
        {"type":"speaker","id":"sp","enabled":true,
         "data":{"speaker":"Poison","dialogue":"Poison line."}},
        {"type":"variable","id":"v","enabled":true,
         "data":{"key":"k","operation":"add","value":"abc"}}
    ])");

    // Gozlemlenebilir sinyal en sonda: hibrit damga once pinlenir
    // (vektor-restore + sag-kalan uye), sinyal-son.
    rollbackCheck(engine->getActiveBackground() == "bg_seed.png", "A: background not restored");
    rollbackCheck(engine->getActiveSpeaker() == "Seed" &&
                      engine->getActiveDialogue() == "Seed line.",
                  "A: speaker/dialogue not restored");
    rollbackCheck(engine->getActiveBackgroundOpacity() == 0.9f, "A: opacity not restored");
    rollbackCheck(engine->getActiveBackgroundParallaxX() == 2.0f &&
                      engine->getActiveBackgroundParallaxY() == 3.0f,
                  "A: parallax not restored");
    // Kamera: hic-uygulanmis gibi temiz.
    rollbackCheck(camera->getPositionX() == 100.0f && camera->getPositionY() == 200.0f,
                  "A: camera position not restored");
    rollbackCheck(camera->getZoom() == 2.0f, "A: camera zoom not restored");
    rollbackCheck(!camera->isShaking(), "A: camera shake leaked");
    // FX: hic-uygulanmis gibi sessiz.
    rollbackCheck(!window->isScreenFlashActive(), "A: screen flash not rolled back");
    rollbackCheck(!window->hasScreenTint() && window->getScreenTintOpacity() == 0.0f,
                  "A: screen tint not rolled back");
    rollbackCheck(!window->isVignetteActive() && window->getVignetteIntensity() == 0.0f,
                  "A: vignette not rolled back");
    // #21: script kumesi + teshisler sag — sessiz olu script yok.
    rollbackCheck(engine->getScriptRuntimeStatuses().size() == 1 &&
                      engine->getScriptRuntimeStatuses()[0].state == "running" &&
                      engine->getScriptRuntimeStatuses()[0].moduleId == "inline#0",
                  "A: script set dead (statuses lost)");
    {
        const char* diag = RowlEngine_GetScriptRuntimeDiagnosticsJson(handle);
        const std::string diagnostics = diag ? diag : "";
        rollbackCheck(diagnostics.find("inline#0") != std::string::npos &&
                          diagnostics.find("running") != std::string::npos,
                      "A: script diagnostics show dead/partial set: " + diagnostics);
    }
    // Gozlemlenebilir sinyal: throw yolu VALIDATION_ERROR isler (bayat OK degil).
    rollbackCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_VALIDATION_ERROR,
                  "A: failed update must signal VALIDATION_ERROR, got code " +
                      std::to_string(RowlEngine_GetLastResultCode(handle)));
    TEST_PASS("Scene rollback probe A — variable-add throw leaves zero residue + signal");

    // Re-seed: basari-sinyalini pinler (OK) ve Prob B tabanini kurar.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"background","id":"bg","enabled":true,
         "data":{"texture":"bg_seed.png","opacity":0.9,"parallax_x":2.0,"parallax_y":3.0}},
        {"type":"camera","id":"cam","enabled":true,
         "data":{"x":100.0,"y":200.0,"zoom":2.0}},
        {"type":"dialogue","id":"dlg","enabled":true,
         "data":{"speaker":"Seed","dialogue":"Seed line."}},
        {"type":"script","id":"s1","enabled":true,
         "data":{"code":"return {}"}}
    ])");
    rollbackCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                  "re-seed must report OK, got code " +
                      std::to_string(RowlEngine_GetLastResultCode(handle)));
    rollbackCheck(engine->getScriptRuntimeStatuses().size() == 1 &&
                      engine->getScriptRuntimeStatuses()[0].state == "running",
                  "re-seed script not running");

    // Prob B: audio-gecerli + script code:123 (type_error). Ses restore +
    // script sag + sinyal.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"audio","id":"a","enabled":true,
         "data":{"dsp_filter":"Telephone","volume":0.9}},
        {"type":"script","id":"s2","enabled":true,
         "data":{"code":123}}
    ])");
    rollbackCheck(audio->getActiveFilter() == Rowl::Audio::DSPFilterType::Normal,
                  "B: audio DSP filter not restored");
    rollbackCheck(audio->getBgmVolume() == 1.0f, "B: audio bgm volume not restored");
    rollbackCheck(engine->getScriptRuntimeStatuses().size() == 1 &&
                      engine->getScriptRuntimeStatuses()[0].state == "running" &&
                      engine->getScriptRuntimeStatuses()[0].moduleId == "inline#0",
                  "B: script set dead after code:123 (statuses lost)");
    {
        const char* diag = RowlEngine_GetScriptRuntimeDiagnosticsJson(handle);
        const std::string diagnostics = diag ? diag : "";
        rollbackCheck(diagnostics.find("inline#0") != std::string::npos &&
                          diagnostics.find("running") != std::string::npos,
                      "B: script diagnostics show dead/partial set: " + diagnostics);
    }
    rollbackCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_VALIDATION_ERROR,
                  "B: failed update must signal VALIDATION_ERROR, got code " +
                      std::to_string(RowlEngine_GetLastResultCode(handle)));
    rollbackCheck(engine->getActiveBackground() == "bg_seed.png", "B: background moved");
    rollbackCheck(camera->getPositionX() == 100.0f && camera->getZoom() == 2.0f,
                  "B: camera moved");
    TEST_PASS("Scene rollback probe B — script code:123 keeps live set + restores audio + signal");

    RowlEngine_Destroy(handle);
}
