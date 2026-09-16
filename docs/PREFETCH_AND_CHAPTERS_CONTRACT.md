# Prefetch & Chapters Contract (Faz 5 Dilim 4 — native hat)

Companion to `CHARACTER_LAYERS_CONTRACT.md` (Faz 5 Dilim 3),
`AUDIO_MIXER_CONTRACT.md` (Dilim 2) and `AUDIO_STREAMING_CONTRACT.md`
(Dilim 1). Those documents are NOT modified by this slice. Sources of truth
are the implementations named below; when this document and code disagree,
**code wins** and this document must be patched.

Locked decisions: additive-only C ABI (new capability bit `65536`,
`ROWL_ENGINE_CAPABILITY_PREFETCH_CHAPTERS`, OR-ed into
`GetCapabilities`; bits 1..32768 intact); `engine.cpp` and `window.cpp`
ZERO diff — new systems live in new files only (`prefetch.*`,
`chapter_loader.*`, `c_api_prefetch_chapters.cpp`); existing story/engine
signatures kept; audio streaming/mixer/character semantics untouched; no
format changes (editor schemas consumed as-is); C#/editor belongs to
another agent.

Owners: `engine/include/rowl/core/prefetch.hpp` +
`engine/src/core/prefetch.cpp` (budget + pump),
`engine/include/rowl/core/chapter_loader.hpp` +
`engine/src/core/chapter_loader.cpp` (windowing),
`engine/src/c_api_prefetch_chapters.cpp` (C API, per-handle state).

## 1. Budget formula (owner: `AssetPrefetch`)

- Effective byte budget: `budget == 0 → 32 MiB`
  (`kPrefetchDefaultBudgetBytes`); otherwise
  `min(budget, 128 MiB)` (`kPrefetchMaxBudgetBytes`). Budgets are
  **clamped, never rejected** — a 1 TiB request prefetches under 128 MiB.
- Time budget per pump: `RowlEngine_PumpPrefetch(handle, ms)` with
  `ms <= 0` or non-finite → `~4 ms`
  (`kPrefetchDefaultPumpMilliseconds`); clamped to `50 ms`
  (`kPrefetchMaxPumpMilliseconds`).
- The pump is **synchronous, update-thread driven, no threads**. Deadline
  is checked per asset via injectable `SteadyNow` (tests inject a fake
  clock; production uses `steady_clock`). An asset that would cross the
  deadline stays queued (`deferred`, still `complete == false`); the next
  pump resumes where it left off.
- Byte accounting: each queued asset is size-probed through VFS
  (`openReadStream` + seek-to-end; falls back to a full `readBytes`).
  `ready_bytes` accumulates only completed assets. An asset whose size
  would exceed the remaining budget stops the pump at that asset
  (**head-of-line block**, `prefetch.cpp` pump `break`): the asset stays
  **queued** (`complete == false`) with a `byte budget exhausted`
  diagnostic, and cheaper assets behind it wait — they do not skip ahead.
  Missing assets
  **never stop the queue**: counted in `missing_assets`, path recorded in
  `missing_paths` (capped at 64), queue continues.
- Progress JSON (`GetPrefetchProgressJson`): `{total_assets,
  ready_assets, missing_assets, queued_assets, ready_bytes, budget_bytes,
  complete, missing_paths[], last_diagnostic}`.
  `complete == (ready + missing == total && queued == 0)`.

## 2. Asset collection (owner: `collectNodeAssets`)

Per story node, the union of every component key the engine already reads
(no new keys invented):

- `node.background` / `background.texture` → image;
- `node.character` / `character.sprite` → image, plus
  `layers.{body,face,outfit,accessory}` each as string shorthand or
  `{asset}` object → image;
- `character` `voice_blip` / typewriter sounds → voice;
- `dialogue.custom_box_texture` → image; `dialogue` typewriter /
  `voice_blip` sounds → voice;
- `audio.bgm_track` → bgm; `audio.sfx_track` → sfx; forward-compat
  `audio.voice_track` → voice, `audio.ambience_track` → ambience;
- `choice` options `normal_image` / `background_image` → image.

Collection dedups within a node and (via `collectDocumentAssets`) across
nodes. The trigger window is the requested chapter + its successor
(loader buckets preferred, else the engine document window; legacy
single-file = current node + successors).

## 3. Neighborhood definition (owner: `ChapterLoader`)

- `kChapterNeighborDistance = 1`: with active chapter A in ordered chapter
  list `[..., A-1, A, A+1, ...]`, resident = `{A-1, A, A+1}` (clamped at
  the ends). `active ± 2` and beyond are **unloaded** (payload memory
  released; bucket metadata retained).
- `setActiveChapter` rebuilds the window (`applyWindow`); `LoadChapter`
  marks one extra chapter resident (window widens, active unchanged);
  `UnloadChapter` evicts one chapter and **refuses the active chapter**.
- Access to an unloaded node (`node(id)`) **transparently reloads** its
  chapter from the stored file payload and records a diagnostic
  (`kMaxChapterDiagnostics = 32`, ring). Unknown node → null, fail closed.
- Legacy single-file graphs (no index/chapters): one implicit chapter, no
  windowing, everything resident (`legacy_single_graph == true`).
- `kMaxGraphChapters = 1024` kept: index parsing caps at 1024 chapters,
  duplicate ids rejected; unknown chapter ids auto-register (editor
  `Merge` parity) under the same cap.
