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
#include "rowl/audio/audio_engine.hpp"
#include "rowl/vfs/vfs.hpp"

#include <cstring>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

/* ── Internal helper ─────────────────────────────────────────────────────── */
namespace {

std::mutex g_handleMutex;

// The opaque C handle is a stable record, not the Engine allocation itself.
// Destroyed records are intentionally retained until process exit so an old
// host callback can never become valid again if malloc reuses an Engine address.
struct HandleRecord {
    std::unique_ptr<Rowl::Core::Engine> engine;
    std::thread::id ownerThread;
};

std::unordered_map<RowlEngineHandle, Rowl::Core::Engine*> g_liveHandles;
std::vector<std::unique_ptr<HandleRecord>> g_handleRecords;

bool isLiveHandle(RowlEngineHandle handle) noexcept {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_liveHandles.find(handle);
    if (it == g_liveHandles.end()) return false;
    const auto* record = static_cast<const HandleRecord*>(handle);
    return record->ownerThread == std::thread::id{} ||
           record->ownerThread == std::this_thread::get_id();
}

bool claimHandleThread(RowlEngineHandle handle) noexcept {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_liveHandles.find(handle);
    if (it == g_liveHandles.end()) return false;
    auto* record = static_cast<HandleRecord*>(handle);
    const auto callingThread = std::this_thread::get_id();
    if (record->ownerThread == std::thread::id{}) {
        record->ownerThread = callingThread;
        return true;
    }
    return record->ownerThread == callingThread;
}

std::unique_ptr<Rowl::Core::Engine> takeLiveHandle(RowlEngineHandle handle) noexcept {
    if (!handle) return {};
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_liveHandles.find(handle);
    if (it == g_liveHandles.end()) return {};
    auto* record = static_cast<HandleRecord*>(handle);
    if (record->ownerThread != std::thread::id{} &&
        record->ownerThread != std::this_thread::get_id()) {
        return {};
    }
    g_liveHandles.erase(it);
    return std::move(record->engine);
}

} // namespace

static inline Rowl::Core::Engine* toEngine(RowlEngineHandle h) {
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_liveHandles.find(h);
    if (it == g_liveHandles.end()) return nullptr;
    const auto* record = static_cast<const HandleRecord*>(h);
    return (record->ownerThread == std::thread::id{} ||
            record->ownerThread == std::this_thread::get_id()) ? it->second : nullptr;
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
    return invokeNoexcept<RowlEngineHandle>([] {
        std::lock_guard<std::mutex> lock(g_handleMutex);
        auto record = std::make_unique<HandleRecord>();
        record->engine = std::make_unique<Rowl::Core::Engine>();
        const auto handle = static_cast<RowlEngineHandle>(record.get());
        g_liveHandles.emplace(handle, record->engine.get());
        try {
            g_handleRecords.push_back(std::move(record));
        } catch (...) {
            g_liveHandles.erase(handle);
            throw;
        }
        return handle;
    }, nullptr);
}

void RowlEngine_Destroy(RowlEngineHandle handle) {
    auto engine = takeLiveHandle(handle);
    if (!engine) return;
    invokeNoexcept([&] { engine.reset(); });
}

int RowlEngine_Init(RowlEngineHandle handle,
                     uint32_t virtualWidth,
                     uint32_t virtualHeight,
                     int vsync) {
    if (!claimHandleThread(handle)) return 0;

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
    if (!claimHandleThread(handle)) return 0;

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
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->run(); });
}

void RowlEngine_Step(RowlEngineHandle handle, float deltaTime) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->step(deltaTime); });
}

void RowlEngine_Shutdown(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->shutdown(); });
}

int RowlEngine_IsRunning(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] { return toEngine(handle)->isRunning() ? 1 : 0; }, 0);
}

/* ── Native window embedding ─────────────────────────────────────────────── */

void RowlEngine_SetExternalWindowHandle(RowlEngineHandle handle,
                                         void* nativeWindowHandle,
                                         uint32_t width,
                                         uint32_t height) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setExternalWindowHandle(nativeWindowHandle, width, height); });
}

