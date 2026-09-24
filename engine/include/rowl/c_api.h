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
 *
 * Threading contract (B3b/K3 — worker-dispatch model):
 *  - One owner thread per handle. The first Init call claims the handle for
 *    the calling thread; every later call from another thread fails closed
 *    and the engine object is untouched. Two tiers (D3/B1d):
 *    silent — ordinary getters/setters return the dead-handle default
 *    (0 / empty / no-op, ResultCode sites INVALID_HANDLE) and record
 *    nothing. Silent is deliberate: stamping every foreign read would
 *    thrash the shared result context and flood the log on hot paths
 *    (native S3/S7 lock this in).
 *    loud — ownership-level calls on a LIVE handle stamp WRONG_THREAD (14)
 *    with the offending operation name, cross-thread readable via
 *    RowlEngine_GetLastResultCode/Message (+Utf8 variants), and log the
 *    rejection. Stamping calls: Shutdown, Destroy, Step, Run, Init (claim
 *    refused by a live owner), the prefetch/character aux guards, the
 *    visible-step dispatch gate, the pause quick-slot trio
 *    (set_quick_save_slot/quick_save/quick_load), get_quick_save_slot,
 *    the pause state quartet (set_paused/is_paused/pause_menu_command/
 *    get_pause_menu_json), the save-slot quintet (save_game_slot/
 *    load_game_slot/has_save_slot/get_save_slot_metadata/delete_save_slot),
 *    rewind, get_audio_spectrum, the embed pair
 *    (set_external_window_handle/resize_viewport), get_asset_provenance,
 *    set_project_directory, and the Set/GetFadeCurveChecked pair (op names
 *    set_fade_curve/get_fade_curve; the legacy void/int mixer forms stay
 *    silent-tier). Native scope: 37 stampWrongThread call-sites (grep-verified;
 *    W8-g: op-name to call-site mapping is NOT one-to-one — init stamps from
 *    two sites, get_pause_menu_json from two, parametric aux guards stamp
 *    per-argument op, and the visible-step gate re-stamps step). Live+foreign
 *    stamps WRONG_THREAD (14), dead stays silent INVALID_HANDLE. C# scope: no
 *    P/Invoke behavior change — legacy int/void forms stay silent, strict
 *    hosts use the Checked ResultCode forms. Calls on a truly dead handle
 *    stay silent no-ops (there is no engine left to record into), so
 *    INVALID_HANDLE keeps meaning "dead", never "foreign".
 *  - Destroyed slots are recycled via a free-list (memory bounded by
 *    peak-live); a stale handle cannot become valid again within the
 *    32-bit generation space because the generation bump on slot reuse
 *    makes it name a different token (recycle + generations, ABA defense).
 *  - Hosts must serialize all calls for one handle onto its owner thread.
 *    The editor does this via OffscreenRuntimeWorker dispatch; standalone
 *    runtimes stay on their host UI/event thread. Concurrent Init of several
 *    engines from several threads is NOT a supported topology.
 *  - Subsystem-internal locks (VFS, logger, audio, save, prefetch window)
 *    guard shared state inside one engine; they do not lift handle affinity.
 *  - Story/graph C API calls use one process-wide shared/exclusive gate.
 *    A story write on any handle blocks story reads and writes on all other
 *    handles until that call returns. Node advance and choice selection can
 *    evaluate Lua choice conditions while holding the write gate. The gate
 *    does not cover Step or C API calls outside the story/graph surface;
 *    hosts must still follow the per-handle owner-thread rule above.
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

/* ── Versioned, result-coded API contract ───────────────────────────────── */

typedef enum RowlEngine_ResultCode {
    ROWL_RESULT_OK = 0,
    ROWL_RESULT_INVALID_HANDLE = 1,
    ROWL_RESULT_INVALID_ARGUMENT = 2,
    ROWL_RESULT_FILE_NOT_FOUND = 3,
    ROWL_RESULT_FILE_TOO_LARGE = 4,
    ROWL_RESULT_PARSE_ERROR = 5,
    ROWL_RESULT_VALIDATION_ERROR = 6,
    ROWL_RESULT_IO_ERROR = 7,
    ROWL_RESULT_SCRIPT_SYNTAX_ERROR = 8,
    ROWL_RESULT_SCRIPT_RUNTIME_ERROR = 9,
    ROWL_RESULT_AUDIO_DECODE_ERROR = 10,
    ROWL_RESULT_STATE_ERROR = 11,
    ROWL_RESULT_BUFFER_TOO_SMALL = 12,
    ROWL_RESULT_UNSUPPORTED = 13,
    /* D3 (B1d #102/#150/#157): call from a non-owner thread on a live
       handle. Appended; earlier values are frozen. */
    ROWL_RESULT_WRONG_THREAD = 14,
    ROWL_RESULT_UNKNOWN_ERROR = 99
} RowlEngine_ResultCode;

typedef struct RowlEngine_ApiVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} RowlEngine_ApiVersion;

#define ROWL_ENGINE_C_API_VERSION_MAJOR 1u
#define ROWL_ENGINE_C_API_VERSION_MINOR 0u
#define ROWL_ENGINE_C_API_VERSION_PATCH 0u

#define ROWL_ENGINE_CAPABILITY_RESULT_CODES          UINT64_C(1)
#define ROWL_ENGINE_CAPABILITY_CALLER_BUFFERS        UINT64_C(2)
#define ROWL_ENGINE_CAPABILITY_USER_DATA_DIRECTORIES UINT64_C(4)
#define ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT           UINT64_C(8)
#define ROWL_ENGINE_CAPABILITY_PLAYER_LOOP           UINT64_C(16)
#define ROWL_ENGINE_CAPABILITY_SAVE_METADATA         UINT64_C(32)
#define ROWL_ENGINE_CAPABILITY_PLAYER_CHOICES        UINT64_C(64)
#define ROWL_ENGINE_CAPABILITY_LOCALIZATION          UINT64_C(128)
#define ROWL_ENGINE_CAPABILITY_RICH_TEXT_MARKUP      UINT64_C(256)
#define ROWL_ENGINE_CAPABILITY_TEXT_SHAPING          UINT64_C(512)
#define ROWL_ENGINE_CAPABILITY_ACCESSIBILITY         UINT64_C(1024)
/* Faz 4.5 Dilim 3: long-audio threshold contract (header probe + warning; decode path untouched). */
#define ROWL_ENGINE_CAPABILITY_LONG_AUDIO_CONTRACT UINT64_C(2048)
#define ROWL_ENGINE_CAPABILITY_CAMERA_ROTATION_IGNORED UINT64_C(4096)
/* Faz 5 Dilim 1: OGG streaming core + mixer bus skeleton + decision observability. */
#define ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING UINT64_C(8192)
/* Faz 5 Dilim 2: native mixer + SFX polyphony + fade curves + ambience beds + pump observability. */
#define ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY UINT64_C(16384)
/* Faz 5 Dilim 3: layered character slots + expression presets + per-handle diagnostics. */
#define ROWL_ENGINE_CAPABILITY_CHARACTER_LAYERS UINT64_C(32768)
/* Faz 5 Dilim 4: budgeted asset prefetch + chapter-windowed loading. */
#define ROWL_ENGINE_CAPABILITY_PREFETCH_CHAPTERS UINT64_C(65536)
/* Faz 5 Dilim 5: converter provenance sidecars (rowl_oggenc / rowl_webp2png). */
#define ROWL_ENGINE_CAPABILITY_CONVERTER_PROVENANCE UINT64_C(131072)
#define ROWL_ENGINE_CAPABILITY_MSDF_RENDER       UINT64_C(262144)

/** Current additive C API version. This query does not require an engine handle. */
ROWL_API RowlEngine_ResultCode RowlEngine_GetApiVersion(
    RowlEngine_ApiVersion* outVersion);

