# Unlockable content: CG gallery, music room, achievements (D07-1)

Native core (D07-1: the catalog file, the profile records, the triggers and the C API) and the
player screens (D07-2, section 6: the title's "Ekstralar" pages and the achievement toast). The editor
surface (the `unlock` component, an extras editor, lint) is D07-3 (section 7). "Replay a scene" is **not**
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
                    "description": {"tr": "...", "en": "..."}, "hidden": false, "icon": "ui/star.png"}]
}
```

- `id`: `[a-z0-9_.-]{1,64}`, unique within its list. `title` and `description` are a plain string or an
  object keyed by language; the player UI language (`tr` / `en`, the D05-1 rule) picks the entry,
  a missing language falls back to the first entry in file order. `title` is optional (shown empty).
- `image` / `thumbnail` / `track`: VFS-relative paths (no absolute path, no `..`, at most 260 bytes).
  `thumbnail` defaults to `image`. `hidden` (achievements) tells the menu to veil the entry until
  it is unlocked; the JSON still carries the text. `icon` (achievements, optional): a VFS image shown on the
  plate once the achievement is unlocked (T2); a locked one never shows it (the theme's `extras.locked_icon`
  stands in). A bad icon path skips that item like a bad thumbnail.
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
  drains (`pending_notices` in the extras JSON, `Engine::pendingUnlockNotices` /
  `clearUnlockNotices`; section 6). Unlocking is idempotent: repeating it
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
stays untouched). The managed `NativeBridge` binds only `RowlEngine_GetExtrasJson` (D07-3, for the
rule-parity test); it does not bind `ProfileUnlock`, `ProfileIsUnlocked` or `RowlEngine_OpenExtras`.

## 5. Tests

`tests/test_extras_unlocks.cpp` (native): parser and path keys, profile store (idempotence, shape,
legacy forms, unknown keys, limits, read-only), triggers on real entries, the guards (load, rewind,
backlog voice replay, condition skip, rolled-back update), explicit component warnings, restart
persistence, the C API, and the shipped sample `samples/kayip_tablo` (clean catalog, every file in
the VFS, every catalog entry reachable from the story). The managed round trip of an object
`unlocks` is `EditorPlayerProfilePerGameTests`. The player screens (section 6) are locked by
`tests/test_extras_screens.cpp`: title row with / without a catalog, page flow and Back rules, grid and viewer
pixels (a locked cell is the flat box colour, an open one its picture), thumbnail cache bounds, music-room
audio state (title, continued game, no GameState), achievement texts, the toast (capsule pixels, clean frame
after, queue order, hidden and frozen under menus, Auto), isolation (variables, GameState object, save bytes,
profile, history, unlocks) and tr/en.

## 6. Player screens (D07-2: `engine_extras_menu.cpp`, `window_extras_overlay.cpp`)

**Entry.** The title screen lists **Ekstralar** / **Extras** between Oyunu Yükle and Tercihler when the
catalog has at least one item (six rows; Tercihler and Çıkış move down one row). No `extras.json`, or one
without items, keeps the older five rows. The pause menu never lists it. `RowlEngine_OpenExtras(handle)` opens
the title screen when it is not up and lands on the root page (`STATE_ERROR` before init and for an empty
catalog). Pause-menu modes: 6 root, 7 gallery, 8 music room, 9 achievements.

**Pages.** Root: Galeri / Müzik Odası / Başarımlar / Geri, each value `unlocked/total`. Back, Esc and a right
click go one level up; from the root Esc / Geri returns to the title with Ekstralar selected (the story never
starts from here).

- *Gallery*: a 4-column grid of 16:9 thumbnails with a caption; whole grid rows scroll (4 rows visible; keys and
  wheel). A locked item is a dark box with "?" and the caption "???"; its picture is never loaded and its path
  is never part of the view or the JSON. Arrow keys / mouse select; Enter or a click opens an unlocked item
  full screen (aspect ratio kept, centred, caption below). In the viewer Left / Right step through the
  **unlocked** items only (wrapping); Esc, any click or a right click returns to the grid with that item
  selected. Grid thumbnails live in a small downscaled cache of their own (at most 24, 356x200, a few decoded
  per frame) so a page of 1920x1080 pictures never touches the 64 MB texture cache; the viewer uses
  `Window::loadTexture` for one image at a time. Drawn on the offscreen surface as well as the GPU window.
- *Music room*: a list (11 rows visible, scrolls); a locked track shows "???". Enter or a click plays an
  unlocked track on the audio engine's BGM channel (the project's BGM transition applies) and marks the row;
  the same row again stops it. The track keeps playing across Ekstralar pages. `GameState.activeBgm` is never
  written. Leaving Ekstralar (back to the title, closing the menu, re-opening the title) stops it and brings
  the sound back: `restoreAudioStateFromGameState()` when there is a GameState, silence otherwise - and only
  if the room actually played or stopped something.
- *Achievements*: a read-only list: title, description and the unlock date (`YYYY-MM-DD`, UTC). Locked: the
  title shows and the description is "???"; a locked `hidden` achievement shows "???" for both. The page has
  its own two-line row layout (`PauseMenuLayout::kAch*`): 96 px boxes at a 104 px pitch (6 rows visible, the
  list scrolls beyond that), the title on top, the description below with at least 6 design pixels of clear
  band between the two lines, the date at the right edge on the title line, and a highlight that covers both
  lines. Rows come from `extras.items` (`y`, `h` in the JSON), not from the generic one-line row list, so
  the slot-page detail rows, Tercihler and the title keep their layout.

**Toast.** While the story runs (not paused) the first queued achievement notice is taken off the queue and
shown for 3 s as a semi-transparent capsule at the top centre ("Başarım: <title>" / "Achievement: <title>").
More notices follow one by one without a clean frame between them. Any menu (pause, title, backlog,
preferences, Ekstralar) hides it and freezes its clock; notices wait in the queue. Auto and Skip do not hold
the story up (the clock is real time). The window tracks the baked capsule text like the AUTO / SKIP badges
(cache invalidation), so no stale capsule survives. `RowlEngine_GetExtrasJson` keeps `pending_notices`
(not yet shown) consistent and adds `toast` (`{kind, id, visible, remaining_seconds}` or `null`).

**State JSON.** `RowlEngine_GetPauseMenuJson` gains an `extras` object in the Ekstralar modes: `page`,
`total`, `unlocked`, `selected` (catalog index), `scroll`, `counter`; gallery `columns`, `viewing`,
`view_index`, `view_title`, `cells[{index,x,y,w,h,unlocked,selected,title,thumbnail?}]` (`thumbnail` only for
unlocked cells); music `playing`, `items`; achievements `items[{description,at}]`.

**Isolation.** Viewing, playing and scrolling write nothing: variables, GameState (`activeBgm` included), save
slots (byte for byte), read marks, history and unlocks stay as they were.

## 7. Editor authoring (D07-3)

**An editor that does not know a component type must not delete it.** Before D07-3 the story loader
skipped a component type it had no view model for ("Unknown component type ... skipping") and the next
save wrote the node without it: opening and saving `samples/kayip_tablo` lost its four `unlock`
components. Now `UnknownComponentViewModel` keeps `type`, `id`, `enabled` and the raw `data` object
(detached JSON clones, nothing re-typed), writes them back in the same place, copies them on node
duplication, and shows a read-only card in the Inspector. Node-library templates still skip a type
they do not know (they report it).

**`unlock` component** (`UnlockComponentViewModel`, "Add component" in the Inspector's advanced
section): *Kind* (CG / Music / Achievement = JSON `cg|music|achievement`) and *Id*, the exact native
schema. The id is a picker over the project catalog's items of that kind; with no usable catalog (no
file, an unusable file, no item of that kind) it is a text box. An id the catalog lacks stays in the
component and is shown as "not in catalog" (the lint reports it); a picker that is rebuilt never erases it.
The edit-time preview (`SerializePreviewComponents`) leaves `unlock` out: the engine applies it as an
entry effect that writes the player profile, and selecting a node in the editor must not unlock anything.
Play mode runs the real story graph, where it applies as in the shipped player.

**Catalog editor** ("..." menu > *Extras catalog*; also "Open the extras catalog" on the unlock card):
`ExtrasCatalogEditorWindow` over `ExtrasCatalogEditorViewModel`. Three lists (Gallery, Music room,
Achievements) with add, remove, move up / down and per-item fields (id, title tr / en, image and
optional thumbnail, track, description tr / en, hidden). Images and tracks are picked with the OS file
dialog and copied into the project like every other asset. `ExtrasCatalogModel` keeps what the editor has no field for
(unknown top-level keys, unknown item keys, language entries other than tr / en, a plain-string title
stays plain until one language is edited), so an untouched open + save leaves the document the same in
meaning; `ExtrasCatalogStore.Save` writes `Assets/json/extras.json` atomically. A file the engine
would ignore as a whole (not JSON, no `"format": 1`, over 256 KiB) is shown as an error and is never
overwritten.

**Rules** (`ExtrasRules`, `ExtrasCatalogModel.Validate`) are the native ones of section 1: id
`[a-z0-9_.-]{1,64}` and unique per list; `image` / `track` required, safe relative paths (no absolute path, drive
letter or `..`, at most 260 UTF-8 bytes); titles and descriptions at most 512 bytes and language
tags at most 16; a wrong JSON type is an error; at most 256 items per list and 256 KiB per file.
Saving is refused while any item would be skipped by the player. `canonicalMediaPath` is restated
byte for byte (`ExtrasRules.CanonicalMediaPath`). `EditorD073ExtrasAuthoringTests` feeds 17 documents
(every rule, boundary lengths in bytes, wrong types, repeated ids, 257 items, files the engine ignores) to the
managed rules and to the native engine (`RowlEngine_GetExtrasJson` through `NativeProfileBridge`) and
requires the same loaded items.

**Project lint** (`ExtrasLint`, option `CheckExtras`): an enabled `unlock` component with an unknown
kind, an empty id, or an id outside the loaded catalog of its kind (or no usable catalog at all) is an
error; catalog items the engine would skip, a repeated id and an unusable file are errors; a catalog
image / thumbnail / track missing on disk uses the node missing-asset level (error, with the asset path);
an item nothing can open is a warning ("the player can never open it"): a gallery image no enabled
background or character sprite shows and no unlock names, a music track no enabled audio component plays
as BGM and no unlock names, an achievement without an unlock component. Catalog images and tracks
count as referenced for the unused-asset rule. `samples/kayip_tablo` raises none of these.

The editor `PlayerWindow` has no Ekstralar screens (the packaged native player has them);
see `docs/CAPABILITY_MATRIX.md` and `docs/PLAYER_RELEASE_PARITY.md`.
