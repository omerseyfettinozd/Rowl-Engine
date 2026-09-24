# Standalone Player Release Parity

Status reviewed: 2026-09-24
Scope: exported native RowlGame versus the Avalonia PlayerWindow available inside the editor.

The exported game is built from engine/src/player/main.cpp and the native
RowlEngineCore C API. It does not ship the Avalonia PlayerWindow. Both surfaces
already play stories and offer save/load, but their surrounding player
workflows are not equivalent.

## Capability matrix

| Player capability | Editor PlayerWindow | Exported RowlGame | Assessment |
| --- | --- | --- | --- |
| Start a new story from a title screen | Title state and New Game command | Loads a graph and enters the native run loop directly | Missing in exported player |
| Continue latest progress | Continue command backed by profile/save discovery | No title-level continue action; quick-load is available during play | Missing as a user-facing flow |
| Load a chosen save | Save/load overlay with paged slots and metadata | Pause menu exposes slots 0–9; F9 loads the active slot | Partial |
| Save a chosen save | Save/load overlay with paged slots and metadata | Pause menu exposes slots 0–9; F5 saves the active slot | Partial |
| Pause, resume, adjust sound/text, exit safely | Pause overlay | Native SDL pause menu, volume/text-speed rows, two-step quit confirmation | Present |
| Story rewind | Player commands and runtime rewind | Backspace/Z rewinds one step | Present |
| Backlog | Scrollable speaker/dialogue history panel | Runtime history and C API JSON snapshot exist; no native backlog screen. Voice replay is absent from both surfaces. | Partial; native UI missing |
| Persistent profile | Versioned atomic profile stores language, read IDs, audio/text/accessibility and auto/skip preferences | No equivalent native player profile load/save flow | Missing in exported player |
| Global auto and read-aware skip | Profile-driven AutoPlayDriver and SkipDriver | Per-node auto_advance exists; no global read-aware skip workflow | Partial |
| Accessibility and language selection | Text scale, contrast, reduced motion, language preference | No equivalent player preferences screen | Missing in exported player |
| Semantic input | Editor player semantic mapper | SDL keyboard/mouse controls; native pause menu handles keyboard/pointer; controller/touch parity is not established here | Partial / needs target proof |

Evidence locations:
- Editor surface: editor/Views/Player/PlayerShellView.axaml,
  TitleScreenView.axaml, PlayerBacklogView.axaml,
  PlayerSaveLoadView.axaml, PlayerPreferencesView.axaml, and
  editor/ViewModels/Player/PlayerViewModel.cs.
- Editor profile: editor/Services/PlayerProfile.cs,
  PlayerProfileStore.cs, and PlayerLoopService.cs.
- Export path: editor/Services/ProjectBuildService.cs stages the native player
  as RowlGame with RowlEngineCore and the packaged project.
- Native surface: engine/src/player/main.cpp documents and enters the native
  player loop; engine/src/core/engine_pause_menu.cpp defines its pause rows;
  engine/include/rowl/c_api.h already exposes dialogue history, save slots,
  quick save/load, and pause-menu state.
- Product gate: docs/PRODUCTIZATION_BASELINE.md and the Phase 2 entry in the
  Rowl Engine productization roadmap.

## P1 release contract

The first external beta must let a player complete the basic visual-novel loop
from the exported package, without opening the editor or using command-line
controls after launch:

1. Launch into a project-branded title screen with New Game, Continue, Load,
   Preferences, and Exit.
2. Start a fresh story, continue the newest valid save, and load or save a
   selected slot. Invalid or empty slots show an actionable message and never
   destroy another save.
3. Open and scroll a bounded backlog while paused or playing. History replay,
   if offered, must not rerun story side effects.
4. Persist player settings independently of save slots: language, volume/text
   preferences, auto state, skip mode, and read IDs. Defaults and damaged or
   unsupported profile behavior must be defined.
5. Auto stops at choices and resets on manual input. Read-only skip advances
   only content already recorded as read; all-skip is an explicit choice.
6. Keyboard and mouse cover every essential flow. Any claimed controller or
   touch support must have its own input and package evidence.
7. Run the acceptance flow from the produced RowlGame package on Linux and
   Windows. Record package identity, OS, inputs, save/profile paths, and result.

## Suggested implementation order

1. Add the title/new/continue flow to the native player host and define how it
   selects the initial graph/state. Reuse existing save-slot and C API paths.
2. Add backlog presentation over the existing bounded dialogue-history API.
3. Connect a persistent native player profile to existing runtime preferences,
   read tracking, and auto/skip behavior. Keep the public C ABI additive if a
   new exported operation is required.
4. Align slot metadata and semantic inputs, then run the package acceptance
   flow on Linux and Windows.

This is the P1 planning and parity-baseline slice. It does not claim that the
native title screen, backlog, or full profile has been implemented.
