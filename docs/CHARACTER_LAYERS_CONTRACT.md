# Character Layers Contract (Faz 5 Dilim 3 — native hat)

Companion to `AUDIO_MIXER_CONTRACT.md` (Faz 5 Dilim 2) and
`AUDIO_STREAMING_CONTRACT.md` (Faz 5 Dilim 1). Those documents are NOT
modified by this slice; their threshold formula, mixer math and ring-buffer
behavior are untouched. Sources of truth are the implementations named
below; when this document and code disagree, **code wins** and this
document must be patched.

Locked decisions: additive-only C ABI (new capability bit `32768`,
`ROWL_ENGINE_CAPABILITY_CHARACTER_LAYERS`, OR-ed into
`GetCapabilities`; bits 1..16384 intact); compositing uses the EXISTING
sprite path (`Window::drawSprite` / `CharacterRenderData`, no new render
backend); `engine.cpp` carries only the playback call-site hook
(`parseComponentData` + `toSpriteDraws`, no new logic there), `window.cpp`,
`SpriteComponent`/story signatures, `docs/AUDIO_*.md`, threshold/formula and mixer semantics are untouched;
short-audio/streaming paths are untouched; C#/editor belongs to another
agent (`EngineHost.cs` zero-diff, adapter-forward pattern reserved).

## 0. Naming note (authoritative ABI)

The authoritative native names — in `engine/include/rowl/c_api.h`,
`engine/src/c_api_character_layers.cpp` and every test — are:

- `RowlEngine_SetCharacterSlotAsset` / `RowlEngine_GetCharacterSlotAssetUtf8`
- `RowlEngine_SetCharacterSlotOpacity` / `RowlEngine_GetCharacterSlotOpacity`
- `RowlEngine_SetCharacterSlotVisible` / `RowlEngine_IsCharacterSlotVisible`
- `RowlEngine_RegisterCharacterPreset` / `RowlEngine_ApplyCharacterExpression`
- `RowlEngine_GetCharacterPresetListJson` / `RowlEngine_GetCharacterDrawListJson`
- `RowlEngine_GetLastCharacterErrorUtf8`

No duplicate ABI was added; no legacy entry point changed signature.
Owners: `engine/include/rowl/scene/character_layers.hpp` +
`engine/src/scene/character_layers.cpp` (pure logic, no render, no I/O).

## 1. Layered character (owner: `CharacterLayers`)

- Four fixed slots: `body` (0) < `face` (1) < `outfit` (2) < `accessory` (3).
  Draw order is a code guarantee (`composeDrawList` iterates indices
  0..3); it is not configurable and not data-driven.
- Each slot carries an independent asset path + opacity + visibility.
  Opacity accepts finite values and clamps to [0,1]; non-finite is
  rejected and the slot is unchanged. Empty asset clears the slot
  (valid, draws nothing, no diagnostic).
- `composeDrawList(resolver)` skips, in order: invisible slots, empty
  slots, zero-opacity slots (all silent), then unresolvable slots (with
  a diagnostic entry). A null resolver means syntax-only resolution.
- "Resolvable" (syntax): non-empty, no NUL byte, at most
  `kMaxCharacterAssetPathBytes` (4096) bytes. Real texture-load failure
  stays where it already is: `window.cpp`'s null-texture guard skips the
  sprite, so a missing file can never crash the frame; the linter/file
  ownership check lives on the C# side (`Validate`).
- `toSpriteDraws(x, y, w, h)` maps the draw list 1:1 onto
  `Window::drawSprite(asset, x, y, w, h, opacity)` records sharing one
  rect — the composition point over the untouched sprite pipeline.

## 2. Expression presets (owner: `CharacterPresetLibrary`)

- A preset is a named `slot → asset` map with a per-slot presence mask.
  Missing slots are left untouched on apply; present-but-empty assets
  clear that slot. Registration replaces an existing name (no duplicates).
- Registration is fail-closed: unknown slot key, non-string value, or
  oversized asset rejects the WHOLE registration (list unchanged).
- `applyExpression` is ATOMIC: all present slots are validated first
  (syntax + resolver); if any fails, NOTHING is applied and the error
  string is filled. Unknown preset name fails without touching layers.
- The preset list lives in memory per engine handle
  (`presetListJson()` → `["happy", ...]`); persistence is C#-side JSON.

