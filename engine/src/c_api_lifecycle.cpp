/**
 * c_api_lifecycle.cpp
 *
 * C-API lifecycle: handle registry, create/destroy, init, step, shutdown.
 * Split from c_api.cpp; bodies are unchanged. The public contract
 * is rowl/c_api.h only — see c_api_internal.hpp for shared guards.
 */

#include "c_api_internal.hpp"
#include "rowl/core/logger.hpp"
#include "rowl/platform/sdl_event_dispatcher.hpp"
#include "rowl/platform/sdl_video_serial.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(hidden)
#endif

// Handle-registry state: defined once here, declared in c_api_internal.hpp
// for the sibling units. Bodies moved verbatim from c_api.cpp.
std::mutex g_handleMutex;
std::unordered_map<RowlEngineHandle, Rowl::Core::Engine*> g_liveHandles;
std::vector<std::unique_ptr<HandleRecord>> g_handleRecords;

// D3 (B1d #150/#157): single locked classification. Liveness (map hit) and
// affinity (owner match) are answered together so callers can no longer
// conflate "foreign thread" with "dead handle".
HandleStanding classifyHandle(RowlEngineHandle handle) noexcept {
    if (!handle) return HandleStanding::Dead;
    std::lock_guard<std::mutex> lock(g_handleMutex);
    if (g_liveHandles.find(handle) == g_liveHandles.end()) return HandleStanding::Dead;
    const auto* record = static_cast<const HandleRecord*>(handle);
    return (record->ownerThread == std::thread::id{} ||
            record->ownerThread == std::this_thread::get_id())
               ? HandleStanding::Mine
               : HandleStanding::Foreign;
}

bool isLiveHandle(RowlEngineHandle handle) noexcept {
    return classifyHandle(handle) == HandleStanding::Mine;
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

// D3 (B1d #151): administrative unclaim. Only RowlEngine_ReclaimHandle calls
// this (which additionally steals the dispatch pin under the video serial).
bool unclaimHandleThread(RowlEngineHandle handle) noexcept {
    if (!handle) return false;
    std::lock_guard<std::mutex> lock(g_handleMutex);
    if (g_liveHandles.find(handle) == g_liveHandles.end()) return false;
    auto* record = static_cast<HandleRecord*>(handle);
    record->ownerThread = std::thread::id{};
    return true;
}

std::shared_ptr<Rowl::Core::Engine> takeLiveHandle(RowlEngineHandle handle) noexcept {
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

// A4-tur1: toEngine ile aynı tek-kilit; null-handle açıkça reddedilir
// (map'te bulunamaz ama cast-öncesi erken-çıkış niyeti belgeler).
// D3 (B1d #106): raw pointer YOK — kilit altında paylaşılan kopya. Arayan,
// kilit bırakıldıktan sonra bu kopyayı kullanır; araya giren Destroy map
// girdisini silse de Engine'i free edemez (son tutan bırakana dek yaşar).
std::shared_ptr<Rowl::Core::Engine> toEngineChecked(RowlEngineHandle h) noexcept {
    if (!h) return nullptr;
    std::lock_guard<std::mutex> lock(g_handleMutex);
    const auto it = g_liveHandles.find(h);
    if (it == g_liveHandles.end()) return nullptr;
    const auto* record = static_cast<const HandleRecord*>(h);
    if (record->ownerThread != std::thread::id{} &&
        record->ownerThread != std::this_thread::get_id()) {
        return nullptr;
    }
    return record->engine;
}

std::shared_ptr<Rowl::Core::Engine> copyEngineAnyThread(RowlEngineHandle h) noexcept {
    if (!h) return nullptr;
    std::lock_guard<std::mutex> lock(g_handleMutex);
    if (g_liveHandles.find(h) == g_liveHandles.end()) return nullptr;
    const auto* record = static_cast<const HandleRecord*>(h);
    return record->engine;
}

void stampWrongThread(RowlEngineHandle handle, const char* op) noexcept {
    try {
        const std::string operation = (op && *op) ? op : "unknown";
        // Shared ownership first: the engine cannot die mid-stamp even if
        // the owner thread destroys it concurrently (#106-class).
        const auto engine = copyEngineAnyThread(handle);
        if (engine) {
            if (Rowl::Core::RuntimeContext* ctx = engine->getContext()) {
                ctx->setError(Rowl::Core::RuntimeErrorCode::WrongThread,
                              "call from non-owner thread; route it to the handle owner thread "
                              "(or RowlEngine_ReclaimHandle after the owner thread exited)",
                              operation, "");
            }
        }
        ROWL_LOG_ERROR(std::string("RowlEngine_") + operation +
                       ": rejected call from non-owner thread (WRONG_THREAD)");
    } catch (...) {
    }
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
        record->engine = std::make_shared<Rowl::Core::Engine>();
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
    // D3 (B1d #138): teardown runs under the process-wide video serial —
    // no SDL video call here can interleave a concurrent Init/Shutdown.
    Rowl::Platform::VideoSerialGuard serial;
    // D3 (B1d #102): foreign-thread Destroy stamps WRONG_THREAD instead of
    // silently no-op'ing (and leaks nothing: the record stays retained).
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "destroy");
        return;
    }
    auto engine = takeLiveHandle(handle);
    if (!engine) return;
    // D2 (#140): per-handle aux-map'ler (prefetch + character) meşru
    // destroy yolunda hiç silinmiyordu — her Create/kullan/Destroy
    // döngüsü kalıcı ChapterLoader+queue+string bırakıyordu.
    clearPrefetchStatesForHandle(handle);
    clearCharacterStatesForHandle(handle);
    // D3 (#106): shared ownership — in-flight readers holding their own
    // copy keep the Engine alive; destruction lands on the last holder.
    invokeNoexcept([&] { engine.reset(); });
}

