/**
 * test_numeric_guards.cpp — B6 numeric-guard slice adversarial tests.
 *
 * Every test pins CWD to an empty temp dir (RAII): the engine's physical
 * story fallback probes the process CWD, and the suite CWD (repo root)
 * carries a real graph that would mask misses and perturb timing.
 */
#include "rowl_test_harness.hpp"
#include "rowl/scene/sprite_component.hpp"
#include "rowl/scene/transform_component.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace {

const float kNaN = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();

struct ScopedEmptyCwd {
    std::filesystem::path saved;
    std::filesystem::path dir;
    bool ok = false;
    explicit ScopedEmptyCwd(const std::string& tag) {
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
    ~ScopedEmptyCwd() {
        std::error_code ec;
        if (ok) std::filesystem::current_path(saved, ec);
        std::filesystem::remove_all(dir, ec);
    }
};

RowlEngineHandle makeBareEngine() {
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle || !RowlEngine_Init(handle, 320, 180, 0)) {
        std::cerr << "B6 setup: bare init failed" << std::endl;
        exit(1);
    }
    return handle;
}

const char* kDialogueJson = R"([
    {"type": "dialogue", "enabled": true, "data": {
        "speaker": "S", "dialogue": "01234567890123456789",
        "typewriter_enabled": true, "text_speed": 30}}
])";

int frameStaticWithin(RowlEngineHandle handle, int maxSteps, float dt = 1.0f) {
    for (int i = 0; i < maxSteps; ++i) {
        RowlEngine_Step(handle, dt);
        if (RowlEngine_IsPreviewFrameStatic(handle)) return i + 1;
    }
    return -1;
}

// B6 (#1/#12): a huge text_speed must clamp to [1,1000] so the reveal cursor
// stays reachable; unfixed code locks the typewriter forever (never static).
void testHugeTextSpeedClamps() {
    TEST_SECTION("B6 Huge text_speed clamps, typewriter completes");
    ScopedEmptyCwd cwd("rowl_b6_cwd_speed");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    RowlEngineHandle handle = makeBareEngine();
    // Completion reports true while paused: play state is required for the
    // static-frame signal to mean anything (elapsed animates regardless).
    RowlEngine_SetPlayState(handle, 1);
    // NOTE: step() clamps dt to 0.25 s, so each 1.0 s step advances 0.25 s.
    // 10 chars at clamped 1000 ms/char = 10 s of play-time (40 steps).
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type": "dialogue", "enabled": true, "data": {
            "speaker": "S", "dialogue": "0123456789",
            "typewriter_enabled": true, "text_speed": 5000}}
    ])");
    // 5000 passes the 1e6 component-magnitude gate but exceeds the [1,1000]
    // clamp; unfixed code needs 50 s (200 steps), so bound 60 discriminates.
    if (frameStaticWithin(handle, 60) < 0) {
        std::cerr << "B6: huge text_speed never completed (typewriter lock)" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 Huge text_speed clamps, typewriter completes");
}

