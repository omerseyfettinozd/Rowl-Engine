#include "rowl/state/save_durability.hpp"

#include "rowl/core/logger.hpp"
#include "rowl/platform/user_data_directories.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
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

std::atomic<int> g_injectErrno{0};

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

bool isSupportedInjectErrno(int code) {
    return code == ENOSPC || code == EACCES || code == EROFS;
}

const char* errnoShortName(int code) {
    switch (code) {
        case ENOSPC: return "ENOSPC";
        case EACCES: return "EACCES";
        case EROFS: return "EROFS";
        default: return nullptr;
    }
}

// "[<NAME> (<code>): <strerror>] " prefix; unknown codes render as
// "[ERRNO<code> (<code>): <strerror>] " so the numeric value is never lost.
std::string errnoPrefix(int code) {
    const char* name = errnoShortName(code);
    const char* description = std::strerror(code);
    std::string prefix = "[";
    if (name != nullptr) {
        prefix += name;
    } else {
        prefix += "ERRNO" + std::to_string(code);
    }
    prefix += " (" + std::to_string(code) + "): ";
    prefix += (description != nullptr ? description : "unknown error");
    prefix += "] ";
    return prefix;
}

// Effective injected errno: explicit setter wins, env-var trigger degrades
// to ENOSPC (legacy behavior).
int effectiveInjectErrno() {
    const int code = g_injectErrno.load(std::memory_order_relaxed);
    if (code != 0) return code;
    if (envEnospcRequested()) return ENOSPC;
    return 0;
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

    // Test-only errno injection (production default off): simulate a
    // mid-write filesystem failure. A partial .tmp is staged so the failure
    // looks like a real interrupted write, then removed; the pre-existing
    // target file is never touched.
    if (const int injectedErrno = effectiveInjectErrno(); injectedErrno != 0) {
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
        // The ENOSPC sentence is kept verbatim as a prefix (legacy message
        // compatibility); the errno tag is appended for UI/telemetry.
        if (injectedErrno == ENOSPC) {
            return fail("No space left on device (injected ENOSPC) while writing " +
                        Rowl::Platform::pathToUtf8(temporaryPath) + " " + errnoPrefix(ENOSPC));
        }
        if (injectedErrno == EACCES) {
            return fail("Permission denied (injected EACCES) while writing " +
                        Rowl::Platform::pathToUtf8(temporaryPath) + " " + errnoPrefix(EACCES));
        }
        return fail("Read-only file system (injected EROFS) while writing " +
                    Rowl::Platform::pathToUtf8(temporaryPath) + " " + errnoPrefix(EROFS));
    }

    {
        std::ofstream output(temporaryPath, std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            // iostream does not guarantee errno on open failure: a stale 0
            // would render a misleading "[ERRNO0 (0): Success]" tag, so fall
            // back to the generic message when no errno was captured.
            const int openErrno = errno;
            if (openErrno == 0) {
                return fail("Failed to open save slot temp file for writing: " +
                            Rowl::Platform::pathToUtf8(temporaryPath));
            }
            return fail(errnoPrefix(openErrno) +
                        "Failed to open save slot temp file for writing: " +
                        Rowl::Platform::pathToUtf8(temporaryPath) + ": " + std::strerror(openErrno));
        }
        output << content;
        output.flush();
        if (!output.good()) {
            output.close();
            std::error_code removeError;
            fs::remove(temporaryPath, removeError);
            return fail("Failed to write complete save slot temp file: " +
                        Rowl::Platform::pathToUtf8(temporaryPath));
        }
    }

    std::error_code replaceError;
    if (!replaceFileAtomically(temporaryPath, finalPath, replaceError)) {
        std::error_code removeError;
        fs::remove(temporaryPath, removeError);
        return fail(errnoPrefix(replaceError.value()) +
                    "Failed to atomically replace save slot file: " +
                    replaceError.message() + " (" + Rowl::Platform::pathToUtf8(temporaryPath) +
                    " -> " + Rowl::Platform::pathToUtf8(finalPath) + ")");
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

void setSaveDurabilityInjectErrno(int errnoValue) {
    if (errnoValue == 0) {
        g_injectErrno.store(0, std::memory_order_relaxed);
        return;
    }
    // Documented choice: unsupported codes fail closed as ENOSPC.
    g_injectErrno.store(isSupportedInjectErrno(errnoValue) ? errnoValue : ENOSPC,
                        std::memory_order_relaxed);
}

int saveDurabilityInjectErrno() {
    return effectiveInjectErrno();
}

void setSaveDurabilityInjectEnospc(bool inject) {
    setSaveDurabilityInjectErrno(inject ? ENOSPC : 0);
}

bool saveDurabilityInjectEnospc() {
    return effectiveInjectErrno() == ENOSPC;
}

} // namespace Rowl::State
