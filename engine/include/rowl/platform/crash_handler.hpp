#pragma once

// crash_handler.hpp — crash-log facility for rowl_player (silent-degrade:
// if the log directory cannot be created the install is skipped and the
// player runs exactly as before).
//
// POSIX behaviour:
//   Installs handlers for SIGSEGV, SIGABRT, SIGFPE and SIGILL plus a
//   std::terminate hook. Each handler emits one log file under the
//   configured directory and then re-raises the signal with the default
//   disposition, so the process still exits abnormally.
//
// Handler-safety contract:
//   The crash path uses only async-signal-safe calls (open, write, close,
//   getpid, raise, signal, _exit, plus the sigemptyset/sigaddset/sigprocmask
//   mask helpers). It never touches the heap, stdio, or the
//   engine C API. All data written by the handler is captured ahead of time:
//   the argv echo is copied at install time, and the engine-result snapshot
//   is copied at explicit safe-point refreshes via RowlCrash_RefreshSnapshot.
//   The snapshot may lag the exact crash instant; the log says so.
//
// Windows behaviour:
//   Fail-closed stub: install is a no-op returning false. See
//   docs/CRASH_LOG_CONTRACT.md for the platform matrix.

#include <cstdint>

#include "rowl/c_api.h"  // ROWL_API export macro only; C ABI untouched.

namespace Rowl::Platform {

// Install crash handlers writing logs into |logDir|.
// Call before argument parsing so every later crash is covered.
// |argc|/|argv| are echoed into each log (truncated to a fixed bound).
// Returns true while handlers are live, false where unavailable.
// A false result is silent: the normal player flow is never disturbed.
ROWL_API bool RowlCrash_Install(const char* logDir, int argc, char* argv[]);

// Remove handlers installed by RowlCrash_Install. Safe to call at any time,
// including when install was never called or reported false.
ROWL_API void RowlCrash_Uninstall();

// True while crash handlers are live.
ROWL_API bool RowlCrash_IsInstalled();

// Refresh the engine-result snapshot echoed by the crash path.
// Safe-point only: call from normal control flow (never from inside a
// handler), e.g. after engine init and after story-graph load. Values are
// copied into fixed-size static storage and truncated when overlong.
// Passing null pointers stores empty strings.
ROWL_API void RowlCrash_RefreshSnapshot(int32_t resultCode, const char* operation, const char* target);

}  // namespace Rowl::Platform