/** Bitwise OR of ROWL_ENGINE_CAPABILITY_* flags supported by this library. */
ROWL_API RowlEngine_ResultCode RowlEngine_GetCapabilities(
    uint64_t* outCapabilities);

/**
 * Copies the active UTF-8 save directory into caller-owned memory.
 * outRequiredSize includes the trailing NUL. Passing NULL/0 for the buffer is
 * the supported size-query form. An undersized non-NULL buffer is cleared and
 * returns ROWL_RESULT_BUFFER_TOO_SMALL.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetSaveDirectoryUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/** Same caller-buffer contract as RowlEngine_GetSaveDirectoryUtf8. */
ROWL_API RowlEngine_ResultCode RowlEngine_GetProfileDirectoryUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

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
 * Fail-loud init (B1a): every rejection records a last-result
 * (RowlEngine_GetLastResultCode/Message) — ValidationError (6) for bad
 * canvas dimensions, StateError (11) for double-init or subsystem failure.
 * A second Init on a live engine returns 0 (was: silent 1); shut down
 * first, then re-init. A failed Init leaves no half-window behind, so a
 * corrected retry on the same handle is safe.
 *
 * The thread that first calls Init becomes the handle owner. Every later
 * call using this handle, including Destroy, must use that same host thread.
 *
 * D3 (B1d): an ownership-level call from another thread on a LIVE handle
 * is rejected LOUDLY, not silently — the rejection stamps WRONG_THREAD (14)
 * on the engine's last-result with the offending operation name, and the
 * calling thread can read it back via RowlEngine_GetLastResultCode/Message
 * (a dead handle still reports INVALID_HANDLE, so the two are
 * distinguishable). Loud calls: the full stamping list in the threading
 * contract above (W8-g: single source — no second list here, lists rot).
 * Ordinary getters/setters stay silent
 * fail-closed (see the threading contract above). Void calls
 * (Shutdown/Destroy/Step) additionally log the rejection. Calls on a truly
 * dead handle stay silent no-ops (there is no engine left to record into).
 *
 * If the owner thread exited without destroying the handle, the handle is
 * NOT permanently wedged: RowlEngine_ReclaimHandle transfers ownership (and
 * the process-wide SDL event-dispatch pin) to the calling thread, after
 * which Shutdown/Destroy work again. Reclaim is an administrative recovery
 * operation — call it only after the previous owner thread has exited; a
 * still-running previous owner degrades loudly (its calls stamp
 * WRONG_THREAD) rather than silently corrupting state.
 *
 * Process-wide SDL video init is serialized AND owner-pinned: concurrent
 * Init/Shutdown/Destroy on different threads cannot interleave SDL video
 * calls (one process-wide video serial), and a second thread cannot acquire
 * the video subsystem while another thread holds it (first-acquirer
 * affinity, released back to unclaimed when the lease count reaches zero).
 * A visible/embedded engine stepped from a thread that does not own the
 * event-dispatch pin advances nothing and stamps WRONG_THREAD (offscreen
 * engines are unaffected — they never touch the dispatch pin).
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
 *
 * Bulgu #14 fail-closed: offscreen/embedded handles can never produce a
 * quit event through this loop (offscreen registers no event window;
 * embedded frames are host-driven via Step), so Run returns immediately on
 * them and stamps StateError (11, op "run") instead of entering an
 * unstoppable loop — the handle stays initialized and drivable via Step.
 * Shutdown is NOT called on this path. Standalone windows keep the
 * blocking contract unchanged.
 */
ROWL_API void RowlEngine_Run(RowlEngineHandle handle);

/**
 * D3 (B1d #151): administrative ownership recovery for a handle whose owner
 * thread exited without destroying it. Transfers handle ownership AND the
 * process-wide SDL event-dispatch pin to the calling thread and reports
 * ROWL_RESULT_OK; a dead/unknown handle reports ROWL_RESULT_INVALID_HANDLE.
 * After a successful reclaim the calling thread may Shutdown/Destroy (or
 * keep stepping) the engine; leases the dead owner held are released by the
 * normal Shutdown/Destroy path on the reclaiming thread.
 *
 * @return ROWL_RESULT_OK on success, ROWL_RESULT_INVALID_HANDLE for a
 * dead/unknown handle. Additive API (ABI baseline grows 218 to 219).
 */
ROWL_API RowlEngine_ResultCode RowlEngine_ReclaimHandle(RowlEngineHandle handle);

/**
 * Advances the engine by one frame.
 * Call this every frame from the host's render/tick loop.
 * @param deltaTime Elapsed time since the last call, in seconds.
 * Visible/embedded handles share one process UI/event thread because SDL's
 * native event queue is process-global. D3 (B1d #155): a visible engine
 * stepped from any other thread advances NOTHING and stamps WRONG_THREAD;
 * offscreen engines keep the plain owner-thread contract.
 */
ROWL_API void RowlEngine_Step(RowlEngineHandle handle, float deltaTime);

/**
 * Shuts down and frees all engine subsystems.
 * The handle remains valid but unusable after this call.
 * D2: shutdown süpürür — oturum profili (play/pause, hız/ofset,
 * bgm-varsayılanı, sahne, playtime), story graph + cursor, VFS mount'ları,
 * gömülü tutamaç ve save-dizin override'ı sıfırlanır; aynı handle'da
 * Shutdown→Init taze-handle ile özdeş başlar. Paylaşılan-VFS senaryosunda
 * (host setVfs ile aynı instance'ı verdiyse) mount'lar da temizlenir.
 * D3 (B1d #102): Shutdown/Destroy on a live handle from a non-owner thread
 * stamp WRONG_THREAD (readable cross-thread) instead of silently no-op'ing.
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
 * D01 (#135) fail-closed: a call on an already-initialized engine is
 * REJECTED without touching the live window and stamps StateError with
 * operation "set_external_window_handle" (readable via
 * RowlEngine_GetLastResultCode). Dead/foreign handles stay silent no-ops.
 * Prefer the checked form RowlEngine_SetExternalWindowHandleChecked
 * (rowl/c_api_embed.hpp), which reports every rejection as a ResultCode.
 *
 * @param nativeWindowHandle  Platform native handle:
 *   - Windows : HWND
 *   - Linux X11: Window (unsigned long) cast to void*
 *   - Linux Wayland: explicitly UNSUPPORTED (fail-closed error, no fallback;
 *     real support is Faz 5, see docs/PLATFORM_SUPPORT.md)
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
 *
 * MS-0 pitch contract: the surface row stride may exceed width*4. Hosts must
 * NOT assume tight packing. Prefer RowlEngine_GetPixelBufferEx and stride
 * copies by the reported pitch. This entry point is a thin wrapper that
 * discards the pitch (equivalent to calling Ex with outPitch = NULL).
 * The returned pointer is borrowed: valid until the next Step/resize/shutdown
 * on this handle. Never free it; copy out what you need during the call frame.
 */
ROWL_API const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH);

/**
 * Pitch-aware framebuffer access (MS-0 contract).
 * outPitch receives the surface row stride in bytes and is always >= (*outW)*4
 * on success. Any out-parameter may be NULL. Null-handle fallback: returns NULL
 * and zeroes every provided out-parameter.
 */
ROWL_API const uint8_t* RowlEngine_GetPixelBufferEx(RowlEngineHandle handle,
                                                    uint32_t* outW, uint32_t* outH,
                                                    uint32_t* outPitch);

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

/** Returns the SDL renderer flush portion of the most recent rendered frame. */
ROWL_API double RowlEngine_GetLastFrameRendererFlushMilliseconds(RowlEngineHandle handle);

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
 * Requires a prior RowlEngine_Init (D1/B1b): pre-init calls are no-ops that
 * report StateError via RowlEngine_GetLastResultCode.
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
 * Extended scene update supporting rotation angles for background and character (in degrees).
 */