void RowlEngine_ResizeViewport(RowlEngineHandle handle,
                                uint32_t newWidth,
                                uint32_t newHeight) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* win = toEngine(handle)->getWindow();
        if (win) win->resizeViewport(newWidth, newHeight);
    });
}

/* ── Offscreen Framebuffer & Playback Control ────────────────────────────── */

const uint8_t* RowlEngine_GetPixelBuffer(RowlEngineHandle handle, uint32_t* outW, uint32_t* outH) {
    if (!isLiveHandle(handle)) {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
        return nullptr;
    }
    return invokeNoexcept<const uint8_t*>([&] { return toEngine(handle)->getPixelBuffer(outW, outH); }, nullptr);
}

uint32_t RowlEngine_GetTextureCacheTextureCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint32_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? static_cast<uint32_t>(window->getTextureCacheTextureCount()) : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheBytes(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getTextureCacheBytes() : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheBudgetBytes(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getTextureCacheBudgetBytes() : 0;
    }, 0);
}

uint64_t RowlEngine_GetTextureCacheEvictionCount(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getTextureCacheEvictionCount() : 0;
    }, 0);
}

double RowlEngine_GetLastFrameTextureLoadMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getLastFrameTextureLoadMilliseconds() : 0.0;
    }, 0.0);
}

double RowlEngine_GetLastFrameNonTextureRenderMilliseconds(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0;
    return invokeNoexcept<double>([&] {
        const auto* window = toEngine(handle)->getWindow();
        return window ? window->getLastFrameNonTextureRenderMilliseconds() : 0.0;
    }, 0.0);
}

void RowlEngine_SetTextureCacheBudgetBytes(RowlEngineHandle handle, uint64_t bytes) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* window = toEngine(handle)->getWindow();
        if (window) window->setTextureCacheBudgetBytes(bytes);
    });
}

void RowlEngine_SetPlayState(RowlEngineHandle handle, int isPlaying) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setPlayState(isPlaying != 0); });
}

void RowlEngine_ResetToStartNode(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
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
    if (!isLiveHandle(handle)) return;
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
    if (!isLiveHandle(handle) || !componentsJson) return;
    invokeNoexcept([&] { toEngine(handle)->updateSceneFromComponents(componentsJson); });
}

void RowlEngine_LoadStoryGraph(RowlEngineHandle handle, const char* jsonPath) {
    if (!isLiveHandle(handle)) return;
    if (!jsonPath || !*jsonPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Story graph path is null or empty",
                                  "load_story_graph_path", "");
                }
            }
        });
        return;
    }
    // Engine'in path'i geçici olarak override et ve graph'i yükle
    invokeNoexcept([&] {
        if (auto* engine = toEngine(handle)) {
            engine->loadStoryGraphFromPath(jsonPath);
        }
    });
}

int RowlEngine_LoadStoryGraphFromVfs(RowlEngineHandle handle, const char* vfsPath) {
    if (!isLiveHandle(handle)) return 0;
    if (!vfsPath || !*vfsPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Story graph VFS path is null or empty",
                                  "load_story_graph_vfs", "");
                }
            }
        });
        return 0;
    }
    int loaded = 0;
    invokeNoexcept([&] {
        if (auto* engine = toEngine(handle)) {
            loaded = engine->loadStoryGraphFromVfs(vfsPath) ? 1 : 0;
        }
    });
    return loaded;
}

const char* RowlEngine_GetLastStoryGraphError(RowlEngineHandle handle) {
    static thread_local std::string buffer;
    buffer.clear();
    if (!isLiveHandle(handle)) return buffer.c_str();
    invokeNoexcept([&] { buffer = toEngine(handle)->getLastStoryGraphLoadError(); });
    return buffer.c_str();
}