## 3. Component JSON migration (owner: `parseComponentData`)

- Optional `layers` key on `character` component data; per slot either a
  string shorthand (`"face": "f.png"`) or an object
  (`{asset, opacity, visible}`). Unknown keys are ignored.
- Legacy files without `layers` keep working: the old `sprite` key falls
  into the body slot. An explicit non-empty `layers.body.asset` wins over
  legacy `sprite`. Absent new keys yield defaults (opacity 1, visible).
- Parsing is atomic: on error `out` is untouched and `outError` is filled.
- `engine.cpp` scene path (`updateSceneFromComponents`, character branch)
  wires the composition point: when component data carries `layers`,
  `parseComponentData` resolves it and `toSpriteDraws` emits one
  `CharacterRenderData` per layer (shared node rect, body<face<outfit<accessory
  order) over the untouched sprite pipeline; without `layers` the legacy
  `sprite` read is byte-identical, and a failed parse falls back to it
  with a warning (covered by `testStoryPlaybackComposesLayers`). Layered
  JSON through legacy `UpdateSceneFromJson` keeps speaker state.
- Preset names are never resolved at runtime: the editor resolves the
  author's expression selection into solved `layers` on the node JSON at
  author time, and playback only composes those `layers`. End to end:
  author picks a preset → node carries `layers` → playback composes.

## 4. Fail-closed matrix (C API)

| Input | Result | State |
|---|---|---|
| dead/null handle | `INVALID_HANDLE` (`0`/`""` carriers) | stale entry pruned, nothing else touched |
| unknown slot (`"tail"`, `"Body"`) | `INVALID_ARGUMENT` | slot unchanged |
| null slot/asset/name/JSON pointer | `INVALID_ARGUMENT` | unchanged |
| input without NUL in 256 KiB + 1 | `INVALID_ARGUMENT` (oversized) | unchanged |
| non-finite opacity | `INVALID_ARGUMENT` | slot unchanged |
| null `outOpacity` / `outRequiredSize` | `INVALID_ARGUMENT` | unchanged |
| undersized caller buffer | `BUFFER_TOO_SMALL` + required size, buffer cleared | unchanged |
| malformed expression JSON | `PARSE_ERROR` + diagnosis | preset list unchanged |
| unknown slot / non-string / oversized asset in preset | `VALIDATION_ERROR` + diagnosis | preset list unchanged |
| unknown preset on apply | `INVALID_ARGUMENT` + diagnosis | layers unchanged |
| oversized preset name (>256 B) | `INVALID_ARGUMENT` | unchanged |

Per-handle diagnosis: every mutating call sets or clears
`GetLastCharacterErrorUtf8`; success clears it.

## 5. Limits

- C API input scan: 256 KiB + 1 (Dilim 1-2 pattern), over → `INVALID_ARGUMENT`.
- Per-asset syntax cap: 4096 bytes (`kMaxCharacterAssetPathBytes`).
- Preset name cap: 256 bytes (`kMaxCharacterPresetNameBytes`).
- Draw-list / preset-list outputs: caller-buffer contract
  (NULL/0 size-query, undersized clears + `BUFFER_TOO_SMALL`).

## 6. Test matrix (owner: `tests/test_character_layers.cpp`)

- Slot order: 4 assets set out of order → draw list is body/face/outfit/accessory.
- Slot skip: resolver rejects one asset → 3 items drawn + diagnostic non-empty.
- Expression atomicity: 3 valid + 1 resolver-broken → apply false, state
  byte-identical, error set; clean apply updates all slots.
- Preset-unknown fail-closed (C++ + C API), registration rejection
  (unknown key / non-string / malformed / oversized) leaves the list unchanged.
- Opacity/visibility: NaN rejected, clamp to [0,1], zero-opacity and
  invisible slots skipped, partial presets leave unmentioned slots untouched.
- C API: bit 32768 set + bits 1..16384 intact; null-handle on all 12
  entries; caller-buffer size-query/undersized/exact; oversized-input
  rejection without state change; draw-list JSON order assertion.
- Migration: legacy `{"sprite":...}` → body slot + defaults; layered parse
  incl. precedence and shorthand; mistyped field atomically rejected;
  layered JSON through legacy `UpdateSceneFromJson` keeps speaker state.
