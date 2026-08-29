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

#include <cstring>

/* ── Internal helper ─────────────────────────────────────────────────────── */
static inline Rowl::Core::Engine* toEngine(RowlEngineHandle h) {
    return static_cast<Rowl::Core::Engine*>(h);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

extern "C" {

RowlEngineHandle RowlEngine_Create(void) {
    return new Rowl::Core::Engine();
}

void RowlEngine_Destroy(RowlEngineHandle handle) {
    if (!handle) return;
    delete toEngine(handle);
}

int RowlEngine_Init(RowlEngineHandle handle,
                     uint32_t virtualWidth,
                     uint32_t virtualHeight,
                     int vsync) {
    if (!handle) return 0;

    Rowl::Core::EngineConfig cfg;
    cfg.appName       = "Rowl Engine";
    cfg.virtualWidth  = virtualWidth;
    cfg.virtualHeight = virtualHeight;
    cfg.vsync         = (vsync != 0);
    cfg.isIpcMode     = false; // IPC artık yok — tek süreç

    return toEngine(handle)->initialize(cfg) ? 1 : 0;
}

void RowlEngine_Step(RowlEngineHandle handle, float deltaTime) {
    if (!handle) return;
    toEngine(handle)->step(deltaTime);
}

void RowlEngine_Shutdown(RowlEngineHandle handle) {
    if (!handle) return;
    toEngine(handle)->shutdown();
}

int RowlEngine_IsRunning(RowlEngineHandle handle) {
    if (!handle) return 0;
    return toEngine(handle)->isRunning() ? 1 : 0;
}

/* ── Native window embedding ─────────────────────────────────────────────── */

void RowlEngine_SetExternalWindowHandle(RowlEngineHandle handle,
                                         void* nativeWindowHandle,
                                         uint32_t width,
                                         uint32_t height) {
    if (!handle) return;
    toEngine(handle)->setExternalWindowHandle(nativeWindowHandle, width, height);
}

void RowlEngine_ResizeViewport(RowlEngineHandle handle,
                                uint32_t newWidth,
                                uint32_t newHeight) {
    if (!handle) return;
    auto* win = toEngine(handle)->getWindow();
    if (win) win->resizeViewport(newWidth, newHeight);
}

/* ── Offscreen Framebuffer & Playback Control ────────────────────────────── */

const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH) {
    if (!handle) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return nullptr;
    }
    return toEngine(handle)->getPixelBuffer(outW, outH);
}

void RowlEngine_SetPlayState(RowlEngineHandle handle, int isPlaying) {
    if (!handle) return;
    toEngine(handle)->setPlayState(isPlaying != 0);
}

void RowlEngine_ResetToStartNode(RowlEngineHandle handle) {
    if (!handle) return;
    toEngine(handle)->resetToStartNode();
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
    toEngine(handle)->updateActiveScene(
        speaker    ? speaker    : "",
        dialogue   ? dialogue   : "",
        background ? background : "",
        bgX, bgY, bgW, bgH,
        character  ? character  : "",
        charX, charY, charW, charH,
        dlgX,  dlgY,  dlgW,  dlgH
    );
}

void RowlEngine_UpdateSceneFromJson(
    RowlEngineHandle handle,
    const char* componentsJson)
{
    if (!handle || !componentsJson) return;
    toEngine(handle)->updateSceneFromComponents(componentsJson ? componentsJson : "[]");
}

void RowlEngine_LoadStoryGraph(RowlEngineHandle handle, const char* jsonPath) {
    if (!handle || !jsonPath) return;
    // Engine'in path'i geçici olarak override et ve graph'i yükle
    toEngine(handle)->loadStoryGraphFromPath(jsonPath);
}

void RowlEngine_AdvanceNode(RowlEngineHandle handle, uint32_t choiceIndex) {
    if (!handle) return;
    toEngine(handle)->advanceToNextNode(choiceIndex);
}

/* ── State queries ───────────────────────────────────────────────────────── */

const char* RowlEngine_GetSpeaker(RowlEngineHandle handle) {
    if (!handle) return "";
    // Returned pointer is valid until next step/update — owned by engine
    static thread_local std::string buf;
    buf = toEngine(handle)->getActiveSpeaker();
    return buf.c_str();
}

const char* RowlEngine_GetDialogue(RowlEngineHandle handle) {
    if (!handle) return "";
    static thread_local std::string buf;
    buf = toEngine(handle)->getActiveDialogue();
    return buf.c_str();
}

uint64_t RowlEngine_GetCurrentNodeId(RowlEngineHandle handle) {
    if (!handle) return 0;
    return toEngine(handle)->getCurrentNodeId();
}

} // extern "C"
