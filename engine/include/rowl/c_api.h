/**
 * rowl/c_api.h
 *
 * Platform-agnostic C API for the Rowl Engine shared library.
 * This header is consumed by C# via P/Invoke (DllImport).
 *
 * Design rules:
 *  - All functions use the `extern "C"` ABI (no C++ name mangling).
 *  - Engine instances are represented as opaque void* handles.
 *  - Strings are passed as null-terminated const char* (UTF-8).
 *  - No C++ types, templates, or exceptions cross the boundary.
 */

#pragma once
#include <stdint.h>

/* ── Export / visibility macros ─────────────────────────────────────────── */
#if defined(_WIN32) || defined(_WIN64)
    #ifdef ROWL_BUILDING_DLL
        #define ROWL_API __declspec(dllexport)
    #else
        #define ROWL_API __declspec(dllimport)
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define ROWL_API __attribute__((visibility("default")))
#else
    #define ROWL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque engine handle ────────────────────────────────────────────────── */
typedef void* RowlEngineHandle;

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

/**
 * Allocates and returns a new Engine instance.
 * Must be paired with RowlEngine_Destroy().
 * Multiple handles may be alive concurrently. Each owns an isolated runtime,
 * including VFS state; SDL subsystem lifetime is coordinated internally.
 */
ROWL_API RowlEngineHandle RowlEngine_Create(void);

/**
 * Frees the engine instance. Calls Shutdown() internally if needed.
 * Repeated destroy calls and calls made with an already-destroyed handle are
 * ignored safely by the native boundary.
 */
ROWL_API void RowlEngine_Destroy(RowlEngineHandle handle);

/**
 * Initialises the engine (SDL3, VFS, audio, scripting).
 * @param handle        Engine handle from RowlEngine_Create().
 * @param virtualWidth  Logical/virtual canvas width  (e.g. 1920).
 * @param virtualHeight Logical/virtual canvas height (e.g. 1080).
 * @param vsync         1 = enable vsync, 0 = disable.
 * @return 1 on success, 0 on failure.
 *
 * The thread that first calls Init becomes the handle owner. Every later
 * call using this handle, including Destroy, must use that same host thread.
 * A call from another thread is safely rejected (void calls do nothing).
 */
ROWL_API int RowlEngine_Init(RowlEngineHandle handle,
                              uint32_t virtualWidth,
                              uint32_t virtualHeight,
                              int vsync);

/**
 * Initialises the engine in standalone desktop mode with a visible top-level SDL3 window.
 * @param handle        Engine handle from RowlEngine_Create().
 * @param appTitle      Title displayed on the window title bar.
 * @param virtualWidth  Target logical width (e.g. 1920).
 * @param virtualHeight Target logical height (e.g. 1080).
 * @param vsync         1 = enable vsync, 0 = disable.
 * @return 1 on success, 0 on failure.
 *
 * All visible/embedded runtimes in one process must be initialized and
 * stepped from the same host UI/event thread. Offscreen runtimes retain the
 * per-handle owner-thread contract described above.
 */
ROWL_API int RowlEngine_InitStandalone(RowlEngineHandle handle,
                                        const char* appTitle,
                                        uint32_t virtualWidth,
                                        uint32_t virtualHeight,
                                        int vsync);

/**
 * Starts the blocking standalone render/event loop until quit or window close.
 */
ROWL_API void RowlEngine_Run(RowlEngineHandle handle);

/**
 * Advances the engine by one frame.
 * Call this every frame from the host's render/tick loop.
 * @param deltaTime Elapsed time since the last call, in seconds.
 * Visible/embedded handles share one process UI/event thread because SDL's
 * native event queue is process-global.
 */
ROWL_API void RowlEngine_Step(RowlEngineHandle handle, float deltaTime);

/**
 * Shuts down and frees all engine subsystems.
 * The handle remains valid but unusable after this call.
 */
ROWL_API void RowlEngine_Shutdown(RowlEngineHandle handle);

