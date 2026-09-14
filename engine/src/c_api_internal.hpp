/**
 * c_api_internal.hpp
 *
 * Shared internals for the split C-API translation units (c_api_lifecycle,
 * c_api_story, c_api_render, c_api_audio, c_api_state). Source-local only:
 * never installed, never included from public headers. The public contract
 * is rowl/c_api.h alone.
 *
 * The handle-registry state is DEFINED once in c_api_lifecycle.cpp and seen
 * here through declarations. Hidden visibility keeps the split from adding
 * dynamic symbols; the noexcept guard discipline below is identical in every
 * unit. Moved verbatim from the former monolithic c_api.cpp.
 */
#pragma once

#include "rowl/c_api.h"
#include "rowl/core/engine.hpp"

#include <exception>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility push(hidden)
#endif

// The opaque C handle is a stable record, not the Engine allocation itself.
// Destroyed records are intentionally retained until process exit so an old
// host callback can never become valid again if malloc reuses an Engine address.
struct HandleRecord {
    std::unique_ptr<Rowl::Core::Engine> engine;
    std::thread::id ownerThread;
};

extern std::mutex g_handleMutex;
extern std::unordered_map<RowlEngineHandle, Rowl::Core::Engine*> g_liveHandles;
extern std::vector<std::unique_ptr<HandleRecord>> g_handleRecords;

bool isLiveHandle(RowlEngineHandle handle) noexcept;
bool claimHandleThread(RowlEngineHandle handle) noexcept;
std::unique_ptr<Rowl::Core::Engine> takeLiveHandle(RowlEngineHandle handle) noexcept;
Rowl::Core::Engine* toEngine(RowlEngineHandle h);

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC visibility pop
#endif

// A C++ exception crossing this ABI boundary is undefined behaviour and can
// terminate the .NET host. Every exported entry point must degrade safely.
template <typename Return, typename Fn>
static Return invokeNoexcept(Fn&& operation, Return fallback) noexcept {
    try {
        return std::forward<Fn>(operation)();
    } catch (...) {
        return fallback;
    }
}

template <typename Fn>
static void invokeNoexcept(Fn&& operation) noexcept {
    try {
        std::forward<Fn>(operation)();
    } catch (...) {
        // No logging here: logging itself must not become another exception path.
    }
}

/// Shared contract for every new variable-size UTF-8 C API output. Required
/// size includes NUL; NULL/0 queries size; an undersized buffer is cleared.
inline RowlEngine_ResultCode copyUtf8ToCaller(
    std::string_view value, char* buffer, uint32_t bufferSize,
    uint32_t* outRequiredSize) noexcept {
    if (!outRequiredSize || (!buffer && bufferSize != 0)) {
        return ROWL_RESULT_INVALID_ARGUMENT;
    }
    if (value.size() >= std::numeric_limits<uint32_t>::max()) {
        *outRequiredSize = 0;
        return ROWL_RESULT_UNKNOWN_ERROR;
    }

    const auto required = static_cast<uint32_t>(value.size() + 1);
    *outRequiredSize = required;
    if (!buffer) return ROWL_RESULT_OK;
    if (bufferSize < required) {
        if (bufferSize > 0) buffer[0] = '\0';
        return ROWL_RESULT_BUFFER_TOO_SMALL;
    }

    if (!value.empty()) std::memcpy(buffer, value.data(), value.size());
    buffer[value.size()] = '\0';
    return ROWL_RESULT_OK;
}
