# Player Loop Slice 1 — Content ID & PlayerProfile Contract (Faz 2 Dilim 1)

Canonical reference for persistent dialogue identity and the save-slot
independent player profile skeleton. Sources of truth are the
implementations named below; when this document and code disagree, code
wins and this document must be patched.

## 1. Content identity (`content_id`)

- Every dialogue component carries a stable `content_id` in UUID form
  (`8-4-4-4-12`, lowercase), stored in component `data["content_id"]`.
  It keys read-tracking, backlog, skip and (Faz 3) localization catalogs.
- Empty means "predates migration" and is valid. A non-empty value that is
  not a UUID is a build-blocking validation error.
- New dialogues (canvas creation, `EditorComponentService.AddComponent`,
  `NodeViewModel` default construction) receive a fresh random v4 id from
  `ContentIdService.NewContentId()`.
- Clones and duplicates (`NodeViewModel.DuplicateObject`,
  `EditorBatchOperationService` node clone) receive fresh ids; reusing a
  source id is a validation error by design.
- The serializer omits an empty `content_id` (never writes null), so
  pre-migration v4 documents stay byte-stable. The native parser reads
  component data with key-default lookups and ignores the extra entry, so
  no C ABI change was needed in this slice.

## 2. One-time UUIDv5 migration

- Legacy content migrates deterministically:
  `UUIDv5(namespace = project_uuid, name = "node:{nodeId}/component:{componentId}")`
  per RFC 4122 §4.3 (`ContentIdService.MigrateContentId`).
- `ContentIdService.EnsureContentIds` fills only empty ids, normalizes
  existing ones to canonical form, never overwrites, and is idempotent
  (a second run migrates zero entries).
- `project_uuid` lives in `project.rowlproj` (`ProjectIdentityService`).
  New projects get one at creation (`ProjectFactory`); legacy projects get
  one generated exactly once and written back atomically, preserving every
  other manifest field. A manifest that cannot be read or written yields a
  structured error and a transient (unpersisted) fallback — identity is
  never half-written.

## 3. Validation (build-blocking)

`ProjectValidationService` reports, for reachable nodes and enabled
dialogue components only:

1. `duplicate content_id '<uuid>'` — the same id on two reachable
   dialogues; blocks the build (`IsError`).
2. `invalid content_id '<value>'` — non-UUID form; blocks the build.

Unreachable nodes and disabled components are out of scope (consistent
with asset-reference checks). v4 and Golden Project files carry no ids
and validate clean.

## 4. PlayerProfile (skeleton, v1)

- (Original slice-1 layout; since D01c-2 the editor uses the per-game profile of
  section 9 and this global path is only the legacy migration source.)
  Save-slot independent, versioned (`version = 1`), persisted at
  `profiles/player-profile.json` below the PlatformHost user-data root —
  the same `rowl-engine/profiles` layout the native
  `Rowl::Platform::resolveUserDataDirectories` produces. Hosts holding a
  native handle should pass the `RowlEngine_GetProfileDirectoryUtf8`
  result; otherwise `PlayerProfileStore.ResolveDefaultProfileDirectory`
  mirrors the layout in managed code.
- Fields: `read_content_ids` (sorted string set of UUIDs),
  `language` (`en` default, `tr` supported, `tr-TR` → `tr` normalization),
  `master/bgm/voice/sfx_volume`, `text_speed_multiplier`,
  `auto_advance_delay`, `skip_mode` (`off` / `read_only` default / `all`),
  `auto_enabled` (default false).
- Writes are atomic (temp file + rename via
  `ProjectFileSystem.WriteAllTextAtomically`); readers never see a
  half-written profile. Loads never throw and never fail silently:
  `PlayerProfileStore.Load` returns `Loaded | MissingDefaults |
  CorruptDefaults | UnsupportedVersionDefaults` plus a diagnostic string
  for every fallback. Malformed read ids are dropped and counted.
- `PlayerSettingsProfile` is untouched; the new profile is additive.

## 5. Limits

The read set is bounded at 1,000,000 ids in the native store (D01c-1; see
section 8): new ids are refused with a diagnostic once it is full and
existing ids are never dropped. The managed store is still bounded only by
available memory. Profile JSON must stay a single small object; multi-MB
profiles are a Faz 5 concern.

## 6. Tests

