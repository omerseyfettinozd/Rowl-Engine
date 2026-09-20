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
// D3 (B1d #106): the Engine is SHARED-owned, not unique-owned. Readers copy
// the shared_ptr under the registry lock and use the copy AFTER unlocking,
// so a concurrent Destroy can erase the map entry but can never free an
// Engine an in-flight call is still using (use-after-destroy → safe
// lifetime extension; the destructor simply runs on the last holder).
struct HandleRecord {
    std::shared_ptr<Rowl::Core::Engine> engine;
    std::thread::id ownerThread;
};

extern std::mutex g_handleMutex;
extern std::unordered_map<RowlEngineHandle, Rowl::Core::Engine*> g_liveHandles;
extern std::vector<std::unique_ptr<HandleRecord>> g_handleRecords;

// D3 (B1d #150/#157): liveness and affinity are DIFFERENT answers. Old code
// folded both into isLiveHandle/toEngineChecked-null, so aux maps treated a
// foreign-thread call on a live handle exactly like a dead handle — and
// ERASED live state. Classify first: erase only on Dead, stamp WrongThread
// (no state touched) on Foreign.
enum class HandleStanding {
    Dead,    // unknown handle, or destroyed (not in the live map)
    Foreign, // live, but owned by another thread
    Mine,    // live and owned by the caller (or unclaimed: any thread may claim)
};

HandleStanding classifyHandle(RowlEngineHandle handle) noexcept;
bool isLiveHandle(RowlEngineHandle handle) noexcept;
bool claimHandleThread(RowlEngineHandle handle) noexcept;
// D4 (#49): tek-kilitli claim-or-reject — classify + claim aynı kilit altında,
// araya claim sızamaz (ayrı-ayrı çağrıdaki TOCTOU INVALID_HANDLE üretirdi).
// Dead → Dead; Foreign → Foreign (claim yok); sahipsiz/sahipli-Mine → claim +
// Mine. Save/Load/Step/Rewind ailesi bunu kullanır.
HandleStanding claimHandleOrClassify(RowlEngineHandle handle) noexcept;
bool unclaimHandleThread(RowlEngineHandle handle) noexcept;
std::shared_ptr<Rowl::Core::Engine> takeLiveHandle(RowlEngineHandle handle) noexcept;
Rowl::Core::Engine* toEngine(RowlEngineHandle h);
// D3 (B1d #106): shared copy under the lock; null on dead/foreign.
std::shared_ptr<Rowl::Core::Engine> toEngineChecked(RowlEngineHandle h) noexcept;
// Ownership-free copy: live map hit regardless of owner thread. For the
// WrongThread stamping path and cross-thread last-result reads only —
// holding this does NOT grant calling rights, it only keeps the Engine
// (and its context) alive while the rejection is recorded/read.
std::shared_ptr<Rowl::Core::Engine> copyEngineAnyThread(RowlEngineHandle h) noexcept;
// D3 (B1d #102): records a loud WrongThread rejection on the engine's own
// (mutex-guarded) context AND logs it. Callable cross-thread precisely
// because it only touches shared ownership + the context lock.
void stampWrongThread(RowlEngineHandle handle, const char* op) noexcept;
// D2 (#140): TU-local aux-map'lerin Destroy-yolu temizliği. Tanımlar
// c_api_prefetch_chapters.cpp / c_api_character_layers.cpp'de (anonim
// namespace dışında, external linkage); çağrı c_api_lifecycle.cpp'de.
void clearPrefetchStatesForHandle(RowlEngineHandle handle) noexcept;
void clearCharacterStatesForHandle(RowlEngineHandle handle) noexcept;
// A4-tur1: tek-kilitli canlı+thread kontrollü erişim — isLiveHandle +
// toEngine çift-bakışındaki TOCTOU penceresini kapatır (arada destroy
// edilirse ikinci bakış null döner, deref UB olurdu). Fail-closed: ölü/
// yabancı-thread handle'da nullptr. noexcept; kilit içerde alınır,
// iç içe g_handleMutex alınmaz.

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

/// R1 (#6): WithLength varyantları kopyalanan baytı raporlar, strlen'i
/// değil. Düz getter tampona yazmışsa (dönen pointer tamponundur) boyut
/// tamponun kendisinden alınır, gömülü NUL korunur; literal fallback
/// (ölü-handle ""/"[]"/"none"...) dönmüşse NUL içermez, strlen güvenlidir.
/// copyUtf8ToCaller ile aynı UINT32 korkuluğu. noexcept.
inline uint32_t withLengthOf(const std::string& buf, const char* value) noexcept {
    size_t n;
    if (value != nullptr && value == buf.data())
        n = buf.size();
    else
        n = std::strlen(value != nullptr ? value : "");
    if (n >= std::numeric_limits<uint32_t>::max())
        n = std::numeric_limits<uint32_t>::max();
    return static_cast<uint32_t>(n);
}

/// D1 (B1b #108-#132): pre-init fail-loud disiplini. Init-öncesi çağrılan
/// giriş davranışını korur (void'ler no-op ya da son-geçerliyi yazar,
/// getter'lar mevcut fallback'u döner) ama last-result kanalına StateError
/// işler; disiplinsiz caller GetLastResultCode'dan ayırt eder. noexcept;
/// engine null ise sessiz false (handle guard'ı çağırandadır).
inline bool requireEngineInitialized(Rowl::Core::Engine* engine, const char* op) noexcept {
    if (engine != nullptr && engine->isInitialized()) return true;
    if (engine != nullptr) {
        if (Rowl::Core::RuntimeContext* ctx = engine->getContext()) {
            ctx->setError(Rowl::Core::RuntimeErrorCode::StateError,
                          "Engine is not initialized; call RowlEngine_Init first",
                          op ? op : "", "");
        }
    }
    return false;
}

// D3 (B1d #106): shared-ownership overload — toEngineChecked artık
// shared_ptr döndürdüğünden onlarca çağrı noktası değişmeden derlenir;
// ham-pointer çağrılar eski imzaya gitmeye devam eder.
inline bool requireEngineInitialized(const std::shared_ptr<Rowl::Core::Engine>& engine,
                                     const char* op) noexcept {
    return requireEngineInitialized(engine.get(), op);
}
