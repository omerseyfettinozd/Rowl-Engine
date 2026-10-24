# Unlockable content: CG gallery, music room, achievements (D07-1)

Native core only: the catalog file, the profile records, the triggers and the C API. The player
menus that show the gallery, the music room and the achievement list are D07-2; the editor
surface (the `unlock` component, an extras editor, lint) is D07-3. "Replay a scene" is **not**
part of this feature (it needs a side-effect-free sub-session; `docs/PRODUCTIZATION_BASELINE.md`
lists it for 1.x). The gallery and the music room only ever show or play media, so they cannot
change the story: variables, saves, read marks, history, `activeBgm` and the step id stay as they
are.

## 1. `Assets/json/extras.json` (format 1)

Read from the project VFS (loose project and `.rowlpkg` alike) when `RowlEngine_SetProjectDirectory`
mounts the project. No file means no extras (normal, no warning).

```json
{
  "format": 1,
  "gallery":      [{"id": "bogaz_ay", "title": {"tr": "Ay Işığında Boğaz", "en": "The Bosphorus by Moonlight"},
                    "image": "bogaz_ay.jpg", "thumbnail": "optional.jpg"}],
  "music":        [{"id": "nokturn", "title": "Nokturn", "track": "audio/nokturn_fa_minor.ogg"}],
  "achievements": [{"id": "ilk_son", "title": {"tr": "...", "en": "..."},
                    "description": {"tr": "...", "en": "..."}, "hidden": false}]
}
```

- `id`: `[a-z0-9_.-]{1,64}`, unique within its list. `title` and `description` are a plain string or an
  object keyed by language; the player UI language (`tr` / `en`, the D05-1 rule) picks the entry,
  a missing language falls back to the first entry in file order. `title` is optional (shown empty).
- `image` / `thumbnail` / `track`: VFS-relative paths (no absolute path, no `..`, at most 260 bytes).
  `thumbnail` defaults to `image`. `hidden` (achievements) tells the menu to veil the entry until
  it is unlocked; the JSON still carries the text.
- Limits: the file is at most 256 KiB, each list at most 256 items. A file that is too large, is not
  a JSON object or does not declare `"format": 1` is ignored as a whole (one diagnostic). Otherwise a
  bad item (invalid id, missing or unsafe path, wrong type) or a repeated id (the second one) is
  skipped with a diagnostic; the rest of the file works. Diagnostics are logged (`[Extras] ...`) and
  returned in `RowlEngine_GetExtrasJson` (`diagnostics`, at most 32 lines).
- Unknown keys are ignored (forward compatible).

## 2. Profile record

`player-profile.json` (still `version: 1`; the field was reserved and opaque, so no bump) keeps
what was unlocked:

```json
"unlocks": {"cg": ["bogaz_ay"], "music": ["nokturn"],
            "achievements": [{"id": "ilk_son", "at": "2026-10-24T19:30:00Z"}]}
```

- Ids only, never catalog text; an id that later leaves the catalog stays recorded and is simply not
  listed. `at` is the UTC ISO-8601 time of the unlock (achievements only).
- Nothing unlocked keeps the older shape `[]` (byte-identical to files written before D07 and by
  the managed store); the first unlock rewrites it as the object above.
- At most 1,024 records per kind: a full kind refuses new ids (`ROWL_RESULT_VALIDATION_ERROR`),
  existing ids stay. A file holding more drops the surplus on load (diagnostic).
- Reading is lenient: a non-empty legacy array, any non-object value, malformed or repeated records
  are dropped with a note in the profile diagnostic (the file is rewritten clean on the next
  change). Unknown keys inside `unlocks` and unknown top-level keys are kept verbatim. A build
  older than D07 reads a non-array `unlocks` as empty and would rewrite it: do not run an old
  build over a profile that holds unlocks.
- Writes use the read-mark path: dirty flag, debounced atomic flush (2 s), `RowlEngine_ProfileFlush`,
  shutdown. A read-only profile (newer schema, other game) never records an unlock.
- The managed editor mirror (`PlayerProfileStore`) keeps `unlocks` as an opaque JSON value, array or
  object, and writes it back unchanged.

## 3. Triggers

Only on a **real node entry** (`replayEntryEffects == true`: advance, choice, new game, graph mount).
Load, rewind, scene restore, the D03 backlog voice replay, a node a `condition` skipped and a scene
update that failed and was rolled back never unlock anything. The work happens last, after the
whole scene update succeeded.

- **CG**: the node's background texture or a character sprite equals a gallery `image`.
- **Music**: the node's `audio.bgm_track` equals a music `track`.
  Both compare the canonical form of the path: `\` becomes `/`, a leading `./` or `/` goes, one
  `Assets/` and one `images/` (CG) or `audio/` (music) prefix are dropped, ASCII letters are
  lowercased (so `bogaz.jpg`, `images/bogaz.jpg` and `Assets/images/Bogaz.JPG` are one asset).
- **Explicit component** `{"type": "unlock", "data": {"kind": "cg|music|achievement", "id": "..."}}`.
  This is the only way to unlock an achievement. An id that is not in the catalog of that kind
  (or a bad `kind`, or a missing field) writes nothing and logs one warning per node and graph.
  A disabled component does nothing.
- A newly unlocked achievement is appended to a notice queue (at most 64) that the toast of D07-2
  will drain; this slice only exposes it (`pending_notices` in the extras JSON,
  `Engine::pendingUnlockNotices` / `clearUnlockNotices`). Unlocking is idempotent: repeating it
  changes nothing, keeps the first `at` and queues no second notice.
- Nodes without `components` (the legacy flat payload) carry no unlock triggers.
- The start node counts as an entry, so a game that shows a CG on its first node unlocks it when
  the project is mounted, before the player presses New Game.

## 4. C API (`ROWL_ENGINE_CAPABILITY_EXTRAS_UNLOCKS = 268435456`)

| Function | Result |
| --- | --- |
| `RowlEngine_GetExtrasJson(handle, buf, size, &required)` | catalog + per-item `unlocked`, achievement `at`, `counts`, `diagnostics`, `pending_notices`, `language`, `profile_status` (caller-buffer contract) |
| `RowlEngine_ProfileUnlock(handle, kind, id)` | `OK` (new or repeated); `INVALID_ARGUMENT` for NULL, an unknown `kind`, a malformed id or an id outside that kind's catalog; `STATE_ERROR` for a read-only or unmounted profile; `VALIDATION_ERROR` when the kind is full |
| `RowlEngine_ProfileIsUnlocked(handle, kind, id, &out)` | `*out` 1 when the profile records the id (also for an id the catalog dropped); `INVALID_ARGUMENT` for NULL or an unknown kind |

Owner-thread API like the profile family; before `RowlEngine_Init` every call is `STATE_ERROR`;
`WRONG_THREAD` / `INVALID_HANDLE` as elsewhere. There is no Lua `rowl.unlock` (the sandbox bridge
stays untouched) and `NativeBridge` does not bind these functions yet (D07-3).

## 5. Tests

`tests/test_extras_unlocks.cpp` (native): parser and path keys, profile store (idempotence, shape,
legacy forms, unknown keys, limits, read-only), triggers on real entries, the guards (load, rewind,
backlog voice replay, condition skip, rolled-back update), explicit component warnings, restart
persistence, the C API, and the shipped sample `samples/kayip_tablo` (clean catalog, every file in
the VFS, every catalog entry reachable from the story). The managed round trip of an object
`unlocks` is `EditorPlayerProfilePerGameTests`.