void RowlEngine_SetProjectDirectory(RowlEngineHandle handle, const char* projectRoot) {
    if (!isLiveHandle(handle) || !projectRoot || !*projectRoot) return;
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        // Save slots belong to the selected game/project. This prevents an
        // embedded editor preview or another standalone game from sharing the
        // process-relative default "saves" directory.
        engine->setSaveDirectory((std::filesystem::path(projectRoot) / "saves").string());
        // Project-owned defaults are read at the mount boundary so player and
        // embedded editor preview resolve the same component contract.
        std::string transition = "instant";
        float transitionDuration = 1.0f;
        const auto manifestPath = std::filesystem::path(projectRoot) / "project.rowlproj";
        std::error_code manifestError;
        if (std::filesystem::is_regular_file(manifestPath, manifestError) && !manifestError &&
            std::filesystem::file_size(manifestPath, manifestError) <= 1024 * 1024 && !manifestError) {
            try {
                std::ifstream manifest(manifestPath);
                const auto json = nlohmann::json::parse(manifest);
                transition = json.value("default_bgm_transition", transition);
                transitionDuration = json.value("default_bgm_transition_duration_seconds", transitionDuration);
            } catch (...) { }
        }
        engine->setBgmTransitionDefaults(transition, transitionDuration);
        if (engine->getVfs()) {
            engine->getVfs()->remountProject(projectRoot);
        }
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
    });
}

void RowlEngine_SetBgmTransitionDefaults(RowlEngineHandle handle, const char* transition, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setBgmTransitionDefaults(transition ? transition : "instant", durationSeconds); });
}

void RowlEngine_AdvanceNode(RowlEngineHandle handle, uint32_t choiceIndex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->advanceToNextNode(choiceIndex); });
}

int RowlEngine_SelectChoice(RowlEngineHandle handle, const char* optionId) {
    if (!isLiveHandle(handle) || !optionId || !*optionId) return 0;
    return invokeNoexcept<int>([&] { return toEngine(handle)->advanceToChoice(optionId) ? 1 : 0; }, 0);
}

int RowlEngine_PointerDown(RowlEngineHandle handle, float x, float y) {
    if (!isLiveHandle(handle)) return 0;
    // Editor supplies 1920x1080 virtual coordinates; the engine's offscreen
    // surface is the same size, so the shared hit-test path remains canonical.
    return invokeNoexcept<int>([&] { return toEngine(handle)->handlePointerDown(x, y) ? 1 : 0; }, 0);
}

/* ── State queries ───────────────────────────────────────────────────────── */

const char* RowlEngine_GetSpeaker(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    // Returned pointer is valid until next step/update — owned by engine
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getActiveSpeaker();
        return buf.c_str();
    }, "");
}

const char* RowlEngine_GetDialogue(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getActiveDialogue();
        return buf.c_str();
    }, "");
}

uint64_t RowlEngine_GetCurrentNodeId(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] { return toEngine(handle)->getCurrentNodeId(); }, 0);
}

void RowlEngine_PlayAudio(RowlEngineHandle handle,
                          const char* assetPath,
                          int channelType,
                          int filterType) {
    if (!isLiveHandle(handle)) return;
    if (!assetPath || !*assetPath) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Audio asset path is null or empty",
                                  "play_audio", "");
                }
            }
        });
        return;
    }
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        if (!engine) return;
        auto* audio = engine->getAudio();
        if (!audio) return;
        auto channel = (channelType == 0) ? Rowl::Audio::AudioChannelType::Bgm :
                       (channelType == 1) ? Rowl::Audio::AudioChannelType::Voice :
                                            Rowl::Audio::AudioChannelType::Sfx;
        auto filter  = (filterType == 1)  ? Rowl::Audio::DSPFilterType::CaveReverb :
                       (filterType == 2)  ? Rowl::Audio::DSPFilterType::Telephone :
                       (filterType == 3)  ? Rowl::Audio::DSPFilterType::UnderwaterLowPass :
                                            Rowl::Audio::DSPFilterType::Normal;
        audio->playAudio(assetPath, channel, filter);
        if (!audio->getLastError().empty()) {
            if (auto ctx = engine->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::AudioDecodeError,
                              audio->getLastError(), "play_audio", assetPath);
            }
        }
    });
}

void RowlEngine_StopBgm(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (audio) audio->stopBgm();
    });
}

void RowlEngine_SetBgmVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (audio) audio->setBgmVolume(volume);
    });
}

void RowlEngine_SetMasterVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto* audio = toEngine(handle)->getAudio()) audio->setMasterVolume(volume); });
}

