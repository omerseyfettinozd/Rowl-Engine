# Player Shell & Screens (Faz 2 Dilim 5 — Faz 2 kapanışı)

The standalone visual-novel interface: title → play → pause → save/load
→ backlog → preferences → exit, driven without editor buttons once the
player window is open (launcher: "🎮 Oyuncu" button,
`MainWindowViewModel.OpenPlayerWindow`).

## 1. Architecture

- `editor/ViewModels/Player/PlayerViewModel.cs` — integration brain.
  Owns `PlayerStateMachine`, `PlayerLoopService`, `AutoPlayDriver`,
  `SkipDriver`, `PlayerInputBindings` and `PlayerSaveSlotsViewModel`,
  and talks to the engine only through `IPlayerEngine` (production:
  `EngineHostPlayerAdapter`, tests: fakes). Every flow below is xUnit
  covered over the fake.
- `editor/Views/Player/` — `PlayerWindow` + `PlayerShellView`
  (state-driven panels, 30 Hz driver timer, keyboard funnel) plus one
  control per screen: Title / HUD / Pause / SaveLoad / Preferences /
  Backlog. Views carry no decisions; `PngBytesToBitmapConverter` decodes
  slot thumbnails on the UI thread.
- `EngineHost` gained only thin wrappers (pause, choice count/labels/
  option ids, slot metadata, `SelectChoice`); engine.cpp/window.cpp are
  untouched. One additive C API pair for stable choice option ids under
  `CAPABILITY_PLAYER_CHOICES (64)`.

## 2. Flows

- Title: New Game (reset + play), Continue (latest `saved_at` slot),
  Load picker, Preferences, Exit (confirm → window closes via
  `ExitRequested`). Title reaches Load/Preferences/Exit directly; Save
  stays pause-mediated per the state machine.
- Playing HUD: dialogue + choices (option id → `SelectChoice`, tracked
  like an advance) + Auto/Skip/Log/Save/Load/Menu buttons + full
  keyboard map (`PlayerShellView` → `PlayerInputMapper`).
- Overlays pause the sim (`SetPaused`); Playing resumes it. Pending
  choices halt skip/auto but block no overlay.
- `Tick(dt)`: engine step → text-stability tracking (a line counts as
  complete once stable across two ticks; no native typewriter query
  exists yet) → skip step (read lines) else auto wait (reading time ∨
  voice; voice length has no native query yet, contributes 0) → tracked
  advance. Menus and choices halt all drivers.
- Save/Load picker: 12-slot pages over 100 slots, metadata + date +
  thumbnail per occupied slot; corrupt thumbnails degrade to empty
  (counted), never crash. Preferences persist the profile atomically
  and apply volumes live.

## 3. Boundaries (later work)

- Voice track length for auto timing (native query).
- Profile key remapping UI (bindings shape is ready).
- Thumbnail prefetch/virtualization for pathological slot counts.
- AXAML bindings are build-checked; VM logic is unit-tested (80/80).
  No Avalonia.Headless harness exists in the repo, so no pixel-level UI
  test was added (adding a framework solely for this slice would
  violate project test policy).