ROWL_API void RowlEngine_UpdateSceneEx(
    RowlEngineHandle handle,
    const char* speaker,
    const char* dialogue,
    const char* background,
    float bgX,   float bgY,   float bgW,   float bgH,   float bgRot,
    const char* character,
    float charX, float charY, float charW, float charH, float charRot,
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

/** Length-reporting variant of GetLastStoryGraphError (see lifetime contract). */
ROWL_API const char* RowlEngine_GetLastStoryGraphErrorWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetLastStoryGraphError (B2b; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLastStoryGraphErrorUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/**
 * Graph vNext chapter queries (ROWL_ENGINE_CAPABILITY_GRAPH_VNEXT).
 *
 * Chapters are the runtime section markers of format v5 graphs: save/load
 * boundaries, backlog clustering and profile progress resolve through them.
 * Format v4 graphs predate chapters, so a v4 graph reports zero chapters and
 * an empty current chapter id. All three calls are additive and leave every
 * older entry point untouched.
 */

/**
 * Copies the chapter id of the current story node into caller-owned memory.
 * Unassigned nodes (including every v4 node) yield an empty string with
 * ROWL_RESULT_OK. Follows the caller-buffer contract: a null buffer with a
 * zero size is a size query, and an undersized buffer returns
 * ROWL_RESULT_BUFFER_TOO_SMALL with the required size written out.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetCurrentChapterIdUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/** Writes how many chapters the loaded graph defines (0 for v4 graphs). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetChapterCount(
    RowlEngineHandle handle, uint32_t* outCount);

/**
 * Copies the chapter id at an order-sorted position (order, then id) into
 * caller-owned memory. An out-of-range index returns
 * ROWL_RESULT_INVALID_ARGUMENT.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetChapterIdAtUtf8(
    RowlEngineHandle handle, uint32_t index, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize);

/**
 * Player-loop read tracking (ROWL_ENGINE_CAPABILITY_PLAYER_LOOP).
 *
 * Copies the content ids of the currently presented dialogues as a UTF-8
 * JSON array of strings (for example `["uuid-1","uuid-2"]`). Lines that
 * predate content_id migration contribute an empty string, so hosts can
 * fail closed (an empty id is never treated as read). Follows the
 * caller-buffer contract: a null buffer with a zero size is a size query,
 * and an undersized buffer returns ROWL_RESULT_BUFFER_TOO_SMALL with the
 * required size written out. All older entry points are untouched.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetActiveDialogueContentIdsJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/**
 * Sets the active project root directory, isolating VFS mounts to that project.
 * D2 (#113): oynanmış oturum ortasında çağrılırsa reddedilir (no-op +
 * StateError via GetLastResultCode) — aksi halde story commit + gameState
 * sıfırlama ilerlemeyi sessizce silerdi. Proje-değişimi için
 * Shutdown→Init döngüsü kullanın; oynanmamış motorda serbesttir.
 * @param projectRoot Absolute or relative path to the active project folder.
 */
ROWL_API void RowlEngine_SetProjectDirectory(RowlEngineHandle handle,
                                             const char* projectRoot);

/**
 * Ends the live story session without touching the presented scene (play
 * state off, lua session cleared, cursor/step/history reset). Mount helper:
 * call before SetProjectDirectory when the session may have been played —
 * the mid-session gate only accepts unplayed engines. Silent no-op on dead
 * or uninitialized handles. The presented scene is left as-is; push a scene
 * after the mount as usual.
 */
ROWL_API void RowlEngine_EndSession(RowlEngineHandle handle);

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

/**
 * Presented choice buttons (ROWL_ENGINE_CAPABILITY_PLAYER_CHOICES).
 * Count reports how many options await manual input (0 = none);
 * label-at copies the button text for building selection UI and follows
 * the caller-buffer contract (out-of-range index reports
 * ROWL_RESULT_INVALID_ARGUMENT). Older entry points are untouched.
 */
ROWL_API uint32_t RowlEngine_GetChoiceCount(RowlEngineHandle handle);
ROWL_API RowlEngine_ResultCode RowlEngine_GetChoiceLabelAtUtf8(
    RowlEngineHandle handle, uint32_t index, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize);
/** Copies the stable option id of a presented choice button (same contract). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetChoiceOptionIdAtUtf8(
    RowlEngineHandle handle, uint32_t index, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize);

/**
 * Runtime localization (ROWL_ENGINE_CAPABILITY_LOCALIZATION, Faz 3 Dilim 1).
 *
 * The manifest locale declaration (project.rowlproj "default_locale" /
 * "supported_locales", camelCase spellings accepted) loads when a project
 * is mounted; projects without locale keys keep the "en" fallback.
 * Catalogs at Assets/locales/<locale>.json map content_id to
 * speaker/text/alt_text. Each field takes a plain string or a plural
 * table (category -> template, mandatory "other"; schema_version 1|2).
 * Malformed rows are skipped and counted — they never veto the whole
 * locale. Resolution falls back from the active locale to
 * the default locale and finally to the node's original text, so unknown
 * keys never surface as errors. All three calls are additive and leave
 * every older entry point untouched.
 */

/**
 * Switches the active locale. Returns ROWL_RESULT_OK on success,
 * ROWL_RESULT_INVALID_HANDLE for a dead handle, and
 * ROWL_RESULT_INVALID_ARGUMENT for a null/empty/unsupported code.
 * A supported-but-unloaded locale (manifest-listed yet no readable
 * catalog while another catalog is loaded) returns
 * ROWL_RESULT_FILE_NOT_FOUND with a context FileNotFound record —
 * no new result code was added; locale-free projects still accept any
 * well-formed tag (legacy path). On success the mounted dialogue is
 * re-resolved from its stored originals, so getters/render flip at once.
 * Tags are full BCP 47 ("pt-BR" resolves from the "pt" catalog when no
 * "pt-BR" catalog is loaded; "en-US" selects manifest "en"); malformed
 * tags are INVALID_ARGUMENT. A rejected call leaves the active locale
 * unchanged.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_SetLocale(
    RowlEngineHandle handle, const char* locale);

/**
 * Copies the active locale code (for example "en") into caller-owned
 * memory. Follows the caller-buffer contract: a null buffer with a zero
 * size is a size query, and an undersized buffer returns
 * ROWL_RESULT_BUFFER_TOO_SMALL with the required size written out.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLocale(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/**
 * Copies the manifest supported locales as a UTF-8 JSON array of strings
 * (for example `["en","tr"]`) into caller-owned memory. Same
 * caller-buffer contract as RowlEngine_GetLocale.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetSupportedLocalesJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/**
 * Rich-text markup (ROWL_ENGINE_CAPABILITY_RICH_TEXT_MARKUP, Faz 3 Dilim 2).
 *
 * Handle-free pure helpers: no engine instance, no thread affinity, no file
 * I/O. The tag contract (supported tags, value ranges, fail-closed literal
 * fallback) lives in docs/RICH_TEXT_MARKUP_CONTRACT.md; the rules in
 * rowl/text/markup_parser.hpp are the source of truth.
 */

/**
 * Parses rich-text markup and copies the result document as UTF-8 JSON into
 * caller-owned memory. The JSON carries `plain_text`, the per-character
 * `chars` token stream, `diagnostics` warnings, `char_count`,
 * `trailing_pause` and `omitted_diagnostics` (see the contract for the
 * exact schema). Follows the caller-buffer contract: a null buffer with a
 * zero size is a size query, and an undersized buffer is cleared and
 * returns ROWL_RESULT_BUFFER_TOO_SMALL with the required size written out.
 * A null markup pointer, a null outRequiredSize, or an input larger than
 * 256 KiB returns ROWL_RESULT_INVALID_ARGUMENT. Older entry points are
 * untouched.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_ParseMarkup(
    const char* markupUtf8, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/**
 * Copies only the stripped plain text of rich-text markup into
 * caller-owned memory. Same input validation and caller-buffer contract
 * as RowlEngine_ParseMarkup. Older entry points are untouched.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_StripMarkup(
    const char* markupUtf8, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/**
 * Shapes rich markup using caller-owned font bytes and returns a deterministic
 * UTF-8 JSON layout. The result contains visual-order glyphs, logical scalar
 * and reveal indices, advances/offsets, wrapped lines, bounds and timing
 * metadata. fontData remains caller-owned and is only read during this call.
 * maxWidth <= 0 disables automatic wrapping. languageUtf8 may be NULL.
 *
 * When the optional advanced dependencies are unavailable, the call remains
 * supported and reports the safe `stb_fallback` backend. Markup is limited to
 * 256 KiB and fontDataSize to 32 MiB. Output follows the caller-buffer query,
 * undersized-buffer and trailing-NUL rules used by RowlEngine_ParseMarkup.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_ShapeMarkup(
    const char* markupUtf8,
    const uint8_t* fontData, uint32_t fontDataSize,
    float fontSize, float maxWidth, const char* languageUtf8,
    char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/** Sends a pointer/touch press in virtual-canvas coordinates. */
ROWL_API int RowlEngine_PointerDown(RowlEngineHandle handle, float x, float y);

/* ── Engine-owned string lifetime contract (MS-0) ─────────────────────────
 * Every `const char*` getter below returns a BORROWED pointer:
 *  - Owned by the engine: never free it, never store it past the call frame.
 *  - A second call to the SAME getter on the same thread overwrites the
 *    previous result (per-getter thread_local buffer). Copy first, call later.
 *  - Different getters use different buffers, so reading speaker then dialogue
 *    back-to-back is safe as long as each is copied before its getter repeats.
 *  - Content is NUL-terminated UTF-8 WITHOUT embedded NULs; plain getters stop
 *    at the first NUL. When exact byte length matters, use the `...WithLength`
 *    variant, which reports the length that was copied.
 *  - Hosts must copy synchronously (e.g. Marshal.PtrToStringUTF8 at the call
 *    site). Holding the raw pointer across Step/resize/shutdown is a
 *    use-after-free.
 * ─────────────────────────────────────────────────────────────────────────── */

/* ── State queries (Engine → Editor) ─────────────────────────────────────── */

/**
 * Returns a pointer to the active speaker name string.
 * The returned pointer is owned by the engine — do NOT free it.
 * It is valid until the next RowlEngine_UpdateScene / RowlEngine_Step call.
 * See the lifetime contract above; prefer GetSpeakerWithLength for exact bytes.
 * Requires a prior RowlEngine_Init (D1/B1b): pre-init returns "" with StateError.
 */
ROWL_API const char* RowlEngine_GetSpeaker(RowlEngineHandle handle);

/**
 * Length-reporting variant of GetSpeaker. Returns the same pointer and writes
 * its byte length (excluding NUL) to outLen when outLen is non-NULL.
 */
ROWL_API const char* RowlEngine_GetSpeakerWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetSpeaker (B2b; no borrowed lifetime).
 * Requires a prior RowlEngine_Init (D1/B1b): pre-init copies empty output
 * and returns StateError instead of serving demo defaults as OK. */
ROWL_API RowlEngine_ResultCode RowlEngine_GetSpeakerUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/** Same ownership rules as RowlEngine_GetSpeaker. */
ROWL_API const char* RowlEngine_GetDialogue(RowlEngineHandle handle);

/** Length-reporting variant of GetDialogue (see lifetime contract above). */
ROWL_API const char* RowlEngine_GetDialogueWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetDialogue (B2b; no borrowed lifetime).
 * Same pre-init contract as GetSpeakerUtf8 (D1/B1b). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetDialogueUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/** Returns the ID of the currently active story node. */
ROWL_API uint64_t RowlEngine_GetCurrentNodeId(RowlEngineHandle handle);

/** Returns the rotation angle (degrees) of the active background. */
ROWL_API float RowlEngine_GetBackgroundRotation(RowlEngineHandle handle);

/** Sets the parallax factor (default 1.0f) for the background in X and Y dimensions. */
ROWL_API void RowlEngine_SetBackgroundParallax(RowlEngineHandle handle, float parallaxX, float parallaxY);

/** Returns the active background parallax factor in X. */
ROWL_API float RowlEngine_GetBackgroundParallaxX(RowlEngineHandle handle);

/** Returns the active background parallax factor in Y. */
ROWL_API float RowlEngine_GetBackgroundParallaxY(RowlEngineHandle handle);

/** Returns the active background opacity (0.0f - 1.0f). */
ROWL_API float RowlEngine_GetBackgroundOpacity(RowlEngineHandle handle);

/** Returns the rotation angle (degrees) of the active character. */
ROWL_API float RowlEngine_GetCharacterRotation(RowlEngineHandle handle);

/* ── Audio Control (Host → Engine) ───────────────────────────────────────── */

/**
 * Plays an audio asset on the specified channel with an optional DSP filter.
 * @param handle      Engine handle from RowlEngine_Create().
 * @param assetPath   Path to WAV audio file (relative to VFS or physical).
 * @param channelType 0 = Bgm, 1 = Voice, 2 = Sfx, 3 = Ambience (loop),
 *   4 = Ui (one-shot). Any other value falls back to the Sfx branch.
 * @param filterType  0 = Normal, 1 = Cave, 2 = Telephone, 3 = Underwater.
 */
ROWL_API void RowlEngine_PlayAudio(RowlEngineHandle handle,
                                   const char* assetPath,
                                   int channelType,
                                   int filterType);

/** Stops currently playing BGM stream. */
ROWL_API void RowlEngine_StopBgm(RowlEngineHandle handle);

/** Sets BGM volume (0.0f - 1.0f). #86: the live gain is committed into
 * GameState (no rewind step); save/load/rewind restore it. */
ROWL_API void RowlEngine_SetBgmVolume(RowlEngineHandle handle, float volume);
/** Master volume setter; #86 persistence commit like SetBgmVolume. */
ROWL_API void RowlEngine_SetMasterVolume(RowlEngineHandle handle, float volume);
/** Voice volume setter; #86 persistence commit like SetBgmVolume. */
ROWL_API void RowlEngine_SetVoiceVolume(RowlEngineHandle handle, float volume);
/** SFX volume setter; #86 persistence commit like SetBgmVolume. */
ROWL_API void RowlEngine_SetSfxVolume(RowlEngineHandle handle, float volume);
/** Applies player-local reading preferences without changing story data. */
ROWL_API void RowlEngine_SetTextSpeedMultiplier(RowlEngineHandle handle, float multiplier);
/**
 * Faz 3 Dilim 5 — accessibility display settings
 * (ROWL_ENGINE_CAPABILITY_ACCESSIBILITY). Text scale multiplies every
 * shaped/measured/rendered font size (clamped to [1.0, 2.0], non-finite
 * resets to 1.0); high contrast outlines glyphs for readability;
 * reduced motion disables camera shake and screen flash. All apply
 * instantly; dead handles are ignored (fail closed).
 */
ROWL_API void RowlEngine_SetTextScale(RowlEngineHandle handle, float scale);
ROWL_API void RowlEngine_SetHighContrast(RowlEngineHandle handle, int enabled);
ROWL_API void RowlEngine_SetReducedMotion(RowlEngineHandle handle, int enabled);
ROWL_API float RowlEngine_GetTextScale(RowlEngineHandle handle);
ROWL_API int RowlEngine_IsHighContrast(RowlEngineHandle handle);
ROWL_API int RowlEngine_IsReducedMotion(RowlEngineHandle handle);
ROWL_API void RowlEngine_SetAutoAdvanceDelayOffset(RowlEngineHandle handle, float seconds);
ROWL_API float RowlEngine_GetMasterVolume(RowlEngineHandle handle);
ROWL_API float RowlEngine_GetVoiceVolume(RowlEngineHandle handle);
ROWL_API float RowlEngine_GetSfxVolume(RowlEngineHandle handle);

/**
 * Faz 5 Dilim 1 — OGG streaming observability + volume matrix completion
 * (ROWL_ENGINE_CAPABILITY_AUDIO_STREAMING). All additive; older entry
 * points are untouched.
 *
 * IsStreaming returns 1 while the current BGM decision is stream, 0 for
 * memory / unknown / no-BGM / dead handle (fail closed). GetStreamInfoJson
 * follows the caller-buffer contract of RowlEngine_GetLocale (NULL/0 size
 * query, undersized buffer clears + BUFFER_TOO_SMALL + outRequiredSize,
 * dead handle INVALID_HANDLE); the JSON schema is
 * mode/duration_seconds/threshold_seconds/threshold_bytes/
 * buffered_seconds/reason/channel/asset. Volume setters clamp to [0,1]
 * and ignore non-finite input (last valid value kept); dead-handle
 * getters return 0.0f.
 */
ROWL_API int RowlEngine_IsStreaming(RowlEngineHandle handle);
ROWL_API RowlEngine_ResultCode RowlEngine_GetStreamInfoJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);
ROWL_API float RowlEngine_GetBgmVolume(RowlEngineHandle handle);
ROWL_API void RowlEngine_SetAmbienceVolume(RowlEngineHandle handle, float volume);
ROWL_API float RowlEngine_GetAmbienceVolume(RowlEngineHandle handle);
ROWL_API void RowlEngine_SetUiVolume(RowlEngineHandle handle, float volume);
ROWL_API float RowlEngine_GetUiVolume(RowlEngineHandle handle);

/**
 * Faz 5 Dilim 2 — native mixer + SFX polyphony + fade curves + ambience
 * beds + pump observability (ROWL_ENGINE_CAPABILITY_AUDIO_MIXER_POLYPHONY).
 * All additive; older entry points are untouched.
 *
 * FadeCurve: 0 = Linear (default, bit-identical legacy math), 1 = EqualPower.
 * SetFadeCurve ignores any other value (last valid kept); dead-handle
 * GetFadeCurve returns 0. Pool depth is clamped to [1,16] (default 8);
 * depth 1 is the legacy single-voice behavior. Ambience beds: 0 = BedA
 * (legacy single-bed path), 1 = BedB; invalid bed is fail-closed
 * (0 / 0.0f / "" / no-op). Crossfade duration <= 0 (or non-finite) is an
 * instant switch — identical to the legacy single-bed replace. JSON getters
 * follow the caller-buffer contract of RowlEngine_GetLocale. Pump stats carry
 * no fail gate (observability only; compare on the same device).
 */
ROWL_API void RowlEngine_SetFadeCurve(RowlEngineHandle handle, int curve);
ROWL_API int RowlEngine_GetFadeCurve(RowlEngineHandle handle);

/**
 * W8-c (bulgu-2): legacy mixer koprusunun checked loud varyantlari
 * (tanim: engine/src/c_api_thread_contract_guard.cpp). Legacy void/int
 * formlar sessiz-tier iken bu formlar loud-tier'dir: canli handle'a
 * yabanci thread'den cagri ROWL_RESULT_WRONG_THREAD damgalar + cross-thread
 * okunur; olu handle sessiz ROWL_RESULT_INVALID_HANDLE. curve 0/1 disi
 * ROWL_RESULT_INVALID_ARGUMENT; null outValue ayni kodla fail-closed.
 * Additive: legacy imzalar aynen durur.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_SetFadeCurveChecked(
    RowlEngineHandle handle, int curve);
ROWL_API RowlEngine_ResultCode RowlEngine_GetFadeCurveChecked(
    RowlEngineHandle handle, int* outValue);
ROWL_API void RowlEngine_SetSfxPoolDepth(RowlEngineHandle handle, int depth);
ROWL_API int RowlEngine_GetSfxPoolDepth(RowlEngineHandle handle);
ROWL_API int RowlEngine_GetSfxActiveVoices(RowlEngineHandle handle);
ROWL_API RowlEngine_ResultCode RowlEngine_GetSfxActivePaths(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);
ROWL_API int RowlEngine_PlayAmbienceBed(RowlEngineHandle handle, const char* assetPath, int bed);
ROWL_API void RowlEngine_StopAmbienceBed(RowlEngineHandle handle, int bed);
ROWL_API void RowlEngine_SetAmbienceBedVolume(RowlEngineHandle handle, int bed, float volume);
ROWL_API float RowlEngine_GetAmbienceBedVolume(RowlEngineHandle handle, int bed);
ROWL_API int RowlEngine_IsAmbienceBedPlaying(RowlEngineHandle handle, int bed);
ROWL_API int RowlEngine_CrossfadeAmbienceTo(RowlEngineHandle handle, const char* assetPath, float durationSeconds, int curve);
ROWL_API int RowlEngine_IsAmbienceCrossfadeActive(RowlEngineHandle handle);
ROWL_API RowlEngine_ResultCode RowlEngine_GetBgmPumpStatsJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);
ROWL_API uint64_t RowlEngine_GetBgmPumpAvgMicroseconds(RowlEngineHandle handle);

/**
 * Faz 5 Dilim 3 — katmanli karakter slotlari + expression presetleri
 * (ROWL_ENGINE_CAPABILITY_CHARACTER_LAYERS). All additive; older entry
 * points are untouched.
 *
 * Slotlar: "body" < "face" < "outfit" < "accessory" (sabit cizim sirasi).
 * Bos asset slotu temizler; opaklik [0,1]'e clamp'lenir (non-finite red);
 * gorunurlukte nonzero = visible. Preset listesi bellekte tutulur
 * (kalicilik C# tarafinda JSON). Expression uygulamasi atomiktir: biri
 * bozuk/eksikse HICBIRI degismez + GetLastCharacterErrorUtf8 tani verir.
 * Tum string girisler 256 KiB tasiyici siniriyla bounded taranir; uzeri
 * INVALID_ARGUMENT ile reddedilir. JSON ciktilar caller-buffer sozlesmesini
 * izler (NULL/0 boyut-sorgu, dar tampon clears + BUFFER_TOO_SMALL).
 */
ROWL_API RowlEngine_ResultCode RowlEngine_SetCharacterSlotAsset(
    RowlEngineHandle handle, const char* slotName, const char* assetPath);
ROWL_API RowlEngine_ResultCode RowlEngine_GetCharacterSlotAssetUtf8(
    RowlEngineHandle handle, const char* slotName, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize);
ROWL_API RowlEngine_ResultCode RowlEngine_SetCharacterSlotOpacity(
    RowlEngineHandle handle, const char* slotName, float opacity);
ROWL_API RowlEngine_ResultCode RowlEngine_GetCharacterSlotOpacity(
    RowlEngineHandle handle, const char* slotName, float* outOpacity);
ROWL_API RowlEngine_ResultCode RowlEngine_SetCharacterSlotVisible(
    RowlEngineHandle handle, const char* slotName, int visible);
ROWL_API int RowlEngine_IsCharacterSlotVisible(
    RowlEngineHandle handle, const char* slotName);
ROWL_API RowlEngine_ResultCode RowlEngine_RegisterCharacterPreset(
    RowlEngineHandle handle, const char* presetName,
    const char* expressionJsonUtf8);
ROWL_API RowlEngine_ResultCode RowlEngine_ApplyCharacterExpression(
    RowlEngineHandle handle, const char* presetName);
ROWL_API RowlEngine_ResultCode RowlEngine_GetCharacterPresetListJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);
ROWL_API RowlEngine_ResultCode RowlEngine_GetCharacterDrawListJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);
ROWL_API RowlEngine_ResultCode RowlEngine_GetLastCharacterErrorUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

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

