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

    /// Pumps SDL once on the dispatch thread and returns only this window's events.
    static std::vector<SDL_Event> takeEvents(uint32_t windowId);

    /// Pumps SDL once on the dispatch thread and returns process-wide events
    /// that belong to no window (audio-device add/remove/format changes and
    /// window minimize/maximize/restore). Window-targeted events and QUIT are
    /// never included here; QUIT keeps its existing per-window fan-out.
    static std::vector<SDL_Event> takeGlobalEvents();
};

} // namespace Rowl::Platform
