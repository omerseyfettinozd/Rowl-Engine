#pragma once

#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>

namespace Rowl::State {

/// Save durability: crash-safe atomic slot writes (Faz 4.5 Dilim 2).
///
/// Protocol: serialize -> write OWNED UNIQUE temp file
/// (<slot>.json.tmp.<pid>.<counter>[.rand]) -> stage pre-save backup of the
/// existing slot (490: "<slot>.json.pre-save-bak") -> rename over the target
/// (on rename failure the target is restored from the backup).
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
///      A normal (non-crash) failure leaks at most ONE backup, and only in the
///      single case where the 490 restore was attempted and itself failed:
///      that backup is then deliberately kept as recovery evidence. Every other
///      exit removes its own temp, its partial backup, and its staged backup.
///   L1b concurrent writers: COVERED for the in-process case by the commit
///      mutex, and for a transient share conflict by the bounded retry
///      (see SaveDurabilityTransientSite). Two writers in one process are
///      fully serialised in the backup+replace section; a cross-process
///      conflict is retried within a short budget, and if it still fails the
///      save fails closed rather than writing a partial slot.
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

/// 490: pre-save backup name ("<slot>.json.pre-save-bak", same directory).
/// writeSlotFileAtomically copies the pre-existing slot here BEFORE the
/// rename; on success the backup is removed (best effort), on rename failure
/// the target is restored from it (best effort) and the backup is removed
/// unless the restore itself failed. A crash between staging and removal
/// leaves at most one backup per INTERRUPTED WRITE; because the name carries
/// the owner's pid, residue is bounded by crash count (not by save count) and
/// is swept with that owner's other temps.
/// NOTE: the backup is named from the caller's OWNED UNIQUE temp, so its real
/// name is "<slot>.json.tmp.<pid>.<counter>.<rand>.pre-save-bak" and
/// concurrent writers never share it. That name DOES match the
/// cleanupStaleOwnedSlotTemps pattern, so a backup whose owner died is swept
/// like any other stale owned temp — the desired outcome, since a live owner's
/// backup is never touched. Never throws.
std::filesystem::path saveBackupPathFor(const std::filesystem::path& finalPath);

/// Atomically replaces finalPath with content. Returns true on success;
/// on failure returns false, sets *errorOut (when non-null), removes the
/// owned unique tmp (best effort), restores any pre-existing finalPath from
/// the pre-save backup (best effort), and leaves any pre-existing finalPath
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

// ---------------------------------------------------------------------------
// Transient sharing-race hook (Windows CI save-slot concurrency fix).
//
// A concurrent writer on Windows transiently refuses the target's DELETE
// access (ERROR_ACCESS_DENIED) or our own source read (ERROR_SHARING_VIOLATION);
// those are not failures, they are the queue. writeSlotFileAtomically
// classifies them and retries within a small bounded budget. That retry loop
// is otherwise unobservable off Windows, so this hook arms a chosen retry site
// to report a transient error WITHOUT performing the syscall — the code under
// test still classifies, backs off, and decides success or fail-closed. The
// sentinel is ERROR_SHARING_VIOLATION on Windows and EAGAIN on POSIX.
//
// The arming is PER SITE, and that is load-bearing rather than decorative: with
// one shared queue the probe would drain it and the backup-copy and restore
// paths could never be reached by a test at all. Per-site counters are what
// let a test drive the backup-staging failure and the 490 restore end to end.
//
// P2-9 — "per site" MUST mean one site == one operation, and it did not.
// The three retry sites of the commit region (:363 backup probe, :537 owned
// temp removal, :660 baseline fingerprint re-measurement) ALL passed the same
// `Probe` label, so one enum value stood behind three different operations.
// A test could arm a budget and could not tell which operation spent it, and
// the consumption order across concurrent writers was schedule-dependent (the
// commit mutex serialises writers, so which writer drains the budget depends
// only on who grabs the mutex next). Each label now names exactly one
// operation; see tests/test_p2_9_transient_site_isolation.cpp, which fails if
// any two of them collapse back onto one counter.
// NOT a data race: the counters are std::atomic<int> and the decrement is a
// compare_exchange_weak CAS loop (see save_durability.cpp), and ThreadSanitizer
// is clean on them. The defect was the SHARED MEANING, not the read/write.
//
// HONEST SCOPE — what this hook gates and what it does not:
//   gates: the retry loop shape, the backoff, the bounded budget, the
//          per-site failure accounting, the classifier's acceptance of the
//          sentinel, and (through those) the fail-closed and restore results.
//   does NOT gate: the Windows-specific arms of the classifier (codes 33/303,
//          and the "5 is transient for rename but permanent for copy" rule).
//          Reaching those needs a real Win32 error, and NO test on any platform
//          produces one - not even the Windows CI leg. They rest on documented
//          Win32 semantics, not on a test. Saying otherwise would be an
//          overclaim, so it is said here instead.
//
// Production default is 0 for every site; nothing arms it except this setter.
// Never throws.
//
// P2-9 DENETIMI (BULGU 1): `Count` ELLE 6 yazilmaz, SON etiketten TURETILIR.
//
// Denetim, onceki surumdeki `Count = 6` + `static_assert(Count == (int)Count)`
// kombinasyonunun TOTOLOJI oldugunu kanitladi: sol zaten sagdan turuyor,
// derleyici katlayip siliyor, `Count` 6->5 mutasyonu DERLENIP kapiyi YESIL
// birakti. Bu, base'deki gercek `== 4` sabit karsilastirmasinin korumasini
// sessizce dusuruyordu (GERILEME).
//
// Simdi `Count` bir ifade olarak yaziliyor, bu yuzden onu elle kismak ya da
// elle buyutmek mumkun degil: yeni site eklemek ZATEN bu satiri degistirmeyi
// gerektirir. Karsilastirmayi yapan asil kapilar bir sonraki bloktaki liste
// ve dizi boyutu assert'leridir; onlar bu turetilmis sayidan BAGIMSIZ
// kaynaklardan gelir, yani ne totoloji ne de "Count'i elle kis" yolu.
enum class SaveDurabilityTransientSite : int {
    Probe = 0,           // "is the existing slot a regular file?" backup probe
    Copy = 1,            // 490 pre-save backup copy
    Replace = 2,         // atomic rename over the target
    BackupRemove = 3,    // best-effort removal of the staged backup
    OwnedTempRemove = 4, // best-effort removal of the OWNED UNIQUE temp
    FingerprintMeasure = 5, // baseline fingerprint re-measurement
    Count,               // == FingerprintMeasure + 1 (TURETILIR, P2-9)
};

// P2-9 DENETIMI (BULGU 2): "her site'i gez" demeyen kapilar kendi listesini
// elle yazarak kapsamlarini daraltiyordu — 7. site eklenince kapı onu
// KAPSAMAZDI, yani düzeltilen kusurun ta kendisi. Burada TEK bir liste
// tanımlanır ve iki kapı da onu kullanır; kapsam artık iki yerde kopyalanmaz.
//
// Bu liste ELLE yazılmıştır ve DERLEME ZAMANINDA enum'a bağlanır. Neden
// otomatik türetmek (0..Count-1 dönüşü) değil: otomatik türetme yeni site'i
// sessizce kapsama ALIR ve kapı yeşil kalır — denetimin istediği "kapsam dışı
// site eklenince kapı KIRMIZI olsun" davranışını bu vermez. Elle liste +
// static_assert ikilisi ise yeni site eklenip burası güncellenmedikçe
// DERLEME HATASI üretir, yani kapı kırmızıya düner (sessizce skip yok).
inline constexpr SaveDurabilityTransientSite kAllSaveDurabilityTransientSites[] =
    {
        SaveDurabilityTransientSite::Probe,
        SaveDurabilityTransientSite::Copy,
        SaveDurabilityTransientSite::Replace,
        SaveDurabilityTransientSite::BackupRemove,
        SaveDurabilityTransientSite::OwnedTempRemove,
        SaveDurabilityTransientSite::FingerprintMeasure,
    };

static_assert(std::size(kAllSaveDurabilityTransientSites) ==
                  static_cast<std::size_t>(
                      SaveDurabilityTransientSite::Count),
              "kAllSaveDurabilityTransientSites, SaveDurabilityTransientSite "
              "enum'unun TUM sitelerini icermeli; yeni site eklendi ve bu "
              "liste guncellenmedi (kapi sessizce daralirdi)");

// Listeye yeni site EKLENMEDEN, enum'a eklenmis olabilir: liste Count kadar
// uzun oldugu icin "sadece uzunluk" buna bakmaz. Son gercek etiket listenin
// SON elemani olmali; boylece "enum'a 7. site eklendi, kapinin listesi
// guncellenmedi" durumu DERLEME HATASI verir.
static_assert(
    kAllSaveDurabilityTransientSites[
        std::size(kAllSaveDurabilityTransientSites) - 1] ==
        SaveDurabilityTransientSite::FingerprintMeasure,
    "enum'a FingerprintMeasure'den SONRA yeni bir site eklendi ama "
    "kAllSaveDurabilityTransientSites guncellenmedi; kapı o site'i KAPSAMAZ");

// Ayni seyin ayna karsi yuzu: listenin ilk elemani enum'un ilk etiketi
// olmali. Ilk etiket degistirilip Count elle kismis olsaydi (artik mumkun
// degil) burasi da tutmazdi.
static_assert(kAllSaveDurabilityTransientSites[0] ==
                  SaveDurabilityTransientSite::Probe,
              "kAllSaveDurabilityTransientSites ilk elemani enum'un ilk "
              "etiketiyle ayni olmali");

void setSaveDurabilityInjectTransientFailures(int count,
                                              SaveDurabilityTransientSite site);
int saveDurabilityInjectTransientFailures(SaveDurabilityTransientSite site);

// Transient failures the retry loop has actually absorbed at one site.
// Monotonic; read it before and after to get a delta.
int saveDurabilityTransientFailuresConsumed(SaveDurabilityTransientSite site);

// The bounded-retry attempt budget (1 initial attempt + N-1 retries), so a
// test sizes its injections from the implementation's own constant instead of
// a hard-coded duplicate.
int saveDurabilityTransientRetryAttempts();

// Test-only: how many times two writers have been inside the commit section at
// the same time. This is the ONLY observable trace of the commit mutex — the
// Windows sharing violation it exists to prevent cannot be produced on Linux —
// so without it, DELETING the mutex would leave every test green. With the
// mutex present the count is necessarily zero, so this can never report a
// false positive; it can only fail to catch a removed mutex on an unlucky
// scheduling. Monotonic; reset it and read the delta.
int saveDurabilityCommitOverlaps();
int saveDurabilityCommitOverlapsReset();

// Test-only: simulate a competing CROSS-PROCESS writer winning the race. When
// armed, writeSlotFileAtomically overwrites the target with a known marker
// AFTER staging the pre-save backup and BEFORE the atomic replace — the
// in-process mutex already prevents a thread from doing this, so without this
// seam the fingerprint guard's whole purpose ("a stale writer must not restore
// its backup over a newer winner's payload") could never be observed by a
// test, on any platform. Pair it with an armed Replace injection to drive the
// save into the failure path and watch the marker survive.
void setSaveDurabilityInjectCompetingWrite(bool inject);

// The exact bytes saveDurabilityInjectCompetingWrite writes.
const char* saveDurabilityCompetingWriteMarker();

// True while a competing-write injection is armed.
bool saveDurabilityInjectCompetingWrite();

} // namespace Rowl::State