- Per-file validation reuses `StoryGraphParser` on an edge-stripped
  payload: `next_id` / `next_nodes[].{id,label,option_id}` are shape-checked
  then stripped (cross-chapter targets are legal), the payload parses
  standalone, edges are re-attached. Node `chapter_id` must match the file
  `chapter_id`; duplicate node ids rejected.
- `IsChapterBoundaryNode(id)`: 1 when the node starts a chapter or has a
  successor in another chapter; 0 otherwise (unknown node / dead handle
  are 0, fail closed).
- `GetLoadedChaptersJson`: `{active, loaded[], neighbors[], node_counts{},
  resident_nodes, total_nodes, legacy_single_graph, last_diagnostic}`.

## 4. Fail-closed matrix (C API)

| Input | Result | State |
|---|---|---|
| dead/null handle (all 9 entries) | `INVALID_HANDLE` (`0`/`""` carriers) | stale entry pruned, nothing else touched |
| unknown chapter (`Load/Unload/Prefetch`) | `INVALID_ARGUMENT` | loader/prefetch unchanged |
| `UnloadChapter` on active chapter | `INVALID_ARGUMENT` | resident set unchanged |
| null JSON pointer | `INVALID_ARGUMENT` | unchanged |
| input without NUL in 256 KiB + 1 (16 MiB + 1 for chapter files) | `INVALID_ARGUMENT` (oversized) | unchanged |
| malformed JSON | `PARSE_ERROR` + diagnosis | unchanged |
| schema violation (dup chapter, dup node, >1024, chapter mismatch, bad edge) | `VALIDATION_ERROR` + diagnosis | unchanged |
| null `outRequiredSize` | `INVALID_ARGUMENT` | unchanged |
| undersized caller buffer | `BUFFER_TOO_SMALL` + required size, buffer cleared | unchanged |
| oversized budget (e.g. 1 TiB) | success, clamped to 128 MiB | budget = 128 MiB |
| `PumpPrefetch` `ms <= 0`/NaN | success, ~4 ms used | unchanged |
| unknown node id (boundary query) | `0` | unchanged |

Per-handle diagnosis: `GetLoadedChaptersJson.last_diagnostic` and
`GetPrefetchProgressJson.last_diagnostic` surface the latest loader/pump
note (incl. transparent-reload events).

## 5. Limits

- C API input scan: 256 KiB + 1 (Dilim 1–3 pattern); chapter
  index/file inputs: 16 MiB + 1. Over → `INVALID_ARGUMENT`.
- Prefetch budget: default 32 MiB, cap 128 MiB; pump: default ~4 ms,
  cap 50 ms.
- Chapter cap: 1024 (`kMaxGraphChapters`); diagnostics ring: 32;
  missing-path list: 64.
- JSON outputs: caller-buffer contract (NULL/0 size-query, undersized
  clears + `BUFFER_TOO_SMALL`).

## 6. Test matrix (owner: `tests/test_prefetch_chapters.cpp`)

- Asset collection: node with 4 character slots + all audio channels →
  16 assets with expected kind split; two-node document dedups shared
  paths (30, not 31/32).
- Byte-budget cut: 250-byte budget → 2 ready / 1 queued / not-complete;
  missing asset counted + diagnosed while the queue continues (default
  budget). Owner `testByteBudgetCut` (`tests/test_prefetch_chapters.cpp:159-179`:
  3 × 100 B assets, 250 B budget → 2 ready / 1 queued / not-complete).
  Head-of-line ordering (first-asset-over-budget blocks cheaper-later
  assets) has no dedicated native probe and none is added here (native is
  out of scope for this round); it cannot be covered from C# either — pump
  order lives in native `AssetPrefetch::pump` and `PrefetchChaptersService`
  mirrors only budget clamps, with no per-asset ordering decision function.
- Time-budget deferral: fake clock +10 ms/call → 1 ready then deferred;
  resume with ample budget completes.
- Chapter window: 5 chapters × 10 nodes, active ch3 → loaded
  `{ch2,ch3,ch4}`, 30/50 nodes resident.
- Transparent reload: unload → access → payload restored + diagnostic
  recorded; unknown node null; active-unload refused; boundary flags
  (chapter-start + cross-chapter successor).
- Legacy single-file: full graph resident, no windowing.
- Memory bound: 100 chapters × 100 nodes (10,000) → active k50 keeps
  300 resident; far access reloads to 400 + diagnostic (RSS/count probe).
- C API vectors: bit 65536 set + bits 1..32768 intact; null-handle on all
  9 entries; unknown/malformed/validation/oversized inputs; caller-buffer
  size-query/undersized/exact; active-unload refused; boundary query;
  1 TiB budget clamps to 128 MiB.
- End-to-end: temp project + VFS remount → 3 ready, 1 missing
  (`voice.ogg`) counted, `ready_bytes == 1792`, `complete == true`.

## 7. Editor wiring boundary (Faz 5 Dilim 4 fix turu 1)

The standalone PlayerWindow path (`MainWindowViewModel.OpenPlayerWindow`)
does not feed chapter files in this slice — only the in-editor Play Mode
coordinator (`EditorPlayModeCoordinator.StartPlayModeAsync` →
`PrefetchChapterFeedService.FeedFromAssetsDir`) feeds
`chapter_index.json` + chapter files after `LoadStoryGraph`. Wiring the
standalone window is deferred to a follow-up slice. The coordinator call
itself has no headless seam test (`EngineHost` is concrete and live init
needs native), so it is covered by code review + this contract note.
