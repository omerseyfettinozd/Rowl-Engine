/**
 * sdl_video_serial.cpp — D3 (B1d #138).
 *
 * One process-wide non-recursive mutex behind an RAII guard. New TU on
 * purpose (engine.cpp / window.cpp growth ban): lifecycle transitions
 * include the header and instantiate the guard; no logic lives anywhere
 * else. See the header for the lock-order contract.
 */

#include "rowl/platform/sdl_video_serial.hpp"

#include <mutex>

namespace Rowl::Platform {

namespace {
std::mutex& serialMutex() {
    static std::mutex mutex;
    return mutex;
}
} // namespace

VideoSerialGuard::VideoSerialGuard() {
    serialMutex().lock();
}

VideoSerialGuard::~VideoSerialGuard() {
    serialMutex().unlock();
}

} // namespace Rowl::Platform
