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
///
/// D09 expansion — guarantee levels (what holds today, what does not):
///   L1 process-crash mid-write: COVERED. Temp-file + rename means the slot
///      file is either the old or the new complete payload; a torn slot is
///      impossible (R1 #3: owned unique tmps make the rename winner always
///      one complete payload, even under concurrent writers).
///   L2 crash residue: BOUNDED. A crash leaks at most one tmp per interrupted
///      save (legacy shared name or one owned unique tmp). The load/delete
///      paths sweep the legacy stray plus dead-owner owned tmps
///      (cleanupStaleOwnedSlotTemps); live-writer tmps are never touched.
///      Residue is therefore capped by crash count, not by save count, so a
///      crash loop cannot fill the disk and push later saves into ENOSPC.
///   L3 OS-crash / power loss: NOT COVERED (deliberate). Closing this gap
///      would require, inside writeSlotFileAtomically only: POSIX file
///      fdatasync/fsync before rename plus a directory fsync after rename
///      (rename itself must be durable), Windows FlushFileBuffers on the
///      temp handle before MoveFileEx (which already passes WRITE_THROUGH
///      for metadata). No caller changes, no format change. There is no
///      fault-injection for power loss in this slice — only process-crash
///      (kill -9 style interruption, staging a partial tmp) is exercised,
///      via the errno-injection hook and the tmp-orphan probe.
///
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

/// D09: best-effort sweep of stale OWNED UNIQUE tmps
/// ("<slot>.json.tmp.<pid>.<counter>.<rand...>") whose owner process is dead.
/// Called from cleanupStraySlotTemp, so the load/delete paths that already
/// call it gain the sweep with no caller changes. A tmp whose owner is alive
/// (or undecidable) is never touched, nor is any name that does not match
/// the minted pattern, nor symlinks — only provably-dead regular files go.
/// Never throws.
void cleanupStaleOwnedSlotTemps(const std::filesystem::path& finalPath);

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
