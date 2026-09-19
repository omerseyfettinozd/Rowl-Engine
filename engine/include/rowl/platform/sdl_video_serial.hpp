#pragma once

namespace Rowl::Platform {

/// D3 (B1d #138): process-wide serial for SDL video lifetime calls.
/// SDL's video subsystem is process-global, but Rowl runs one Engine per
/// handle on whatever thread the host chose — so concurrent Init/Shutdown/
/// Destroy on different owner threads used to interleave SDL video calls
/// with no common lock (per-handle owner pins only order one handle).
///
/// A VideoSerialGuard is held across the whole RowlEngine_Init/
/// InitStandalone/Shutdown/Destroy/ReclaimHandle body, so two threads can
/// never run SDL video lifetime transitions at once. Deliberately NOT held
/// by Run/Step: Run is a blocking loop (holding the serial would deadlock
/// a concurrent Shutdown that needs it to request the quit), and per-frame
/// stepping needs no lifetime serialization.
///
/// Lock order (documented once, checked in review): VideoSerial may nest
/// OUTSIDE the handle-registry mutex and the subsystem-lease mutex —
/// VideoSerial > handle > lease. The event-dispatcher mutex is never taken
/// under any other lock (dispatch queries are always lock-release-lock
/// leaf calls). Reversing this order is a deadlock; keep it that way.
class VideoSerialGuard {
public:
    VideoSerialGuard();
    ~VideoSerialGuard();

    VideoSerialGuard(const VideoSerialGuard&) = delete;
    VideoSerialGuard& operator=(const VideoSerialGuard&) = delete;
};

} // namespace Rowl::Platform
