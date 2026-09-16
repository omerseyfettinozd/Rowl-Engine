# Rowl Engine Data Formats & Migration Surface (1.0)

Canonical reference for every versioned format the 1.0 line reads and
writes, and for how older data migrates. Sources of truth are the
implementations named in each section; when this document and code
disagree, code wins and this document must be patched.

Conventions: JSON files are UTF-8, LF line endings (enforced by
`.gitattributes` for `*.rowlproj`/`*.json`). Readers tolerate missing
optional fields and ignore unknown fields unless stated otherwise.

## 1. Story graph (`full_story_graph.json`) — writer v4/v5, readers v1–v5

- **Writer:** `StoryGraphSerializer.SerializeFullStoryGraph` emits
  `format_version = 4`, `start_node_id`, and `nodes[]`; when the document
  carries Graph vNext structure (groups, subgraphs, chapters, or node
  chapter assignments) it emits `format_version = 5` plus the non-empty
  sections. See `docs/GRAPH_VNEXT_CONTRACT.md` for the v5 schema, rules,
  limits, and the runtime/C API surface.
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
- **Display metadata (Faz 2 Dilim 4, still v3):** `saved_at` (ISO-8601),
  `playtime_seconds`, `chapter_id`/`chapter_title`, `summary`,
  `thumbnail_width/height` and `thumbnail_png_base64` (320 px PNG, `""`
  when no framebuffer). All optional on decode; no version bump —
  see `docs/SAVE_SLOTS_AND_SEMANTIC_INPUT.md`.
- **`dialogue_history` entries** carry an optional `content_id` (Faz 2
  Dilim 2, UUID form, `""` for pre-migration lines). Decode defaults a
  missing key to `""` and rejects entries over 1024 bytes; no format
  version bump — old readers ignore the key, new readers default it.
- **Atomicity:** `SessionPersistence::saveSlot` writes a `.tmp` sibling
  and atomically renames over the final path; readers never observe a
  half-written slot. The ENOSPC injection message is prefix-compatible, not
  byte-identical (legacy sentence kept verbatim as a prefix with an
  `[ENOSPC (28): …]` tag appended), so downstream exact-match checks must
  compare by prefix. On Windows, rename-failure text resolves the
  GetLastError-domain value through strerror, so the wording is approximate.
- **Load reporting:** `SessionPersistence::loadSlotDetailed` returns
  `SessionLoadResult` (`Loaded`, `Migrated`, `NotFound`, `FileTooLarge`,
  `IoError`, `InvalidData`, `UnsupportedVersion`) with `sourceVersion`.
  The engine logs `Migrated save format version <v> to version 3`.
- **History:** states form an immutable chain (`previousState`);
  `SessionPersistence::checkpoint` aligns the chain with the live cursor
  before a save, and `rewind` steps back over it (surfaced as
  `RowlEngine_Rewind`).
- **Sürüm karar kilidi (Faz 6 Dilim 7 IS 2/2):** yazıcı her zaman
  `j["version"] = 3` yazar (`engine/src/state/game_state.cpp:206`;
  `CurrentSaveFormatVersion = 3` — `engine/include/rowl/state/game_state.hpp:67`).
  Okuyucu karar matrisi (`game_state.cpp:261-268, 379-381`):
  | Girdi | Sonuç | `sourceVersion` |
  |---|---|---|
  | `version` yok | `Loaded` (3 varsayılır, `:265`) | 3 |
  | 1 / 2 | `Migrated` (pasif tolerans — dönüşüm kodu yok, eksik alanlar varsayılana döner) | 1 / 2 |
  | 3 | `Loaded` | 3 |
  | 4, 999, … (bilinmeyen unsigned) | `UnsupportedVersion` + red | gelen değer |
  | string / negatif / float / obje (unsigned değil) | `InvalidData` | 0 |
  v1/v2 tarihsel okur-uyumluluğudur; sürümler arası gerçek dönüşüm kodu yoktur,
  tek yol aynı varsayılan-doldurmadır. Bilinmeyen sürüm asla sessizce kabul edilmez:
  `loadSlotDetailed` birebir eşler (`engine/src/state/session_persistence.cpp:120-127`),
  engine `loadGameSlot` reddedip `ValidationError` + `Unsupported save format version <v>`
  raporlar (`engine/src/core/engine.cpp:2068-2096`).
- **Sürümsüz JSON kararı:** `Loaded`-olarak-3. Gerekçe: sürüm alanı 1.0 öncesi alan
  verisinde hiç yoktur, sürümsüz kaydın sürüme-özgü şekli de yoktur; güncel varsaymak
  güvenlidir. `Migrated` yapmak `loadSlotDetailed`/engine raporunu çevirirdi ve
  `tests/test_game_state.cpp` (~351-357) ile kilitli davranışı bozardı — bu yüzden
  davranış değiştirilmedi, sürüm-matrisi testiyle kilitlendi
  (`TEST_PASS("Save-Format Version Matrix Lock …")`, `tests/test_game_state.cpp`).