- xUnit `EditorPlayerLoopSlice1Tests` (16): RFC 4122 UUIDv5 test vector,
  determinism/shape/sensitivity, v4 uniqueness, migration input guards,
  migrate-once-and-preserve (+ idempotence), serialize omit/write/round-trip,
  duplicate gate, malformed gate, disabled-duplicate exemption, v4 Golden
  compat (`first_light`, `second_signal`), project identity
  create/persist/reuse, profile atomic round-trip (+ no `.tmp` residue),
  missing/corrupt/unsupported-version fallbacks, sanitizer ranges.
- Untouched gates stay green: full xUnit suite, headless Test 34/35,
  native CTest, `git diff --check`.

## 7. Dilim 2 — runtime tracking, backlog ids, skip gate

- Native dialogue payloads flow `content_id` into
  `DialogueRenderData.contentId` (`updateSceneFromComponents`); graph-file
  loads already carried it verbatim through `StoryNode.components[].data`.
- Backlog entries (`DialogueHistoryEntry.contentId`) are recorded at
  presentation, serialized into `GetDialogueHistoryJson` as `content_id`
  and into save-slot `dialogue_history` (same key). Decode is tolerant:
  missing → `""`, oversized (>1024 B) → `InvalidData`. No save-format
  version bump (old readers ignore the key, new readers default it).
- New additive C API `RowlEngine_GetActiveDialogueContentIdsJson`
  (caller-buffer contract) returns the presented dialogues' ids as a JSON
  array; legacy lines contribute `""` so hosts fail closed. Gated by
  `ROWL_ENGINE_CAPABILITY_PLAYER_LOOP (16)`.
- Managed side: `DialogueHistoryEntry.content_id` flows into
  `BacklogViewModel` via the existing history pull; `SkipGate` is the pure
  Off/ReadOnly/All decision (choices always stop; empty/malformed ids never
  count as read); `PlayerLoopService` snapshots the departed line
  *before* advancing, marks it read, and atomically persists
  (`AdvanceAndTrack`, `TrySkipStep` single-step).
- `EngineHost` gained only seams: `GetActiveDialogueContentIds`,
  `AdvancePlayerLoop`, `TrySkipPlayerLoopStep`. Plain `AdvanceNode` stays
  preview-side and untracked. The continuous auto-skip driver belongs to
  the Faz 2 Playing-state loop (later dilim); this slice delivers the
  tested single step it will call per tick.
- Tests: native contract (capability + null-handle + ids/history payload),
  native save codec (round-trip, legacy fallback, hostile rejection),
  xUnit `EditorPlayerLoopSlice2Tests` (10): gate matrix, track-on-advance,
  query-failure abort, flush no-op, skip-step allow/stop, backlog parse.

## 8. Native profile (D01c-1)

The native engine now owns a profile store (`engine/src/state/player_profile.cpp`)
that reads and writes the same `player-profile.json` keys as section 4, plus
`game_id` and a reserved `unlocks` array. It loads when `SetProjectDirectory`
resolves the game data layout (per-game dir `.../games/<game-id>/profile`;
`<project>/profile` beside the project-local saves for derived ids), marks dialogues read when
they are presented in play state, and exposes `RowlEngine_Profile*`
(`ROWL_ENGINE_CAPABILITY_PLAYER_PROFILE`). Corrupt files are quarantined,
files from a newer schema or another game are read-only and left untouched.
The full contract (schema, statuses, flush policy) is in
`docs/DATA_FORMATS_AND_MIGRATION.md` section 9. Both gaps listed here in D01c-1 are
closed in section 10 (D01d). The editor side (managed store
parity, per-game location, single writer) is section 9.

## 9. Editor uses the per-game profile (D01c-2)

- **One profile per game.** The editor player window no longer shares one
  global `profiles/player-profile.json` between projects. With the native
  capability and a mounted project, `PlayerProfileSession.Open` takes the
  directory from `RowlEngine_GetProfileDirectoryUtf8`
  (`<user-data>/games/<game-id>/profile`, or `<project>/profile` for a game
  without an authored id) and the **native store is the only writer** of the
  file: the editor parses `RowlEngine_ProfileGetJson`, pushes preference changes
  with `RowlEngine_ProfileSetPreferencesJson` + `RowlEngine_ProfileFlush`
  (preferences screen, toggles), hands read lines to `RowlEngine_ProfileMarkRead`
  (the native hook marks presented lines too; double marking is harmless), and
  flushes when the player window closes. All native calls run on the engine
  thread (`EngineHost.InvokeOnEngineThread`).
- **Read-only stays read-only.** A newer-schema or other-game file (native
  status `unsupported_version` / `wrong_game`) is shown as a diagnostic when the
  player opens and is never written. A player window that outlives a project
  switch detaches (one error, then silent) instead of writing the other game's
  profile.