/**
 * Returns 1 if the engine is running (no quit was requested), 0 otherwise.
 */
ROWL_API int RowlEngine_IsRunning(RowlEngineHandle handle);

/* ── Native window embedding (Single-Window mode) ────────────────────────── */

/**
 * Provides an external native window handle so the engine renders inside
 * the host UI (e.g. Avalonia NativeControlHost) instead of creating its
 * own top-level window.
 *
 * Must be called BEFORE RowlEngine_Init().
 *
 * @param nativeWindowHandle  Platform native handle:
 *   - Windows : HWND
 *   - Linux X11: Window (unsigned long) cast to void*
 *   - Linux Wayland: wl_surface* (SDL3 fallback)
 *   - macOS   : NSView*
 *   - Android : ANativeWindow*
 *   - iOS     : UIView*
 * @param width   Initial render surface width  in pixels.
 * @param height  Initial render surface height in pixels.
 */
ROWL_API void RowlEngine_SetExternalWindowHandle(RowlEngineHandle handle,
                                                  void* nativeWindowHandle,
                                                  uint32_t width,
                                                  uint32_t height);

/**
 * Notifies the engine that the embedded render area has been resized.
 * Call this whenever the host control changes size.
 */
ROWL_API void RowlEngine_ResizeViewport(RowlEngineHandle handle,
                                         uint32_t newWidth,
                                         uint32_t newHeight);

/* ── Offscreen Framebuffer & Playback Control ────────────────────────────── */

/**
 * Returns a pointer to the offscreen RGBA32 pixel buffer and populates width/height.
 */
ROWL_API const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH);

/** Returns the number of distinct decoded textures currently held in the runtime cache. */
ROWL_API uint32_t RowlEngine_GetTextureCacheTextureCount(RowlEngineHandle handle);

/** Returns the estimated RGBA byte footprint of distinct cached textures. */
ROWL_API uint64_t RowlEngine_GetTextureCacheBytes(RowlEngineHandle handle);

/** Returns the active decoded texture-cache ceiling in bytes. */
ROWL_API uint64_t RowlEngine_GetTextureCacheBudgetBytes(RowlEngineHandle handle);

/** Returns the number of LRU texture evictions since the last cache clear. */
ROWL_API uint64_t RowlEngine_GetTextureCacheEvictionCount(RowlEngineHandle handle);

/** Returns texture decode/upload work measured during the most recent rendered frame. */
ROWL_API double RowlEngine_GetLastFrameTextureLoadMilliseconds(RowlEngineHandle handle);

/** Returns non-texture renderer work measured during the most recent rendered frame. */
ROWL_API double RowlEngine_GetLastFrameNonTextureRenderMilliseconds(RowlEngineHandle handle);

/** Returns the TrueType rasterization portion of the most recent rendered frame. */
ROWL_API double RowlEngine_GetLastFrameTextRasterizationMilliseconds(RowlEngineHandle handle);

/**
 * Sets the decoded texture-cache budget in bytes. Values below 1 MiB clamp to
 * 1 MiB; textures that cannot fit are rejected without disturbing live state.
 */
ROWL_API void RowlEngine_SetTextureCacheBudgetBytes(RowlEngineHandle handle, uint64_t bytes);

/**
 * Sets the playback state (1 = playing, 0 = stopped/editing).
 */
ROWL_API void RowlEngine_SetPlayState(RowlEngineHandle handle, int isPlaying);

/**
 * Resets the engine story state to the starting node.
 */
ROWL_API void RowlEngine_ResetToStartNode(RowlEngineHandle handle);

/* ── Scene / story control (Editor → Engine) ─────────────────────────────── */

/**
 * Updates the currently displayed visual novel scene.
 * All pointer parameters must remain valid only for the duration of the call.
 */
