#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <vector>

namespace Rowl::Platform {

/// Routes SDL's process-wide event queue to its owning visible runtime.
/// All registered windows must be created and stepped from one UI/event thread.
class SdlEventDispatcher {
public:
    static bool registerWindow(uint32_t windowId);
    static void unregisterWindow(uint32_t windowId) noexcept;

    // D3 (B1d #115/#128/#151/#153/#155): dispatch-pin recovery surface.
    // The pin is process-wide and first-registrant-wins; when the owning
    // thread dies without Shutdown the pin points at a dead id and every
    // register/pump/take fails closed forever. isDispatchThread reports
    // whether the caller owns the pin right now (unclaimed pin: false);
    // isEligibleForRegister reports whether the caller may proceed to
    // CREATE a native window (unclaimed or self-owned: true — creating
    // first and checking later aborts on macOS/Cocoa, #153); stealDispatch-
    // Thread administratively transfers the pin to the caller, keeping the
    // window table (called only by RowlEngine_ReclaimHandle). All three are
    // noexcept and take the event mutex leaf-style (never nested).
    static bool isDispatchThread() noexcept;
    static bool isEligibleForRegister() noexcept;
    static void stealDispatchThread() noexcept;

    /// Pumps SDL once on the dispatch thread and returns only this window's events.
    static std::vector<SDL_Event> takeEvents(uint32_t windowId);

    /// Claims the dispatch pin when unclaimed, then pumps SDL once.
    /// Offscreen runtimes register no window, so their pollEvents path never
    /// touches the queue and the pin stays unclaimed — a later takeGlobal-
    /// Events would no-op without reaching SDL_PollEvent (#75). Calling this
    /// first makes the global drain work there. On a foreign-owned pin this
    /// is a fail-closed no-op; on the owning thread it is just a pump.
    static void pumpOnly();

    /// Pumps SDL once on the dispatch thread and returns process-wide events
    /// that belong to no window (audio-device add/remove/format changes and
    /// window minimize/maximize/restore). Window-targeted events and QUIT are
    /// never included here; QUIT keeps its existing per-window fan-out.
    static std::vector<SDL_Event> takeGlobalEvents();
};

} // namespace Rowl::Platform