/** Length-reporting variant of GetLastAudioError (see lifetime contract). */
ROWL_API const char* RowlEngine_GetLastAudioErrorWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetLastAudioError (B2b; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLastAudioErrorUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/** Returns 1 when a physical audio output device is open, 0 while in silent fallback. */
ROWL_API int RowlEngine_IsAudioDeviceAvailable(RowlEngineHandle handle);

/** Returns 1 while audio output is suspended (minimized window), 0 otherwise. */
ROWL_API int RowlEngine_IsAudioOutputSuspended(RowlEngineHandle handle);

/** Returns the peak level (0.0f - 1.0f) for a channel (0: Bgm, 1: Voice, 2: Sfx, 3: Master) and channelIndex (0: Left, 1: Right). */
ROWL_API float RowlEngine_GetAudioChannelPeak(RowlEngineHandle handle, int channelType, int channelIndex);

/** Returns the RMS level (0.0f - 1.0f) for a channel (0: Bgm, 1: Voice, 2: Sfx, 3: Master) and channelIndex (0: Left, 1: Right). */
ROWL_API float RowlEngine_GetAudioChannelRms(RowlEngineHandle handle, int channelType, int channelIndex);

/** Fills outBands with up to bandCount frequency band estimates (0: Bass, 1: Mid-Low, 2: Mid-High, 3: Treble). Dead handle (unknown/destroyed/null, any bandCount): zero-fills outBands[0..bandCount) and records nothing (GetLastResultCode stays INVALID_HANDLE, never WRONG_THREAD). Live handle from a foreign thread: buffer left untouched, stamps WRONG_THREAD (14) readable via RowlEngine_GetLastResultCode. Null buffer or bandCount <= 0 is a silent no-op regardless of handle state. */
ROWL_API void RowlEngine_GetAudioSpectrum(RowlEngineHandle handle, float* outBands, int bandCount);

/* ── Typewriter Voice Blips & Audio Effects (Milestone 25) ───────────────── */

/** Plays a voice blip preview or trigger. If soundPath is NULL or empty, generates procedural synth blip. */
ROWL_API void RowlEngine_PlayVoiceBlip(RowlEngineHandle handle, const char* soundPath, float pitch, float volume, int channelType);

/** Configures dialogue typewriter voice blip parameters on active scene dialogue. */
ROWL_API void RowlEngine_SetDialogueVoiceBlip(RowlEngineHandle handle, const char* soundPath, float basePitch, float pitchVariance, int cadence, int skipPunctuation, int channelType);

/** Returns active dialogue voice blip sound path (engine-owned string). */
ROWL_API const char* RowlEngine_GetDialogueVoiceBlipSound(RowlEngineHandle handle);

/** Length-reporting variant of GetDialogueVoiceBlipSound (see lifetime contract). */
ROWL_API const char* RowlEngine_GetDialogueVoiceBlipSoundWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetDialogueVoiceBlipSound (B2b; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetDialogueVoiceBlipSoundUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/** Returns active dialogue voice blip base pitch multiplier. */
ROWL_API float RowlEngine_GetDialogueVoiceBlipPitch(RowlEngineHandle handle);

/** Returns active dialogue voice blip pitch variance. */
ROWL_API float RowlEngine_GetDialogueVoiceBlipVariance(RowlEngineHandle handle);

/** Returns active dialogue voice blip cadence (characters per blip). */
ROWL_API int RowlEngine_GetDialogueVoiceBlipCadence(RowlEngineHandle handle);

/** Returns 1 if dialogue voice blips skip punctuation and whitespace, 0 otherwise. */
ROWL_API int RowlEngine_GetDialogueVoiceBlipSkipPunctuation(RowlEngineHandle handle);

/** Returns active dialogue voice blip audio channel (1 = Voice, 2 = Sfx). */
ROWL_API int RowlEngine_GetDialogueVoiceBlipChannel(RowlEngineHandle handle);

/** Returns active dialogue voice blip volume (0.0 - 1.0). */
ROWL_API float RowlEngine_GetDialogueVoiceBlipVolume(RowlEngineHandle handle);

/** Sets active dialogue voice blip volume (0.0 - 1.0). */
ROWL_API void RowlEngine_SetDialogueVoiceBlipVolume(RowlEngineHandle handle, float volume);

/** Returns cumulative total of voice blips triggered by the engine. */
ROWL_API uint32_t RowlEngine_GetVoiceBlipCount(RowlEngineHandle handle);

/** Resets cumulative voice blip counter to zero. */
ROWL_API void RowlEngine_ResetVoiceBlipCount(RowlEngineHandle handle);

/** Returns how many blips fell back to procedural synthesis (A5-tur3; synth <= voice). */
ROWL_API uint32_t RowlEngine_GetSynthBlipCount(RowlEngineHandle handle);

/** Returns cumulative audio chunks dropped at queue time (A5-tur3). */
ROWL_API uint64_t RowlEngine_GetAudioDropCount(RowlEngineHandle handle);

/**
 * Returns a JSON array of current script-component diagnostics. The result is
 * engine-owned and valid until the next diagnostics query on the same thread.
 */
ROWL_API const char* RowlEngine_GetScriptRuntimeDiagnosticsJson(RowlEngineHandle handle);

/** Length-reporting variant of GetScriptRuntimeDiagnosticsJson (see lifetime contract). */
ROWL_API const char* RowlEngine_GetScriptRuntimeDiagnosticsJsonWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetScriptRuntimeDiagnosticsJson (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetScriptRuntimeDiagnosticsJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/** Returns the bounded player dialogue backlog as an engine-owned JSON array. */
ROWL_API const char* RowlEngine_GetDialogueHistoryJson(RowlEngineHandle handle);

/** Length-reporting variant of GetDialogueHistoryJson (see lifetime contract). */
ROWL_API const char* RowlEngine_GetDialogueHistoryJsonWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetDialogueHistoryJson (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetDialogueHistoryJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/* ── Save / Load Slots & History Rewind ───────────────────────────────────── */

/** Result-coded save entry point for new hosts.
 * Requires a prior RowlEngine_Init (D1/B1b): pre-init saves return
 * StateError without writing a file. */
ROWL_API RowlEngine_ResultCode RowlEngine_SaveGameSlotResult(
    RowlEngineHandle handle, int32_t slotIndex);

/**
 * Legacy compatibility wrapper around RowlEngine_SaveGameSlotResult.
 * Returns 1 on success, 0 on failure.
 */
ROWL_API int RowlEngine_SaveGameSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Result-coded load entry point for new hosts.
 * Requires a prior RowlEngine_Init (D1/B1b): pre-init loads return
 * StateError without touching session state. */
ROWL_API RowlEngine_ResultCode RowlEngine_LoadGameSlotResult(
    RowlEngineHandle handle, int32_t slotIndex);

/** Legacy compatibility wrapper around RowlEngine_LoadGameSlotResult. */
ROWL_API int RowlEngine_LoadGameSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Returns 1 if the specified save slot exists, 0 otherwise. */
ROWL_API int RowlEngine_HasSaveSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Deletes the specified save slot. Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_DeleteSaveSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Rewinds the game state by the specified number of steps (default 1). Returns 1 on success, 0 on failure.
 * Requires a prior RowlEngine_Init (D1/B1b): pre-init rewind returns 0 with StateError. */
ROWL_API int RowlEngine_Rewind(RowlEngineHandle handle, uint32_t steps);

/** Returns the current history step ID. */
ROWL_API uint64_t RowlEngine_GetCurrentStepId(RowlEngineHandle handle);

/**
 * Save-slot display metadata (ROWL_ENGINE_CAPABILITY_SAVE_METADATA).
 *
 * Copies a UTF-8 JSON object describing a save slot without loading it
 * into the live story: slot, saved_at (ISO-8601, "" for legacy saves),
 * playtime_seconds, chapter_id, chapter_title, summary, thumbnail_width,
 * thumbnail_height, has_thumbnail and thumbnail_png_base64 ("" when the
 * save captured no framebuffer). Follows the caller-buffer contract.
 * Missing slots report ROWL_RESULT_FILE_NOT_FOUND; unreadable slots
 * report ROWL_RESULT_PARSE_ERROR; out-of-range indices report
 * ROWL_RESULT_INVALID_ARGUMENT. Older entry points are untouched.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetSaveSlotMetadataJson(
    RowlEngineHandle handle, int32_t slotIndex, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize);

/* ── MS-6 Quick Slots & Pause Menu ────────────────────────────────────────── */

/**
 * Sets the active quick-save slot (0-9) used by F5/F9 and the QuickSave /
 * QuickLoad calls below. Returns 1 on success, 0 for an out-of-range slot
 * (see RowlEngine_GetLastResultCode for InvalidArgument details).
 */
ROWL_API int RowlEngine_SetQuickSaveSlot(RowlEngineHandle handle, int32_t slotIndex);

/** Returns the active quick-save slot (default 0), or -1 for a dead handle. */
ROWL_API int32_t RowlEngine_GetQuickSaveSlot(RowlEngineHandle handle);

/** Saves through the active quick-save slot. Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_QuickSave(RowlEngineHandle handle);

/** Loads through the active quick-save slot. Returns 1 on success, 0 on failure. */
ROWL_API int RowlEngine_QuickLoad(RowlEngineHandle handle);

/**
 * Opens (nonzero) or closes (zero) the pause menu. Opening resets navigation
 * to the main page; closing resumes the simulation. Never quits the game.
 */
ROWL_API void RowlEngine_SetPaused(RowlEngineHandle handle, int paused);

/** Returns 1 while the pause menu is open, 0 otherwise. */
ROWL_API int RowlEngine_IsPaused(RowlEngineHandle handle);

/** Pause-menu navigation commands for RowlEngine_PauseMenuCommand. */
enum RowlPauseMenuCommand {
    ROWL_PAUSE_MENU_UP = 0,
    ROWL_PAUSE_MENU_DOWN = 1,
    ROWL_PAUSE_MENU_LEFT = 2,
    ROWL_PAUSE_MENU_RIGHT = 3,
    ROWL_PAUSE_MENU_BACK = 4,
    ROWL_PAUSE_MENU_CONFIRM = 5
};

/**
 * Drives pause-menu navigation (arrow-key equivalent). Unknown commands are
 * ignored. No-op unless the menu is open.
 */
ROWL_API void RowlEngine_PauseMenuCommand(RowlEngineHandle handle, int command);

/**
 * Pause-menu snapshot JSON: open, mode (0=main,1=save,2=load), selected row,
 * confirm_quit arm, quick_slot, title, hint, and rows[] with label/value.
 * Same ownership rules as RowlEngine_GetDialogueHistoryJson.
 */
ROWL_API const char* RowlEngine_GetPauseMenuJson(RowlEngineHandle handle);

/** Same ownership rules as RowlEngine_GetDialogueHistoryJsonWithLength. */
ROWL_API const char* RowlEngine_GetPauseMenuJsonWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetPauseMenuJson (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetPauseMenuJsonUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/* ── Scripting & Variable Evaluation ─────────────────────────────────────── */

/** Sets a string variable in the scripting environment and game state. */
ROWL_API void RowlEngine_SetVariable(RowlEngineHandle handle, const char* key, const char* value);

/**
 * Gets a variable string by key. The returned pointer is owned by the engine.
 *
 * Contract: values set explicitly (SetVariable, variable components with
 * operation "set") come back verbatim. Values produced by numeric operations
 * (variable components with operation "add") come back in engine-canonical
 * double formatting (e.g. "1.000000", never "1"). Compare numerically when
 * the producer may be arithmetic.
 */
ROWL_API const char* RowlEngine_GetVariable(RowlEngineHandle handle, const char* key);

/** Length-reporting variant of GetVariable (see lifetime contract). */
ROWL_API const char* RowlEngine_GetVariableWithLength(RowlEngineHandle handle, const char* key, uint32_t* outLen);

/** Caller-buffer variant of GetVariable (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetVariableUtf8(
    RowlEngineHandle handle, const char* key, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/**
 * Evaluates a Lua condition expression (e.g. "gold >= 50"). Returns 1 for true, 0 for false.
 *
 * Fail-closed contract: any error — dead/invalid handle, null expression, or
 * uninitialized/broken sandbox — returns 0 and records a result code readable
 * via RowlEngine_GetLastResultCode() (InvalidHandle, InvalidArgument, or the
 * script error). A null expression never passes a branch.
 */
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
 *  12 = BufferTooSmall
 *  13 = Unsupported
 *  14 = WrongThread (D3: foreign-thread rejection on a live handle)
 *  99 = UnknownError
 */
ROWL_API int32_t RowlEngine_GetLastResultCode(RowlEngineHandle handle);

/**
 * Returns the operation name of the last runtime call (e.g. "save_game_slot", "load_story_graph_vfs").
 * Pointer is thread-local/engine-owned UTF-8 string.
 */
ROWL_API const char* RowlEngine_GetLastResultOperation(RowlEngineHandle handle);

/** Length-reporting variant of GetLastResultOperation (see lifetime contract). */
ROWL_API const char* RowlEngine_GetLastResultOperationWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetLastResultOperation (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLastResultOperationUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/**
 * Returns the human-readable diagnostic message of the last runtime operation.
 * Pointer is thread-local/engine-owned UTF-8 string.
 */
ROWL_API const char* RowlEngine_GetLastResultMessage(RowlEngineHandle handle);

/** Length-reporting variant of GetLastResultMessage (see lifetime contract). */
ROWL_API const char* RowlEngine_GetLastResultMessageWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetLastResultMessage (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLastResultMessageUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

/**
 * Returns the target identifier or path of the last runtime operation (e.g. slot index or file path).
 * Pointer is thread-local/engine-owned UTF-8 string.
 */
ROWL_API const char* RowlEngine_GetLastResultTarget(RowlEngineHandle handle);

/** Length-reporting variant of GetLastResultTarget (see lifetime contract). */
ROWL_API const char* RowlEngine_GetLastResultTargetWithLength(RowlEngineHandle handle, uint32_t* outLen);

/** Caller-buffer variant of GetLastResultTarget (B2a; no borrowed lifetime). */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLastResultTargetUtf8(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize, uint32_t* outRequiredSize);

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
 * MS-4 dirty-frame query: returns 1 when the next rendered frame cannot differ
 * from the presented one (no transition, flash, camera motion, incomplete
 * typewriter, active scripts, or scene entities), 0 otherwise. Hosts skip the
 * pixel-buffer copy on 1. Conservative by design; errors report 0 (copy).
 */
ROWL_API int RowlEngine_IsPreviewFrameStatic(RowlEngineHandle handle);

/**
 * Sets 2D camera position and zoom factor.
 * x, y: camera center coordinates in virtual canvas space (default: 960, 540).
 * zoom: zoom factor (clamped between 0.1 and 10.0, default: 1.0).
 * Pre-init calls keep prior behavior but report StateError via
 * RowlEngine_GetLastResultCode (D1/B1b); same for the other audio/render setters.
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

/**
 * Triggers camera screen shake using a cinematic preset ("subtle", "earthquake", "explosion", "heartbeat"/"pulse").
 */
ROWL_API void RowlEngine_TriggerCameraShakePreset(RowlEngineHandle handle, const char* presetName, float intensityMultiplier, float durationSeconds);

/**
 * Triggers camera screen shake with full harmonic profile parameters (frequency, damping, directional constraints).
 */
ROWL_API void RowlEngine_TriggerCameraShakeProfile(RowlEngineHandle handle, float intensity, float durationSeconds, float frequency, float damping, float dirX, float dirY);

/**
 * Returns current camera shake offset in virtual canvas pixels.
 */
ROWL_API float RowlEngine_GetCameraShakeOffsetX(RowlEngineHandle handle);
ROWL_API float RowlEngine_GetCameraShakeOffsetY(RowlEngineHandle handle);

/**
 * Triggers screen flash effect decaying over durationSeconds.
 */
ROWL_API void RowlEngine_TriggerScreenFlash(RowlEngineHandle handle, uint8_t r, uint8_t g, uint8_t b, float durationSeconds, float intensity);
ROWL_API void RowlEngine_TriggerScreenFlashHex(RowlEngineHandle handle, const char* colorHex, float durationSeconds, float intensity);
ROWL_API int RowlEngine_IsScreenFlashActive(RowlEngineHandle handle);

/**
 * Sets persistent screen color tint overlay (opacity 0..1).
 */
ROWL_API void RowlEngine_SetScreenTint(RowlEngineHandle handle, uint8_t r, uint8_t g, uint8_t b, float opacity);
ROWL_API void RowlEngine_SetScreenTintHex(RowlEngineHandle handle, const char* colorHex, float opacity);
ROWL_API void RowlEngine_ClearScreenTint(RowlEngineHandle handle);
ROWL_API float RowlEngine_GetScreenTintOpacity(RowlEngineHandle handle);

/**
 * Configures cinematic vignette darkening effect.
 */
ROWL_API void RowlEngine_SetVignette(RowlEngineHandle handle, float intensity, float radius, const char* colorHex);
ROWL_API float RowlEngine_GetVignetteIntensity(RowlEngineHandle handle);

/**
 * Faz 5 Dilim 4 — butceli asset prefetch + chapter-sinirli yukleme
 * (ROWL_ENGINE_CAPABILITY_PREFETCH_CHAPTERS). All additive; older entry
 * points are untouched.
 *
 * Prefetch: active + next scene asset list (node component image/audio
 * paths: sprite/texture, bgm/voice/sfx/ambience, character slot assets),
 * byte-budgeted (default 32 MiB, clamped to 128 MiB) + time-budgeted
 * (~4 ms per pump, deferred past the deadline) synchronous pump over VFS.
 * No threads: the host update thread drives RowlEngine_PumpPrefetch.
 * Progress is observable (ready/missing counters + bytes). Missing assets
 * never stop the queue (counted + diagnosed).
 *
 * Chapters: index ({format_version?, start_node_id?, chapters[]}, the editor
 * chapters/chapter_index.json schema) + per-chapter files ({chapter_id,
 * nodes[]}, the editor LoadChapterFile schema). Only the active chapter and
 * its +-1 neighbors stay resident; distant chapters unload; access to an
 * unloaded node transparently reloads it + records a diagnostic. Legacy
 * single-file graphs keep working (one implicit chapter, no windowing).
 *
 * Fail-closed: dead/null handle -> INVALID_HANDLE (0/"" carriers); unknown
 * chapter -> INVALID_ARGUMENT; budget clamped, never rejected; oversized
 * input (no NUL within 256 KiB + 1; 16 MiB + 1 for chapter files) rejected
 * with INVALID_ARGUMENT. JSON outputs follow the caller-buffer contract of
 * RowlEngine_GetLocale (NULL/0 size query, undersized buffer clears +
 * BUFFER_TOO_SMALL + outRequiredSize).
 */

/** Feeds the chapter index (editor chapter_index.json schema). Replaces prior loader state. */
ROWL_API RowlEngine_ResultCode RowlEngine_LoadChapterIndexJson(
    RowlEngineHandle handle, const char* indexJsonUtf8);
/** Appends one chapter file (editor LoadChapterFile schema); re-feeding a chapter replaces it. */
ROWL_API RowlEngine_ResultCode RowlEngine_AppendChapterFileJson(
    RowlEngineHandle handle, const char* chapterJsonUtf8);
/** Marks one chapter resident (widens the window, active unchanged). Unknown id -> INVALID_ARGUMENT. */
ROWL_API RowlEngine_ResultCode RowlEngine_LoadChapter(
    RowlEngineHandle handle, const char* chapterIdUtf8);
/** Unloads one chapter. Refuses the active chapter and unknown ids with INVALID_ARGUMENT. */
ROWL_API RowlEngine_ResultCode RowlEngine_UnloadChapter(
    RowlEngineHandle handle, const char* chapterIdUtf8);
/** Copies {active, loaded[], neighbors[], node_counts{}, resident_nodes, total_nodes,
 * legacy_single_graph, last_diagnostic} as UTF-8 JSON. */
ROWL_API RowlEngine_ResultCode RowlEngine_GetLoadedChaptersJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);
/** Returns 1 when the node starts a chapter or has a successor in another chapter; 0 otherwise
 * (unknown node and dead handle are 0, fail closed). Requires a prior RowlEngine_Init
 * for engine-graph reads (D1/B1b): pre-init returns 0 with StateError. */
ROWL_API int RowlEngine_IsChapterBoundaryNode(RowlEngineHandle handle, uint64_t nodeId);
/**
 * Triggers a prefetch for the requested chapter window (chapter + its
 * successor; null/empty selects the active chapter: loader-active, else the
 * engine current chapter, else the current node + successors for legacy
 * single-file graphs). budgetBytes 0 selects the 32 MiB default; larger
 * values clamp to 128 MiB. Runs one synchronous ~4 ms pump before returning;
 * further progress via RowlEngine_PumpPrefetch. Requires a prior
 * RowlEngine_Init (D1/B1b): pre-init returns StateError (no phantom-missing).
 */
ROWL_API RowlEngine_ResultCode RowlEngine_PrefetchChapterAssets(
    RowlEngineHandle handle, const char* chapterIdUtf8, uint64_t budgetBytes);
/** Pumps the prefetch queue for at most maxMilliseconds (<= 0 or non-finite
 * selects ~4 ms; clamped to 50 ms). Returns newly-ready assets (0 fail closed).
 * Pre-init pumps 0 with StateError (D1/B1b). */
ROWL_API int RowlEngine_PumpPrefetch(RowlEngineHandle handle, float maxMilliseconds);
/** Copies {total_assets, ready_assets, missing_assets, queued_assets,
 * ready_bytes, budget_bytes, complete, missing_paths[], last_diagnostic} as UTF-8 JSON. */
ROWL_API RowlEngine_ResultCode RowlEngine_GetPrefetchProgressJson(
    RowlEngineHandle handle, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize);

/**
 * Faz 5 Dilim 5 — converter provenance sidecars
 * (ROWL_ENGINE_CAPABILITY_CONVERTER_PROVENANCE). Additive; older entry
 * points are untouched.
 *
 * Copies the `<path>.rowlconv.json` sidecar written by rowl_oggenc /
 * rowl_webp2png (see docs/MEDIA_CONVERTERS_CONTRACT.md) through the active
 * VFS into caller-owned memory. The JSON carries source_sha256,
 * converter_name, converter_version, settings, output_sha256 and
 * created_by. Follows the caller-buffer contract of RowlEngine_GetLocale
 * (NULL/0 size query, undersized buffer clears + BUFFER_TOO_SMALL +
 * outRequiredSize). A missing sidecar is a NORMAL condition for assets
 * that were never converter-produced and reports ROWL_RESULT_FILE_NOT_FOUND
 * (not an error in the asset itself). A present-but-unparseable sidecar
 * reports ROWL_RESULT_PARSE_ERROR. Asset paths are bounded (NUL within
 * 256 KiB + 1) and rejected with ROWL_RESULT_INVALID_ARGUMENT above that.
 */
ROWL_API RowlEngine_ResultCode RowlEngine_GetAssetProvenanceJson(
    RowlEngineHandle handle, const char* assetPathUtf8, char* buffer,
    uint32_t bufferSize, uint32_t* outRequiredSize);

#ifdef __cplusplus
} /* extern "C" */
#endif