ROWL_API void RowlEngine_UpdateScene(
    RowlEngineHandle handle,
    const char* speaker,
    const char* dialogue,
    const char* background,
    float bgX,   float bgY,   float bgW,   float bgH,
    const char* character,
    float charX, float charY, float charW, float charH,
    float dlgX,  float dlgY,  float dlgW,  float dlgH
);

/**
 * Updates the scene from a JSON string containing component data.
 * This is the component-based alternative to RowlEngine_UpdateScene.
 * The JSON should be an array of component objects with 'type', 'enabled', and 'data' fields.
 */
ROWL_API void RowlEngine_UpdateSceneFromJson(
    RowlEngineHandle handle,
    const char* componentsJson
);

/**
 * Loads a story graph from a JSON file on disk.
 * @param jsonPath Absolute or CWD-relative path to the JSON file.
 */
ROWL_API void RowlEngine_LoadStoryGraph(RowlEngineHandle handle,
                                         const char* jsonPath);

/** Loads a story graph through the active VFS (for example a package entry). */
ROWL_API int RowlEngine_LoadStoryGraphFromVfs(RowlEngineHandle handle,
                                              const char* vfsPath);

/** Returns the diagnostic from the last file or VFS story graph load attempt. */
ROWL_API const char* RowlEngine_GetLastStoryGraphError(RowlEngineHandle handle);

/**
 * Sets the active project root directory, isolating VFS mounts to that project.
 * @param projectRoot Absolute or relative path to the active project folder.
 */
ROWL_API void RowlEngine_SetProjectDirectory(RowlEngineHandle handle,
                                             const char* projectRoot);

/** Updates project-owned BGM defaults without remounting assets or resetting the active scene. */
ROWL_API void RowlEngine_SetBgmTransitionDefaults(RowlEngineHandle handle,
                                                  const char* transition,
                                                  float durationSeconds);

/**
 * Advances playback to the next story node.
 * @param choiceIndex Branch index (0 = first / only branch).
 */
ROWL_API void RowlEngine_AdvanceNode(RowlEngineHandle handle,
                                      uint32_t choiceIndex);

/** Advances a branch using its stable option ID (graph format v4). */
ROWL_API int RowlEngine_SelectChoice(RowlEngineHandle handle,
                                      const char* optionId);

/** Sends a pointer/touch press in virtual-canvas coordinates. */
ROWL_API int RowlEngine_PointerDown(RowlEngineHandle handle, float x, float y);

/* ── State queries (Engine → Editor) ─────────────────────────────────────── */

/**
 * Returns a pointer to the active speaker name string.
 * The returned pointer is owned by the engine — do NOT free it.
 * It is valid until the next RowlEngine_UpdateScene / RowlEngine_Step call.
 */
ROWL_API const char* RowlEngine_GetSpeaker(RowlEngineHandle handle);

/** Same ownership rules as RowlEngine_GetSpeaker. */
ROWL_API const char* RowlEngine_GetDialogue(RowlEngineHandle handle);

/** Returns the ID of the currently active story node. */
ROWL_API uint64_t RowlEngine_GetCurrentNodeId(RowlEngineHandle handle);

/* ── Audio Control (Host → Engine) ───────────────────────────────────────── */

/**
 * Plays an audio asset on the specified channel with an optional DSP filter.
 * @param handle      Engine handle from RowlEngine_Create().
 * @param assetPath   Path to WAV audio file (relative to VFS or physical).
 * @param channelType 0 = Bgm, 1 = Voice, 2 = Sfx.
 * @param filterType  0 = Normal, 1 = Cave, 2 = Telephone, 3 = Underwater.
 */
ROWL_API void RowlEngine_PlayAudio(RowlEngineHandle handle,
                                   const char* assetPath,
                                   int channelType,
                                   int filterType);

/** Stops currently playing BGM stream. */
ROWL_API void RowlEngine_StopBgm(RowlEngineHandle handle);

