# Rowl Engine Data Formats & Migration Surface (1.0)

Canonical reference for every versioned format the 1.0 line reads and
writes, and for how older data migrates. Sources of truth are the
implementations named in each section; when this document and code
disagree, code wins and this document must be patched.

Conventions: JSON files are UTF-8, LF line endings (enforced by
`.gitattributes` for `*.rowlproj`/`*.json`). Readers tolerate missing
optional fields and ignore unknown fields unless stated otherwise.

## 1. Story graph (`full_story_graph.json`) — writer v4, readers v1–v4

- **Writer:** `StoryGraphSerializer.SerializeFullStoryGraph` emits
  `format_version = 4`, `start_node_id`, and `nodes[]`.
- **v4 node:** `id`, `title`, `objects[]` (Unity GameObject style: each
  object has `id`, `name`, `is_active`, and `components[]` with `type`,
  `id`, `enabled`, `data`), plus `next_nodes[]` edges of
  `{ id, label, option_id }`. `option_id` is the stable branch identity
  consumed by `RowlEngine_SelectChoice`.
- **Editor loading** (`StoryGraphLoaderService.Load`) is structural, not
  version-gated, and migrates upward:
  | Present shape | Treated as | Migration |
  |---|---|---|
  | `objects[]` per node | v3/v4 | used directly; unknown component types warn and skip |
  | `components[]` directly under node | v2 | each component is wrapped into its own object |
  | flat fields only (`speaker`, `background_x`, …) | v1 | `PopulateLegacyFields` builds objects/components |
  | `next_nodes[]` edges | v2+ | `{ id, label, option_id }` (`option_id` optional) |
  | scalar `next_id` | v1 | single unlabeled edge |
- **Native loading** (`StoryGraphParser::parse`) accepts the same union:
  flat visual fields, `components[]`, `objects[]` (inactive objects and
  typeless legacy components are skipped), and `next_nodes[]` or legacy
  `next_id`. Hardening limits apply (`kMaxStoryNodes`,
  `kMaxComponentsPerScene`, `kMaxEdgesPerStoryNode`,
  `kMaxStoryJsonBytes`); dangling edges, duplicate/zero node IDs, and a
  dangling `start_node_id` are rejected. An absent or dangling start falls
  back to the smallest node ID.
- **Runtime push payload** (`active_story.json`,
  `SerializeActiveStory`) is a separate, stable `format_version = 2`
  envelope (`node_id`, `components[]`, plus flat proxy fields) sent to
  `RowlEngine_UpdateSceneFromJson`. It is not the graph file and does not
  migrate.

## 2. Save slots (`save_slot_<n>.json`) — v3, v1/v2 migrate

- **Current version:** `GameState::CurrentSaveFormatVersion = 3`.
  `GameState::decodeJson` accepts versions 1–3; anything else yields
  `GameStateDecodeStatus::UnsupportedVersion`. Versions 1–2 decode with
  status `Migrated` (and `sourceVersion` set); version 3 yields `Loaded`.
  Corrupt data yields `InvalidData`, never a half-state.
- **Slot files:** `save_slot_<n>.json`, `n = 0..99` (slot 0 is quicksave),
  under the save directory (`saves/` by default,
  `PlatformHost::writableSavePath()` on hosted platforms).
- **Atomicity:** `SessionPersistence::saveSlot` writes a `.tmp` sibling
  and atomically renames over the final path; readers never observe a
  half-written slot.
- **Load reporting:** `SessionPersistence::loadSlotDetailed` returns
  `SessionLoadResult` (`Loaded`, `Migrated`, `NotFound`, `FileTooLarge`,
  `IoError`, `InvalidData`, `UnsupportedVersion`) with `sourceVersion`.
  The engine logs `Migrated save format version <v> to version 3`.
- **History:** states form an immutable chain (`previousState`);
  `SessionPersistence::checkpoint` aligns the chain with the live cursor
  before a save, and `rewind` steps back over it (surfaced as
  `RowlEngine_Rewind`).

## 3. Asset package (`.rowlpkg`) — v1 + embedded manifest

- **Layout (unchanged v1):** 18-byte header (`ROWL`, `uint16 version = 1`,
  `uint32 fileCount`, `uint64 indexOffset`), contiguous payload blobs,
  then the index table of `RowlPkgEntryRaw` (`uint64` FNV-1a path hash,
  `uint32` path length, `uint64` offset, `uint64` compressed size,
  `uint64` uncompressed size, `uint32` flags `0 = raw / 1 = zstd`,
  UTF-8 path bytes). The path hash is advisory; the canonical path is
  the lookup key and duplicates are rejected.
