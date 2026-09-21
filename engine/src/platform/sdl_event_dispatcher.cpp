#include "rowl/platform/sdl_event_dispatcher.hpp"

#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>

namespace Rowl::Platform {
namespace {

std::mutex g_eventMutex;
std::optional<std::thread::id> g_eventThread;
std::unordered_map<uint32_t, std::deque<SDL_Event>> g_windowEvents;
std::deque<SDL_Event> g_globalEvents;

bool isGlobalEvent(uint32_t type) {
    switch (type) {
        case SDL_EVENT_AUDIO_DEVICE_ADDED:
        case SDL_EVENT_AUDIO_DEVICE_REMOVED:
        case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
        // #164: RENDER_* cihaz olaylari cihaza aittir, pencereye degil —
        // ses-cihazi olaylari gibi global kuyruga duser. Eskiden
        // targetWindowId'de eslesmeyip routeEvent:79'da sessiz dusuyorlardi;
        // global olduklarindan beri Engine::step tuketir. (Offscreen
        // bagisikligi ele almadan degil ulastirilamazliktandi; gorunur/
        // embedded hedef bu yolla rebuild + sinyal alir.)
        case SDL_EVENT_RENDER_TARGETS_RESET:
        case SDL_EVENT_RENDER_DEVICE_RESET:
        case SDL_EVENT_RENDER_DEVICE_LOST:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            return true;
        default:
            return false;
    }
}

std::optional<uint32_t> targetWindowId(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return event.key.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event.button.windowID;
        case SDL_EVENT_MOUSE_MOTION:
            return event.motion.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return event.wheel.windowID;
        case SDL_EVENT_TEXT_INPUT:
            return event.text.windowID;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_CANCELED:
            return event.tfinger.windowID;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return event.window.windowID;
        default:
            return std::nullopt;
    }
}

bool isDispatchThreadLockedless(const std::optional<std::thread::id>& pin) {
    return pin && *pin == std::this_thread::get_id();
}

bool isDispatchThread() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    return isDispatchThreadLockedless(g_eventThread);
}

void routeEvent(const SDL_Event& event) {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    if (event.type == SDL_EVENT_QUIT) {
        for (auto& [_, queue] : g_windowEvents) queue.push_back(event);
        return;
    }
    if (isGlobalEvent(event.type)) {
        g_globalEvents.push_back(event);
        return;
    }
    const auto target = targetWindowId(event);
    if (!target || *target == 0) return;
    if (const auto it = g_windowEvents.find(*target); it != g_windowEvents.end()) {
        it->second.push_back(event);
    }
}

void pumpEvents() {
    if (!isDispatchThread()) return;
    SDL_Event event;
    while (SDL_PollEvent(&event)) routeEvent(event);
}

} // namespace

bool SdlEventDispatcher::registerWindow(uint32_t windowId) {
    if (windowId == 0) return false;
    std::lock_guard<std::mutex> lock(g_eventMutex);
    const auto currentThread = std::this_thread::get_id();
    if (!g_eventThread) g_eventThread = currentThread;
    if (*g_eventThread != currentThread || g_windowEvents.contains(windowId)) return false;
    g_windowEvents.emplace(windowId, std::deque<SDL_Event>{});
    return true;
}

void SdlEventDispatcher::unregisterWindow(uint32_t windowId) noexcept {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    g_windowEvents.erase(windowId);
    if (g_windowEvents.empty()) g_eventThread.reset();
}

void SdlEventDispatcher::pumpOnly() {
    // #75: offscreen runtimes never register a window, so the pin is still
    // unclaimed on their step thread and pumpEvents() below would fail
    // closed without touching SDL_PollEvent. Claim-if-unclaimed mirrors
    // registerWindow's first-claim rule; a foreign-owned pin stays a no-op.
    {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        if (!g_eventThread) g_eventThread = std::this_thread::get_id();
    }
    pumpEvents();
}

std::vector<SDL_Event> SdlEventDispatcher::takeEvents(uint32_t windowId) {
    if (!isDispatchThread()) return {};
    pumpEvents();
    std::lock_guard<std::mutex> lock(g_eventMutex);
    const auto it = g_windowEvents.find(windowId);
    if (it == g_windowEvents.end()) return {};
    std::vector<SDL_Event> events;
    events.reserve(it->second.size());
    while (!it->second.empty()) {
        events.push_back(it->second.front());
        it->second.pop_front();
    }
    return events;
}

std::vector<SDL_Event> SdlEventDispatcher::takeGlobalEvents() {
    if (!isDispatchThread()) return {};
    pumpEvents();
    std::lock_guard<std::mutex> lock(g_eventMutex);
    std::vector<SDL_Event> events;
    events.reserve(g_globalEvents.size());
    while (!g_globalEvents.empty()) {
        events.push_back(g_globalEvents.front());
        g_globalEvents.pop_front();
    }
    return events;
}

// D3 (B1d #115/#128/#151/#155): the dispatch pin outlives its thread when a
// visible owner dies without Shutdown — every later register/pump/take on
// the surviving threads fails closed against the dead id and the process
// can never init or pump again. These three predicates/transfers are the
// recovery surface; the table itself is never touched by them.
bool SdlEventDispatcher::isDispatchThread() noexcept {
    try {
        return ::Rowl::Platform::isDispatchThread();
    } catch (...) {
        return false;
    }
}

bool SdlEventDispatcher::isEligibleForRegister() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        // Unclaimed pin: this thread WILL claim it inside registerWindow.
        // Claimed by us: re-entry is fine (duplicate-id still rejected there).
        // Claimed by anyone else: creating a native window first would abort
        // on macOS/Cocoa (#153) — the caller must fail BEFORE touching SDL.
        return !g_eventThread || isDispatchThreadLockedless(g_eventThread);
    } catch (...) {
        return false;
    }
}

void SdlEventDispatcher::stealDispatchThread() noexcept {
    try {
        std::lock_guard<std::mutex> lock(g_eventMutex);
        // Administrative transfer only (RowlEngine_ReclaimHandle): the table
        // is kept — the dead owner's window registration is still valid, its
        // events keep routing, only the pumping thread changes. Multi-handle
        // caveat, documented on ReclaimHandle: a second live visible handle
        // on yet another thread must Shutdown/Init to re-register after the
        // reclaimed handle is torn down (the pin clears when the table
        // empties, so a full Shutdown→Init cycle always recovers).
        g_eventThread = std::this_thread::get_id();
    } catch (...) {
    }
}

} // namespace Rowl::Platform
