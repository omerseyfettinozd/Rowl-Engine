/**
 * test_native_c_api.cpp — Native C-API lifecycle, VFS-first flows, step wiring.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_native_c_api() {
    TEST_SECTION("Native C-API & Full Render Loop");

    // The ABI must be defensive because this is called through P/Invoke.
    // Invalid handles and malformed editor data must never unwind into .NET.
    uint32_t nullWidth = 99;
    uint32_t nullHeight = 99;
    if (RowlEngine_Init(nullptr, 1920, 1080, 0) != 0 ||
        RowlEngine_IsRunning(nullptr) != 0 ||
        RowlEngine_GetPixelBuffer(nullptr, &nullWidth, &nullHeight) != nullptr ||
        nullWidth != 0 || nullHeight != 0 ||
        std::strlen(RowlEngine_GetSpeaker(nullptr)) != 0 ||
        std::strlen(RowlEngine_GetDialogue(nullptr)) != 0 ||
        RowlEngine_GetCurrentNodeId(nullptr) != 0) {
        std::cerr << "C API null-handle fallback contract failed" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Null-Handle Fallback Contract");

    // Invalid host dimensions must fail before SDL/offscreen allocation, and a
    // later valid initialization on the same opaque handle must still work.
    RowlEngineHandle invalidDimensionHandle = RowlEngine_Create();
    if (!invalidDimensionHandle || RowlEngine_Init(invalidDimensionHandle, 0, 1080, 0) != 0 ||
        RowlEngine_Init(invalidDimensionHandle, 20'000, 1080, 0) != 0 ||
        RowlEngine_Init(invalidDimensionHandle, 1920, 1080, 0) != 1) {
        std::cerr << "C-API virtual canvas dimension validation failed" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(invalidDimensionHandle);
    TEST_PASS("C-API Virtual Canvas Bounds and Retry Safety");

    RowlEngineHandle handle = RowlEngine_Create();
    if (!handle) exit(1);

    int initRes = RowlEngine_Init(handle, 1920, 1080, 0);
    if (initRes != 1 || RowlEngine_IsRunning(handle) != 1) exit(1);
    TEST_PASS("RowlEngine_Create & Init (1920x1080 Offscreen)");

    // Explicit ownership: observe this handle's Engine through the test-only
    // bridge, never through a process global.
    Rowl::Core::Engine* cApiEngine = Rowl::Core::testEngineFromHandle(handle);
    if (!cApiEngine) {
        std::cerr << "Test bridge could not resolve the C-API engine" << std::endl;
        exit(1);
    }

    // Every C API handle owns its runtime state. Destroying one must neither
    // invalidate the other handle nor tear down its shared SDL subsystems.
    RowlEngineHandle secondHandle = RowlEngine_Create();
    if (!secondHandle || RowlEngine_Init(secondHandle, 320, 180, 0) != 1) {
        std::cerr << "C-API failed to initialize a second concurrent runtime" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"speaker","data":{"speaker":"Runtime A","dialogue":"A"}}])");
    RowlEngine_UpdateSceneFromJson(secondHandle,
        R"([{"type":"speaker","data":{"speaker":"Runtime B","dialogue":"B"}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Runtime A" ||
        std::string(RowlEngine_GetSpeaker(secondHandle)) != "Runtime B") {
        std::cerr << "Concurrent C-API runtimes leaked scene state" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(secondHandle);
    RowlEngine_Step(handle, 0.016f);
    if (RowlEngine_IsRunning(handle) != 1 || std::string(RowlEngine_GetSpeaker(handle)) != "Runtime A") {
        std::cerr << "Destroying one C-API runtime damaged its sibling runtime" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Concurrent Runtime Isolation and SDL Lease Retention");

    RowlEngine_Step(handle, std::numeric_limits<float>::quiet_NaN());
    RowlEngine_Step(handle, -1.0f);
    RowlEngine_Step(handle, 10.0f);
    if (RowlEngine_IsRunning(handle) != 1) {
        std::cerr << "C-API frame delta normalization destabilized the engine" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Frame Delta NaN, Negative, and Spike Containment");

    // Hostile-input matrix: extreme dimensions, null outputs, infinities and
    // NaN coordinates must degrade safely without crashing or corrupting state.
    {
        RowlEngineHandle extremeHandle = RowlEngine_Create();
        if (!extremeHandle ||
            RowlEngine_Init(extremeHandle, 0xFFFFFFFFu, 0xFFFFFFFFu, 0) != 0 ||
            RowlEngine_Init(extremeHandle, 1920, 1080, 0) != 1) {
            std::cerr << "C-API extreme dimensions were not rejected with retry intact" << std::endl;
            exit(1);
        }
        RowlEngine_Destroy(extremeHandle);
        if (RowlEngine_GetPixelBuffer(handle, nullptr, nullptr) == nullptr) {
            std::cerr << "C-API pixel buffer rejected null dimension outputs" << std::endl;
            exit(1);
        }
        RowlEngine_Step(handle, std::numeric_limits<float>::infinity());
        const uint64_t nodeBeforeNaN = RowlEngine_GetCurrentNodeId(handle);
        RowlEngine_PointerDown(handle, std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::quiet_NaN());
        RowlEngine_Step(handle, 0.016f);
        if (RowlEngine_IsRunning(handle) != 1 ||
            RowlEngine_GetCurrentNodeId(handle) != nodeBeforeNaN) {
            std::cerr << "C-API infinity step or NaN pointer destabilized the engine" << std::endl;
            exit(1);
        }
        if (RowlEngine_SelectChoice(handle, nullptr) != 0 ||
            RowlEngine_SelectChoice(handle, "") != 0) {
            std::cerr << "C-API null or empty choice was not rejected" << std::endl;
            exit(1);
        }
    }
    TEST_PASS("C-API Hostile Input Matrix Degrades Without Crash or Corruption");

    // Component JSON Scene Push
    const char* compJson = R"([
        {"type":"speaker","id":"s1","enabled":true,"data":{"speaker":"Alice","dialogue":"Automated C++ Unit Test Dialogue\nWith second line."}},
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"scale":1}},
        {"type":"character","id":"c1","enabled":true,"data":{"sprite":"Margot.jpg","x":300,"y":200,"width":360,"height":540,"scale":1}},
        {"type":"character","id":"c2","enabled":true,"data":{"sprite":"Margot.jpg","x":1200,"y":200,"width":360,"height":540,"scale":1}},
        {"type":"dialogue_box","id":"d1","enabled":true,"data":{"x":80,"y":840,"width":1760,"height":200,"scale":1}},
        {"type":"audio","id":"a1","enabled":true,"data":{"dsp_filter":"Underwater"}}
    ])";

    RowlEngine_UpdateSceneFromJson(handle, compJson);
    TEST_PASS("RowlEngine_UpdateSceneFromJson (Multi-Character + Multi-Line Dialogue)");

    // Milestone 22: Visual Transform Gizmo & Rotation/Scale Controls C-API Verification
    RowlEngine_UpdateSceneEx(handle, "Evelyn", "Rotated scene test", "bg_beach_sunset.png",
                            0.0f, 0.0f, 1920.0f, 1080.0f, 45.0f,
                            "spr_evelyn.png", 1440.0f, 340.0f, 360.0f, 540.0f, -15.0f,
                            80.0f, 860.0f, 1760.0f, 180.0f);
    if (std::abs(RowlEngine_GetBackgroundRotation(handle) - 45.0f) > 0.001f ||
        std::abs(RowlEngine_GetCharacterRotation(handle) - (-15.0f)) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneEx failed to set background/character rotation" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);

    const char* compJsonRot = R"([
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"scale":1,"rotation":90.0}},
        {"type":"character","id":"c1","enabled":true,"data":{"sprite":"Margot.jpg","x":300,"y":200,"width":360,"height":540,"scale":1,"scale_x":1.2,"scale_y":0.8,"rotation":180.0}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, compJsonRot);
    if (std::abs(RowlEngine_GetBackgroundRotation(handle) - 90.0f) > 0.001f ||
        std::abs(RowlEngine_GetCharacterRotation(handle) - 180.0f) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneFromJson failed to parse background/character rotation" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    TEST_PASS("Milestone 22: RowlEngine_UpdateSceneEx & UpdateSceneFromJson Rotation / Scale Controls");

    // Milestone 23: Real-Time Audio Telemetry, Peak/RMS & Spectrum C-API Verification
    float peakBgm = RowlEngine_GetAudioChannelPeak(handle, 0, 0);
    float rmsBgm = RowlEngine_GetAudioChannelRms(handle, 0, 0);
    float bands[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    RowlEngine_GetAudioSpectrum(handle, bands, 4);
    if (peakBgm < 0.0f || peakBgm > 1.0f || rmsBgm < 0.0f || rmsBgm > 1.0f) {
        std::cerr << "RowlEngine_GetAudioChannelPeak / Rms returned out-of-range value" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    RowlEngine_GetAudioSpectrum(handle, bands, 4);
    TEST_PASS("Milestone 23: RowlEngine_GetAudioChannelPeak, Rms & Spectrum Telemetry C-API");

    // Milestone 24: Parallax Depth Background C-API Verification
    RowlEngine_SetBackgroundParallax(handle, 0.4f, 0.6f);
    if (std::abs(RowlEngine_GetBackgroundParallaxX(handle) - 0.4f) > 0.001f ||
        std::abs(RowlEngine_GetBackgroundParallaxY(handle) - 0.6f) > 0.001f) {
        std::cerr << "RowlEngine_SetBackgroundParallax failed" << std::endl;
        exit(1);
    }

    const char* compJsonParallax = R"([
        {"type":"background","id":"bg_parallax","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"parallax_x":0.25,"parallax_y":0.5,"opacity":0.85}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, compJsonParallax);
    if (std::abs(RowlEngine_GetBackgroundParallaxX(handle) - 0.25f) > 0.001f ||
        std::abs(RowlEngine_GetBackgroundParallaxY(handle) - 0.5f) > 0.001f ||
        std::abs(RowlEngine_GetBackgroundOpacity(handle) - 0.85f) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneFromJson failed to parse parallax and opacity" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.016f);
    TEST_PASS("Milestone 24: RowlEngine_SetBackgroundParallax & Background Parallax/Opacity C-API");

    // Milestone 25: Typewriter Voice Blips & Dialogue Audio Effects C-API Verification
    RowlEngine_ResetVoiceBlipCount(handle);
    if (RowlEngine_GetVoiceBlipCount(handle) != 0) {
        std::cerr << "RowlEngine_ResetVoiceBlipCount failed" << std::endl;
        exit(1);
    }
    RowlEngine_PlayVoiceBlip(handle, "", 1.25f, 0.8f, 1);
    if (RowlEngine_GetVoiceBlipCount(handle) != 1) {
        std::cerr << "RowlEngine_PlayVoiceBlip failed to increment blip count" << std::endl;
        exit(1);
    }
    RowlEngine_SetDialogueVoiceBlip(handle, "blip.wav", 1.15f, 0.08f, 2, 1, 1);
    if (std::string(RowlEngine_GetDialogueVoiceBlipSound(handle)) != "blip.wav" ||
        std::abs(RowlEngine_GetDialogueVoiceBlipPitch(handle) - 1.15f) > 0.001f ||
        std::abs(RowlEngine_GetDialogueVoiceBlipVariance(handle) - 0.08f) > 0.001f ||
        RowlEngine_GetDialogueVoiceBlipCadence(handle) != 2 ||
        RowlEngine_GetDialogueVoiceBlipSkipPunctuation(handle) != 1) {
        std::cerr << "RowlEngine_SetDialogueVoiceBlip failed to set dialogue voice blip parameters" << std::endl;
        exit(1);
    }

    const char* compJsonVoiceBlip = R"([
        {"type":"dialogue","id":"d_voice","enabled":true,"data":{
            "speaker":"Evelyn",
            "dialogue":"Testing typewriter voice blip effects! Amazing...",
            "typewriter_enabled":true,
            "typewriter_speed":50.0,
            "voice_blip_sound":"",
            "voice_blip_pitch":1.2,
            "voice_blip_variance":0.05,
            "voice_blip_cadence":1,
            "voice_blip_skip_punctuation":true,
            "voice_blip_volume":0.8,
            "voice_blip_channel":1
        }}
    ])";
    RowlEngine_SetPlayState(handle, 1);
    RowlEngine_ResetVoiceBlipCount(handle);
    RowlEngine_UpdateSceneFromJson(handle, compJsonVoiceBlip);
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Evelyn" ||
        RowlEngine_GetDialogueVoiceBlipCadence(handle) != 1 ||
        RowlEngine_GetDialogueVoiceBlipSkipPunctuation(handle) != 1 ||
        std::abs(RowlEngine_GetDialogueVoiceBlipPitch(handle) - 1.2f) > 0.001f ||
        std::abs(RowlEngine_GetDialogueVoiceBlipVolume(handle) - 0.8f) > 0.001f) {
        std::cerr << "RowlEngine_UpdateSceneFromJson failed to parse voice blip configuration" << std::endl;
        exit(1);
    }
    // Test Volume C API setter and getter
    RowlEngine_SetDialogueVoiceBlipVolume(handle, 0.45f);
    if (std::abs(RowlEngine_GetDialogueVoiceBlipVolume(handle) - 0.45f) > 0.001f) {
        std::cerr << "RowlEngine_SetDialogueVoiceBlipVolume failed" << std::endl;
        exit(1);
    }

    // Step forward 100ms to reveal characters and trigger voice blips
    RowlEngine_Step(handle, 0.1f);
    if (RowlEngine_GetVoiceBlipCount(handle) == 0) {
        std::cerr << "Typewriter progression did not trigger voice blips" << std::endl;
        exit(1);
    }

    // Deep Verification: Character Default Voice Blip Inheritance (Procedural Pitch & Cadence)
    const char* compJsonInherit = R"([
        {"type":"character","id":"c_evelyn","enabled":true,"data":{
            "sprite":"spr_evelyn.png",
            "voice_blip_sound":"",
            "voice_blip_pitch":1.45,
            "voice_blip_cadence":2
        }},
        {"type":"dialogue","id":"d_inherit","enabled":true,"data":{
            "speaker":"Evelyn",
            "dialogue":"Hello world!",
            "typewriter_enabled":true,
            "typewriter_speed":40.0
        }}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, compJsonInherit);
    if (std::abs(RowlEngine_GetDialogueVoiceBlipPitch(handle) - 1.45f) > 0.001f ||
        RowlEngine_GetDialogueVoiceBlipCadence(handle) != 2) {
        std::cerr << "Dialogue failed to inherit character procedural voice blip pitch/cadence" << std::endl;
        exit(1);
    }

    // Deep Verification: Punctuation-Only Dialogue Triggers 0 Blips When SkipPunctuation is Enabled
    const char* compJsonPunctuation = R"([
        {"type":"dialogue","id":"d_punct","enabled":true,"data":{
            "speaker":"Evelyn",
            "dialogue":"......",
            "typewriter_enabled":true,
            "typewriter_speed":30.0,
            "voice_blip_skip_punctuation":true
        }}
    ])";
    RowlEngine_ResetVoiceBlipCount(handle);
    RowlEngine_UpdateSceneFromJson(handle, compJsonPunctuation);
    RowlEngine_Step(handle, 0.2f);
    if (RowlEngine_GetVoiceBlipCount(handle) != 0) {
        std::cerr << "Punctuation skipping failed: voice blips were triggered on punctuation-only text" << std::endl;
        exit(1);
    }

    RowlEngine_SetPlayState(handle, 0);
    TEST_PASS("Milestone 25: Typewriter Voice Blips, Volume C-API, Character Inheritance & Punctuation Defense");

    // MS-6: click-to-complete is a property of the presented line, not of the
    // play state. A non-playing (preview-like) engine must complete a typing
    // line on the first advance request and move on only on the second one.
    // Keyboard (AdvanceNode) and pointer (PointerDown on empty canvas) share
    // the single advanceToNextNode funnel, so both behave identically.
    {
        const auto typeGraphPath = std::filesystem::temp_directory_path() / "rowl_ms6_typewriter_graph.json";
        {
            std::ofstream graph(typeGraphPath);
            graph << R"({"format_version":4,"start_node_id":101,"nodes":[
              {"id":101,"speaker":"Evelyn","dialogue":"Typed.","components":[
                {"type":"dialogue","id":"d_ms6","enabled":true,"data":{
                  "speaker":"Evelyn",
                  "dialogue":"This line is long enough that a short step leaves it mid-typing.",
                  "typewriter_enabled":true,"text_speed":30}}],
               "next_nodes":[{"id":102}]},
              {"id":102,"speaker":"Evelyn","dialogue":"Second."}]})";
        }
        RowlEngine_LoadStoryGraph(handle, typeGraphPath.string().c_str());
        RowlEngine_Step(handle, 0.0f);
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "MS-6: typewriter fixture did not present node 101" << std::endl;
            exit(1);
        }
        if (cApiEngine->getActiveDialogues().empty() ||
            cApiEngine->getActiveDialogues()[0].elapsedTypewriterTime != 0.0f) {
            std::cerr << "MS-6: presenting a line must arm its typewriter even while paused" << std::endl;
            exit(1);
        }
        // Paused presentation still renders full text and reports a static
        // frame (MS-4 dirty-frame gate preserved).
        if (RowlEngine_IsPreviewFrameStatic(handle) != 1) {
            std::cerr << "MS-6: paused typewriter presentation must report a static frame" << std::endl;
            exit(1);
        }
        RowlEngine_Step(handle, 0.2f);
        const float elapsedMid = cApiEngine->getActiveDialogues()[0].elapsedTypewriterTime;
        if (!(elapsedMid > 0.0f)) {
            std::cerr << "MS-6: typewriter did not progress on a real-dt step while paused" << std::endl;
            exit(1);
        }
        // First keyboard advance completes the line; the node must not move.
        RowlEngine_AdvanceNode(handle, 0);
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "MS-6: mid-typing AdvanceNode skipped the node instead of completing it" << std::endl;
            exit(1);
        }
        // Second advance moves on.
        RowlEngine_AdvanceNode(handle, 0);
        if (RowlEngine_GetCurrentNodeId(handle) != 102) {
            std::cerr << "MS-6: completed line did not advance on the second request" << std::endl;
            exit(1);
        }
        // Pointer parity: reload, step mid-typing, click empty canvas. The
        // C-API PointerDown reports choice hits only; hosts compose it with
        // AdvanceNode exactly like the player event loop and the editor
        // preview do, so the test mirrors that composition.
        auto clickEmptyCanvas = [&] {
            if (RowlEngine_PointerDown(handle, 960.0f, 100.0f) != 0) return;
            RowlEngine_AdvanceNode(handle, 0);
        };
        RowlEngine_LoadStoryGraph(handle, typeGraphPath.string().c_str());
        RowlEngine_Step(handle, 0.0f);
        RowlEngine_Step(handle, 0.2f);
        clickEmptyCanvas();
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "MS-6: mid-typing pointer click skipped the node instead of completing it" << std::endl;
            exit(1);
        }
        clickEmptyCanvas();
        if (RowlEngine_GetCurrentNodeId(handle) != 102) {
            std::cerr << "MS-6: completed line did not advance on the second pointer click" << std::endl;
            exit(1);
        }
        // NOTE: typeGraphPath is reused by the slot/pause blocks below and
        // removed at the end of the pause-menu tour.
        TEST_PASS("MS-6 Preview Click-to-Complete & Keyboard/Pointer Parity");
    }

    // MS-6: quick slots 0-9, active-slot quick save/load wiring, and the
    // pause-menu skeleton (nav, values, slot pages, exit confirmation).
    {
        const auto typeGraphPath = std::filesystem::temp_directory_path() / "rowl_ms6_typewriter_graph.json";
        if (RowlEngine_GetQuickSaveSlot(handle) != 0) {
            std::cerr << "MS-6: default quick slot must be 0" << std::endl;
            exit(1);
        }
        if (RowlEngine_SetQuickSaveSlot(handle, 11) != 0 ||
            RowlEngine_SetQuickSaveSlot(handle, -1) != 0 ||
            RowlEngine_GetQuickSaveSlot(handle) != 0) {
            std::cerr << "MS-6: out-of-range quick slot must be rejected" << std::endl;
            exit(1);
        }
        if (RowlEngine_SetQuickSaveSlot(handle, 4) != 1 ||
            RowlEngine_GetQuickSaveSlot(handle) != 4) {
            std::cerr << "MS-6: quick slot selection failed" << std::endl;
            exit(1);
        }
        // The fixture left node 102 current; quick-save pins it to slot 4.
        if (RowlEngine_QuickSave(handle) != 1 || RowlEngine_HasSaveSlot(handle, 4) != 1) {
            std::cerr << "MS-6: QuickSave did not persist the active slot" << std::endl;
            exit(1);
        }
        if (RowlEngine_SaveGameSlot(handle, 7) != 1 || RowlEngine_HasSaveSlot(handle, 7) != 1) {
            std::cerr << "MS-6: extended slot 7 round-trip failed" << std::endl;
            exit(1);
        }
        RowlEngine_LoadStoryGraph(handle, typeGraphPath.string().c_str());
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "MS-6: fixture reload did not rewind to node 101" << std::endl;
            exit(1);
        }
        if (RowlEngine_QuickLoad(handle) != 1 || RowlEngine_GetCurrentNodeId(handle) != 102) {
            std::cerr << "MS-6: QuickLoad did not restore the active slot" << std::endl;
            exit(1);
        }
        RowlEngine_DeleteSaveSlot(handle, 4);
        RowlEngine_DeleteSaveSlot(handle, 7);
        RowlEngine_SetQuickSaveSlot(handle, 0);
        if (RowlEngine_HasSaveSlot(handle, 4) != 0 || RowlEngine_HasSaveSlot(handle, 7) != 0) {
            std::cerr << "MS-6: slot cleanup failed" << std::endl;
            exit(1);
        }
        TEST_PASS("MS-6 Quick Slots 0-9 & Active-Slot Quick Save/Load");
    }

    // MS-6: pause-menu tour on the live handle (volumes deltas are relative:
    // earlier blocks set BGM to 0.8 on this handle).
    {
        const auto typeGraphPath = std::filesystem::temp_directory_path() / "rowl_ms6_typewriter_graph.json";
        if (RowlEngine_IsPaused(handle) != 0) {
            std::cerr << "MS-6: menu must start closed" << std::endl;
            exit(1);
        }
        RowlEngine_SetPaused(handle, 1);
        if (RowlEngine_IsPaused(handle) != 1) {
            std::cerr << "MS-6: SetPaused did not open the menu" << std::endl;
            exit(1);
        }
        auto menuJson = [&] {
            return nlohmann::json::parse(RowlEngine_GetPauseMenuJson(handle));
        };
        nlohmann::json menu = menuJson();
        if (!menu["open"].get<bool>() || menu["mode"].get<int>() != 0 ||
            menu["selected"].get<int>() != 0 || menu["rows"].size() != 9 ||
            menu["rows"][0]["label"].get<std::string>() != "Devam Et") {
            std::cerr << "MS-6: freshly opened menu snapshot mismatch" << std::endl;
            exit(1);
        }
        // Freeze: real-dt steps while paused must not progress the sim.
        RowlEngine_LoadStoryGraph(handle, typeGraphPath.string().c_str());
        RowlEngine_Step(handle, 0.0f);
        RowlEngine_Step(handle, 0.2f);
        if (cApiEngine->getActiveDialogues().empty() ||
            cApiEngine->getActiveDialogues()[0].elapsedTypewriterTime != 0.0f) {
            std::cerr << "MS-6: paused steps progressed the typewriter" << std::endl;
            exit(1);
        }
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "MS-6: node moved while paused" << std::endl;
            exit(1);
        }
        RowlEngine_AdvanceNode(handle, 0);
        if (RowlEngine_GetCurrentNodeId(handle) != 101) {
            std::cerr << "MS-6: direct advance escaped the modal pause menu" << std::endl;
            exit(1);
        }
        // Down -> row 1 ("Oyunu Kaydet"), Confirm -> save slot page.
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_CONFIRM);
        menu = menuJson();
        if (menu["mode"].get<int>() != 1 || menu["rows"].size() != 10 ||
            menu["title"].get<std::string>() != "Kayit Yuvasi Sec") {
            std::cerr << "MS-6: save slot page did not open" << std::endl;
            exit(1);
        }
        // Down -> slot 1, Confirm -> save current node (101) there.
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_CONFIRM);
        if (RowlEngine_HasSaveSlot(handle, 1) != 1) {
            std::cerr << "MS-6: menu save to slot 1 failed" << std::endl;
            exit(1);
        }
        menu = menuJson();
        if (menu["rows"][1]["value"].get<std::string>() != "dolu" ||
            menu["rows"][2]["value"].get<std::string>() != "bos") {
            std::cerr << "MS-6: slot occupancy did not refresh in the menu" << std::endl;
            exit(1);
        }
        if (RowlEngine_IsPaused(handle) != 1) {
            std::cerr << "MS-6: menu save must stay paused" << std::endl;
            exit(1);
        }
        // Back -> main page, selection reset.
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_BACK);
        menu = menuJson();
        if (menu["mode"].get<int>() != 0 || menu["selected"].get<int>() != 0) {
            std::cerr << "MS-6: Back did not return to the main page" << std::endl;
            exit(1);
        }
        // Down x3 -> Master volume; Left/Right adjust in 0.05 steps.
        const float masterBefore = RowlEngine_GetMasterVolume(handle);
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_DOWN);
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_LEFT);
        if (std::abs(RowlEngine_GetMasterVolume(handle) - (masterBefore - 0.05f)) > 0.001f) {
            std::cerr << "MS-6: master volume did not step down" << std::endl;
            exit(1);
        }
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_RIGHT);
        if (std::abs(RowlEngine_GetMasterVolume(handle) - masterBefore) > 0.001f) {
            std::cerr << "MS-6: master volume did not step back up" << std::endl;
            exit(1);
        }
        // Down x4 more -> text speed row (7); Left steps 1.00x -> 0.75x.
        for (int i = 0; i < 4; ++i) RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_DOWN);
        menu = menuJson();
        if (menu["selected"].get<int>() != 7 ||
            menu["rows"][7]["value"].get<std::string>() != "1.00x") {
            std::cerr << "MS-6: text-speed row mismatch" << std::endl;
            exit(1);
        }
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_LEFT);
        menu = menuJson();
        if (menu["rows"][7]["value"].get<std::string>() != "0.75x") {
            std::cerr << "MS-6: text speed did not step to 0.75x" << std::endl;
            exit(1);
        }
        RowlEngine_PauseMenuCommand(handle, ROWL_PAUSE_MENU_RIGHT);
        // Pointer parity: click row 0 (resume) middle band -> resumes.
        // Row 0 spans virtual y [290, 346); click its center.
        if (RowlEngine_PointerDown(handle, 960.0f, 318.0f) != 1 ||
            RowlEngine_IsPaused(handle) != 0) {
            std::cerr << "MS-6: pointer click on resume did not close the menu" << std::endl;
            exit(1);
        }
        RowlEngine_DeleteSaveSlot(handle, 1);
        std::filesystem::remove(typeGraphPath);
        TEST_PASS("MS-6 Pause Menu Tour (nav, values, slots, freeze, pointer)");
    }

    // MS-6: exit needs two-step confirmation on an isolated handle, and a
    // single Escape-equivalent Back press must never quit.
    {
        RowlEngineHandle quitHandle = RowlEngine_Create();
        if (!quitHandle || RowlEngine_Init(quitHandle, 640, 360, 0) != 1) {
            std::cerr << "MS-6: quit-confirm fixture init failed" << std::endl;
            exit(1);
        }
        RowlEngine_SetPaused(quitHandle, 1);
        // Up from row 0 wraps to the last row (exit).
        RowlEngine_PauseMenuCommand(quitHandle, ROWL_PAUSE_MENU_UP);
        nlohmann::json quitMenu = nlohmann::json::parse(RowlEngine_GetPauseMenuJson(quitHandle));
        if (quitMenu["selected"].get<int>() != 8) {
            std::cerr << "MS-6: menu selection did not wrap to exit" << std::endl;
            exit(1);
        }
        RowlEngine_PauseMenuCommand(quitHandle, ROWL_PAUSE_MENU_CONFIRM);
        if (RowlEngine_IsRunning(quitHandle) != 1) {
            std::cerr << "MS-6: first exit confirm quit without the second step" << std::endl;
            exit(1);
        }
        quitMenu = nlohmann::json::parse(RowlEngine_GetPauseMenuJson(quitHandle));
        if (!quitMenu["confirm_quit"].get<bool>()) {
            std::cerr << "MS-6: exit confirmation was not armed" << std::endl;
            exit(1);
        }
        // Back disarms instead of quitting.
        RowlEngine_PauseMenuCommand(quitHandle, ROWL_PAUSE_MENU_BACK);
        if (RowlEngine_IsRunning(quitHandle) != 1) {
            std::cerr << "MS-6: Back quit the game" << std::endl;
            exit(1);
        }
        quitMenu = nlohmann::json::parse(RowlEngine_GetPauseMenuJson(quitHandle));
        if (quitMenu["confirm_quit"].get<bool>()) {
            std::cerr << "MS-6: Back did not disarm the exit confirmation" << std::endl;
            exit(1);
        }
        RowlEngine_PauseMenuCommand(quitHandle, ROWL_PAUSE_MENU_CONFIRM);
        RowlEngine_PauseMenuCommand(quitHandle, ROWL_PAUSE_MENU_CONFIRM);
        if (RowlEngine_IsRunning(quitHandle) != 0) {
            std::cerr << "MS-6: double-confirmed exit did not stop the engine" << std::endl;
            exit(1);
        }
        RowlEngine_Shutdown(quitHandle);
        RowlEngine_Destroy(quitHandle);
        TEST_PASS("MS-6 Exit Confirmation (two-step, Back disarms)");
    }


    // Script components on the same node deliberately share lifecycle names.
    // The runtime must dispatch both callbacks and tear them down in reverse
    // activation order instead of letting the latter overwrite the former.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"script","id":"script_a","data":{"code":"private_value = 'first'; function on_enter() rowl.var_set('component_first_enter', private_value) end; function on_update(dt) rowl.var_set('component_first_update', tostring(dt)) end; function on_exit() rowl.var_set('component_exit_order', 'first') end"}},
        {"type":"script","id":"script_b","data":{"code":"private_value = 'second'; function on_enter() rowl.var_set('component_second_enter', private_value) end; function on_update(dt) rowl.var_set('component_second_update', tostring(dt * 2)) end; function on_exit() rowl.var_set('component_exit_order', 'second') end"}}
    ])");
    if (std::string(RowlEngine_GetVariable(handle, "component_first_enter")) != "first" ||
        std::string(RowlEngine_GetVariable(handle, "component_second_enter")) != "second") {
        std::cerr << "Multiple script components did not activate independently" << std::endl;
        exit(1);
    }
    RowlEngine_Step(handle, 0.25f);
    if (std::string(RowlEngine_GetVariable(handle, "component_first_update")) != "0.25" ||
        std::string(RowlEngine_GetVariable(handle, "component_second_update")) != "0.5") {
        std::cerr << "Multiple script component update callbacks were not dispatched" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, "[]");
    if (std::string(RowlEngine_GetVariable(handle, "component_exit_order")) != "first" ||
        std::string(RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)) != "[]") {
        std::cerr << "Script component teardown did not run in reverse activation order" << std::endl;
        exit(1);
    }

    // Editor diagnostics must expose per-component status without leaking Lua
    // source. A syntax error is contained to its component and reported through
    // the same C ABI consumed by the Avalonia preview.
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"script","id":"broken_script","data":{"code":"function on_enter( this is invalid end"}}
    ])");
    const auto diagnosticJson = nlohmann::json::parse(
        std::string(RowlEngine_GetScriptRuntimeDiagnosticsJson(handle)));
    if (!diagnosticJson.is_array() || diagnosticJson.size() != 1 ||
        diagnosticJson[0].value("state", "") != "failed" ||
        diagnosticJson[0].value("error", "").empty() ||
        diagnosticJson[0].contains("source")) {
        std::cerr << "Script runtime diagnostic contract failed" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, compJson);
    TEST_PASS("C-API Script Components: Isolation, Teardown, and Editor Diagnostics");

    // The native renderer and SDL event loop are host-thread-affine. A
    // second thread must not be able to mutate this handle or observe a
    // running runtime through the C ABI.
    std::atomic<bool> foreignThreadRejected{false};
    std::thread foreignCaller([&] {
        RowlEngine_UpdateSceneFromJson(handle,
            R"([{"type":"speaker","data":{"speaker":"Foreign","dialogue":"must not apply"}}])");
        foreignThreadRejected.store(
            RowlEngine_IsRunning(handle) == 0 &&
            std::strlen(RowlEngine_GetSpeaker(handle)) == 0);
    });
    foreignCaller.join();
    if (!foreignThreadRejected.load() || std::string(RowlEngine_GetSpeaker(handle)) != "Alice") {
        std::cerr << "C-API accepted a call from a non-owner thread" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Rejects Non-Owner Thread Calls");

    const std::string oversizedComponents(16 * 1024 * 1024 + 1, ' ');
    RowlEngine_UpdateSceneFromJson(handle, oversizedComponents.c_str());
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice") {
        std::cerr << "Oversized component JSON replaced the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Oversized Component JSON Containment");

    // Invalid JSON is user-editable input. It must be contained inside the
    // native boundary and leave the last valid scene usable.
    RowlEngine_UpdateSceneFromJson(handle, "{ definitely-not-json");
    if (std::strlen(RowlEngine_GetSpeaker(handle)) == 0 ||
        std::strlen(RowlEngine_GetDialogue(handle)) == 0) {
        std::cerr << "Malformed component JSON invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Malformed JSON Containment");

    RowlEngine_UpdateSceneFromJson(handle, R"([{"type":42,"data":{}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Schema-invalid component JSON invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Schema Containment");

    std::string tooManyComponents = "[";
    for (std::size_t i = 0; i <= 2'048; ++i) {
        if (i != 0) tooManyComponents += ',';
        tooManyComponents += R"({"type":"speaker","data":{}})";
    }
    tooManyComponents += ']';
    RowlEngine_UpdateSceneFromJson(handle, tooManyComponents.c_str());
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Over-count component JSON invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Count Containment");

    RowlEngine_SetVariable(handle, "finite_score", "5");
    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"variable","data":{"key":"finite_score","value":"nan","operation":"add"}}])");
    if (std::string(RowlEngine_GetVariable(handle, "finite_score")) != "5") {
        std::cerr << "Non-finite variable addition corrupted persistent game state" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Non-Finite Variable Arithmetic Containment");

    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"character","data":{"sprite":"Margot.jpg","x":1e30}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Out-of-range component numeric value invalidated the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Numeric Range Containment");

    RowlEngine_UpdateSceneFromJson(handle,
        R"([{"type":"character","data":{"sprite":"Margot.jpg","x":"not-a-number"}}])");
    if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
        std::string(RowlEngine_GetDialogue(handle)).empty()) {
        std::cerr << "Type-invalid component JSON did not roll back the active scene" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Component Value-Type Transaction Rollback");

    // Start from a known-good editor scene, then mutate it and append an
    // invalid trailing byte. The suffix guarantees each generated payload is
    // invalid while the mutations cover different parser and schema paths.
    const std::string componentFuzzSeed = compJson;
    uint32_t componentFuzzState = 0xA11CE55u;
    const auto nextComponentFuzzByte = [&componentFuzzState] {
        componentFuzzState = componentFuzzState * 1103515245u + 12345u;
        return static_cast<char>('!' + ((componentFuzzState >> 16u) % 94u));
    };
    for (uint32_t caseIndex = 0; caseIndex < 96; ++caseIndex) {
        std::string fuzzed = componentFuzzSeed;
        const uint32_t mutationCount = 1 + (caseIndex % 6);
        for (uint32_t mutation = 0; mutation < mutationCount; ++mutation) {
            const size_t position = (static_cast<size_t>(caseIndex) * 37u + mutation * 53u) % fuzzed.size();
            fuzzed[position] = nextComponentFuzzByte();
        }
        fuzzed.push_back('#');
        RowlEngine_UpdateSceneFromJson(handle, fuzzed.c_str());
        if (std::string(RowlEngine_GetSpeaker(handle)) != "Alice" ||
            std::string(RowlEngine_GetDialogue(handle)).empty()) {
            std::cerr << "Malformed component fuzz input replaced the active scene: case " << caseIndex << std::endl;
            exit(1);
        }
    }
    TEST_PASS("Deterministic malformed-component fuzz corpus preserves active scene");

    // Audio commands are device side effects, so a malformed component that
    // follows an audio component must not partially apply its filter.
    const auto* audioBeforeRollback = cApiEngine->getAudio();
    if (!audioBeforeRollback ||
        audioBeforeRollback->getActiveFilter() != Rowl::Audio::DSPFilterType::UnderwaterLowPass) {
        std::cerr << "Expected the valid component scene to leave the Underwater DSP active" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, R"([
        {"type":"audio","data":{"dsp_filter":"Cave"}},
        {"type":"character","data":{"sprite":"Margot.jpg","x":"not-a-number"}}
    ])");
    const auto* audioAfterRollback = cApiEngine->getAudio();
    if (!audioAfterRollback ||
        audioAfterRollback->getActiveFilter() != Rowl::Audio::DSPFilterType::UnderwaterLowPass) {
        std::cerr << "Invalid component scene leaked a partial audio side effect" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Deferred Audio Side Effects on Scene Rollback");

    // Multi-Dialogue Box Test (Two Simultaneous Chat Bubbles in Game Mode)
    const char* multiDlgJson = R"([
        {"type":"background","id":"b1","enabled":true,"data":{"texture":"Woman.png","x":0,"y":0,"width":1920,"height":1080,"scale":1}},
        {"type":"dialogue","id":"d1","enabled":true,"data":{"speaker":"Alice","dialogue":"First dialogue bubble!","x":80,"y":860,"width":1760,"height":180}},
        {"type":"dialogue","id":"d2","enabled":true,"data":{"speaker":"Bob","dialogue":"Second simultaneous dialogue bubble!","x":120,"y":660,"width":1760,"height":180}}
    ])";
    RowlEngine_UpdateSceneFromJson(handle, multiDlgJson);
    if (cApiEngine->getActiveDialogues().size() != 2) {
        std::cerr << "Expected 2 active dialogues in Engine, got: " << cApiEngine->getActiveDialogues().size() << std::endl;
        exit(1);
    }
    TEST_PASS("RowlEngine_UpdateSceneFromJson (Simultaneous Multi-Dialogue Boxes)");

    // Empty frames must clear legacy getters instead of leaking the previous
    // node's speaker/dialogue into save state or editor synchronization.
    RowlEngine_UpdateSceneFromJson(handle, "[]");
    if (std::strlen(RowlEngine_GetSpeaker(handle)) != 0 ||
        std::strlen(RowlEngine_GetDialogue(handle)) != 0 ||
        !cApiEngine->getActiveDialogues().empty()) {
        std::cerr << "Empty component scene retained stale dialogue state" << std::endl;
        exit(1);
    }
    RowlEngine_UpdateSceneFromJson(handle, multiDlgJson);
    TEST_PASS("Empty Component Scene Clears Previous Dialogue State");

    // Execute 30 frames of multi-dialogue step
    for (int i = 0; i < 30; ++i) {
        RowlEngine_Step(handle, 0.0166f);
    }
    TEST_PASS("RowlEngine_Step (Simultaneous Multi-Dialogue Render & Pixel Buffer Validation)");

    // Graph v4: choose by stable ID, not fragile array position.
    const auto graphPath = std::filesystem::temp_directory_path() / "rowl_choice_graph_test.json";
    {
        std::ofstream graph(graphPath);
        graph << R"({"format_version":4,"start_node_id":101,"nodes":[
          {"id":101,"speaker":"Guide","dialogue":"Choose.","objects":[{"id":"choices","name":"Choices","is_active":true,"components":[
            {"type":"choice","id":"choice_main","enabled":true,"data":{"options":[
              {"option_id":"go_left","text":"Left","x":680,"y":520,"width":560,"height":64,"background_color":"#1E293B"},
              {"option_id":"go_right","text":"Right","x":680,"y":600,"width":560,"height":64,"background_color":"#1E293B"}]}}]}],"next_nodes":[
            {"id":102,"label":"Left","option_id":"go_left"},
            {"id":103,"label":"Right","option_id":"go_right"}]},
          {"id":102,"speaker":"Guide","dialogue":"Left path."},
          {"id":103,"speaker":"Guide","dialogue":"Right path."}]})";
    }
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    RowlEngine_Step(handle, 0.0f);
    if (RowlEngine_PointerDown(handle, 700.0f, 620.0f) != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Choice button pointer hit-test routing failed" << std::endl;
        exit(1);
    }
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    RowlEngine_ResizeViewport(handle, 1080, 1920);
    if (RowlEngine_PointerDown(handle, 540.0f, 100.0f) != 1 || RowlEngine_GetCurrentNodeId(handle) != 101) {
        std::cerr << "Letterbox margin pointer advanced the story" << std::endl;
        exit(1);
    }
    if (RowlEngine_PointerDown(handle, 394.0f, 1005.0f) != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Portrait viewport pointer did not route to the choice" << std::endl;
        exit(1);
    }
    RowlEngine_ResizeViewport(handle, 1920, 1080);
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    if (RowlEngine_SelectChoice(handle, "go_right") != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Stable choice ID routing failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "does_not_exist") != 0) {
        std::cerr << "Unknown stable choice ID was accepted" << std::endl;
        exit(1);
    }

    // A failed graph load must be transactional: the active graph, current
    // node, and scene stay usable instead of being replaced by partial data.
    const auto invalidGraphPath = std::filesystem::temp_directory_path() / "rowl_invalid_graph_test.json";
    {
        std::ofstream invalidGraph(invalidGraphPath);
        invalidGraph << R"({"start_node_id":101,"nodes":[{"id":101,"next_nodes":[{"id":999}]}]})";
    }
    RowlEngine_LoadStoryGraph(handle, invalidGraphPath.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 103 ||
        std::string(RowlEngine_GetSpeaker(handle)) != "Guide") {
        std::cerr << "Invalid graph load replaced the active story" << std::endl;
        exit(1);
    }
    std::filesystem::remove(invalidGraphPath);

    const std::string graphFuzzSeed = R"({"start_node_id":101,"nodes":[{"id":101,"speaker":"Replacement"}]})";
    uint32_t graphFuzzState = 0x51A7E123u;
    const auto nextGraphFuzzByte = [&graphFuzzState] {
        graphFuzzState = graphFuzzState * 22695477u + 1u;
        return static_cast<char>('!' + ((graphFuzzState >> 16u) % 94u));
    };
    const auto graphFuzzPath = std::filesystem::temp_directory_path() / "rowl_graph_fuzz_test.json";
    for (uint32_t caseIndex = 0; caseIndex < 64; ++caseIndex) {
        std::string fuzzed = graphFuzzSeed;
        for (uint32_t mutation = 0; mutation < 1 + (caseIndex % 5); ++mutation) {
            const size_t position = (static_cast<size_t>(caseIndex) * 29u + mutation * 41u) % fuzzed.size();
            fuzzed[position] = nextGraphFuzzByte();
        }
        fuzzed.push_back('#');
        std::ofstream graph(graphFuzzPath, std::ios::binary);
        graph.write(fuzzed.data(), static_cast<std::streamsize>(fuzzed.size()));
        graph.close();
        RowlEngine_LoadStoryGraph(handle, graphFuzzPath.string().c_str());
        if (RowlEngine_GetCurrentNodeId(handle) != 103 ||
            std::string(RowlEngine_GetSpeaker(handle)) != "Guide") {
            std::cerr << "Malformed graph fuzz input replaced the active story: case " << caseIndex << std::endl;
            exit(1);
        }
    }
    std::filesystem::remove(graphFuzzPath);
    TEST_PASS("Deterministic malformed-graph fuzz corpus preserves active story");

    const auto overCountGraphPath = std::filesystem::temp_directory_path() / "rowl_overcount_graph_test.json";
    {
        std::ofstream overCountGraph(overCountGraphPath);
        overCountGraph << R"({"start_node_id":1000,"nodes":[)";
        for (uint64_t nodeId = 1000; nodeId <= 11'000; ++nodeId) {
            if (nodeId != 1000) overCountGraph << ',';
            overCountGraph << R"({"id":)" << nodeId << '}';
        }
        overCountGraph << "]}";
    }
    RowlEngine_LoadStoryGraph(handle, overCountGraphPath.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 103 ||
        std::string(RowlEngine_GetSpeaker(handle)) != "Guide") {
        std::cerr << "Over-count graph load replaced the active story" << std::endl;
        exit(1);
    }
    std::filesystem::remove(overCountGraphPath);
    TEST_PASS("Story Graph Count Containment");

    // A successful reload starts a fresh story state and cannot carry script
    // variables or rewind history over from the previously loaded graph.
    RowlEngine_SetVariable(handle, "old_graph_variable", "stale");
    RowlEngine_LoadStoryGraph(handle, graphPath.string().c_str());
    if (RowlEngine_GetCurrentNodeId(handle) != 101 ||
        !std::string(RowlEngine_GetVariable(handle, "old_graph_variable")).empty()) {
        std::cerr << "Story graph reload retained state from the previous graph" << std::endl;
        exit(1);
    }
    if (RowlEngine_SelectChoice(handle, "go_right") != 1 || RowlEngine_GetCurrentNodeId(handle) != 103) {
        std::cerr << "Story graph was unusable after transactional reload" << std::endl;
        exit(1);
    }
    std::filesystem::remove(graphPath);
    TEST_PASS("Story Graph Routing, Transactional Validation, and Fresh Reload State");

    // Audio C-API calls
    RowlEngine_PlayAudio(handle, "test_bgm.wav", 0, 1);
    RowlEngine_SetBgmVolume(handle, 0.8f);
    RowlEngine_SetBgmVolume(handle, std::numeric_limits<float>::quiet_NaN());
    if (!cApiEngine->getAudio() ||
        !std::isfinite(cApiEngine->getAudio()->getBgmGain()) ||
        std::abs(cApiEngine->getAudio()->getBgmGain() - 0.8f) > 0.001f) {
        std::cerr << "C-API accepted a non-finite BGM volume" << std::endl;
        exit(1);
    }
    RowlEngine_TriggerVoiceDucking(handle, 1);
    RowlEngine_TriggerVoiceDucking(handle, 0);
    RowlEngine_StopBgm(handle);
    // A5-tur2: string↔kod senkronu. Kayıp-dosya play'i kod 10 + dolu string
    // verir; stop-başarısı ikisini de sıfırlar; blip (synth) kod 0 verir.
    // Kırmızı-kanıt: setSuccess/setError yayılımlarından biri kaldırılırsa
    // aşağıdaki assert'ler düşer.
    RowlEngine_PlayAudio(handle, "missing_a5t2.wav", 0, 0);
    if (RowlEngine_GetLastResultCode(handle) != 10) {
        std::cerr << "Missing-file play did not yield AudioDecodeError (10), got: "
                  << RowlEngine_GetLastResultCode(handle) << std::endl;
        exit(1);
    }
    if (std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Missing-file play left the audio error string empty" << std::endl;
        exit(1);
    }
    RowlEngine_StopBgm(handle);
    if (RowlEngine_GetLastResultCode(handle) != 0) {
        std::cerr << "Successful stop did not reset the result code, got: "
                  << RowlEngine_GetLastResultCode(handle) << std::endl;
        exit(1);
    }
    if (!std::string(RowlEngine_GetLastAudioError(handle)).empty()) {
        std::cerr << "Successful stop left a stale audio error string" << std::endl;
        exit(1);
    }
    RowlEngine_PlayVoiceBlip(handle, "", 1.0f, 0.8f, 1);
    if (RowlEngine_GetLastResultCode(handle) != 0) {
        std::cerr << "Voice blip did not yield success code, got: "
                  << RowlEngine_GetLastResultCode(handle) << std::endl;
        exit(1);
    }
    // A5-tur3: synth/drop sayaç okuyucuları — boş-path blip'i synth'e düşer
    // (cihazlıda synth>=1); synth<=voice her koşulda; sağlıklı dummy'de drop 0.
    if (RowlEngine_IsAudioDeviceAvailable(handle)) {
        if (RowlEngine_GetSynthBlipCount(handle) == 0) {
            std::cerr << "C-API synth blip count is zero after synth blips" << std::endl;
            exit(1);
        }
    }
    if (RowlEngine_GetSynthBlipCount(handle) > RowlEngine_GetVoiceBlipCount(handle)) {
        std::cerr << "C-API synth blip count exceeds voice blip count" << std::endl;
        exit(1);
    }
    if (RowlEngine_GetAudioDropCount(handle) != 0) {
        std::cerr << "C-API audio drop count nonzero on healthy device" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Audio Control (PlayAudio, SetBgmVolume, Ducking, StopBgm)");

    // Variable & Scripting C-API
    RowlEngine_SetVariable(handle, "hero_gold", "150");
    if (std::string(RowlEngine_GetVariable(handle, "hero_gold")) != "150") {
        std::cerr << "C-API GetVariable mismatch" << std::endl;
        exit(1);
    }
    if (RowlEngine_EvaluateCondition(handle, "hero_gold >= 100") != 1 ||
        RowlEngine_EvaluateCondition(handle, "hero_gold < 50") != 0) {
        std::cerr << "C-API EvaluateCondition mismatch" << std::endl;
        exit(1);
    }
    if (RowlEngine_ExecuteScript(handle,
            "rowl.var_set('hero_gold', '300'); rowl.var_set('quest_state', 'accepted')") != 1 ||
        std::string(RowlEngine_GetVariable(handle, "hero_gold")) != "300" ||
        std::string(RowlEngine_GetVariable(handle, "quest_state")) != "accepted") {
        std::cerr << "C-API ExecuteScript mismatch" << std::endl;
        exit(1);
    }
    if (RowlEngine_Rewind(handle, 1) != 1 ||
        std::string(RowlEngine_GetVariable(handle, "hero_gold")) != "150" ||
        !std::string(RowlEngine_GetVariable(handle, "quest_state")).empty()) {
        std::cerr << "Lua script state was not atomically persisted and rewound" << std::endl;
        exit(1);
    }
    if (RowlEngine_ExecuteScript(handle,
            "rowl.var_set('hero_gold', '300'); rowl.var_set('quest_state', 'accepted')") != 1) {
        std::cerr << "C-API ExecuteScript replay mismatch" << std::endl;
        exit(1);
    }
    TEST_PASS("C-API Scripting & Dynamic Variables (SetVariable, GetVariable, EvaluateCondition, ExecuteScript)");

    // Save/Load Slots & Rewind C-API
    if (RowlEngine_SaveGameSlot(handle, 0) != 1) {
        std::cerr << "C-API SaveGameSlot failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_HasSaveSlot(handle, 0) != 1) {
        std::cerr << "C-API HasSaveSlot failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_LoadGameSlot(handle, 0) != 1) {
        std::cerr << "C-API LoadGameSlot failed" << std::endl;
        exit(1);
    }
    RowlEngine_DeleteSaveSlot(handle, 0);
    if (RowlEngine_HasSaveSlot(handle, 0) != 0) {
        std::cerr << "C-API DeleteSaveSlot failed" << std::endl;
        exit(1);
    }
    if (RowlEngine_GetCurrentStepId(handle) == 0) {
        std::cerr << "C-API GetCurrentStepId mismatch" << std::endl;
        exit(1);
    }
    RowlEngine_Rewind(handle, 1);
    TEST_PASS("C-API Save / Load Slots & State Rewind (SaveGameSlot, LoadGameSlot, Rewind)");

    RowlEngine_Shutdown(handle);
    RowlEngine_Destroy(handle);
    uint32_t staleWidth = 123;
    uint32_t staleHeight = 456;
    RowlEngine_Step(handle, 0.016f);
    RowlEngine_Destroy(handle); // Double destroy must be harmless.
    if (RowlEngine_IsRunning(handle) != 0 || RowlEngine_GetCurrentNodeId(handle) != 0 ||
        RowlEngine_GetPixelBuffer(handle, &staleWidth, &staleHeight) != nullptr ||
        staleWidth != 0 || staleHeight != 0 ||
        RowlEngine_IsRunning(reinterpret_cast<RowlEngineHandle>(static_cast<uintptr_t>(1))) != 0) {
        std::cerr << "C-API accepted a stale or unknown engine handle" << std::endl;
        exit(1);
    }
    RowlEngineHandle replacementHandle = RowlEngine_Create();
    if (!replacementHandle || replacementHandle == handle ||
        RowlEngine_Init(replacementHandle, 320, 180, 0) != 1) {
        std::cerr << "C-API failed to create an isolated replacement handle" << std::endl;
        exit(1);
    }
    RowlEngine_SetVariable(handle, "stale_callback", "must_not_reach_replacement");
    if (!std::string(RowlEngine_GetVariable(replacementHandle, "stale_callback")).empty()) {
        std::cerr << "Stale C-API handle targeted a replacement engine" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(replacementHandle);
    TEST_PASS("RowlEngine_Shutdown & Destroy (Clean Resource Teardown)");
    TEST_PASS("C-API Stale and Unknown Handle Containment");
}
