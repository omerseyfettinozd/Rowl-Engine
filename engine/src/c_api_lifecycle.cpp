/**
 * c_api_lifecycle.cpp
 *
 * C-API lifecycle: handle registry, create/destroy, init, step, shutdown.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(hidden)
#endif

// Handle-registry state: defined once here, declared in c_api_internal.hpp
// for the sibling units. Bodies moved verbatim from c_api.cpp.
std::mutex g_handleMutex;
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

Rowl::Core::Engine* toEngine(RowlEngineHandle h) {
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_liveHandles.find(h);
    if (it == g_liveHandles.end()) return nullptr;
    const auto* record = static_cast<const HandleRecord*>(h);
    return (record->ownerThread == std::thread::id{} ||
            record->ownerThread == std::this_thread::get_id()) ? it->second : nullptr;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif

namespace Rowl::Core {
// Test-only bridge (declared in tests/rowl_test_harness.hpp). Exposes the
// explicit Engine behind a C-API handle so tests observe exact ownership
// instead of the removed Engine::instance() process global. Production code
// must use handles, never this.
Engine* testEngineFromHandle(RowlEngineHandle handle) {
    return toEngine(handle);
}
} // namespace Rowl::Core

extern "C" {
/* ── Lifecycle ───────────────────────────────────────────────────────────── */

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

} // extern "C"
