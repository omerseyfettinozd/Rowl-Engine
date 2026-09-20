#pragma once

#include <filesystem>
#include <string>

namespace Rowl::State {

/// Save durability: crash-safe atomic slot writes (Faz 4.5 Dilim 2).
///
/// Protocol: serialize -> write OWNED UNIQUE temp file
/// (<slot>.json.tmp.<pid>.<counter>[.rand]) -> rename over the target.
/// A crash mid-write can only leave a stray unique tmp; the previous
/// good slot file is never truncated in place, so load always sees either
/// the old or the new complete payload — never a half slot. Concurrent
/// writers never share a tmp name, so the rename winner is always one
/// complete payload (R1 #3; the legacy shared "<slot>.json.tmp" name
/// below survives only as the stray-cleanup anchor).
///
/// FSYNC DECISION: fsync is OFF in this slice (documented, deliberate).
/// Rationale: the write path uses std::ofstream for portability across
/// Linux/Windows/macOS and the atomicity guarantee comes from the rename
/// (POSIX rename / Windows MoveFileEx WRITE_THROUGH for metadata), not from
/// forcing payload bytes to stable storage. That covers process-crash
/// mid-write (the slice goal). Power-loss / OS-crash durability (file-data
/// fdatasync + directory fsync on POSIX) is explicitly out of scope and can
/// be added later inside writeSlotFileAtomically without touching callers.
/// Legacy shared temp name ("<slot>.json.tmp"): no longer used for writing
/// (R1 #3 — writers mint owned unique tmps), kept as the stray-cleanup
/// anchor for interrupted writes from older builds. Never throws.
std::filesystem::path saveTempPathFor(const std::filesystem::path& finalPath);

/// Atomically replaces finalPath with content. Returns true on success;
/// on failure returns false, sets *errorOut (when non-null), removes the
/// owned unique tmp (best effort), and leaves any pre-existing finalPath
/// byte-identical. Never throws.
bool writeSlotFileAtomically(const std::filesystem::path& finalPath,
                             const std::string& content,
                             std::string* errorOut = nullptr);

/// Best-effort removal of a stray "<slot>.json.tmp" left by an interrupted
/// write. Called on the load path so a half-tmp beside a good slot is
/// ignored and cleaned. Never throws.
void cleanupStraySlotTemp(const std::filesystem::path& finalPath);

// Test-only ENOSPC (disk-full) injection hook. Production default is OFF:
// injection is active only after setSaveDurabilityInjectEnospc(true) or when
// the ROWL_SAVE_INJECT_ENOSPC environment variable is set to "1" at call
// time (env-var trigger is compiled out under NDEBUG so a stray variable in
// a shipped environment can never break saves; the setter works everywhere).
// When active, writeSlotFileAtomically fails closed mid-write with an
// ENOSPC error and preserves any pre-existing target file.
void setSaveDurabilityInjectEnospc(bool inject);
bool saveDurabilityInjectEnospc();

// Test-only errno-parametric injection hook (Faz 6 Dilim 7, IS 1/2).
// Generalizes the ENOSPC hook above: 0 disables injection; ENOSPC, EACCES
// and EROFS are supported failure codes. Any other non-zero value is
// normalized to ENOSPC (documented choice: fail closed as disk-full rather
// than silently accepting an unknown probe). The active code is staged as a
// mid-write failure exactly like the ENOSPC hook and reported in *errorOut
// with a "[<NAME> (<code>): <strerror>] ..." prefix so UI/telemetry can
// distinguish EACCES/EROFS from ENOSPC.
// setSaveDurabilityInjectEnospc(true) is a thin wrapper over
// setSaveDurabilityInjectErrno(ENOSPC) and preserves the ENOSPC message
// verbatim as a prefix; setSaveDurabilityInjectEnospc(false) clears any
// active errno injection.
void setSaveDurabilityInjectErrno(int errnoValue);
int saveDurabilityInjectErrno();

} // namespace Rowl::State