// D3 (B1d #151): ownership recovery after owner-thread death. Serial first
// (lock order VideoSerial > handle), then unclaim + dispatch-pin steal.
RowlEngine_ResultCode RowlEngine_ReclaimHandle(RowlEngineHandle handle) {
    Rowl::Platform::VideoSerialGuard serial;
    {
        std::lock_guard<std::mutex> lock(g_handleMutex);
        const auto it = g_liveHandles.find(handle);
        if (it == g_liveHandles.end()) return ROWL_RESULT_INVALID_HANDLE;
        auto* record = static_cast<HandleRecord*>(handle);
        // Unconditional administrative transfer: the caller asserts the
        // previous owner thread has exited. A still-running previous owner
        // is NOT silently hijacked — its calls become Foreign and stamp
        // WRONG_THREAD loudly (fail-loud, never split-brain-silent).
        record->ownerThread = std::this_thread::get_id();
    }
    // The dead owner may hold the process-wide dispatch pin (#115/#128);
    // move it to the reclaiming thread, keeping the window table so live
    // registrations and their event routing survive the transfer.
    Rowl::Platform::SdlEventDispatcher::stealDispatchThread();
    if (const auto engine = copyEngineAnyThread(handle)) {
        if (Rowl::Core::RuntimeContext* ctx = engine->getContext()) {
            ctx->setSuccess("reclaim", "");
        }
    }
    return ROWL_RESULT_OK;
}

int RowlEngine_Init(RowlEngineHandle handle,
                     uint32_t virtualWidth,
                     uint32_t virtualHeight,
                     int vsync) {
    // D3 (#138): Init runs under the video serial — concurrent
    // Init/Shutdown/Destroy on other threads cannot interleave SDL calls.
    Rowl::Platform::VideoSerialGuard serial;
    // D3 (B1d #102): live bir handle'daki yabancı claim reddi damgalanır;
    // ölü handle sessizce 0 döner (fail-closed, log-spam yok).
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "init");
        return 0;
    }
    if (!claimHandleThread(handle)) return 0;

    return invokeNoexcept<int>([&] {
        Rowl::Core::EngineConfig cfg;
        cfg.appName       = "Rowl Engine";
        cfg.virtualWidth  = virtualWidth;
        cfg.virtualHeight = virtualHeight;
        cfg.vsync         = (vsync != 0);
        cfg.isIpcMode     = false; // IPC artık yok — tek süreç
        auto checked = toEngineChecked(handle);
        return (checked && checked->initialize(cfg)) ? 1 : 0;
    }, 0);
}

