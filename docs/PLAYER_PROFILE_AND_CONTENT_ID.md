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

- Save-slot independent, versioned (`version = 1`), persisted at
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

Read set is bounded only by available memory in this slice (stressing it
belongs to the Faz 4 large-project gate). Profile JSON must stay a single
small object; multi-MB profiles are a Faz 5 concern.

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