- **Fallback without a native store** (no engine, older library, nothing
  mounted): the managed `PlayerProfileStore`, but still per game
  (`<user-data>/games/<project_uuid>/profile`, `<project>/profile` without an
  id); the legacy global directory only when no project is known.
- **Managed/native parity.** `PlayerProfileStore` keeps `game_id`, the opaque
  `unlocks` array and unknown top-level keys on rewrite (`PlayerProfile.GameId /
  Unlocks / ExtraKeys`), writes `unlocks: []` like the native store, and treats
  a `game_id` of another game as `WrongGameDefaults` (read-only). Future-version
  files stay read-only on the managed path too (the preferences save no longer
  overwrites them).
- **Migration of the old global profile.** If the game has no profile file yet
  and the legacy `<user-data>/profiles/player-profile.json` exists and has not
  been claimed, its preferences and read set are copied **once** into the first
  project with an authored id whose player window is opened (native
  `SetPreferencesJson` + `MarkRead` + `Flush`, so the store stamps `game_id`).
  A claim marker `player-profile.migrated.json` (`{format, game_id, time,
  read_count}`) is written next to the global file first, so no other project
  inherits it; if the copy fails the marker is removed again. The global file is
  never modified or deleted. Projects without an authored id (derived identity)
  neither migrate nor claim. A one-line `[Profile]` log entry records it.
- `LocalizationService.Load(profileDirectory)` is not wired into production; its
  language is the player's per-game preference (resolved against the game's
  manifest locales), so callers must pass the mounted game's profile directory.
  The editor's own UI language is a separate editor setting.
- Tests: `EditorPlayerProfilePerGameTests` (two projects isolated and persisted,
  golden-fixture and native/managed round trips, read-only profiles
  byte-identical, project switch detach, migration once/claim/skip).

## 10. Standalone player wiring (D01d)

- **First line is marked read.** The start node's dialogue is presented before
  play state begins, so the presentation hook never saw it. `Engine::setPlayState`
  now marks the active dialogues (non-empty `content_id` only) read at the
  false-to-true transition, which both `RowlEngine_Run` and the editor's
  `RowlEngine_SetPlayState(1)` go through. It touches the read set only: no backlog
  entry is recorded, so nothing is recorded twice; stopping and re-entering play
  state is harmless (already-read ids are `Unchanged`). Native test:
  `tests/test_native_player_profile.cpp` (start-node section).
- **The player applies stored preferences at start.** After
  `RowlEngine_SetProjectDirectory` (and the story graph) the player reads
  `RowlEngine_ProfileGetJson` and, when the profile status is `loaded`, applies
  through the existing public setters: master/bgm/voice/sfx/ambience/ui volume,
  `text_speed_multiplier`, `text_scale`, `high_contrast`, `reduced_motion`,
  `mixer_fade_curve`, `sfx_pool_depth`, and `language` (only when it is not the
  profile default `en` and the game offers that locale: the profile cannot tell
  "never chosen" from "chose en", and a project whose own default locale is not
  `en` must keep it). It prints `[Player] Applied stored preferences: key=value ...`
  with the values read back from the engine.
  **Not applied yet** (no native setter or semantics): `auto_advance_delay` (the
  native knob, `RowlEngine_SetAutoAdvanceDelayOffset`, is an additive per-line
  offset, not an absolute delay), `skip_mode`, `auto_enabled`; the explicit
  "language chosen" marker and a way for the in-game pause menu to write back
  its volume/text-speed changes belong to the title/menu work (D02/D05).
  Note that loading a save restores the volumes stored in that save.
- **Diagnostics and `--print-game-identity`.** The player prints a short block
  after mounting (game id and identity source, data/save dirs, profile path and
  status, legacy-save migration summary). `--print-game-identity` prints the same
  facts as one JSON line and exits without opening a window; `--package-smoke-test`
  marks the first line read, changes `text_speed_multiplier` and restores it
  (the player's settings stay as they were), flushes and prints
  `Package smoke profile: status=... read_count=...` (a read-only profile is
  reported, never written). See `docs/PLAYER_GUIDE.md`.
- **Acceptance.** `rowl_packaged_game_isolation`: two packages of one sample with
  different game ids keep separate ids, data dirs, profiles and saves; relaunch
  finds the read id and applies the stored volumes; a hand-written
  `"version": 99` profile stays byte-identical and is reported read-only; the
  launcher does not move the data dir.
