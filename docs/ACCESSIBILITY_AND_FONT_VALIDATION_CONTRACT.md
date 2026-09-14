# Accessibility & Font Validation Contract (Faz 3 Dilim 5)

Canonical reference for player accessibility settings and the font
license/glyph gate. Sources of truth are the implementations named
below; when this document and code disagree, code wins and this
document must be patched.

## 1. Profile settings

| Key | Type | Default | Rule |
|---|---|---|---|
| `text_scale` | float | `1.0` | Snapped onto `{1.0, 1.25, 1.5}` by `PlayerProfile.Sanitized()` (`SnapTextScale`; non-finite → `1.0`). |
| `high_contrast` | bool | `false` | Dark glyph outline pass at render time. |
| `reduced_motion` | bool | `false` | Camera shake becomes a no-op; screen flash is suppressed. |

- Keys persist in `player-profile.json` via `PlayerProfileStore`
  (additive: legacy files without the keys load with defaults).
- Preferences UI (`PlayerPreferencesView`): text-size combo bound to
  `AvailableTextScales`, two check boxes. `SavePreferences` applies
  volumes + accessibility and persists; the player constructor applies
  both so a saved profile takes effect on open.
- Input remap compatibility (`PlayerInputMapper`, additive):
  `T` → `CycleTextScale` (1.0 → 1.25 → 1.5 → wrap),
  `H` → `ToggleHighContrast`. Ctrl/Shift/Alt chords stay reserved for
  the editor; custom binding tables override the defaults as before.
  Reduced motion is preferences-only by design (no accidental key).

## 2. Native application (no engine.cpp / window.cpp growth)

- `FontRenderer` owns `textScale` (clamped `[1.0, 2.0]`, non-finite
  resets to `1.0`) and `highContrast`. Every public shaping /
  measurement / render entry scales its own font size exactly once;
  the shape cache keys the scale, so switching steps never serves a
  stale layout. Dialogue, typewriter and HUD paths apply it with zero
  call-site changes. High contrast stamps a dark 8-neighbourhood halo
  behind each glyph texel, preserving author `<color>` choices.
- `Camera2D` owns `reducedMotion`: while set, `shake` /
  `shakePreset` / `shakeWithProfile` reset offsets to zero and report
  not-shaking, covering story presets, C-API triggers and gameplay.
  Enabling mid-shake freezes motion immediately.
- Screen flash consults the same camera flag in `c_api_render.cpp`;
  both `TriggerScreenFlash*` entry points return early while reduced
  motion is on.
- C ABI (`c_api_accessibility.cpp`, additive):
  `SetTextScale / SetHighContrast / SetReducedMotion` plus
  `GetTextScale / IsHighContrast / IsReducedMotion`; dead handles are
  ignored and report defaults. Capability
  `ROWL_ENGINE_CAPABILITY_ACCESSIBILITY = 1024`.
- Managed bridge: `NativeBridge` P/Invoke → `EngineHost`
  setters → `IPlayerEngine` → `PlayerViewModel.ApplyAccessibility`
  (pure helper `AccessibilityService.ApplyToEngine`, null-safe).

## 3. Font license & glyph gate (`tests/test_font_license_glyph.py`)

- Discovers `Assets/fonts/**` and `samples/*/Assets/fonts/**`
  (byte-identical copies checked once).
- License: name-table ID 13/14 mentioning OFL/SIL, or a
  `LICENSE*`/`*OFL*` sibling — else a WARNING diagnostic.
- Coverage: every character used in shipped locale catalogs plus the
  alphabets required by manifests (`tr` adds `çÇğĞıIiİöÖşŞüÜâÂêÊîÎôÔûÛ`;
  typographic punctuation always required) must exist in each font —
  else a WARNING naming `U+XXXX`, the character and the catalogs using
  it. RTL/CJK sets are enforced only when a manifest lists those
  locales.
- Exit `1` on ERROR (no fonts, corrupt font), `0` with WARNING lines
  otherwise; missing `fontTools` degrades to SKIP. Registered as CTest
  `rowl_font_license_glyph_tests` (16 tests total).

## 4. Faz 3 closing matrix

- `tests/test_accessibility.cpp` (CTest `rowl_native_tests`):
  scale ratio ≈ step with identical reveal segmentation,
  clamp/NaN rules, cache freshness, halo coverage gain, shake
  suppression + resume, C-ABI roundtrip + null-handle guards +
  capability bit, flash suppression, TR/EN `second_signal` catalogs
  shaped at 24/32 px × 1.0/1.5x with every line ≤ 1760 px,
  Arabic/Hebrew/CJK/combining/ZWJ fixtures with scalar-coverage and
  width invariants (graceful skip when a system font is absent).
- `tests/test_font_license_glyph.py`: license + coverage gate.
- `editor/Tests/EditorAccessibilitySlice5Tests.cs` (xUnit):
  snapping, sanitize, store roundtrip + legacy fallback, engine
  application, VM start/save/command flows, mapper defaults + remap,
  capability value.
- Incidental fix: `FontRenderer::loadFontFromMemory` now probes all
  faces of font collections (`.ttc`), so CJK system fonts load.

## 5. Explicitly out of scope

Screen-reader narration, per-locale font fallback chains, subtitle
positioning options, and photosensitivity certification.