- **v4'te yapılacaklar:** `CurrentSaveFormatVersion` bump edilir ve sürüme-özgü
  dönüştürücü `GameState::decodeJson` içindeki sürüm kabul/red bloğuna
  (`game_state.cpp:261-268` ve sonrası doldurma yolu) yazılır; her eski sürüm için
  ayrı dal + matris testine yeni satır eklenir. Gerekmedikçe yeni anahtar tek başına
  sürüm yükseltmez (`content_id` ve display metadata v3'te kaldı — eski okur bilmediği
  anahtarı yok sayar, yeni okur varsayılana döner).
- **Checksum yok kararı:** bütünlük denetimi JSON ayrıştırma + sınır/şema doğrulaması
  (`kMaxSaveFileBytes`, id/volume/playtime sınırları) ile sağlanır; ayrı checksum alanı
  yoktur. Hash/MAC follow-up adayı olarak bırakıldı.
- **Kapsam dışı:** C# katmanı save version metadata taşımaz ve göstermez, editör bu karardan etkilenmez.

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

## 7. MVP media formats (Faz 1 Dilim 1)

Single capability table, consumed by every chain — import
(`EditorAssetImportService`), pickers/preview (`EditorVisualAssetPickerService`,
`EditorAudioAssetPickerService`, `AssetBrowserViewModel`, component drop
filters), linter (`ProjectValidationService`), and the packager
(`tools/package_assets.py`). Native decoders already match: `stb_image`
(PNG/JPEG/BMP/TGA), `SDL_LoadWAV_IO` + libvorbis (WAV/OGG), `stb_truetype`
(TTF/OTF).

| Kind | Accepted | Rejected with explicit error |
|---|---|---|
| Image | `.png` `.jpg` `.jpeg` `.bmp` `.tga` | `.webp` (converter-pending), `.gif` `.psd` `.hdr` (unsupported) |
| Audio | `.wav` `.ogg` | `.mp3` `.flac` (converter-pending), `.aiff` `.aif` `.m4a` `.wma` `.aac` `.opus` (unsupported) |
| Font | `.ttf` `.otf` | `.woff` `.woff2` `.eot` (unsupported) |

- Extension matching is case-insensitive; asset *references* stay
  case-sensitive (Linux runtime and `.rowlpkg` lookup).
- MP3/FLAC/WebP carry the `converter-required` reason: they need the Faz 5
  converter (MP3/FLAC → OGG Vorbis, WebP → PNG) and are rejected until then.
- Enforcement: import skips rejected files with an explicit log (signatures
  unchanged); validation emits build-blocking `error` issues (bad format,
  `Asset name collision`, case-only mismatch, `outside the project`);
  the packager fails fast (exit 2, `[converter-required]` /
  `[unsupported-media-format]`, no output published).
- Mirror rule: the C# sets in `editor/Services/MediaFormatCatalog.cs`
  (`CONTRACT(...)` blocks) and the Python frozensets in
  `tools/package_assets.py` must stay identical; `tests/test_media_format_gate.py`
  parses both and fails on drift.

## 8. Camera rotation no-op (Faz 4.5 Dilim 2)

- **Decision (locked):** `Camera2D::setRotation` is a capability-gated
  no-op — NO deletion, NO C ABI removal or signature change.
  `m_rotation` stays `0.0f`; `getRotation()` is unchanged (ABI preserved,
  returns 0). A nonzero request logs one WARN per process; `rotation: 0`
  (the default for camera components without a `rotation` key) is silent.
- **Why behavior-preserving:** nothing in the render pipeline ever read
  the stored angle (`transformRect`/`transformRectParallax`/`transformPoint`
  use position/zoom/shake only), so nonzero `rotation` values in existing
  story JSON never affected a pixel. The no-op only makes the previous
  silent dead-store explicit and loud.
- **Capability bit:** `ROWL_ENGINE_CAPABILITY_CAMERA_ROTATION_IGNORED =
  UINT64_C(4096)`, included in the `RowlEngine_GetCapabilities` OR mask.
  Bit 2048 stays reserved for Dilim 3 (audio). Hosts must treat a set
  bit as "rotation requests are ignored, rotation reads 0".
- **Migration:** existing `camera` components with a nonzero `rotation`
  key keep loading (unknown/missing keys were already tolerated) and
  render exactly as before; no file rewrite is required. The editor
  disables the Camera Rotation control with a tooltip pointing at the
  no-op. Character/Background object `rotation` is unaffected (separate
  object-rotation path, out of scope).
