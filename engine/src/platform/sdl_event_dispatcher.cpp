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

std::optional<uint32_t> targetWindowId(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return event.key.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return event.button.windowID;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            return event.window.windowID;
        default:
            return std::nullopt;
    }
}

bool isDispatchThread() {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    return g_eventThread && *g_eventThread == std::this_thread::get_id();
}

void routeEvent(const SDL_Event& event) {
    std::lock_guard<std::mutex> lock(g_eventMutex);
    if (event.type == SDL_EVENT_QUIT) {
        for (auto& [_, queue] : g_windowEvents) queue.push_back(event);
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

} // namespace Rowl::Platform