// B6 (#1/#12, negative arm): a negative text_speed must fall back into
// [1,1000] instead of silently disabling the typewriter. Disabled typewriter
// reports complete on the first step; fallback speed=1 needs 20 ms of play
// for 20 chars, so one 1 ms step must NOT be static (fixed) while unfixed
// code is instantly static.
void testNegativeTextSpeedFallsBack() {
    TEST_SECTION("B6 Negative text_speed falls back, typewriter stays enabled");
    ScopedEmptyCwd cwd("rowl_b6_cwd_negspeed");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    RowlEngineHandle handle = makeBareEngine();
    // Completion reports true while paused: play state is required for the
    // static-frame signal to mean anything (elapsed animates regardless).
    RowlEngine_SetPlayState(handle, 1);
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type": "dialogue", "enabled": true, "data": {
            "speaker": "S", "dialogue": "01234567890123456789",
            "typewriter_enabled": true, "text_speed": -50}}
    ])");
    // 20 chars at fallback speed need more than one 1 ms step to complete.
    RowlEngine_Step(handle, 0.001f);
    if (RowlEngine_IsPreviewFrameStatic(handle)) {
        std::cerr << "B6: negative text_speed disabled the typewriter (instantly static)"
                  << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    if (frameStaticWithin(handle, 40) < 0) {
        std::cerr << "B6: fallback text_speed never completed" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 Negative text_speed falls back, typewriter stays enabled");
}

// B6 (#2): NaN/Inf multiplier must keep the last-good value; unfixed code
// poisons elapsedTypewriterTime to NaN and the reveal never completes.
void testNonFiniteMultiplierKeepsLastGood() {
    TEST_SECTION("B6 Non-finite multiplier keeps last-good value");
    ScopedEmptyCwd cwd("rowl_b6_cwd_mult");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    for (float poison : {kNaN, kInf}) {
        RowlEngineHandle handle = makeBareEngine();
        RowlEngine_UpdateSceneFromJson(handle, kDialogueJson);
        RowlEngine_SetTextSpeedMultiplier(handle, 4.0f);
        RowlEngine_SetTextSpeedMultiplier(handle, poison);
        // 20 chars at 30 ms / 4x = 150 ms of play-time; generous bound.
        if (frameStaticWithin(handle, 20, 0.05f) < 0) {
            std::cerr << "B6: poisoned multiplier stuck the typewriter" << std::endl;
            RowlEngine_Destroy(handle);
            exit(1);
        }
        RowlEngine_Destroy(handle);
    }
    TEST_PASS("B6 Non-finite multiplier keeps last-good value");
}

// B6 (#3/#11): component setters keep last-valid on non-finite input;
// opacity additionally clamps to [0,1]. Pure C++ level, no engine needed.
void testComponentSettersKeepLastValid() {
    TEST_SECTION("B6 Transform/Sprite setters keep last-valid values");
    Rowl::Scene::TransformComponent t;
    t.setPosition(10.0f, 20.0f);
    t.setScale(2.0f);
    t.setRotation(45.0f);
    t.setPosition(kNaN, kInf);
    t.setX(kNaN);
    t.setY(kInf);
    t.translate(kNaN, 1.0f);
    t.setScale(kNaN, 2.0f);
    t.setRotation(kNaN);
    t.rotate(kInf);
    if (t.getX() != 10.0f || t.getY() != 20.0f || t.getScaleX() != 2.0f ||
        t.getScaleY() != 2.0f || t.getRotation() != 45.0f) {
        std::cerr << "B6: Transform setter stored a non-finite value" << std::endl;
        exit(1);
    }
    t.setPosition(11.0f, 21.0f);
    if (t.getX() != 11.0f || t.getY() != 21.0f) {
        std::cerr << "B6: Transform setter rejected a valid value" << std::endl;
        exit(1);
    }
    Rowl::Scene::SpriteComponent s;
    s.setOpacity(0.5f);
    s.setOpacity(kNaN);
    if (s.getOpacity() != 0.5f) {
        std::cerr << "B6: Sprite opacity stored NaN" << std::endl;
        exit(1);
    }
    s.setOpacity(2.0f);
    if (s.getOpacity() != 1.0f) {
        std::cerr << "B6: Sprite opacity did not clamp to [0,1]" << std::endl;
        exit(1);
    }
    s.setOpacity(-1.0f);
    if (s.getOpacity() != 0.0f) {
        std::cerr << "B6: Sprite opacity did not clamp to [0,1]" << std::endl;
        exit(1);
    }
    s.setWidth(100.0f);
    s.setSize(kNaN, 50.0f);
    if (s.getWidth() != 100.0f) {
        std::cerr << "B6: Sprite size stored a non-finite value" << std::endl;
        exit(1);
    }
    TEST_PASS("B6 Transform/Sprite setters keep last-valid values");
}

// B6 (#4/#7): dialogue delay 60 + offset 60 must advance at ~60 s, not ~120 s.
void testAutoAdvanceSumClampedToCeiling() {
    TEST_SECTION("B6 Auto-advance sum keeps the 60 s ceiling");
    ScopedEmptyCwd cwd("rowl_b6_cwd_advance");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    const auto project = cwd.dir / "proj";
    std::error_code ec;
    std::filesystem::create_directories(project / "Assets" / "json", ec);
    {
        std::ofstream graph(project / "Assets" / "json" / "full_story_graph.json");
        graph << R"({"format_version":4,"start_node_id":1,"nodes":[
            {"id":1,"title":"A","objects":[{"id":"d1","name":"D","is_active":true,
              "components":[{"type":"dialogue","id":"d1c","enabled":true,
                "data":{"speaker":"S","dialogue":"Hi","typewriter_enabled":true,
                  "text_speed":30,"auto_advance":true,"auto_advance_delay":60}}]}],
             "next_nodes":[{"id":2,"label":"go"}]},
            {"id":2,"title":"B","objects":[],"next_nodes":[]}]})";
    }
    RowlEngineHandle handle = makeBareEngine();
    RowlEngine_SetProjectDirectory(handle, project.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 1) {
        std::cerr << "B6 setup: auto-advance graph did not load" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_SetPlayState(handle, 1);
    RowlEngine_SetAutoAdvanceDelayOffset(handle, 60.0f);
    // NOTE: step() clamps dt to 0.25 s: 260 x 1.0 s steps = 65 s of sim
    // time. Fixed code advances at the 60 s ceiling (step ~240); unfixed
    // code needs 120 s (480 steps), so this bound discriminates.
    for (int i = 0; i < 260; ++i) RowlEngine_Step(handle, 1.0f);
    const uint64_t node = RowlEngine_GetCurrentNodeId(handle);
    RowlEngine_Destroy(handle);
    if (node != 2) {
        std::cerr << "B6: auto-advance sum exceeded the 60 s ceiling (node " << node << ")"
                  << std::endl;
        exit(1);
    }
    TEST_PASS("B6 Auto-advance sum keeps the 60 s ceiling");
}