void RowlEngine_SetVoiceVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto* audio = toEngine(handle)->getAudio()) audio->setVoiceVolume(volume); });
}

void RowlEngine_SetSfxVolume(RowlEngineHandle handle, float volume) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto* audio = toEngine(handle)->getAudio()) audio->setSfxVolume(volume); });
}

void RowlEngine_SetTextSpeedMultiplier(RowlEngineHandle handle, float multiplier) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setTextSpeedMultiplier(multiplier); });
}

void RowlEngine_SetAutoAdvanceDelayOffset(RowlEngineHandle handle, float seconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { toEngine(handle)->setAutoAdvanceDelayOffset(seconds); });
}

float RowlEngine_GetMasterVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { const auto* audio = toEngine(handle)->getAudio(); return audio ? audio->getMasterVolume() : 0.0f; }, 0.0f);
}

float RowlEngine_GetVoiceVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { const auto* audio = toEngine(handle)->getAudio(); return audio ? audio->getVoiceVolume() : 0.0f; }, 0.0f);
}

float RowlEngine_GetSfxVolume(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0.0f;
    return invokeNoexcept<float>([&] { const auto* audio = toEngine(handle)->getAudio(); return audio ? audio->getSfxVolume() : 0.0f; }, 0.0f);
}

void RowlEngine_TriggerVoiceDucking(RowlEngineHandle handle, int isVoiceActive) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* audio = toEngine(handle)->getAudio();
        if (audio) audio->triggerVoiceDucking(isVoiceActive != 0);
    });
}

int RowlEngine_IsBgmPlaying(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isBgmPlaying() ? 1 : 0;
    }, 0);
}

int RowlEngine_IsVoicePlaying(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        return audio && audio->isVoicePlaying() ? 1 : 0;
    }, 0);
}

int RowlEngine_GetActiveDspFilter(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        if (!audio) return 0;
        switch (audio->getActiveFilter()) {
            case Rowl::Audio::DSPFilterType::CaveReverb: return 1;
            case Rowl::Audio::DSPFilterType::Telephone: return 2;
            case Rowl::Audio::DSPFilterType::UnderwaterLowPass: return 3;
            case Rowl::Audio::DSPFilterType::Normal: return 0;
        }
        return 0;
    }, 0);
}

const char* RowlEngine_GetLastAudioError(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        const auto* audio = toEngine(handle)->getAudio();
        buffer = audio ? audio->getLastError() : "";
        return buffer.c_str();
    }, "");
}

const char* RowlEngine_GetScriptRuntimeDiagnosticsJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "[]";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        nlohmann::json diagnostics = nlohmann::json::array();
        for (const auto& status : toEngine(handle)->getScriptRuntimeStatuses()) {
            diagnostics.push_back({
                {"module_id", status.moduleId},
                {"path", status.sourcePath},
                {"state", status.state},
                {"error", status.lastError},
            });
        }
        buffer = diagnostics.dump();
        return buffer.c_str();
    }, "[]");
}

const char* RowlEngine_GetDialogueHistoryJson(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return "[]";
    static thread_local std::string buffer;
    return invokeNoexcept<const char*>([&] {
        nlohmann::json history = nlohmann::json::array();
        for (const auto& entry : toEngine(handle)->getDialogueHistory()) {
            history.push_back({
                {"node_id", entry.nodeId}, {"speaker", entry.speaker},
                {"dialogue", entry.dialogue}, {"read", entry.read},
            });
        }
        buffer = history.dump();
        return buffer.c_str();
    }, "[]");
}

int RowlEngine_SaveGameSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->saveGameSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_LoadGameSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->loadGameSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_HasSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->hasSaveSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_DeleteSaveSlot(RowlEngineHandle handle, int32_t slotIndex) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->deleteSaveSlot(slotIndex) ? 1 : 0;
    }, 0);
}

int RowlEngine_Rewind(RowlEngineHandle handle, uint32_t steps) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->rewind(steps) ? 1 : 0;
    }, 0);
}

uint64_t RowlEngine_GetCurrentStepId(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<uint64_t>([&] {
        return toEngine(handle)->getCurrentStepId();
    }, 0);
}

