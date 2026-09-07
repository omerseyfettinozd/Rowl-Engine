/**
 * c_api.cpp
 *
 * Implementation of the Rowl Engine C-API (c_api.h).
 * Bridges the C-linkage surface to the internal C++ Engine class.
 *
 * Thread-safety: all public functions must be called from the same thread
 * that called RowlEngine_Init() (i.e., the host UI thread / GL thread).
 * The Engine internally manages its own worker threads where needed.
 */

#include "rowl/c_api.h"
#include "rowl/core/engine.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/vfs/vfs.hpp"

#include <cstring>
#include <exception>
#include <utility>

/* ── Internal helper ─────────────────────────────────────────────────────── */
static inline Rowl::Core::Engine* toEngine(RowlEngineHandle h) {
    return static_cast<Rowl::Core::Engine*>(h);
}

// A C++ exception crossing this ABI boundary is undefined behaviour and can
// terminate the .NET host. Every exported entry point must degrade safely.
template <typename Return, typename Fn>
static Return invokeNoexcept(Fn&& operation, Return fallback) noexcept {
    try {
        return std::forward<Fn>(operation)();
    } catch (...) {
        return fallback;
    }
}

template <typename Fn>
static void invokeNoexcept(Fn&& operation) noexcept {
    try {
        std::forward<Fn>(operation)();
    } catch (...) {
        // No logging here: logging itself must not become another exception path.
    }
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

extern "C" {

RowlEngineHandle RowlEngine_Create(void) {
    return invokeNoexcept<RowlEngineHandle>([] { return new Rowl::Core::Engine(); }, nullptr);
}

void RowlEngine_Destroy(RowlEngineHandle handle) {
    if (!handle) return;
    invokeNoexcept([&] { delete toEngine(handle); });
}

int RowlEngine_Init(RowlEngineHandle handle,
                     uint32_t virtualWidth,
                     uint32_t virtualHeight,
                     int vsync) {
    if (!handle) return 0;

    return invokeNoexcept<int>([&] {
        Rowl::Core::EngineConfig cfg;
        cfg.appName       = "Rowl Engine";
        cfg.virtualWidth  = virtualWidth;
        cfg.virtualHeight = virtualHeight;
        cfg.vsync         = (vsync != 0);
        cfg.isIpcMode     = false; // IPC artık yok — tek süreç
        return toEngine(handle)->initialize(cfg) ? 1 : 0;
    }, 0);
}

int RowlEngine_InitStandalone(RowlEngineHandle handle,
                               const char* appTitle,
                               uint32_t virtualWidth,
                               uint32_t virtualHeight,
                               int vsync) {
    if (!handle) return 0;

    return invokeNoexcept<int>([&] {
        Rowl::Core::EngineConfig cfg;
        cfg.appName          = (appTitle && appTitle[0] != '\0') ? appTitle : "Rowl Game";
        cfg.virtualWidth     = virtualWidth > 0 ? virtualWidth : 1920;
        cfg.virtualHeight    = virtualHeight > 0 ? virtualHeight : 1080;
        cfg.vsync            = (vsync != 0);
        cfg.standaloneWindow = true;
        return toEngine(handle)->initialize(cfg) ? 1 : 0;
    }, 0);
}

void RowlEngine_Run(RowlEngineHandle handle) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->run(); });
}

void RowlEngine_Step(RowlEngineHandle handle, float deltaTime) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->step(deltaTime); });
}

void RowlEngine_Shutdown(RowlEngineHandle handle) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->shutdown(); });
}

int RowlEngine_IsRunning(RowlEngineHandle handle) {
    if (!handle) return 0;
    return invokeNoexcept<int>([&] { return toEngine(handle)->isRunning() ? 1 : 0; }, 0);
}

/* ── Native window embedding ─────────────────────────────────────────────── */

void RowlEngine_SetExternalWindowHandle(RowlEngineHandle handle,
                                         void* nativeWindowHandle,
                                         uint32_t width,
                                         uint32_t height) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->setExternalWindowHandle(nativeWindowHandle, width, height); });
}

void RowlEngine_ResizeViewport(RowlEngineHandle handle,
                                uint32_t newWidth,
                                uint32_t newHeight) {
    if (!handle) return;
    invokeNoexcept([&] {
        auto* win = toEngine(handle)->getWindow();
        if (win) win->resizeViewport(newWidth, newHeight);
    });
}

