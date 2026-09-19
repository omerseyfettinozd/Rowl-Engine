#include "rowl/platform/sdl_subsystem_lease.hpp"

#include <SDL3/SDL.h>

#include <mutex>
#include <thread>

namespace Rowl::Platform {
namespace {

// D3 (B1d #133/#134): the VIDEO subsystem additionally remembers WHICH
// thread acquired it first. SDL video init is only legal on one thread per
// process (on macOS/Cocoa, off-main init aborts the process), so a second
// thread must be refused BEFORE any SDL call — not after creating a window.
// The owner resets to "nobody" when the lease count reaches zero, so
// strictly sequential use from different threads (init, full shutdown,
// re-init elsewhere) keeps working; only CONCURRENT multi-thread video
// init fails closed. Audio keeps the plain refcount: the finding (and the
// Cocoa abort) is video-specific, and audio teardown runs on engine-owned
// threads the lease must not second-guess.
std::mutex g_sdlLeaseMutex;
uint32_t g_videoLeases = 0;
uint32_t g_audioLeases = 0;
std::thread::id g_videoOwner{};

bool acquireOne(uint32_t flag, uint32_t& leases) {
    if (leases == 0 && !SDL_InitSubSystem(flag)) return false;
    ++leases;
    return true;
}

void releaseOne(uint32_t flag, uint32_t& leases) noexcept {
    if (leases == 0) return;
    --leases;
    if (leases == 0) SDL_QuitSubSystem(flag);
}

} // namespace

bool SdlSubsystemLease::acquire(uint32_t flags) {
    std::lock_guard<std::mutex> lock(g_sdlLeaseMutex);
    const bool needsVideo = (flags & SDL_INIT_VIDEO) != 0;
    const bool needsAudio = (flags & SDL_INIT_AUDIO) != 0;
    if ((flags & ~(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) != 0) return false;

    // D3 (#133): another thread owns video right now — refuse with no SDL
    // call at all. The caller (Window init) turns this into a fail-closed
    // init failure, never a half-created native window.
    if (needsVideo && g_videoLeases > 0 &&
        g_videoOwner != std::this_thread::get_id()) {
        return false;
    }
    if (needsVideo && !acquireOne(SDL_INIT_VIDEO, g_videoLeases)) return false;
    if (g_videoLeases == 1 && needsVideo) g_videoOwner = std::this_thread::get_id();
    if (needsAudio && !acquireOne(SDL_INIT_AUDIO, g_audioLeases)) {
        if (needsVideo) {
            releaseOne(SDL_INIT_VIDEO, g_videoLeases);
            if (g_videoLeases == 0) g_videoOwner = std::thread::id{};
        }
        return false;
    }
    return true;
}

void SdlSubsystemLease::release(uint32_t flags) noexcept {
    std::lock_guard<std::mutex> lock(g_sdlLeaseMutex);
    if ((flags & SDL_INIT_AUDIO) != 0) releaseOne(SDL_INIT_AUDIO, g_audioLeases);
    // D3 (#102/#151): release is INTENTIONALLY not owner-gated. A handle
    // reclaimed after its owner thread died tears its leases down on the
    // RECLAIMING thread; gating release would leak those leases forever.
    // Misuse is contained one layer up: only the Shutdown/Destroy path of a
    // live handle reaches this function, and that path is handle-gated.
    if ((flags & SDL_INIT_VIDEO) != 0) {
        releaseOne(SDL_INIT_VIDEO, g_videoLeases);
        if (g_videoLeases == 0) g_videoOwner = std::thread::id{};
    }
}

} // namespace Rowl::Platform