void RowlEngine_SetVariable(RowlEngineHandle handle, const char* key, const char* value) {
    if (!isLiveHandle(handle) || !key || !value) return;
    invokeNoexcept([&] {
        toEngine(handle)->setScriptVariable(key, value);
    });
}

const char* RowlEngine_GetVariable(RowlEngineHandle handle, const char* key) {
    if (!isLiveHandle(handle) || !key) return "";
    static thread_local std::string buf;
    return invokeNoexcept<const char*>([&] {
        buf = toEngine(handle)->getScriptVariable(key);
        return buf.c_str();
    }, "");
}

int RowlEngine_EvaluateCondition(RowlEngineHandle handle, const char* conditionExpr) {
    if (!isLiveHandle(handle) || !conditionExpr) return 1;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->evaluateCondition(conditionExpr) ? 1 : 0;
    }, 1);
}

int RowlEngine_ExecuteScript(RowlEngineHandle handle, const char* scriptCode) {
    if (!isLiveHandle(handle)) return 0;
    if (!scriptCode) {
        invokeNoexcept([&] {
            if (auto* engine = toEngine(handle)) {
                if (auto ctx = engine->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::InvalidArgument,
                                  "Script code string pointer is null",
                                  "execute_script", "");
                }
            }
        });
        return 0;
    }
    return invokeNoexcept<int>([&] {
        auto* engine = toEngine(handle);
        return (engine && engine->executeScript(scriptCode)) ? 1 : 0;
    }, 0);
}

int32_t RowlEngine_GetLastResultCode(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::InvalidHandle);
    return invokeNoexcept<int32_t>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError);
        return engine->getContext()->getLastResult().rawCode();
    }, static_cast<int32_t>(Rowl::Core::RuntimeErrorCode::UnknownError));
}

const char* RowlEngine_GetLastResultOperation(RowlEngineHandle handle) {
    static thread_local std::string buf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        buf = "none";
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return "unknown";
        buf = engine->getContext()->getLastResult().operation;
        return buf.c_str();
    }, "unknown");
}

const char* RowlEngine_GetLastResultMessage(RowlEngineHandle handle) {
    static thread_local std::string buf;
    buf.clear();
    if (!isLiveHandle(handle)) {
        buf = "Invalid or uninitialized engine handle";
        return buf.c_str();
    }
    return invokeNoexcept<const char*>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return "Internal error occurred";
        buf = engine->getContext()->getLastResult().message;
        return buf.c_str();
    }, "Internal error occurred");
}

const char* RowlEngine_GetLastResultTarget(RowlEngineHandle handle) {
    static thread_local std::string buf;
    buf.clear();
    if (!isLiveHandle(handle)) return buf.c_str();
    return invokeNoexcept<const char*>([&] {
        auto* engine = toEngine(handle);
        if (!engine || !engine->getContext()) return "";
        buf = engine->getContext()->getLastResult().target;
        return buf.c_str();
    }, "");
}

void RowlEngine_ClearLastResult(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* engine = toEngine(handle);
        if (engine && engine->getContext()) {
            engine->getContext()->clearResult();
        }
    });
}

void RowlEngine_StartTransition(RowlEngineHandle handle, const char* kind, float durationSeconds, const char* colorHex) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        std::string k = kind ? kind : "crossfade";
        std::string c = colorHex ? colorHex : "";
        toEngine(handle)->startTransition(k, durationSeconds, c);
    });
}

int RowlEngine_IsTransitionActive(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        return toEngine(handle)->isTransitionActive() ? 1 : 0;
    }, 0);
}

void RowlEngine_SetCamera(RowlEngineHandle handle, float x, float y, float zoom) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->setPosition(x, y);
            cam->setZoom(zoom);
        }
    });
}

void RowlEngine_TriggerCameraShake(RowlEngineHandle handle, float intensity, float durationSeconds) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->shake(intensity, durationSeconds);
        }
    });
}

void RowlEngine_ResetCamera(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] {
        auto* cam = toEngine(handle)->getCamera();
        if (cam) {
            cam->reset();
        }
    });
}

} // extern "C"
