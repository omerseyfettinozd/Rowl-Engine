/**
 * test_camera_and_transition_pipeline.cpp — Camera pipeline, transitions, render benchmarks.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/frame_composition.hpp"
#include "rowl/text/hex_color.hpp"
#include <cmath>
#include <limits>

namespace {

// Faz 4.5 Dilim 2 — HEX BIRLESTIRME: gecis yedegi siyahtir (0,0,0,255).
// Birlesik baslik uzerinden gecerli-alfa/bos/bozuk matrisi + davranissal
// startTransitionFromKind dumani. TransitionManager renk okuyucu sunmadigi
// icin renk altinlari paylasilan cozucu API'sinda sabitlenir (uretim
// kullanim yolu ayni fonksiyondur).
void testTransitionHexUnification() {
    TEST_SECTION("Hex Parser Unification (transition black-fallback flavor)");
    using Rowl::Text::HexColor;
    using Rowl::Text::parseHexColor;

    const HexColor kBlack{0, 0, 0, 255};
    auto expectParsed = [&](const char* input, uint8_t r, uint8_t g,
                            uint8_t b, uint8_t a) {
        bool ok = false;
        const HexColor resolved = parseHexColor(input, kBlack, &ok);
        if (!ok || resolved.r != r || resolved.g != g || resolved.b != b ||
            resolved.a != a) {
            std::cerr << "Transition hex golden failed: '" << input << "'"
                      << std::endl;
            exit(1);
        }
    };
    auto expectFallback = [&](const char* input) {
        bool ok = true;
        const HexColor resolved = parseHexColor(input, kBlack, &ok);
        if (ok || !(resolved == kBlack)) {
            std::cerr << "Transition hex must fall back to black + failure: '"
                      << input << "'" << std::endl;
            exit(1);
        }
    };

    expectParsed("#FFF", 255, 255, 255, 255);
    expectParsed("#10B981", 0x10, 0xB9, 0x81, 255);
    expectParsed("10B981", 0x10, 0xB9, 0x81, 255);
    expectParsed("#F008", 255, 0, 0, 0x88);
    expectParsed("#10B981CC", 0x10, 0xB9, 0x81, 0xCC);
    TEST_PASS("Transition flavor parses #FFF, #RRGGBB and alpha forms");

    expectFallback("");
    expectFallback("#GGG");
    expectFallback("#12");
    expectFallback("#12345");
    expectFallback("#FF00GG");
    TEST_PASS("Transition flavor rejects empty/malformed hex to black + failure");

    // Davranissal duman: her hex sinifi FadeToColor'u kazasiz baslatir.
    for (const char* hex : {"#FFF", "#10B981", "#F008", "#GGG", "", "#12"}) {
        Rowl::Render::TransitionManager transition;
        transition.startTransitionFromKind("fade_color", 1.0f, hex);
        if (!transition.isTransitionActive() ||
            transition.getType() != Rowl::Render::TransitionType::FadeToColor) {
            std::cerr << "fade_color did not start for hex '" << hex << "'"
                      << std::endl;
            exit(1);
        }
    }
    TEST_PASS("fade_color starts for valid, alpha, empty and malformed hex");
}

// D6-#147 — VALIDATE-BEFORE-SNAPSHOT: geçersiz tür/süre snapshot'a (SDL
// readback) mal olmaz; erken aynı-tür tetikleme birleştirilir. Kanıt:
// readback sayacı + 10x spam (snapshot'ı doğrulama-öncesine taşıyan bir
// mutant sayaç kilidine takılır).
void testTransitionValidateBeforeSnapshot() {
    TEST_SECTION("Transition Validate-Before-Snapshot & Retrigger Coalesce");
    using Rowl::Render::TransitionManager;
    using Rowl::Render::TransitionType;

    // Kapı tablosu: 10 geçerli tür, reddedilenler.
    for (const char* kind : {"crossfade", "fade", "fade_black", "black",
                             "fade_white", "white", "fade_color", "color",
                             "wipe_left", "wipe_right"}) {
        if (!TransitionManager::isKnownKind(kind) ||
            !TransitionManager().canStartTransition(kind, 0.5f)) {
            std::cerr << "canStartTransition rejected valid kind '" << kind
                      << "'" << std::endl;
            exit(1);
        }
    }
    if (TransitionManager::isKnownKind("uydurma") ||
        TransitionManager::isKnownKind("")) {
        std::cerr << "isKnownKind accepted an unknown kind" << std::endl;
        exit(1);
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float bad : {0.0f, -1.0f, nan, inf}) {
        if (TransitionManager::isUsableDuration(bad) ||
            TransitionManager().canStartTransition("crossfade", bad)) {
            std::cerr << "canStartTransition accepted bad duration " << bad
                      << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Transition gate accepts 10 known kinds, rejects unknown/NaN/non-positive");

    // Birleştirme: erken aynı-tür tetikleme elapsed'i sıfırlamaz.
    {
        TransitionManager transition;
        transition.startTransitionFromKind("crossfade", 1.0f);
        transition.update(0.1f);
        const float elapsedBefore = transition.getElapsed();
        transition.startTransitionFromKind("crossfade", 1.0f);
        if (transition.getType() != TransitionType::CrossFade ||
            std::fabs(transition.getElapsed() - elapsedBefore) > 1e-6f) {
            std::cerr << "Early same-kind retrigger must coalesce (no restart)"
                      << std::endl;
            exit(1);
        }
        // %90 sonrası yeniden tetikleme baştan başlatır.
        transition.update(0.85f);
        transition.startTransitionFromKind("crossfade", 1.0f);
        if (!transition.isTransitionActive() || transition.getElapsed() != 0.0f) {
            std::cerr << "Late retrigger must restart the transition"
                      << std::endl;
            exit(1);
        }
    }
    // Farklı tür erken tetiklemede de birleşir (#147-artık genellemesi:
    // alterne-tür spam'i snapshot artırmaz); %90 sonrası tür değiştirir.
    {
        TransitionManager transition;
        transition.startTransitionFromKind("crossfade", 1.0f);
        transition.update(0.1f);
        transition.startTransitionFromKind("wipe_left", 1.0f);
        if (transition.getType() != TransitionType::CrossFade ||
            std::fabs(transition.getElapsed() - 0.1f) > 1e-6f) {
            std::cerr << "Early different-kind retrigger must coalesce (no restart)"
                      << std::endl;
            exit(1);
        }
        transition.update(0.85f);
        transition.startTransitionFromKind("wipe_left", 1.0f);
        if (transition.getType() != TransitionType::WipeLeft ||
            transition.getElapsed() != 0.0f) {
            std::cerr << "Late different-kind retrigger must switch the transition"
                      << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Early retrigger coalesces (any kind); late retrigger restarts");

    // Pencere düzeyi: 10x spam geçersiz girdi readback'e mal olmaz, sürmekte
    // olan geçişi öldürmez; birleştirilmiş tetikleme yeniden yakalamaz.
    {
        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(std::filesystem::current_path().string());
        Rowl::Render::Window window(&vfs);
        if (!window.initializeOffscreen(320, 180)) {
            std::cerr << "Could not initialize offscreen window for transition gate test"
                      << std::endl;
            exit(1);
        }
        Rowl::Render::TransitionManager* manager = window.getTransitionManager();
        if (!manager) {
            std::cerr << "Offscreen window has no transition manager" << std::endl;
            exit(1);
        }
        const uint64_t base = manager->getSnapshotCaptureCount();
        for (int i = 0; i < 10; ++i) {
            window.startTransition("uydurma-tur", 0.5f, "");
            window.startTransition("crossfade", 0.0f, "");
            window.startTransition("crossfade", -1.0f, "");
            window.startTransition("crossfade", nan, "");
            window.startTransition("crossfade", inf, "");
        }
        if (manager->getSnapshotCaptureCount() != base) {
            std::cerr << "Invalid transition input reached the snapshot readback"
                      << std::endl;
            exit(1);
        }
        if (window.isTransitionActive()) {
            std::cerr << "Invalid transition input started a transition"
                      << std::endl;
            exit(1);
        }

        window.startTransition("crossfade", 0.5f, "");
        if (manager->getSnapshotCaptureCount() != base + 1 ||
            !window.isTransitionActive()) {
            std::cerr << "Valid transition must capture once and go active"
                      << std::endl;
            exit(1);
        }
        window.update(0.05f);
        for (int i = 0; i < 10; ++i)
            window.startTransition("crossfade", 0.5f, "");
        if (manager->getSnapshotCaptureCount() != base + 1 ||
            !window.isTransitionActive()) {
            std::cerr << "Coalesced retrigger must not recapture the snapshot"
                      << std::endl;
            exit(1);
        }
        // Geçersiz girdi sürmekte olan geçişi öldürmez.
        window.startTransition("uydurma-tur", 0.5f, "");
        if (!window.isTransitionActive() ||
            manager->getSnapshotCaptureCount() != base + 1) {
            std::cerr << "Invalid input must not kill the running transition"
                      << std::endl;
            exit(1);
        }
        window.shutdown();
    }
    TEST_PASS("10x invalid/coalesced spam costs zero readbacks; running transition survives");
}

// D6-#147-artık: flash/shake koşulsuz resetleri + tür-alternatifli spam.
// Üç bacak: (a) flash yeniden tetiklemede progress sıfırlanmaz;
// (b) shake erken tetiklemede sayaç sıfırlanmaz, %10-kala kabul edilir;
// (c) pencere düzeyinde alterne-tür spam snapshot artırmaz, geçiş
// progress=1'e ulaşır, %90-sonrası tür değişimi yeni yakalama yapar.
void testScreenFxRetriggerCoalesce() {
    TEST_SECTION("Screen FX & Cross-Kind Retrigger Coalesce (#147 residue)");
    using Rowl::Render::TransitionManager;
    using Rowl::Render::TransitionType;

    // Bacak (a): flash ilerler, erken yeniden tetikleme sıfırlamaz.
    {
        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(std::filesystem::current_path().string());
        Rowl::Render::Window window(&vfs);
        if (!window.initializeOffscreen(320, 180)) {
            std::cerr << "Could not initialize offscreen window for flash gate test"
                      << std::endl;
            exit(1);
        }
        window.triggerScreenFlash(255, 0, 0, 1.0f, 1.0f);
        window.update(0.1f);
        const float progressBefore = window.getScreenFlashProgress();
        if (std::fabs(progressBefore - 0.1f) > 1e-5f) {
            std::cerr << "Flash progress did not advance to 0.1" << std::endl;
            exit(1);
        }
        window.triggerScreenFlash(0, 255, 0, 1.0f, 1.0f);
        if (!window.isScreenFlashActive() ||
            std::fabs(window.getScreenFlashProgress() - progressBefore) > 1e-6f) {
            std::cerr << "Early flash retrigger must coalesce (no restart)"
                      << std::endl;
            exit(1);
        }
        window.update(1.0f);
        if (window.isScreenFlashActive()) {
            std::cerr << "Finished flash must go inactive" << std::endl;
            exit(1);
        }
        window.triggerScreenFlash(0, 255, 0, 1.0f, 1.0f);
        if (!window.isScreenFlashActive() ||
            window.getScreenFlashProgress() != 0.0f) {
            std::cerr << "Idle flash retrigger must restart from zero"
                      << std::endl;
            exit(1);
        }
        window.shutdown();
    }
    TEST_PASS("Flash retrigger coalesces mid-flight; restarts when idle/finished");

    // Bacak (b): shake sayacı erken tetiklemede sıfırlanmaz.
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        camera.shake(25.0f, 0.4f, 30.0f);
        camera.update(0.05f);
        const float timerBefore = camera.getShakeTimer();
        if (timerBefore <= 0.3f) {
            std::cerr << "Shake timer did not keep running after update"
                      << std::endl;
            exit(1);
        }
        camera.shakeWithProfile(Rowl::Render::CameraShakePreset::Custom,
                                99.0f, 9.0f, 30.0f, 1.5f, 0.8f, 0.4f);
        if (std::fabs(camera.getShakeTimer() - timerBefore) > 1e-6f) {
            std::cerr << "Early shake retrigger must coalesce (timer keeps running)"
                      << std::endl;
            exit(1);
        }
        // Kalan %10'un altına inince yeni shake kabul edilir.
        camera.update(0.32f);
        camera.shakeWithProfile(Rowl::Render::CameraShakePreset::Custom,
                                99.0f, 9.0f, 30.0f, 1.5f, 0.8f, 0.4f);
        if (camera.getShakeTimer() < 9.0f - 1e-5f) {
            std::cerr << "Late shake retrigger must restart the timer"
                      << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Shake retrigger coalesces early; restarts inside the last 10%");

    // Bacak (c): alterne-tür spam snapshot artırmaz, geçiş tamamlanır.
    {
        Rowl::VFS::VFSManager vfs;
        vfs.remountProject(std::filesystem::current_path().string());
        Rowl::Render::Window window(&vfs);
        if (!window.initializeOffscreen(320, 180)) {
            std::cerr << "Could not initialize offscreen window for cross-kind spam test"
                      << std::endl;
            exit(1);
        }
        Rowl::Render::TransitionManager* manager = window.getTransitionManager();
        if (!manager) {
            std::cerr << "Offscreen window has no transition manager" << std::endl;
            exit(1);
        }
        const uint64_t base = manager->getSnapshotCaptureCount();
        window.startTransition("crossfade", 0.5f, "");
        if (manager->getSnapshotCaptureCount() != base + 1 ||
            !window.isTransitionActive()) {
            std::cerr << "Valid transition must capture once and go active"
                      << std::endl;
            exit(1);
        }
        window.update(0.05f);
        const float elapsedBefore = manager->getElapsed();
        for (int i = 0; i < 10; ++i)
            window.startTransition("wipe_left", 0.5f, "");
        if (manager->getSnapshotCaptureCount() != base + 1 ||
            manager->getType() != TransitionType::CrossFade ||
            std::fabs(manager->getElapsed() - elapsedBefore) > 1e-6f) {
            std::cerr << "Cross-kind spam must coalesce (no recapture, no switch)"
                      << std::endl;
            exit(1);
        }
        window.update(0.5f);
        if (window.isTransitionActive() || manager->getProgress() != 1.0f) {
            std::cerr << "Coalesced transition must run to progress=1 and complete"
                      << std::endl;
            exit(1);
        }
        window.startTransition("wipe_left", 0.5f, "");
        if (manager->getSnapshotCaptureCount() != base + 2 ||
            !window.isTransitionActive() ||
            manager->getType() != TransitionType::WipeLeft) {
            std::cerr << "Late different-kind start must capture and switch"
                      << std::endl;
            exit(1);
        }
        window.shutdown();
    }
    TEST_PASS("Cross-kind spam costs zero readbacks; transition completes; late switch captures");
}

}  // namespace

void test_camera_and_transition_pipeline() {
    TEST_SECTION("2D Camera & Scene Transition Subsystem Tests");

    // Test 1: Camera2D virtual projection & zoom identity
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        if (std::abs(camera.getPositionX() - 960.0f) > 0.001f ||
            std::abs(camera.getPositionY() - 540.0f) > 0.001f ||
            std::abs(camera.getZoom() - 1.0f) > 0.001f) {
            std::cerr << "Camera2D default initialization failed" << std::endl;
            exit(1);
        }

        float outX = 0, outY = 0, outW = 0, outH = 0;
        camera.transformRect(100.0f, 200.0f, 300.0f, 400.0f, outX, outY, outW, outH);
        if (std::abs(outX - 100.0f) > 0.001f || std::abs(outY - 200.0f) > 0.001f ||
            std::abs(outW - 300.0f) > 0.001f || std::abs(outH - 400.0f) > 0.001f) {
            std::cerr << "Camera2D identity transformation failed" << std::endl;
            exit(1);
        }

        // Test 2: Camera2D Zoom clamping & center scaling
        camera.setZoom(2.0f);
        if (std::abs(camera.getZoom() - 2.0f) > 0.001f) {
            std::cerr << "Camera2D setZoom failed" << std::endl;
            exit(1);
        }
        camera.transformRect(960.0f, 540.0f, 100.0f, 100.0f, outX, outY, outW, outH);
        if (std::abs(outX - 960.0f) > 0.001f || std::abs(outY - 540.0f) > 0.001f ||
            std::abs(outW - 200.0f) > 0.001f || std::abs(outH - 200.0f) > 0.001f) {
            std::cerr << "Camera2D zoom projection failed" << std::endl;
            exit(1);
        }

        // Clamping test: negative zoom and excessive zoom
        camera.setZoom(-5.0f);
        if (camera.getZoom() < 0.1f) {
            std::cerr << "Camera2D negative zoom clamp failed" << std::endl;
            exit(1);
        }
        camera.setZoom(100.0f);
        if (camera.getZoom() > 10.0f) {
            std::cerr << "Camera2D max zoom clamp failed" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Virtual Canvas Projection & Zoom Scaling");
    }

    // Test 3: Camera2D Screen Shake & Harmonic Decay
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        camera.shake(25.0f, 0.4f, 30.0f);
        if (!camera.isShaking()) {
            std::cerr << "Camera2D shake trigger failed" << std::endl;
            exit(1);
        }

        camera.update(0.1f);
        if (!camera.isShaking() || (camera.getShakeOffsetX() == 0.0f && camera.getShakeOffsetY() == 0.0f)) {
            std::cerr << "Camera2D shake offset update failed" << std::endl;
            exit(1);
        }

        // Advance past shake duration
        camera.update(0.5f);
        if (camera.isShaking() || camera.getShakeOffsetX() != 0.0f || camera.getShakeOffsetY() != 0.0f) {
            std::cerr << "Camera2D shake decay to zero failed" << std::endl;
            exit(1);
        }

        // Reset
        camera.setPosition(1200.0f, 600.0f);
        camera.setZoom(1.5f);
        camera.reset();
        if (std::abs(camera.getPositionX() - 960.0f) > 0.001f ||
            std::abs(camera.getPositionY() - 540.0f) > 0.001f ||
            std::abs(camera.getZoom() - 1.0f) > 0.001f) {
            std::cerr << "Camera2D reset failed" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Screen Shake Harmonic Decay & Reset");
    }

    // Test 3b: Camera2D Smooth Tweening & Easing (PanTo & ZoomTo)
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        camera.panTo(1200.0f, 600.0f, 1.0f, Rowl::Render::CameraEasing::EaseInOutCubic);
        camera.zoomTo(2.0f, 1.0f, Rowl::Render::CameraEasing::EaseInOutCubic);

        if (!camera.isPanning() || !camera.isZooming() || !camera.isMoving()) {
            std::cerr << "Camera2D tween start query failed" << std::endl;
            exit(1);
        }

        // Halfway step: 0.5s -> EaseInOutCubic factor = 0.5
        camera.update(0.5f);
        if (std::abs(camera.getPositionX() - 1080.0f) > 0.05f ||
            std::abs(camera.getPositionY() - 570.0f) > 0.05f ||
            std::abs(camera.getZoom() - 1.5f) > 0.05f) {
            std::cerr << "Camera2D halfway tween interpolation failed" << std::endl;
            exit(1);
        }

        // Complete step: 0.5s -> reaches target exactly
        camera.update(0.5f);
        if (std::abs(camera.getPositionX() - 1200.0f) > 0.01f ||
            std::abs(camera.getPositionY() - 600.0f) > 0.01f ||
            std::abs(camera.getZoom() - 2.0f) > 0.01f) {
            std::cerr << "Camera2D tween target completion failed" << std::endl;
            exit(1);
        }
        if (camera.isPanning() || camera.isZooming() || camera.isMoving()) {
            std::cerr << "Camera2D tween end state query failed" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Smooth Tweening & EaseInOutCubic Interpolation");
    }

    // Faz 4.5 Dilim 2: Camera2D::setRotation is a capability-gated no-op.
    // The angle is ignored, rotation stays 0.0f, and the 4096 capability
    // bit is advertised. getRotation() is unchanged (ABI preserved).
    {
        Rowl::Render::Camera2D camera(1920.0f, 1080.0f);
        camera.setRotation(45.0f);
        if (std::abs(camera.getRotation() - 0.0f) > 0.0001f) {
            std::cerr << "Camera2D setRotation(45) was not ignored" << std::endl;
            exit(1);
        }
        camera.setRotation(-30.0f);
        if (std::abs(camera.getRotation() - 0.0f) > 0.0001f) {
            std::cerr << "Camera2D setRotation(-30) was not ignored" << std::endl;
            exit(1);
        }
        camera.setRotation(std::numeric_limits<float>::quiet_NaN());
        if (std::abs(camera.getRotation() - 0.0f) > 0.0001f) {
            std::cerr << "Camera2D setRotation(NaN) was not ignored" << std::endl;
            exit(1);
        }
        camera.reset();
        if (std::abs(camera.getRotation() - 0.0f) > 0.0001f) {
            std::cerr << "Camera2D reset did not leave rotation at 0" << std::endl;
            exit(1);
        }

        uint64_t capabilities = 0;
        if (RowlEngine_GetCapabilities(&capabilities) != ROWL_RESULT_OK ||
            (capabilities & ROWL_ENGINE_CAPABILITY_CAMERA_ROTATION_IGNORED) == 0) {
            std::cerr << "ROWL_ENGINE_CAPABILITY_CAMERA_ROTATION_IGNORED (4096) missing from capabilities" << std::endl;
            exit(1);
        }

        // A nonzero camera "rotation" in scene JSON stays
        // behavior-preserving: ignored, frame still renders.
        RowlEngineHandle handle = RowlEngine_Create();
        RowlEngine_Init(handle, 1920, 1080, 0);
        std::string rotationJson = R"([
            {
                "type": "camera",
                "data": { "x": 960, "y": 540, "zoom": 1.0, "rotation": 45.0 }
            }
        ])";
        RowlEngine_UpdateSceneFromJson(handle, rotationJson.c_str());
        RowlEngine_Step(handle, 0.016f);
        uint32_t rotW = 0, rotH = 0;
        const uint8_t* rotBuffer = RowlEngine_GetPixelBuffer(handle, &rotW, &rotH);
        if (!rotBuffer || rotW != 1920 || rotH != 1080) {
            std::cerr << "Pixel buffer invalid after ignored camera rotation JSON" << std::endl;
            exit(1);
        }
        RowlEngine_Destroy(handle);

        TEST_PASS("Camera2D setRotation Ignored (Stays 0.0f, Bit 4096 Present)");
    }

    // Test 4: TransitionManager Lifecycle & State Transitions
    {
        Rowl::Render::TransitionManager transition;
        if (transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::None) {
            std::cerr << "TransitionManager initial state should be None" << std::endl;
            exit(1);
        }

        transition.startTransitionFromKind("crossfade", 0.5f);
        if (!transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::CrossFade) {
            std::cerr << "TransitionManager CrossFade start failed" << std::endl;
            exit(1);
        }

        transition.update(0.25f);
        if (std::abs(transition.getProgress() - 0.5f) > 0.01f || !transition.isTransitionActive()) {
            std::cerr << "TransitionManager halfway progress failed" << std::endl;
            exit(1);
        }

        transition.update(0.3f);
        if (transition.isTransitionActive()) {
            std::cerr << "TransitionManager completion failed" << std::endl;
            exit(1);
        }

        // Test FadeToColor with custom hex
        transition.startTransitionFromKind("fade_color", 1.0f, "#10B981");
        if (!transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::FadeToColor) {
            std::cerr << "TransitionManager FadeToColor start failed" << std::endl;
            exit(1);
        }

        // Wipe transitions — #147-artık: erken tür değişimi birleşir
        // (fade_color sürer, baştan başlamaz); %90 sonrası wipe_left devralır.
        transition.startTransitionFromKind("wipe_left", 0.8f);
        if (!transition.isTransitionActive() ||
            transition.getType() != Rowl::Render::TransitionType::FadeToColor) {
            std::cerr << "Early different-kind retrigger must coalesce" << std::endl;
            exit(1);
        }

        transition.update(0.95f);
        transition.startTransitionFromKind("wipe_left", 0.8f);
        if (!transition.isTransitionActive() || transition.getType() != Rowl::Render::TransitionType::WipeLeft) {
            std::cerr << "TransitionManager WipeLeft start failed" << std::endl;
            exit(1);
        }

        transition.reset();
        if (transition.isTransitionActive()) {
            std::cerr << "TransitionManager reset failed" << std::endl;
            exit(1);
        }

        TEST_PASS("TransitionManager Lifecycle, Progress & Hex Parsing");
    }

    // Test 5: C API Integration for Camera & Transition
    {
        RowlEngineHandle handle = RowlEngine_Create();
        if (!handle) {
            std::cerr << "Failed to create RowlEngine handle" << std::endl;
            exit(1);
        }
        if (!RowlEngine_Init(handle, 1920, 1080, 0)) {
            std::cerr << "Failed to initialize offscreen engine" << std::endl;
            exit(1);
        }

        RowlEngine_SetCamera(handle, 1000.0f, 500.0f, 1.25f);
        RowlEngine_TriggerCameraShake(handle, 15.0f, 0.5f);
        RowlEngine_Step(handle, 0.016f);

        // Smooth Panning and Zooming via C API
        RowlEngine_CameraPanTo(handle, 1200.0f, 600.0f, 0.4f, 3);
        RowlEngine_CameraZoomTo(handle, 1.75f, 0.4f, 3);
        if (!RowlEngine_IsCameraMoving(handle)) {
            std::cerr << "RowlEngine_IsCameraMoving expected true during pan/zoom" << std::endl;
            exit(1);
        }

        RowlEngine_StartTransition(handle, "crossfade", 0.4f, nullptr);
        if (!RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "RowlEngine_IsTransitionActive expected true after start" << std::endl;
            exit(1);
        }

        // Since Engine::step clamps dt to 0.25s maximum, step twice to advance 0.5s
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        if (RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "RowlEngine_IsTransitionActive expected false after completion" << std::endl;
            exit(1);
        }
        if (RowlEngine_IsCameraMoving(handle)) {
            std::cerr << "RowlEngine_IsCameraMoving expected false after completion" << std::endl;
            exit(1);
        }

        RowlEngine_ResetCamera(handle);
        RowlEngine_Destroy(handle);

        TEST_PASS("C API Camera & Transition Integration");
    }

    // MS-4: the preview-frame static query behind the host dirty-frame gate.
    // Settled -> 1 (copy can be skipped); transition/shake -> 0; dead
    // handle -> 0 so hosts fall back to copying.
    {
        if (RowlEngine_IsPreviewFrameStatic(nullptr) != 0) {
            std::cerr << "MS-4: dead handle must report not-static" << std::endl;
            exit(1);
        }

        RowlEngineHandle handle = RowlEngine_Create();
        if (!handle) {
            std::cerr << "Failed to create RowlEngine handle" << std::endl;
            exit(1);
        }
        if (!RowlEngine_Init(handle, 960, 540, 0)) {
            std::cerr << "Failed to initialize offscreen engine" << std::endl;
            exit(1);
        }

        // An empty component scene leaves no dialogue, script, or entity
        // behind, so the frame is provably still.
        RowlEngine_UpdateSceneFromJson(handle, "[]");
        RowlEngine_Step(handle, 0.0f);
        if (RowlEngine_IsPreviewFrameStatic(handle) != 1) {
            std::cerr << "MS-4: settled frame must report static" << std::endl;
            exit(1);
        }

        RowlEngine_StartTransition(handle, "crossfade", 0.4f, nullptr);
        if (RowlEngine_IsPreviewFrameStatic(handle) != 0) {
            std::cerr << "MS-4: active transition must report not-static" << std::endl;
            exit(1);
        }
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        if (RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "MS-4: transition did not complete after 0.5s of steps" << std::endl;
            exit(1);
        }
        if (RowlEngine_IsPreviewFrameStatic(handle) != 1) {
            std::cerr << "MS-4: completed transition must settle back to static" << std::endl;
            exit(1);
        }

        RowlEngine_TriggerCameraShake(handle, 15.0f, 0.5f);
        if (RowlEngine_IsPreviewFrameStatic(handle) != 0) {
            std::cerr << "MS-4: camera shake must report not-static" << std::endl;
            exit(1);
        }
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        if (RowlEngine_IsPreviewFrameStatic(handle) != 1) {
            std::cerr << "MS-4: completed shake must settle back to static" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(handle);
        TEST_PASS("MS-4 Preview-Frame Static Query (Dirty-Frame Gate)");
    }

    // Test 6: Component JSON Ingestion for Camera & Transition
    {
        RowlEngineHandle handle = RowlEngine_Create();
        RowlEngine_Init(handle, 1920, 1080, 0);

        std::string componentJson = R"([
            {
                "type": "background",
                "data": { "image": "bg_test.png", "x": 0, "y": 0, "width": 1920, "height": 1080 }
            },
            {
                "type": "camera",
                "data": { "x": 960, "y": 540, "zoom": 1.1, "shake_intensity": 10.0, "shake_duration": 0.3 }
            },
            {
                "type": "transition",
                "data": { "kind": "fade_black", "duration": 0.5 }
            }
        ])";

        RowlEngine_UpdateSceneFromJson(handle, componentJson.c_str());
        if (!RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "Transition was not activated from component JSON" << std::endl;
            exit(1);
        }

        RowlEngine_Step(handle, 0.016f);
        uint32_t bufW = 0, bufH = 0;
        const uint8_t* buffer = RowlEngine_GetPixelBuffer(handle, &bufW, &bufH);
        if (!buffer || bufW != 1920 || bufH != 1080) {
            std::cerr << "Failed to retrieve pixel buffer with camera/transition active" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(handle);
        TEST_PASS("Scene JSON Camera & Transition Component Ingestion");
    }

    // Test 7: Active Transition Render Performance Benchmark
    {
        RowlEngineHandle handle = RowlEngine_Create();
        RowlEngine_Init(handle, 1920, 1080, 0);

        RowlEngine_StartTransition(handle, "crossfade", 2.0f, nullptr);

        const int frameCount = 60;
        const auto benchStart = std::chrono::high_resolution_clock::now();

        for (int f = 0; f < frameCount; ++f) {
            RowlEngine_Step(handle, 0.016f);
        }

        const auto benchEnd = std::chrono::high_resolution_clock::now();
        double elapsedMs = std::chrono::duration<double, std::milli>(benchEnd - benchStart).count();
        double fps = (frameCount / elapsedMs) * 1000.0;

        std::cout << "  ⚡ [Benchmark] Active Transition Render: " << frameCount << " frames rendered in "
                  << std::fixed << std::setprecision(2) << elapsedMs << "ms (~"
                  << static_cast<int>(fps) << " FPS)" << std::endl;
        g_transitionFps = fps;

        // A wall-clock FPS floor is meaningless under sanitizer
        // instrumentation (2-5x slowdown is the tool, not the engine), so it
        // is enforced only on clean builds. Frame correctness above is
        // asserted unconditionally.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || defined(__SANITIZE_UNDEFINED__)
        std::cout << "  (sanitizer build: 30 FPS floor reported, not enforced)" << std::endl;
#else
        // A universal wall-clock gate is not valid until a compatible baseline
        // exists for the exact machine/build/fixture. Report by default; a
        // controlled performance host may opt in with ROWL_PERF_FLOOR=enforced.
        const bool perfFloorEnforced = environmentValue("ROWL_PERF_FLOOR", "report") == "enforced";
        if (perfFloorEnforced && fps < 30.0) {
            std::cerr << "Transition render FPS is too low: " << fps << std::endl;
            exit(1);
        }
        if (!perfFloorEnforced) {
            std::cout << "  (ROWL_PERF_FLOOR=report: 30 FPS floor reported, not enforced)" << std::endl;
        }
#endif

        RowlEngine_Destroy(handle);
        TEST_PASS("Active Scene Transition 60-Frame Render Performance");
    }

    // Test 8: Cinematic Camera Shake Presets & Directional Profiles
    {
        Rowl::Render::Camera2D cam(1920.0f, 1080.0f);

        // Earthquake preset: predominantly horizontal (X-axis heavy, dirX=1.0, dirY=0.2)
        cam.shakePreset(Rowl::Render::CameraShakePreset::Earthquake);
        if (cam.getShakePreset() != Rowl::Render::CameraShakePreset::Earthquake ||
            std::abs(cam.getShakeDirX() - 1.0f) > 0.01f ||
            std::abs(cam.getShakeDirY() - 0.2f) > 0.01f ||
            std::abs(cam.getShakeDamping() - 0.7f) > 0.01f) {
            std::cerr << "Camera Earthquake preset attributes mismatch" << std::endl;
            exit(1);
        }
        cam.update(0.05f);
        if (!cam.isShaking() || !cam.isMoving()) {
            std::cerr << "Camera should be shaking during earthquake" << std::endl;
            exit(1);
        }

        // Explosion preset: rapid damping (2.2) and high frequency.
        // #147-artık: erken preset değişimi birleşir; deprem shake'i
        // bitmeden patlama uygulanmaz — önce settle edilir.
        cam.update(1.2f);
        cam.shakePreset(Rowl::Render::CameraShakePreset::Explosion, 1.5f);
        if (cam.getShakePreset() != Rowl::Render::CameraShakePreset::Explosion ||
            std::abs(cam.getShakeDamping() - 2.2f) > 0.01f) {
            std::cerr << "Camera Explosion preset damping mismatch" << std::endl;
            exit(1);
        }

        // Heartbeat / Pulse preset: string overload (#147-artık: settle sonrası).
        cam.update(0.7f);
        cam.shakePreset("heartbeat");
        if (cam.getShakePreset() != Rowl::Render::CameraShakePreset::Heartbeat ||
            std::abs(cam.getShakeDirX() - 0.15f) > 0.01f ||
            std::abs(cam.getShakeDirY() - 1.0f) > 0.01f) {
            std::cerr << "Camera Heartbeat preset directional profile mismatch" << std::endl;
            exit(1);
        }

        // Custom profile with full parameters (#147-artık: settle sonrası).
        cam.update(1.5f);
        cam.shakeWithProfile(Rowl::Render::CameraShakePreset::Custom, 20.0f, 0.5f, 30.0f, 1.5f, 0.8f, 0.4f);
        if (std::abs(cam.getShakeDirX() - 0.8f) > 0.01f || std::abs(cam.getShakeDirY() - 0.4f) > 0.01f ||
            std::abs(cam.getShakeDamping() - 1.5f) > 0.01f) {
            std::cerr << "Camera shakeWithProfile parameters mismatch" << std::endl;
            exit(1);
        }

        // std::clamp does not sanitize NaN. Invalid direction values must
        // leave the active profile untouched rather than reaching projection.
        cam.shakeWithProfile(Rowl::Render::CameraShakePreset::Custom, 99.0f, 9.0f, 30.0f, 1.5f,
                             std::numeric_limits<float>::quiet_NaN(), 0.4f);
        if (!std::isfinite(cam.getShakeDirX()) || !std::isfinite(cam.getShakeDirY()) ||
            std::abs(cam.getShakeDirX() - 0.8f) > 0.01f || std::abs(cam.getShakeDirY() - 0.4f) > 0.01f) {
            std::cerr << "Non-finite camera shake direction corrupted active profile" << std::endl;
            exit(1);
        }

        cam.reset();
        if (cam.isShaking() || cam.isMoving() || cam.getShakeOffsetX() != 0.0f || cam.getShakeOffsetY() != 0.0f) {
            std::cerr << "Camera reset failed to clear shake state" << std::endl;
            exit(1);
        }

        TEST_PASS("Camera2D Cinematic Shake Presets & Directional Profiles");
    }

    // Test 9: Screen Visual FX Pipeline (Flash, Tint, Vignette) via C API
    {
        RowlEngineHandle handle = RowlEngine_Create();
        if (!RowlEngine_Init(handle, 1920, 1080, 0)) {
            std::cerr << "Failed to init offscreen engine for screen FX test" << std::endl;
            exit(1);
        }

        // Trigger Screen Flash
        RowlEngine_TriggerScreenFlashHex(handle, "#FFFFFF", 0.4f, 1.0f);
        if (!RowlEngine_IsScreenFlashActive(handle)) {
            std::cerr << "RowlEngine_IsScreenFlashActive expected true after trigger" << std::endl;
            exit(1);
        }

        // Advance 0.5s via two steps (clamped at 0.25s)
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        if (RowlEngine_IsScreenFlashActive(handle)) {
            std::cerr << "RowlEngine_IsScreenFlashActive expected false after duration" << std::endl;
            exit(1);
        }

        // Set Screen Tint
        RowlEngine_SetScreenTintHex(handle, "#0A183D", 0.45f);
        if (std::abs(RowlEngine_GetScreenTintOpacity(handle) - 0.45f) > 0.01f) {
            std::cerr << "RowlEngine_GetScreenTintOpacity mismatch" << std::endl;
            exit(1);
        }
        RowlEngine_Step(handle, 0.016f);

        // Clear Screen Tint
        RowlEngine_ClearScreenTint(handle);
        if (RowlEngine_GetScreenTintOpacity(handle) != 0.0f) {
            std::cerr << "RowlEngine_ClearScreenTint failed" << std::endl;
            exit(1);
        }

        RowlEngine_SetScreenTintHex(handle, "#0A183D", 0.45f);
        RowlEngine_SetScreenTintHex(handle, "#FFFFFF", std::numeric_limits<float>::quiet_NaN());
        if (!std::isfinite(RowlEngine_GetScreenTintOpacity(handle)) ||
            std::abs(RowlEngine_GetScreenTintOpacity(handle) - 0.45f) > 0.01f) {
            std::cerr << "Non-finite screen tint opacity corrupted active tint" << std::endl;
            exit(1);
        }

        // Set Vignette
        RowlEngine_SetVignette(handle, 0.70f, 0.8f, "#000000");
        if (std::abs(RowlEngine_GetVignetteIntensity(handle) - 0.70f) > 0.01f) {
            std::cerr << "RowlEngine_GetVignetteIntensity mismatch" << std::endl;
            exit(1);
        }
        RowlEngine_SetVignette(handle, 0.95f, std::numeric_limits<float>::quiet_NaN(), "#FFFFFF");
        if (!std::isfinite(RowlEngine_GetVignetteIntensity(handle)) ||
            std::abs(RowlEngine_GetVignetteIntensity(handle) - 0.70f) > 0.01f) {
            std::cerr << "Non-finite vignette radius corrupted active effect" << std::endl;
            exit(1);
        }

        RowlEngine_TriggerScreenFlashHex(handle, "#FFFFFF", 0.4f, 0.5f);
        RowlEngine_TriggerScreenFlashHex(handle, "#FFFFFF", 60.0f, std::numeric_limits<float>::quiet_NaN());
        RowlEngine_Step(handle, 0.25f);
        RowlEngine_Step(handle, 0.25f);
        if (RowlEngine_IsScreenFlashActive(handle)) {
            std::cerr << "Non-finite screen flash intensity extended active effect" << std::endl;
            exit(1);
        }
        RowlEngine_Step(handle, 0.016f);

        // Verify pixel buffer with active vignette
        uint32_t bufW = 0, bufH = 0;
        const uint8_t* buffer = RowlEngine_GetPixelBuffer(handle, &bufW, &bufH);
        if (!buffer || bufW != 1920 || bufH != 1080) {
            std::cerr << "Failed to retrieve pixel buffer with vignette active" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(handle);
        TEST_PASS("Screen Visual FX Pipeline (Flash, Tint, Vignette) via C API");
    }

    // Test 10: Scene JSON Ingestion for Shake Presets & Screen Visual FX
    {
        RowlEngineHandle handle = RowlEngine_Create();
        RowlEngine_Init(handle, 1920, 1080, 0);

        std::string componentJson = R"([
            {
                "type": "background",
                "data": { "image": "bg_test.png", "x": 0, "y": 0, "width": 1920, "height": 1080 }
            },
            {
                "type": "camera",
                "data": { "x": 960, "y": 540, "zoom": 1.0, "shake_preset": "earthquake", "shake_intensity_multiplier": 1.2 }
            },
            {
                "type": "transition",
                "data": {
                    "kind": "crossfade",
                    "duration": 0.5,
                    "flash_enabled": true,
                    "flash_color": "#FFEEEE",
                    "flash_duration": 0.4,
                    "tint_enabled": true,
                    "tint_color": "#102040",
                    "tint_opacity": 0.3,
                    "vignette_enabled": true,
                    "vignette_intensity": 0.5
                }
            }
        ])";

        RowlEngine_UpdateSceneFromJson(handle, componentJson.c_str());

        if (!RowlEngine_IsTransitionActive(handle)) {
            std::cerr << "Scene transition was not activated from JSON" << std::endl;
            exit(1);
        }
        if (!RowlEngine_IsScreenFlashActive(handle)) {
            std::cerr << "Screen flash was not activated from JSON" << std::endl;
            exit(1);
        }
        if (std::abs(RowlEngine_GetScreenTintOpacity(handle) - 0.3f) > 0.01f) {
            std::cerr << "Screen tint opacity was not applied from JSON" << std::endl;
            exit(1);
        }
        if (std::abs(RowlEngine_GetVignetteIntensity(handle) - 0.5f) > 0.01f) {
            std::cerr << "Vignette intensity was not applied from JSON" << std::endl;
            exit(1);
        }
        if (!RowlEngine_IsCameraMoving(handle)) {
            std::cerr << "Camera was not shaking from earthquake preset JSON" << std::endl;
            exit(1);
        }

        RowlEngine_Step(handle, 0.016f);
        uint32_t bufW = 0, bufH = 0;
        const uint8_t* buffer = RowlEngine_GetPixelBuffer(handle, &bufW, &bufH);
        if (!buffer || bufW != 1920 || bufH != 1080) {
            std::cerr << "Failed to retrieve pixel buffer with JSON shake & visual FX active" << std::endl;
            exit(1);
        }

        RowlEngine_Destroy(handle);
        TEST_PASS("Scene JSON Shake Presets & Screen Visual FX Ingestion");
    }

    // Test: ComposedFrame renders byte-identical pixels to the legacy call.
    {
        Rowl::VFS::VFSManager frameVfs;
        frameVfs.remountProject(std::filesystem::current_path().string());
        Rowl::Render::Window window(&frameVfs);
        if (!window.initializeOffscreen(320, 180)) {
            std::cerr << "Could not initialize offscreen window for composed-frame test" << std::endl;
            exit(1);
        }
        const std::vector<Rowl::Render::CharacterRenderData> noCharacters;
        const std::vector<Rowl::Render::DialogueRenderData> noDialogues;
        const std::vector<Rowl::Render::ChoiceButtonRenderData> noChoices;
        window.renderVisualNovelFrame(false, "", 0.0f, 0.0f, 1920.0f, 1080.0f,
                                      noCharacters, noDialogues, noChoices,
                                      0.0f, 1.0f, 1.0f, 1.0f);
        window.endFrame();
        const uint32_t pixelBytes = window.getWidth() * window.getHeight() * 4u;
        const uint8_t* legacyPixels = window.getPixelBuffer();
        if (!legacyPixels || pixelBytes == 0) {
            std::cerr << "Legacy frame produced no pixel buffer" << std::endl;
            exit(1);
        }
        const std::vector<uint8_t> legacyCopy(legacyPixels, legacyPixels + pixelBytes);

        Rowl::Render::ComposedFrame frame;
        frame.hasBackground = false;
        frame.background = "";
        frame.backgroundX = 0.0f;
        frame.backgroundY = 0.0f;
        frame.backgroundWidth = 1920.0f;
        frame.backgroundHeight = 1080.0f;
        frame.backgroundRotation = 0.0f;
        frame.backgroundParallaxX = 1.0f;
        frame.backgroundParallaxY = 1.0f;
        frame.backgroundOpacity = 1.0f;
        window.renderComposedFrame(frame);
        window.endFrame();
        const uint8_t* composedPixels = window.getPixelBuffer();
        if (!composedPixels ||
            std::memcmp(composedPixels, legacyCopy.data(), pixelBytes) != 0) {
            std::cerr << "ComposedFrame pixels differ from the legacy frame call" << std::endl;
            exit(1);
        }
        window.shutdown();
        TEST_PASS("ComposedFrame Forwards Byte-Identical Pixels to Render Boundary");
    }

    // Test: identical frames reuse cached pixels; any content change
    // re-renders deterministically.
    {
        Rowl::VFS::VFSManager cacheVfs;
        cacheVfs.remountProject(std::filesystem::current_path().string());
        Rowl::Render::Window window(&cacheVfs);
        if (!window.initializeOffscreen(320, 180)) {
            std::cerr << "Could not initialize offscreen window for frame-cache test" << std::endl;
            exit(1);
        }
        Rowl::Render::ComposedFrame frameA;
        frameA.hasBackground = false;
        window.renderComposedFrame(frameA);
        window.endFrame();
        if (window.lastFrameReusedCache() ||
            window.getLastFrameRendererFlushMilliseconds() <= 0.0) {
            std::cerr << "First identical frame did not render" << std::endl;
            exit(1);
        }
        const uint32_t pixelBytes = window.getWidth() * window.getHeight() * 4u;
        const std::vector<uint8_t> snapshotA(
            window.getPixelBuffer(), window.getPixelBuffer() + pixelBytes);

        window.renderComposedFrame(frameA);
        window.endFrame();
        if (!window.lastFrameReusedCache() ||
            window.getLastFrameRendererFlushMilliseconds() != 0.0 ||
            window.getLastFrameNonTextureRenderMilliseconds() != 0.0 ||
            window.getLastFrameTextureLoadMilliseconds() != 0.0) {
            std::cerr << "Identical frame did not reuse the pixel cache" << std::endl;
            exit(1);
        }
        if (std::memcmp(window.getPixelBuffer(), snapshotA.data(), pixelBytes) != 0) {
            std::cerr << "Cached frame pixels differ from the rendered frame" << std::endl;
            exit(1);
        }

        Rowl::Render::ComposedFrame frameB = frameA;
        frameB.dialogues.emplace_back();
        window.renderComposedFrame(frameB);
        window.endFrame();
        if (window.lastFrameReusedCache()) {
            std::cerr << "Changed frame incorrectly reused the pixel cache" << std::endl;
            exit(1);
        }
        if (std::memcmp(window.getPixelBuffer(), snapshotA.data(), pixelBytes) == 0) {
            std::cerr << "Changed frame did not produce new pixels" << std::endl;
            exit(1);
        }

        window.renderComposedFrame(frameA);
        window.endFrame();
        if (window.lastFrameReusedCache() ||
            std::memcmp(window.getPixelBuffer(), snapshotA.data(), pixelBytes) != 0) {
            std::cerr << "Restored frame did not re-render deterministically" << std::endl;
            exit(1);
        }
        window.shutdown();
        TEST_PASS("Identical Frames Reuse Cached Pixels; Changes Re-render");
    }

    testTransitionHexUnification();
    testTransitionValidateBeforeSnapshot();
    testScreenFxRetriggerCoalesce();
}
