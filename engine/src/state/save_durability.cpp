#include "rowl/state/save_durability.hpp"

#include "rowl/core/logger.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

namespace Rowl::State {

namespace {

// FSYNC DECISION (see header): fsync is OFF in this slice — deliberate.
// The crash-mid-write guarantee is atomicity via temp-file + rename, which
// needs no flushing contract beyond the stream flush before rename: either
// the rename happened (new complete file) or it did not (old file intact).
// Forcing bytes to stable storage (POSIX fdatasync/fsync + dir fsync,
// Windows FlushFileBuffers) would only matter for OS/power loss, which is
// out of scope here. If that is ever required, add it here — inside this
// module — without touching the SessionPersistence save path.

std::atomic<bool> g_injectEnospc{false};

bool envEnospcRequested() {
#ifdef NDEBUG
    // The env-var trigger is a dev/test convenience only: in release builds
    // a stray ROWL_SAVE_INJECT_ENOSPC=1 in the process environment must never
    // break real saves. Tests use the explicit setter, which works in all
    // configurations.
    return false;
#else
    const char* value = std::getenv("ROWL_SAVE_INJECT_ENOSPC");
    return value != nullptr && std::strcmp(value, "1") == 0;
#endif
}

bool replaceFileAtomically(const std::filesystem::path& temporaryPath,
                           const std::filesystem::path& finalPath,
                           std::error_code& error) {
#if defined(_WIN32)
    if (MoveFileExW(temporaryPath.c_str(), finalPath.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
#else
    std::filesystem::rename(temporaryPath, finalPath, error);
    return !error;
#endif
}

} // namespace

std::filesystem::path saveTempPathFor(const std::filesystem::path& finalPath) {
    std::filesystem::path temporaryPath = finalPath;
    temporaryPath += ".tmp";
    return temporaryPath;
}

bool writeSlotFileAtomically(const std::filesystem::path& finalPath,
                             const std::string& content,
                             std::string* errorOut) {
    namespace fs = std::filesystem;
    auto fail = [&](const std::string& message) {
        if (errorOut != nullptr) *errorOut = message;
        ROWL_LOG_ERROR(message);
        return false;
    };

    const fs::path temporaryPath = saveTempPathFor(finalPath);

    // Test-only ENOSPC injection (production default off): simulate a
    // disk-full failure mid-write. A partial .tmp is staged so the failure
    // looks like a real interrupted write, then removed; the pre-existing
    // target file is never touched.
    if (g_injectEnospc.load(std::memory_order_relaxed) || envEnospcRequested()) {
        try {
            {
                std::ofstream partial(temporaryPath, std::ios::out | std::ios::trunc);
                if (partial.is_open()) {
                    partial << content.substr(0, content.size() / 2);
                    partial.flush();
                }
            }
            fs::remove(temporaryPath);
        } catch (...) {
        }
        return fail("No space left on device (injected ENOSPC) while writing " +
                    temporaryPath.string());
    }

    {
        std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            return fail("Failed to open save slot temp file for writing: " +
                        temporaryPath.string());
        }
        output << content;
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code removeError;
            fs::remove(temporaryPath, removeError);
            return fail("Failed to write complete save slot temp file: " +
                        temporaryPath.string());
        }
    }

    std::error_code replaceError;
    if (!replaceFileAtomically(temporaryPath, finalPath, replaceError)) {
        std::error_code removeError;
        fs::remove(temporaryPath, removeError);
        return fail("Failed to atomically replace save slot file: " +
                    replaceError.message());
    }
    return true;
}

void cleanupStraySlotTemp(const std::filesystem::path& finalPath) {
    try {
        std::error_code error;
        std::filesystem::remove(saveTempPathFor(finalPath), error);
    } catch (...) {
    }
}

void setSaveDurabilityInjectEnospc(bool inject) {
    g_injectEnospc.store(inject, std::memory_order_relaxed);
}

bool saveDurabilityInjectEnospc() {
    return g_injectEnospc.load(std::memory_order_relaxed) || envEnospcRequested();
}

} // namespace Rowl::State
