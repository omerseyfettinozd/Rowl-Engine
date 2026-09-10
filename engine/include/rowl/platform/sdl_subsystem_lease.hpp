#pragma once

#include <cstdint>

namespace Rowl::Platform {

/// Process-wide SDL subsystem ownership. SDL itself is process-global, while
/// Rowl runtimes are not: each Window/AudioEngine holds a lease for exactly
/// the subsystem it uses so one runtime cannot tear down another's SDL state.
class SdlSubsystemLease {
public:
    static bool acquire(uint32_t flags);
    static void release(uint32_t flags) noexcept;
};

} // namespace Rowl::Platform