int RowlEngine_InitStandalone(RowlEngineHandle handle,
                               const char* appTitle,
                               uint32_t virtualWidth,
                               uint32_t virtualHeight,
                               int vsync) {
    Rowl::Platform::VideoSerialGuard serial;
    // D3 (B1d #102): yabancı claim reddi damgalanır (Init ile aynı kural).
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "init");
        return 0;
    }
    if (!claimHandleThread(handle)) return 0;

    return invokeNoexcept<int>([&] {
        Rowl::Core::EngineConfig cfg;
        cfg.appName          = (appTitle && appTitle[0] != '\0') ? appTitle : "Rowl Game";
        cfg.virtualWidth     = virtualWidth > 0 ? virtualWidth : 1920;
        cfg.virtualHeight    = virtualHeight > 0 ? virtualHeight : 1080;
        cfg.vsync            = (vsync != 0);
        cfg.standaloneWindow = true;
        auto checked = toEngineChecked(handle);
        return (checked && checked->initialize(cfg)) ? 1 : 0;
    }, 0);
}

void RowlEngine_Run(RowlEngineHandle handle) {
    // No video serial here by design: Run blocks until quit, so holding the
    // serial would deadlock a concurrent Shutdown waiting for it. Liveness
    // + shared ownership is the whole guard (D3 #106).
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto checked = toEngineChecked(handle)) checked->run(); });
}

void RowlEngine_Step(RowlEngineHandle handle, float deltaTime) {
    if (!isLiveHandle(handle)) {
        // D3 (#102): foreign Step stamps WRONG_THREAD instead of vanishing.
        if (classifyHandle(handle) == HandleStanding::Foreign) {
            stampWrongThread(handle, "step");
        }
        return;
    }
    invokeNoexcept([&] {
        auto checked = toEngineChecked(handle);
        if (!checked) return;
        // D3 (B1d #155): a VISIBLE engine stepped off the dispatch thread
        // advances nothing. takeEvents already gates intake off-thread; an
        // ungated step would accrue playtime and complete typewriter lines
        // on eventless frames. Offscreen engines never touch the pin and
        // are unaffected. Initialized-visible ⟺ registered (a failed
        // visible init leaves the engine uninitialized), so no new Window
        // accessor was needed for the gate.
        if (checked->isInitialized()) {
            const auto* window = checked->getWindow();
            if (window != nullptr && !window->isOffscreen() &&
                !Rowl::Platform::SdlEventDispatcher::isDispatchThread()) {
                if (Rowl::Core::RuntimeContext* ctx = checked->getContext()) {
                    ctx->setError(Rowl::Core::RuntimeErrorCode::WrongThread,
                                  "visible engine stepped off the SDL event-dispatch thread; "
                                  "step from the dispatch owner thread",
                                  "step", "");
                }
                ROWL_LOG_ERROR("RowlEngine_Step: rejected step of a visible engine "
                               "from a non-dispatch thread (WRONG_THREAD)");
                return;
            }
        }
        checked->step(deltaTime);
    });
}

void RowlEngine_Shutdown(RowlEngineHandle handle) {
    Rowl::Platform::VideoSerialGuard serial;
    // D3 (B1d #102): foreign-thread Shutdown stamps WRONG_THREAD instead
    // of silently no-op'ing while leaking every lease it never released.
    if (classifyHandle(handle) == HandleStanding::Foreign) {
        stampWrongThread(handle, "shutdown");
        return;
    }
    if (!isLiveHandle(handle)) return;
    invokeNoexcept([&] { if (auto checked = toEngineChecked(handle)) checked->shutdown(); });
}

int RowlEngine_IsRunning(RowlEngineHandle handle) {
    if (!isLiveHandle(handle)) return 0;
    return invokeNoexcept<int>([&] {
        auto checked = toEngineChecked(handle);
        return (checked && checked->isRunning()) ? 1 : 0;
    }, 0);
}

} // extern "C"