- **Determinism contract** (`tools/package_assets.py`): entries are
  processed in canonical byte-wise rel-path order, the archive carries no
  timestamps, compression uses fixed settings, and publishing is atomic
  (temp file + rename). Packing the same tree twice yields byte-identical
  output (covered by `rowl_package_determinism_tests`).
- **Pack-time validation** fails fast (exit 2, structured
  `[Packer][ERROR][code]` lines, no output published): `missing-input-dir`,
  `dangling-symlink`, `symlink-outside-root`, `zero-byte`, `unreadable`,
  `reserved-path`. `*.rowlpkg`/`*.tmp`/`*.gitkeep` are skipped, not packed.
- **Embedded manifest:** every package contains an uncompressed
  `rowl/manifest.json` entry (canonical JSON: `format = 1`, `files[]` of
  `{ path, size, sha256, compressed_size, flags }` over uncompressed
  bytes, sorted by path). `tools/verify_release_package.py` requires the
  manifest and cross-checks it against the index (order, path set, sizes;
  SHA-256 re-hash for raw entries).
- **Release gate:** a valid standalone release carries exactly one
  package at `Assets/packages/game.rowlpkg` (no loose `Assets/` content),
  `mods/` overrides, launchers, the native runtime, `README.txt`, and
  `THIRD_PARTY_NOTICES.md`.

## 4. Project manifest (`project.rowlproj`) — informational, unversioned

- Written by `ProjectFactory` (`name`, `version`, `engineVersion`,
  `createdAt`/`savedAt`, `nodeCount`, `startNodeId`, `save_slot_count`,
  `default_bgm_transition*`); older files may carry `virtualResolution`
  instead. There is no schema-version gate.
- Role: presence marks a project root (registry/discovery, Save-As,
  standalone build copies it into the release). Readers treat it as
  informational and tolerate missing/extra fields.

## 5. C API / P-Invoke boundary (`engine/include/rowl/c_api.h`)

- Sole public contract: `extern "C"`, opaque `RowlEngineHandle`
  (`void*`), UTF-8 `const char*` strings, no C++ types or exceptions
  across the boundary. Exported via `ROWL_API` (`__declspec` on Windows,
  default visibility on GCC/Clang; the library builds with
  `-fvisibility=hidden`).
- **Layout:** five translation units sharing `c_api_internal.hpp` —
  `c_api_lifecycle` (create/destroy/init/step/shutdown/run),
  `c_api_story` (scene/story/graph/project), `c_api_render`
  (viewport/pixel-buffer/texture-cache/camera/transitions/fx),
  `c_api_audio` (channels/volumes/blips/telemetry),
  `c_api_state` (save slots/rewind/variables/scripts/diagnostics).
  The C# editor binds the subset it uses (`NativeBridge`, ~80 entry
  points) via P/Invoke.
- **Safety:** every entry point runs inside `invokeNoexcept` guards, so a
  C++ exception degrades to a fallback return and can never cross into
  the .NET host. Handles are registry records (not raw `Engine`
  pointers): destroyed records are retained until process exit so a stale
  handle can never validate again; the first-`Init` thread owns the
  handle and foreign-thread calls are rejected. `RowlEngine_Destroy` is
  idempotent; `Shutdown` leaves the handle valid-but-unusable.
- **Diagnostics:** the last-operation record (`GetLastResultCode` 0–11/99
  + operation/message/target strings) and per-domain last-error strings
  are engine-owned and valid until the next call on the same thread.

## 6. Platform host boundary (`PlatformHost`)

Six capabilities, and only these, differ by host — everything else stays
inside `Engine`:

1. `openAssetStream(path)` — read a project asset;
2. `writableSavePath()` — save-slot directory;
3. `lifecycleState()` — Active / Suspended / Stopping;
4. `takeInputEvents()` — platform-neutral actions (Advance, QuickSave,
   QuickLoad, Rewind, PointerDown, SwipeForward/Back);
5. `renderSurface()` — Automatic / Offscreen / Native + dimensions;
6. `audioFocus()` — Granted / Lost.

`DefaultPlatformHost` is the behavior-preserving desktop adapter (VFS
assets, default policy) until a native shell injects its own host. Event
translation belongs to the host/window adapter; story and session
behavior never leave `Engine`. See `docs/PLATFORM_SUPPORT.md` for the
honest per-platform support matrix.