// B6 (#5): camera/flash/transition entries are already guarded natively —
// this test pins the fail-closed contract so a regression turns red.
void testRenderEntriesFailClosed() {
    TEST_SECTION("B6 Camera/flash/transition entries fail closed");
    ScopedEmptyCwd cwd("rowl_b6_cwd_render");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    RowlEngineHandle handle = makeBareEngine();
    RowlEngine_CameraZoomTo(handle, kNaN, 1.0f, 0);
    RowlEngine_CameraPanTo(handle, kInf, 0.0f, 1.0f, 0);
    RowlEngine_TriggerScreenFlash(handle, 255, 255, 255, -1.0f, 1.0f);
    RowlEngine_StartTransition(handle, "crossfade", kNaN, nullptr);
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsCameraMoving(handle) != 0 ||
        RowlEngine_IsScreenFlashActive(handle) != 0 ||
        RowlEngine_IsTransitionActive(handle) != 0) {
        std::cerr << "B6: render entry accepted a non-finite argument" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    // Valid-path regression: a real 1 s pan still reports moving.
    RowlEngine_CameraPanTo(handle, 100.0f, 100.0f, 1.0f, 0);
    if (RowlEngine_IsCameraMoving(handle) != 1) {
        std::cerr << "B6: valid camera pan stopped reporting motion" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 Camera/flash/transition entries fail closed");
}

// B6 (#6): legacy UpdateSceneEx with NaN geometry reports InvalidArgument,
// keeps rendering (no crash), and valid input still applies cleanly.
void testLegacySceneGuardsFailLoud() {
    TEST_SECTION("B6 Legacy UpdateScene guards fail loud");
    ScopedEmptyCwd cwd("rowl_b6_cwd_legacy");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    RowlEngineHandle handle = makeBareEngine();
    RowlEngine_UpdateSceneEx(handle, "S", "Hi", "", 0, 0, 1920, 1080, 0, "",
                             1440, 340, 360, 540, 0, 80, 860, 1760, 180);
    RowlEngine_UpdateSceneEx(handle, "S", "Hi", "", kNaN, 0, -50, kInf, 0, "",
                             1440, 340, 360, 540, 0, 80, 860, 1760, 180);
    if (RowlEngine_GetLastResultCode(handle) != ROWL_RESULT_INVALID_ARGUMENT) {
        std::cerr << "B6: NaN legacy geometry left no InvalidArgument record" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    if (std::string(RowlEngine_GetSpeaker(handle)) != "S" ||
        std::string(RowlEngine_GetDialogue(handle)) != "Hi") {
        std::cerr << "B6: scene content lost after guarded legacy call" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 Legacy UpdateScene guards fail loud");
}

// B6 (#8): parallax keeps last-valid on non-finite input, clamps to [-8,8].
void testParallaxKeepsLastValid() {
    TEST_SECTION("B6 Parallax keeps last-valid, clamps bounds");
    ScopedEmptyCwd cwd("rowl_b6_cwd_parallax");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    RowlEngineHandle handle = makeBareEngine();
    RowlEngine_SetBackgroundParallax(handle, 1.0f, 1.0f);
    RowlEngine_SetBackgroundParallax(handle, kNaN, kInf);
    if (RowlEngine_GetBackgroundParallaxX(handle) != 1.0f ||
        RowlEngine_GetBackgroundParallaxY(handle) != 1.0f) {
        std::cerr << "B6: parallax stored a non-finite value" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_SetBackgroundParallax(handle, 1.0e6f, -1.0e6f);
    if (RowlEngine_GetBackgroundParallaxX(handle) != 8.0f ||
        RowlEngine_GetBackgroundParallaxY(handle) != -8.0f) {
        std::cerr << "B6: parallax did not clamp to [-8,8]" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 Parallax keeps last-valid, clamps bounds");
}

// B6 (#9): a non-finite tap is not a tap — consumed without advancing.
void testPointerDownNaNConsumed() {
    TEST_SECTION("B6 NaN pointer tap consumed without advancing");
    ScopedEmptyCwd cwd("rowl_b6_cwd_pointer");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    const auto project = cwd.dir / "proj";
    std::error_code ec;
    std::filesystem::create_directories(project / "Assets" / "json", ec);
    {
        std::ofstream graph(project / "Assets" / "json" / "full_story_graph.json");
        graph << R"({"format_version":4,"start_node_id":7,"nodes":[
            {"id":7,"title":"A","objects":[],"next_nodes":[]}]})";
    }
    RowlEngineHandle handle = makeBareEngine();
    RowlEngine_SetProjectDirectory(handle, project.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 7) {
        std::cerr << "B6 setup: pointer graph did not load" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    // No choice buttons: unfixed code misclassifies NaN as bezelTap and the
    // bezel path returns false (advance signal, C API 0).
    if (RowlEngine_PointerDown(handle, kNaN, kNaN) != 1 ||
        RowlEngine_PointerDown(handle, kInf, 0.0f) != 1 ||
        RowlEngine_GetCurrentNodeId(handle) != 7) {
        std::cerr << "B6: non-finite tap was not consumed as a no-op" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 NaN pointer tap consumed without advancing");
}

// B6 (#10): narrow box + speaker used to hit std::clamp lo>hi (UB) and a
// negative wrap width; assert the frame still renders and content survives.
void testNarrowBadgeBoxRenders() {
    TEST_SECTION("B6 Narrow speaker-badge box renders without UB");
    ScopedEmptyCwd cwd("rowl_b6_cwd_badge");
    if (!cwd.ok) { std::cerr << "B6 setup: CWD pin failed" << std::endl; exit(1); }
    RowlEngineHandle handle = makeBareEngine();
    // Completion reports true while paused: play state is required for the
    // static-frame signal to mean anything (elapsed animates regardless).
    RowlEngine_SetPlayState(handle, 1);
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type": "dialogue", "enabled": true, "data": {
            "speaker": "Evelyn", "dialogue": "Hi",
            "x": 80, "y": 860, "width": 100, "height": 180,
            "typewriter_enabled": false}}
    ])");
    RowlEngine_Step(handle, 0.016f);
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Evelyn") {
        std::cerr << "B6: narrow-box scene lost its speaker" << std::endl;
        RowlEngine_Destroy(handle);
        exit(1);
    }
    RowlEngine_Destroy(handle);
    TEST_PASS("B6 Narrow speaker-badge box renders without UB");
}

} // namespace

void test_numeric_guards() {
    testHugeTextSpeedClamps();
    testNegativeTextSpeedFallsBack();
    testNonFiniteMultiplierKeepsLastGood();
    testComponentSettersKeepLastValid();
    testAutoAdvanceSumClampedToCeiling();
    testRenderEntriesFailClosed();
    testLegacySceneGuardsFailLoud();
    testParallaxKeepsLastValid();
    testPointerDownNaNConsumed();
    testNarrowBadgeBoxRenders();
}