/** Sets BGM volume (0.0f - 1.0f). */
ROWL_API void RowlEngine_SetBgmVolume(RowlEngineHandle handle, float volume);
ROWL_API void RowlEngine_SetMasterVolume(RowlEngineHandle handle, float volume);
ROWL_API void RowlEngine_SetVoiceVolume(RowlEngineHandle handle, float volume);
ROWL_API void RowlEngine_SetSfxVolume(RowlEngineHandle handle, float volume);
/** Applies player-local reading preferences without changing story data. */
ROWL_API void RowlEngine_SetTextSpeedMultiplier(RowlEngineHandle handle, float multiplier);
ROWL_API void RowlEngine_SetAutoAdvanceDelayOffset(RowlEngineHandle handle, float seconds);
ROWL_API float RowlEngine_GetMasterVolume(RowlEngineHandle handle);
ROWL_API float RowlEngine_GetVoiceVolume(RowlEngineHandle handle);
ROWL_API float RowlEngine_GetSfxVolume(RowlEngineHandle handle);

/** Triggers voice ducking attenuation on BGM (1 = voice active, 0 = restored). */
ROWL_API void RowlEngine_TriggerVoiceDucking(RowlEngineHandle handle, int isVoiceActive);

/** Returns 1 while a BGM track is active, 0 otherwise. */
ROWL_API int RowlEngine_IsBgmPlaying(RowlEngineHandle handle);

/** Returns 1 while voice playback is active, 0 otherwise. */
ROWL_API int RowlEngine_IsVoicePlaying(RowlEngineHandle handle);

/** Returns the currently active DSP filter (0 Normal, 1 Cave, 2 Telephone, 3 Underwater). */
ROWL_API int RowlEngine_GetActiveDspFilter(RowlEngineHandle handle);

/** Returns the most recent audio error. The pointer is engine-owned and valid until the next audio call. */
ROWL_API const char* RowlEngine_GetLastAudioError(RowlEngineHandle handle);

/**
 * Returns a JSON array of current script-component diagnostics. The result is
 * engine-owned and valid until the next diagnostics query on the same thread.
 */
ROWL_API const char* RowlEngine_GetScriptRuntimeDiagnosticsJson(RowlEngineHandle handle);

/** Returns the bounded player dialogue backlog as an engine-owned JSON array. */
ROWL_API const char* RowlEngine_GetDialogueHistoryJson(RowlEngineHandle handle);

/* ── Save / Load Slots & History Rewind ───────────────────────────────────── */

/** Saves the current game state to the specified slot (0 = quicksave). Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_SaveGameSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Loads game state from the specified slot. Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_LoadGameSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Returns 1 if the specified save slot exists, 0 otherwise. */
ROWL_API int RowlEngine_HasSaveSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Deletes the specified save slot. Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_DeleteSaveSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Rewinds the game state by the specified number of steps (default 1). Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_Rewind(RowlEngineHandle handle, uint32_t steps);

/** Returns the current history step ID. */
ROWL_API uint64_t RowlEngine_GetCurrentStepId(RowlEngineHandle handle);

/* ── Scripting & Variable Evaluation ─────────────────────────────────────── */

/** Sets a string variable in the scripting environment and game state. */
ROWL_API void RowlEngine_SetVariable(RowlEngineHandle handle, const char* key, const char* value);

/** Gets a variable string by key. The returned pointer is owned by the engine. */
ROWL_API const char* RowlEngine_GetVariable(RowlEngineHandle handle, const char* key);

/** Evaluates a Lua condition expression (e.g. "gold >= 50"). Returns 1 for true, 0 for false. */
ROWL_API int RowlEngine_EvaluateCondition(RowlEngineHandle handle, const char* conditionExpr);

/** Executes a sandboxed Lua script string. Returns 1 on success, 0 on error. */
ROWL_API int RowlEngine_ExecuteScript(RowlEngineHandle handle, const char* scriptCode);

/* ── Structured Runtime Results & Diagnostics ────────────────────────────── */

