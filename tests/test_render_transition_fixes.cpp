/**
 * test_render_transition_fixes.cpp — Lock tests for #160/#162/#163/#164.
 *
 * Each leg pins the fixed behavior AND the pre-fix RED shape:
 *  - #160: Step(0) with a tween in flight stamps edge-triggered StateError
 *    (frozen vs progressing becomes host-observable); positive dt clears it.
 *  - #162: failed captureSnapshot keeps the previous snapshot (stage-then-
 *    commit) and Window::startTransition reports false; gate/coalesce stays
 *    silent-success (D6-#147 preserved).
 *  - #163 (narrowed remainder): a post-FX throw (script stageScripts
 *    type_error AFTER applyScreenFxComponent) restores ScreenFx scalars and
 *    aborts the orphaned transition; VALIDATION_ERROR still signals.
 *  - #164: RENDER_* events route globally (not dropped); RESET rebuilds
 *    render resources silently, LOST signals IoError.
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/transition_manager.hpp"

namespace {

struct FxLockCwd {
    std::filesystem::path saved;
    std::filesystem::path dir;
    bool ok = false;
    explicit FxLockCwd(const std::string& tag) {
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
    ~FxLockCwd() {
        std::error_code ec;
        if (ok) std::filesystem::current_path(saved, ec);
        std::filesystem::remove_all(dir, ec);
    }
};

void fxLockCheck(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "render-transition-fix: " << what << std::endl;
        std::exit(1);
    }
}

RowlEngineHandle fxLockCreateEngine(const std::string& root) {
    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) {
        std::cerr << "render-transition-fix: Create failed" << std::endl;
        std::exit(1);
    }
    RowlEngine_SetProjectDirectory(handle, root.c_str());
    if (!RowlEngine_Init(handle, 320, 180, 0)) {
        std::cerr << "render-transition-fix: Init failed" << std::endl;
        std::exit(1);
    }
    return handle;
}

} // namespace

void test_render_transition_fixes() {
    TEST_SECTION("Render/Transition Fixes (#160/#162/#163/#164)");

    // #160: zero-dt storm with a tween in flight signals instead of freezing
    // silently. Drive: PanTo -> Step(0)x3 -> StateError + stall count; then
    // positive dt completes the tween and clears the stall; re-seed Ok, then
    // a second stall episode re-signals (latch re-armed, not stuck).
    {
        FxLockCwd cwd("rowl_render_fx_160");
        fxLockCheck(cwd.ok, "#160: CWD pin failed");
        RowlEngineHandle handle = fxLockCreateEngine(cwd.dir.string());
        auto* engine = Rowl::Core::testEngineFromHandle(handle);
        fxLockCheck(engine != nullptr && engine->getCamera() != nullptr,
                    "#160: engine/camera missing");
        auto* camera = engine->getCamera();

        RowlEngine_CameraPanTo(handle, 400.0f, 300.0f, 2.0f, 0);
        fxLockCheck(RowlEngine_IsCameraMoving(handle) == 1, "#160: pan did not start");
        const float startX = camera->getPositionX();
        for (int i = 0; i < 3; ++i) RowlEngine_Step(handle, 0.0f);
        fxLockCheck(RowlEngine_IsCameraMoving(handle) == 1,
                    "#160: zero-dt must not advance or cancel the tween");
        fxLockCheck(camera->getPositionX() == startX,
                    "#160: zero-dt steps moved the tween");
        fxLockCheck(camera->isTweenStalled() && camera->stalledStepCount() == 3,
                    "#160: stall counter must read 3 after three Step(0)");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_STATE_ERROR,
                    "#160: stalled Step(0) must stamp STATE_ERROR");
        {
            const char* msg = RowlEngine_GetLastResultMessage(handle);
            const std::string message = msg ? msg : "";
            fxLockCheck(message.find("stalled") != std::string::npos,
                        "#160: stall diagnosis must name the stall");
        }
        TEST_PASS("#160 stall episode signals STATE_ERROR + stall count (no silent freeze)");

        for (int i = 0; i < 200; ++i) RowlEngine_Step(handle, 0.016f);
        fxLockCheck(RowlEngine_IsCameraMoving(handle) == 0,
                    "#160: positive dt must complete the tween");
        fxLockCheck(!camera->isTweenStalled() && camera->stalledStepCount() == 0,
                    "#160: positive dt must clear the stall count");
        fxLockCheck(std::abs(camera->getPositionX() - 400.0f) < 0.01f,
                    "#160: tween must reach its target");

        // Re-seed Ok (Step stamps nothing), then a fresh stall episode must
        // re-signal — the latch cleared instead of sticking.
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"background","id":"bg","enabled":true,
             "data":{"texture":"reseed.png"}}
        ])");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#160: re-seed must stamp OK");
        RowlEngine_CameraPanTo(handle, 100.0f, 100.0f, 2.0f, 0);
        RowlEngine_Step(handle, 0.0f);
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_STATE_ERROR,
                    "#160: second stall episode must re-signal (latch stuck otherwise)");
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        TEST_PASS("#160 recovery clears stall; second episode re-signals");
    }

    // #162: stage-then-commit capture + checked bool. A failed capture keeps
    // the previous snapshot (pre-fix: cleanup-first destroyed it) and the
    // Engine path reports instead of starting a snapshot-less transition.
    {
        SDL_Surface* surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
        fxLockCheck(surface != nullptr, "#162: software surface failed");
        SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(surface);
        fxLockCheck(renderer != nullptr, "#162: software renderer failed");

        Rowl::Render::TransitionManager manager;
        fxLockCheck(manager.captureSnapshot(surface, renderer),
                    "#162: valid capture must succeed");
        fxLockCheck(manager.hasSnapshot(), "#162: snapshot must exist after valid capture");
        const uint64_t capturesBefore = manager.getSnapshotCaptureCount();
        fxLockCheck(!manager.captureSnapshot(surface, nullptr),
                    "#162: null-renderer capture must fail");
        fxLockCheck(manager.hasSnapshot(),
                    "#162: failed capture must preserve the previous snapshot");
        fxLockCheck(manager.getSnapshotCaptureCount() == capturesBefore + 1,
                    "#162: failed attempt must still count (D6-#147 counter)");

        SDL_DestroyRenderer(renderer);
        SDL_DestroySurface(surface);
        TEST_PASS("#162 failed capture preserves previous snapshot + counts attempt");
    }

    // #162 Engine wiring + D6-#147 preservation: invalid kind stays silent
    // (no error, no transition); a valid offscreen transition starts.
    {
        FxLockCwd cwd("rowl_render_fx_162");
        fxLockCheck(cwd.ok, "#162: CWD pin failed");
        RowlEngineHandle handle = fxLockCreateEngine(cwd.dir.string());

        // OK baseline: an empty project root stamps a story-load error at
        // SetProjectDirectory; a successful scene update re-seeds Ok so the
        // silence assertion below reads the gate, not setup residue.
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"background","id":"bg","enabled":true,
             "data":{"texture":"baseline.png"}}
        ])");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#162: baseline seed must stamp OK");

        RowlEngine_StartTransition(handle, "bogus-kind", 1.0f, "");
        fxLockCheck(RowlEngine_IsTransitionActive(handle) == 0,
                    "#162: invalid kind must not start a transition");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#162: gate rejection must stay silent (D6-#147)");

        RowlEngine_StartTransition(handle, "crossfade", 1.0f, "");
        fxLockCheck(RowlEngine_IsTransitionActive(handle) == 1,
                    "#162: valid offscreen transition must start");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#162: successful start stamps no error");
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        TEST_PASS("#162 gate silence preserved; valid transition starts offscreen");
    }

    // #163 (narrowed remainder): post-FX throw restores window FX. Trigger:
    // [valid screen_fx WITH transition kind, script with numeric "code"] —
    // validation passes, FX applies at :1300-1302, stageScripts throws
    // type_error at :1350. Leg 2 replaces an in-flight transition (abort,
    // not resume — the replaced snapshot texture is unrecoverable).
    {
        FxLockCwd cwd("rowl_render_fx_163");
        fxLockCheck(cwd.ok, "#163: CWD pin failed");
        RowlEngineHandle handle = fxLockCreateEngine(cwd.dir.string());
        auto* engine = Rowl::Core::testEngineFromHandle(handle);
        fxLockCheck(engine != nullptr && engine->getWindow() != nullptr,
                    "#163: engine/window missing");
        auto* window = engine->getWindow();

        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"background","id":"bg","enabled":true,
             "data":{"texture":"bg_seed.png"}},
            {"type":"dialogue","id":"dlg","enabled":true,
             "data":{"speaker":"Seed","dialogue":"Seed line."}}
        ])");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#163: seed must stamp OK");
        fxLockCheck(RowlEngine_IsTransitionActive(handle) == 0, "#163: seed starts no transition");

        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"screen_fx","id":"fx","enabled":true,
             "data":{"kind":"crossfade","duration":1.0,
                     "flash_enabled":true,"flash_color":"#FF0000","flash_duration":5.0,
                     "flash_intensity":1.0,"tint_enabled":true,"tint_color":"#00FF00",
                     "tint_opacity":0.5,"vignette_enabled":true,"vignette_intensity":0.6}},
            {"type":"script","id":"poison","enabled":true,
             "data":{"code":12345}}
        ])");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_VALIDATION_ERROR,
                    "#163: post-FX throw must signal VALIDATION_ERROR");
        {
            const char* msg = RowlEngine_GetLastResultMessage(handle);
            const std::string message = msg ? msg : "";
            fxLockCheck(message.find("previous scene restored") != std::string::npos,
                        "#163: failure message must confirm restore");
        }
        fxLockCheck(RowlEngine_IsTransitionActive(handle) == 0,
                    "#163: orphaned transition must be aborted");
        fxLockCheck(!window->isScreenFlashActive(), "#163: flash leaked past rollback");
        fxLockCheck(!window->hasScreenTint(), "#163: tint leaked past rollback");
        fxLockCheck(!window->isVignetteActive(), "#163: vignette leaked past rollback");
        TEST_PASS("#163 post-FX throw restores window FX + aborts orphan + signals");

        // Leg 2: a live in-flight transition replaced by the failed update is
        // aborted (its snapshot was destroyed by the replacing capture).
        RowlEngine_StartTransition(handle, "crossfade", 5.0f, "");
        fxLockCheck(RowlEngine_IsTransitionActive(handle) == 1,
                    "#163 leg2: setup transition must start");
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"screen_fx","id":"fx","enabled":true,
             "data":{"kind":"fade_black","duration":1.0}},
            {"type":"script","id":"poison","enabled":true,
             "data":{"code":12345}}
        ])");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_VALIDATION_ERROR,
                    "#163 leg2: must still signal VALIDATION_ERROR");
        fxLockCheck(RowlEngine_IsTransitionActive(handle) == 0,
                    "#163 leg2: replaced in-flight transition must abort");
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        TEST_PASS("#163 replaced in-flight transition aborts on failed update");
    }

    // #164: RENDER_* routing + recovery. Dispatcher leg pins global routing
    // (pre-fix: dropped at routeEvent:79); Engine leg pins rebuild-silence
    // on RESET and IoError on LOST.
    {
        const bool eventsAlreadyInit = SDL_WasInit(SDL_INIT_EVENTS) != 0;
        if (!eventsAlreadyInit && !SDL_InitSubSystem(SDL_INIT_EVENTS)) {
            std::cerr << "render-transition-fix: SDL events unavailable" << std::endl;
            std::exit(1);
        }
        // Pin kurtarma: S5 (thread_lease) pin'i temizler, S7'nin worker
        // Step'leri sahipsiz pin'i worker'a kaptırır; suite'in geri kalani
        // pin'e dokunmadigi icin yesil kalir — pin'e ihtiyac duyan ilk test
        // burasidir. stealDispatchThread D3'un idari devir yuzeyidir
        // (ReclaimHandle preceden'i); tablo korunur, yalnizca pompa thread'i
        // main'e gecer. Sonrasinda drain, stale global'lari temizler.
        if (!Rowl::Platform::SdlEventDispatcher::isEligibleForRegister()) {
            Rowl::Platform::SdlEventDispatcher::stealDispatchThread();
        }
        constexpr uint32_t renderProbeWindow = 0x8E164;
        fxLockCheck(Rowl::Platform::SdlEventDispatcher::registerWindow(renderProbeWindow),
                    "#164: probe window registration failed");
        Rowl::Platform::SdlEventDispatcher::takeGlobalEvents();

        SDL_Event resetEvent{};
        resetEvent.type = SDL_EVENT_RENDER_DEVICE_RESET;
        SDL_Event keyEvent{};
        keyEvent.type = SDL_EVENT_KEY_DOWN;
        keyEvent.key.key = SDLK_F5;
        keyEvent.key.windowID = renderProbeWindow;
        fxLockCheck(SDL_PushEvent(&resetEvent) && SDL_PushEvent(&keyEvent),
                    "#164: could not enqueue synthetic render/key events");
        const auto globals = Rowl::Platform::SdlEventDispatcher::takeGlobalEvents();
        fxLockCheck(globals.size() == 1 && globals[0].type == SDL_EVENT_RENDER_DEVICE_RESET,
                    "#164: RENDER_DEVICE_RESET must route globally (not dropped)");
        const auto windowed = Rowl::Platform::SdlEventDispatcher::takeEvents(renderProbeWindow);
        fxLockCheck(windowed.size() == 1 && windowed[0].type == SDL_EVENT_KEY_DOWN,
                    "#164: window-targeted event must stay isolated");
        Rowl::Platform::SdlEventDispatcher::unregisterWindow(renderProbeWindow);
        TEST_PASS("#164 RENDER_* routes globally without window leakage");
    }
    {
        FxLockCwd cwd("rowl_render_fx_164");
        fxLockCheck(cwd.ok, "#164: CWD pin failed");
        RowlEngineHandle handle = fxLockCreateEngine(cwd.dir.string());
        auto* engine = Rowl::Core::testEngineFromHandle(handle);
        fxLockCheck(engine != nullptr && engine->getWindow() != nullptr,
                    "#164: engine/window missing");
        auto* window = engine->getWindow();
        // OK baseline (see #162 leg): empty-root story-load error is setup
        // residue, not signal — re-seed Ok before asserting rebuild silence.
        RowlEngine_UpdateSceneFromJson(handle, R"([
            {"type":"background","id":"bg","enabled":true,
             "data":{"texture":"baseline.png"}}
        ])");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#164: baseline seed must stamp OK");

        const bool eventsAlreadyInit = SDL_WasInit(SDL_INIT_EVENTS) != 0;
        if (!eventsAlreadyInit && !SDL_InitSubSystem(SDL_INIT_EVENTS)) {
            std::cerr << "render-transition-fix: SDL events unavailable" << std::endl;
            std::exit(1);
        }
        SDL_Event resetEvent{};
        resetEvent.type = SDL_EVENT_RENDER_TARGETS_RESET;
        fxLockCheck(SDL_PushEvent(&resetEvent), "#164: could not enqueue TARGETS_RESET");
        RowlEngine_Step(handle, 0.016f);
        fxLockCheck(window->getRenderDeviceRebuildCount() == 1,
                    "#164: TARGETS_RESET must rebuild render resources");
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_OK,
                    "#164: successful rebuild stamps no error");

        SDL_Event lostEvent{};
        lostEvent.type = SDL_EVENT_RENDER_DEVICE_LOST;
        fxLockCheck(SDL_PushEvent(&lostEvent), "#164: could not enqueue DEVICE_LOST");
        RowlEngine_Step(handle, 0.016f);
        fxLockCheck(RowlEngine_GetLastResultCode(handle) == ROWL_RESULT_IO_ERROR,
                    "#164: DEVICE_LOST must signal IO_ERROR");
        {
            const char* msg = RowlEngine_GetLastResultMessage(handle);
            const std::string message = msg ? msg : "";
            fxLockCheck(message.find("Render device lost") != std::string::npos,
                        "#164: LOST diagnosis must name the device loss");
        }
        RowlEngine_Shutdown(handle);
        RowlEngine_Destroy(handle);
        TEST_PASS("#164 RESET rebuilds silently; LOST signals IO_ERROR");
    }
}
