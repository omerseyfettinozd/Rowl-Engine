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
 */
ROWL_API RowlEngineHandle RowlEngine_Create(void);

/**
 * Frees the engine instance. Calls Shutdown() internally if needed.
 */
ROWL_API void RowlEngine_Destroy(RowlEngineHandle handle);

/**
 * Initialises the engine (SDL3, VFS, audio, scripting).
 * @param handle        Engine handle from RowlEngine_Create().
 * @param virtualWidth  Logical/virtual canvas width  (e.g. 1920).
 * @param virtualHeight Logical/virtual canvas height (e.g. 1080).
 * @param vsync         1 = enable vsync, 0 = disable.
 * @return 1 on success, 0 on failure.
 */
ROWL_API int RowlEngine_Init(RowlEngineHandle handle,
                              uint32_t virtualWidth,
                              uint32_t virtualHeight,
                              int vsync);

/**
 * Advances the engine by one frame.
 * Call this every frame from the host's render/tick loop.
 * @param deltaTime Elapsed time since the last call, in seconds.
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

/**
 * Sets the active project root directory, isolating VFS mounts to that project.
 * @param projectRoot Absolute or relative path to the active project folder.
 */
ROWL_API void RowlEngine_SetProjectDirectory(RowlEngineHandle handle,
                                             const char* projectRoot);

/**
 * Advances playback to the next story node.
 * @param choiceIndex Branch index (0 = first / only branch).
 */
ROWL_API void RowlEngine_AdvanceNode(RowlEngineHandle handle,
                                      uint32_t choiceIndex);

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

#ifdef __cplusplus
} /* extern "C" */
#endif
