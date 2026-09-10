#include "rowl/platform/sdl_subsystem_lease.hpp"

#include <SDL3/SDL.h>

#include <mutex>

namespace Rowl::Platform {
namespace {

std::mutex g_sdlLeaseMutex;
uint32_t g_videoLeases = 0;
uint32_t g_audioLeases = 0;

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

    if (needsVideo && !acquireOne(SDL_INIT_VIDEO, g_videoLeases)) return false;
    if (needsAudio && !acquireOne(SDL_INIT_AUDIO, g_audioLeases)) {
        if (needsVideo) releaseOne(SDL_INIT_VIDEO, g_videoLeases);
        return false;
    }
    return true;
}

void SdlSubsystemLease::release(uint32_t flags) noexcept {
    std::lock_guard<std::mutex> lock(g_sdlLeaseMutex);
    if ((flags & SDL_INIT_AUDIO) != 0) releaseOne(SDL_INIT_AUDIO, g_audioLeases);
    if ((flags & SDL_INIT_VIDEO) != 0) releaseOne(SDL_INIT_VIDEO, g_videoLeases);
}

} // namespace Rowl::Platform
