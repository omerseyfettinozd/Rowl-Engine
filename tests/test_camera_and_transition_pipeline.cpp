/**
 * test_camera_and_transition_pipeline.cpp — Camera pipeline, transitions, render benchmarks.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"
#include "rowl/render/frame_composition.hpp"

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

        // Wipe transitions
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

        // Explosion preset: rapid damping (2.2) and high frequency
        cam.shakePreset(Rowl::Render::CameraShakePreset::Explosion, 1.5f);
        if (cam.getShakePreset() != Rowl::Render::CameraShakePreset::Explosion ||
            std::abs(cam.getShakeDamping() - 2.2f) > 0.01f) {
            std::cerr << "Camera Explosion preset damping mismatch" << std::endl;
            exit(1);
        }

        // Heartbeat / Pulse preset: string overload
        cam.shakePreset("heartbeat");
        if (cam.getShakePreset() != Rowl::Render::CameraShakePreset::Heartbeat ||
            std::abs(cam.getShakeDirX() - 0.15f) > 0.01f ||
            std::abs(cam.getShakeDirY() - 1.0f) > 0.01f) {
            std::cerr << "Camera Heartbeat preset directional profile mismatch" << std::endl;
            exit(1);
        }

        // Custom profile with full parameters
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
}
