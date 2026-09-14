# Enriched Save Slots & Semantic Input (Faz 2 Dilim 4)

Sources of truth are the implementations named below; when this document
and code disagree, code wins and this document must be patched.

## 1. Slot metadata (`SaveMetadata`, display-only)

`engine/include/rowl/state/game_state.hpp` carries new `GameState`
fields: `savedAt`, `playtimeSeconds`, `chapterId`, `chapterTitle`,
`summary`, `thumbnailPng` (+ dimensions). They never affect simulation,
rewind or migration.

- `saved_at`: ISO-8601 UTC written at serialize time (`savedAt` is
  decoded back for display). The legacy unix `timestamp` is kept.
- `playtime_seconds`: accumulated in `Engine::step` while playing and
  unpaused, stamped by `withSaveMetadata` at save, restored on load.
- `chapter_id`/`chapter_title`: resolved from the story document at save
  (`""` for v4 graphs without chapters).
- `summary`: last backlog dialogue, UTF-8-safe truncated to 320 bytes.
- `thumbnail_png_base64`: current framebuffer downscaled to 320 px wide
  (box average) and PNG-encoded dependency-free (stored deflate blocks),
  then base64. Empty when no pixels were available — never a failure.

Decode is tolerant: every key optional (legacy → defaults), finite
non-negative playtime, text caps (1 KiB ids/titles, 4 KiB summary),
base64 must decode and stay ≤ 1 MiB. No format version bump: old
readers ignore the keys, new readers default them.

## 2. Slot metadata C API (additive)

`RowlEngine_GetSaveSlotMetadataJson` (caller-buffer) reads one slot file
without touching the live story: slot, saved_at, playtime_seconds,
chapter, summary, thumbnail dims, has_thumbnail, thumbnail_png_base64.
Missing → FILE_NOT_FOUND, unreadable → PARSE_ERROR, 0–100 dışı →
INVALID_ARGUMENT. Gated by `ROWL_ENGINE_CAPABILITY_SAVE_METADATA (32)`.

## 3. Semantic input (`editor/Services/PlayerInputMapper.cs`)

Device-independent `PlayerInputCommand`: Advance, CancelBack,
TogglePause, ToggleAuto, ToggleSkip, OpenBacklog, QuickSave, QuickLoad.
Sources: Avalonia `Key`, `PlayerPointerButton`, positional
`PlayerGamepadButton`, `PlayerTouchGesture`. Unmapped → null.
Ctrl/Shift/Alt chords never map (editor shortcuts stay disjoint).

Default map (pinned by tests, remappable via `PlayerInputBindings`):

| Command | Key | Pointer | Gamepad | Touch |
|---|---|---|---|---|
| Advance | Space, Enter | Left | South | Tap, SwipeForward |
| CancelBack | Backspace | Right | East | – |
| TogglePause | Escape, P | – | Start | – |
| ToggleAuto | A | – | West | – |
| ToggleSkip | S | – | North | – |
| OpenBacklog | B | – | LeftShoulder | SwipeBack |
| QuickSave | F5 | – | RightShoulder | – |
| QuickLoad | F9 | – | Back | – |

Native parity notes: native Esc/P = pause, Space/Enter = advance,
F5/F9 = quick save/load (same); native Backspace/Z = rewind stays a
native-only event (no managed command in this slice). No
MainWindowViewModel wiring: the player shell binds these later.

## 4. Tests

- Native `test_game_state`: metadata round-trip, legacy defaults,
  malformed/oversized/negative rejection; PNG magic + downscale cap,
  base64 round-trip + rejection, ISO-8601 shape, UTF-8 truncation.
- Native `test_c_api_contract`: capability bit, error-code matrix,
  end-to-end slot metadata payload (chapter/summary/saved_at/playtime/
  PNG-magic thumbnail) and live-story isolation.
- xUnit `EditorPlayerLoopSlice4Tests` (5): keyboard/pointer/gamepad/
  touch matrix, unknown → null, chords → null, binding overrides.