/* ── Offscreen Framebuffer & Playback Control ────────────────────────────── */

const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH) {
    if (!handle) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return nullptr;
    }
    return invokeNoexcept<const uint8_t*>([&] { return toEngine(handle)->getPixelBuffer(outW, outH); }, nullptr);
}

void RowlEngine_SetPlayState(RowlEngineHandle handle, int isPlaying) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->setPlayState(isPlaying != 0); });
}

void RowlEngine_ResetToStartNode(RowlEngineHandle handle) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->resetToStartNode(); });
}

/* ── Scene / story control ───────────────────────────────────────────────── */

void RowlEngine_UpdateScene(
    RowlEngineHandle handle,
    const char* speaker,
    const char* dialogue,
    const char* background,
    float bgX,   float bgY,   float bgW,   float bgH,
    const char* character,
    float charX, float charY, float charW, float charH,
    float dlgX,  float dlgY,  float dlgW,  float dlgH)
{
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->updateActiveScene(
        speaker    ? speaker    : "",
        dialogue   ? dialogue   : "",
        background ? background : "",
        bgX, bgY, bgW, bgH,
        character  ? character  : "",
        charX, charY, charW, charH,
        dlgX,  dlgY,  dlgW,  dlgH
    ); });
}

void RowlEngine_UpdateSceneFromJson(
    RowlEngineHandle handle,
    const char* componentsJson)
{
    if (!handle || !componentsJson) return;
    invokeNoexcept([&] { toEngine(handle)->updateSceneFromComponents(componentsJson); });
}

void RowlEngine_LoadStoryGraph(RowlEngineHandle handle, const char* jsonPath) {
    if (!handle || !jsonPath) return;
    // Engine'in path'i geçici olarak override et ve graph'i yükle
    invokeNoexcept([&] { toEngine(handle)->loadStoryGraphFromPath(jsonPath); });
}

void RowlEngine_SetProjectDirectory(RowlEngineHandle handle, const char* projectRoot) {
    if (!projectRoot || !*projectRoot) return;
    invokeNoexcept([&] {
    Rowl::VFS::VFSManager::instance().remountProject(projectRoot);
    if (handle) {
        auto* engine = toEngine(handle);
        auto* win = engine->getWindow();
        if (win) {
            win->reloadFonts();
        }
        // Try loading story graph from the newly mounted project directory
        std::string graphPath = std::string(projectRoot) + "/Assets/json/full_story_graph.json";
        if (std::filesystem::exists(graphPath)) {
            engine->loadStoryGraphFromPath(graphPath);
        } else {
            std::string altGraphPath = std::string(projectRoot) + "/full_story_graph.json";
            if (std::filesystem::exists(altGraphPath)) {
                engine->loadStoryGraphFromPath(altGraphPath);
            }
        }
    }
    });
}

void RowlEngine_AdvanceNode(RowlEngineHandle handle, uint32_t choiceIndex) {
    if (!handle) return;
    invokeNoexcept([&] { toEngine(handle)->advanceToNextNode(choiceIndex); });
}

int RowlEngine_SelectChoice(RowlEngineHandle handle, const char* optionId) {
    if (!handle || !optionId || !*optionId) return 0;
    return invokeNoexcept<int>([&] { return toEngine(handle)->advanceToChoice(optionId) ? 1 : 0; }, 0);
}

int RowlEngine_PointerDown(RowlEngineHandle handle, float x, float y) {
    if (!handle) return 0;
    // Editor supplies 1920x1080 virtual coordinates; the engine's offscreen
    // surface is the same size, so the shared hit-test path remains canonical.
    return invokeNoexcept<int>([&] { return toEngine(handle)->handlePointerDown(x, y) ? 1 : 0; }, 0);
}

/* ── State queries ───────────────────────────────────────────────────────── */

const char* RowlEngine_GetSpeaker(RowlEngineHandle handle) {
    if (!handle) return "";
    // Returned pointer is valid until next step/update — owned by engine
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getActiveSpeaker();
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetDialogue(RowlEngineHandle handle) {
    if (!handle) return "";
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getActiveDialogue();
        return buf.c_str();
    }, "");
}

uint64_t RowlEngine_GetCurrentNodeId(RowlEngineHandle handle) {
    if (!handle) return 0;
    return invokeNoexcept<uint64_t>([&] { return toEngine(handle)->getCurrentNodeId(); }, 0);
}

} // extern "C"