/**
 * Returns the numeric error code of the last runtime operation on this handle.
 * 0 = Success (OK), >0 = specific error code:
 *   1 = InvalidHandle
 *   2 = InvalidArgument
 *   3 = FileNotFound
 *   4 = FileTooLarge
 *   5 = ParseError
 *   6 = ValidationError
 *   7 = IoError
 *   8 = ScriptSyntaxError
 *   9 = ScriptRuntimeError
 *  10 = AudioDecodeError
 *  11 = StateError
 *  99 = UnknownError
 */
ROWL_API int32_t RowlEngine_GetLastResultCode(RowlEngineHandle handle);

/**
 * Returns the operation name of the last runtime call (e.g. "save_game_slot", "load_story_graph_vfs").
 * Pointer is thread-local/engine-owned UTF-8 string.
 */
ROWL_API const char* RowlEngine_GetLastResultOperation(RowlEngineHandle handle);

/**
 * Returns the human-readable diagnostic message of the last runtime operation.
 * Pointer is thread-local/engine-owned UTF-8 string.
 */
ROWL_API const char* RowlEngine_GetLastResultMessage(RowlEngineHandle handle);

/**
 * Returns the target identifier or path of the last runtime operation (e.g. slot index or file path).
 * Pointer is thread-local/engine-owned UTF-8 string.
 */
ROWL_API const char* RowlEngine_GetLastResultTarget(RowlEngineHandle handle);

/**
 * Clears the last runtime result and resets it to OK/Success.
 */
ROWL_API void RowlEngine_ClearLastResult(RowlEngineHandle handle);

/**
 * Starts a visual scene transition (e.g. "crossfade", "fade_black", "fade_white", "fade_color", "wipe_left", "wipe_right").
 * durationSeconds: duration of the transition in seconds.
 * colorHex: hex color string used if kind is "fade_color" (e.g. "#FF0000").
 */
ROWL_API void RowlEngine_StartTransition(RowlEngineHandle handle, const char* kind, float durationSeconds, const char* colorHex);

/**
 * Returns 1 if a visual scene transition is currently running, 0 otherwise.
 */
ROWL_API int RowlEngine_IsTransitionActive(RowlEngineHandle handle);

/**
 * Sets 2D camera position and zoom factor.
 * x, y: camera center coordinates in virtual canvas space (default: 960, 540).
 * zoom: zoom factor (clamped between 0.1 and 10.0, default: 1.0).
 */
ROWL_API void RowlEngine_SetCamera(RowlEngineHandle handle, float x, float y, float zoom);

/**
 * Triggers camera screen shake with harmonic decay.
 * intensity: maximum pixel displacement (clamped to 1000).
 * durationSeconds: shake duration in seconds.
 */
ROWL_API void RowlEngine_TriggerCameraShake(RowlEngineHandle handle, float intensity, float durationSeconds);

/**
 * Resets 2D camera to default center (960, 540) and zoom 1.0.
 */
ROWL_API void RowlEngine_ResetCamera(RowlEngineHandle handle);

/**
 * Smoothly pans camera to target center coordinates over durationSeconds.
 * easingType: 0=Linear, 1=EaseIn, 2=EaseOut, 3=EaseInOutCubic, 4=SmoothStep.
 */
ROWL_API void RowlEngine_CameraPanTo(RowlEngineHandle handle, float targetX, float targetY, float durationSeconds, int easingType);

/**
 * Smoothly zooms camera to target scale factor over durationSeconds.
 * easingType: 0=Linear, 1=EaseIn, 2=EaseOut, 3=EaseInOutCubic, 4=SmoothStep.
 */
ROWL_API void RowlEngine_CameraZoomTo(RowlEngineHandle handle, float targetZoom, float durationSeconds, int easingType);

/**
 * Returns 1 if the camera is currently panning, zooming, or shaking; 0 otherwise.
 */
ROWL_API int RowlEngine_IsCameraMoving(RowlEngineHandle handle);

#ifdef __cplusplus
} /* extern "C" */
#endif
