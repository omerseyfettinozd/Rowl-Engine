/**
 * test_audio_device_recovery.cpp — Audio device recovery and Engine::step wiring.
 * Split from main_test_runner.cpp; behavior unchanged.
 */
#include "rowl_test_harness.hpp"

void test_audio_device_recovery() {
    TEST_SECTION("Audio Device Hotplug Recovery & Output Suspend");

    // SDL_PushEvent needs the events subsystem, which an earlier audio
    // shutdown may have released. Hold it explicitly for this test only.
    const bool eventsAlreadyInit = SDL_WasInit(SDL_INIT_EVENTS) != 0;
    if (!eventsAlreadyInit && !SDL_InitSubSystem(SDL_INIT_EVENTS)) {
        std::cerr << "Could not initialize SDL events for recovery test" << std::endl;
        exit(1);
    }

    // 1. Process-wide (non-window) events must reach the global queue while
    // window-targeted events stay isolated in their own runtime.
    constexpr uint32_t routingProbeWindow = 0xA011CE;
    if (!Rowl::Platform::SdlEventDispatcher::registerWindow(routingProbeWindow)) {
        std::cerr << "Could not register routing probe window" << std::endl;
        exit(1);
    }
    Rowl::Platform::SdlEventDispatcher::takeGlobalEvents();

    SDL_Event removedEvent{};
    removedEvent.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
    removedEvent.adevice.which = 7;
    SDL_Event minimizedEvent{};
    minimizedEvent.type = SDL_EVENT_WINDOW_MINIMIZED;
    minimizedEvent.window.windowID = 0;
    SDL_Event keyEvent{};
    keyEvent.type = SDL_EVENT_KEY_DOWN;
    keyEvent.key.key = SDLK_F5;
    keyEvent.key.windowID = routingProbeWindow;
    if (!SDL_PushEvent(&removedEvent) || !SDL_PushEvent(&minimizedEvent) || !SDL_PushEvent(&keyEvent)) {
        std::cerr << "Could not enqueue synthetic global/window events" << std::endl;
        exit(1);
    }
    const auto globals = Rowl::Platform::SdlEventDispatcher::takeGlobalEvents();
    if (globals.size() != 2 || globals[0].type != SDL_EVENT_AUDIO_DEVICE_REMOVED ||
        globals[1].type != SDL_EVENT_WINDOW_MINIMIZED) {
        std::cerr << "Dispatcher did not route process-wide events to the global queue" << std::endl;
        exit(1);
    }
    const auto windowed = Rowl::Platform::SdlEventDispatcher::takeEvents(routingProbeWindow);
    if (windowed.size() != 1 || windowed[0].type != SDL_EVENT_KEY_DOWN) {
        std::cerr << "Window-targeted event leaked into or out of the global queue" << std::endl;
        exit(1);
    }
    Rowl::Platform::SdlEventDispatcher::unregisterWindow(routingProbeWindow);
    TEST_PASS("Dispatcher routes audio-device and minimize events globally without window leakage");

    // 2. Device rebuild preserves playback intent; suspend tracking works
    // with or without a physical device.
    Rowl::VFS::VFSManager recoveryVfs;
    Rowl::Audio::AudioEngine recovery(&recoveryVfs);
    if (!recovery.initialize() || !recovery.isInitialized()) {
        std::cerr << "Recovery audio init failed" << std::endl;
        exit(1);
    }
    if (recovery.isAudioDeviceAvailable()) {
        const auto recoveryRoot = std::filesystem::temp_directory_path() / "rowl_audio_recovery_test";
        const auto recoveryAssetDir = recoveryRoot / "Assets" / "audio";
        const auto recoveryTone = recoveryAssetDir / "recovery_tone.wav";
        const std::string recoveryToneAsset = "audio/recovery_tone.wav";
        std::filesystem::create_directories(recoveryAssetDir);
        const uint8_t recoveryWav[] = {
            'R','I','F','F', 38,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
            68,172,0,0, 136,88,1,0, 2,0, 16,0,
            'd','a','t','a', 2,0,0,0, 0,0
        };
        {
            std::ofstream tone(recoveryTone, std::ios::binary);
            tone.write(reinterpret_cast<const char*>(recoveryWav), sizeof(recoveryWav));
        }
        recoveryVfs.remountProject(recoveryRoot.string());
        recovery.playAudio(recoveryToneAsset, Rowl::Audio::AudioChannelType::Bgm);
        recovery.update();
        if (recovery.getCurrentBgmPath() != recoveryToneAsset || !recovery.isBgmPlaying()) {
            std::cerr << "Recovery BGM setup failed" << std::endl;
            exit(1);
        }
        recovery.setBgmVolume(0.7f);
        recovery.applyDspFilter(Rowl::Audio::DSPFilterType::Telephone);

        recovery.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_REMOVED);
        if (!recovery.isAudioDeviceAvailable() || !recovery.isBgmPlaying() ||
            recovery.getCurrentBgmPath() != recoveryToneAsset ||
            std::abs(recovery.getBgmVolume() - 0.7f) > 0.001f ||
            recovery.getActiveFilter() != Rowl::Audio::DSPFilterType::Telephone ||
            !recovery.getLastError().empty()) {
            std::cerr << "Device rebuild lost playback intent, gains, filter, or reported an error" << std::endl;
            exit(1);
        }
        recovery.update(0.016f);
        if (!recovery.isBgmPlaying() || recovery.getCurrentBgmPath() != recoveryToneAsset) {
            std::cerr << "BGM loop did not resume into the rebuilt stream" << std::endl;
            exit(1);
        }
        TEST_PASS("Audio device rebuild preserves BGM intent, gains, and filter");

        std::filesystem::remove_all(recoveryRoot);
        // No global-restore remount: recoveryVfs is function-local.
    }
    recovery.handleDeviceEvent(SDL_EVENT_AUDIO_DEVICE_ADDED);
    recovery.setOutputSuspended(true);
    if (!recovery.isOutputSuspended()) {
        std::cerr << "Output suspend flag was not recorded" << std::endl;
        exit(1);
    }
    recovery.update(0.016f);
    recovery.setOutputSuspended(false);
    if (recovery.isOutputSuspended()) {
        std::cerr << "Output resume flag was not recorded" << std::endl;
        exit(1);
    }
    recovery.shutdown();
    TEST_PASS("Audio output suspend/resume tracking");

    // 3. End to end through Engine::step: minimize suspends, restore resumes,
    // device removal reopens, all observable through the C API.
    if (RowlEngine_IsAudioDeviceAvailable(nullptr) != 0 || RowlEngine_IsAudioOutputSuspended(nullptr) != 0) {
        std::cerr << "Audio observer null-handle contract failed" << std::endl;
        exit(1);
    }
    RowlEngineHandle stepHandle = RowlEngine_Create();
    if (!stepHandle || RowlEngine_Init(stepHandle, 320, 180, 0) != 1) {
        std::cerr << "C-API init failed for step-wiring test" << std::endl;
        exit(1);
    }
    if (RowlEngine_IsAudioOutputSuspended(stepHandle) != 0) {
        std::cerr << "Fresh runtime must not start suspended" << std::endl;
        exit(1);
    }
    Rowl::Platform::SdlEventDispatcher::registerWindow(0xE2E0);
    Rowl::Platform::SdlEventDispatcher::takeGlobalEvents();
    SDL_Event stepMinimized{};
    stepMinimized.type = SDL_EVENT_WINDOW_MINIMIZED;
    stepMinimized.window.windowID = 0;
    SDL_PushEvent(&stepMinimized);
    RowlEngine_Step(stepHandle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(stepHandle) != 1) {
        std::cerr << "Minimize did not suspend audio output through Engine::step" << std::endl;
        exit(1);
    }
    SDL_Event stepRestored{};
    stepRestored.type = SDL_EVENT_WINDOW_RESTORED;
    stepRestored.window.windowID = 0;
    SDL_PushEvent(&stepRestored);
    RowlEngine_Step(stepHandle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(stepHandle) != 0) {
        std::cerr << "Restore did not resume audio output through Engine::step" << std::endl;
        exit(1);
    }
    SDL_Event stepRemoved{};
    stepRemoved.type = SDL_EVENT_AUDIO_DEVICE_REMOVED;
    stepRemoved.adevice.which = 7;
    SDL_PushEvent(&stepRemoved);
    RowlEngine_Step(stepHandle, 0.016f);
    if (RowlEngine_IsAudioOutputSuspended(stepHandle) != 0) {
        std::cerr << "Device event leaked into suspend state" << std::endl;
        exit(1);
    }
    // CTest pins SDL_AUDIODRIVER=dummy, so the reopen must have restored a
    // live device here rather than silently falling back.
    if (RowlEngine_IsAudioDeviceAvailable(stepHandle) != 1) {
        std::cerr << "Device removal did not reopen the dummy audio device" << std::endl;
        exit(1);
    }
    RowlEngine_Destroy(stepHandle);
    Rowl::Platform::SdlEventDispatcher::unregisterWindow(0xE2E0);
    if (!eventsAlreadyInit) SDL_QuitSubSystem(SDL_INIT_EVENTS);
    TEST_PASS("Engine::step wires minimize/restore/device events to audio output");
}
