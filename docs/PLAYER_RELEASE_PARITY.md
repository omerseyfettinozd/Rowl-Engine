# Standalone Player Release Parity

Status reviewed: 2026-10-25 (after D01-D07-3)
Scope: exported native RowlGame versus the Avalonia PlayerWindow available inside the editor.

The exported game is built from engine/src/player/main.cpp and the native
RowlEngineCore C API. It does not ship the Avalonia PlayerWindow. Both surfaces
play stories and offer save/load; since D02-D06 the native player carries the
same surrounding workflow (title, backlog, profile, auto/skip, accessibility),
implemented natively rather than by the editor's C# drivers. Per-component and
per-capability evidence is in [CAPABILITY_MATRIX.md](CAPABILITY_MATRIX.md).

## Capability matrix

| Player capability | Editor PlayerWindow | Exported RowlGame | Assessment |
| --- | --- | --- | --- |
| Start a new story from a title screen | Title state and New Game command | Native title screen (D02): New Game / Continue / Load / Extras (D07-2, only when the game ships an `extras.json`) / Preferences / Exit before the story; `--title-state story` skips it. Labels follow the player language (D05-1) | Present |
| Continue latest progress | Continue command backed by profile/save discovery | Title-level Continue loads the newest valid save (`saved_at`, slots 0-99); corrupt/empty saves are skipped or reported | Present (D02) |
| Load a chosen save | Save/load overlay with paged slots and metadata | Pause menu and title Load page expose slots 0-9 (`kSlotRows = 10`) with date, chapter, summary and thumbnail; F9 loads the active slot | Partial (no paging to slots 10-99; Continue still finds them) |
| Save a chosen save | Save/load overlay with paged slots and metadata | Pause menu exposes slots 0-9; F5 saves the active slot | Partial (same slot-page limit) |
| Pause, resume, adjust sound/text, exit safely | Pause overlay | Native SDL pause menu, two-step quit confirmation; volumes and text speed live on the Preferences page (D05-2) | Present |
| Story rewind | Player commands and runtime rewind | Backspace/Z rewinds one state link (`rewind(1)`); a node with entry effects owns several links, so one press can land on the same node with its effects undone | Present (granularity caveat, see CAPABILITY_MATRIX.md "Open items") |
| Unlockable extras (CG gallery, music room, achievements, toast) | **No screens**: the editor `PlayerWindow` has no Ekstralar pages and no achievement toast. The editor authors the content (D07-3: `unlock` component, extras catalog editor, lint) and the unlocks still land in the profile while it plays | Native title row "Ekstralar" (only when the game ships an `extras.json`) with Gallery / Music room / Achievements pages and a 3 s achievement toast (D07-2); the player records unlocks in `player-profile.json` (D07-1) | Native-only by design (editor PlayerWindow: Ekstralar yok, native oyuncuda var) |
| Backlog | Scrollable speaker/dialogue history panel | Native backlog screen opened from the pause menu (D03-2), bounded history, per-line voice replay that reruns no story side effect (D03-1) | Present |
| Persistent profile | Versioned atomic profile stores language, read IDs, audio/text/accessibility and auto/skip preferences | Native profile store + `RowlEngine_Profile*` (D01c-1); the player applies the stored preferences before the first frame (D01d, D05) | Present |
| Global auto and read-aware skip | Profile-driven AutoPlayDriver and SkipDriver | Native Auto (A) and read-aware Skip (S / Ctrl) drivers with AUTO/SKIP badges and Preferences rows (D04) | Present (two driver implementations, no parity test) |
| Accessibility and language selection | Text scale, contrast, reduced motion, language preference | Native Preferences page: volumes, text speed, auto delay, skip mode, language, text size, contrast, motion (D05-1, D05-2), persisted in the profile | Present |
| Chapters and asset prefetch | Play Mode only (`PrefetchChapterFeedService`) | The player feeds the chapter loader from its own files (package or loose Assets, `json/chapters/*`) and warms the current + next chapter's assets from the frame hook; stale chapter files are ignored (D06-3) | Present (`rowl_player_chapter_prefetch_packaged`) |
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
- Native surface: engine/src/player/main.cpp (loop, preferences, prefetch
  hook), engine/include/rowl/player/chapter_prefetch_driver.hpp (prefetch
  driver, shared with the tests), engine/src/core/engine_pause_menu.cpp,
  engine_title_menu.cpp, engine_backlog.cpp and engine_player_drivers.cpp;
  engine/include/rowl/c_api.h exposes dialogue history, save slots, quick
  save/load, pause-menu state, the profile and the player drivers.
- Tests: native `rowl_tests` (test_title_screen, test_backlog_replay,
  test_backlog_screen, test_native_player_profile, test_auto_skip,
  test_auto_skip_ui, test_player_ui_language, test_player_preferences_menu,
  test_accessibility, test_chapter_boundary_persistence); packaged player:
  `rowl_demo_first_light_packaged`, `rowl_demo_second_signal_packaged`,
  `rowl_packaged_game_isolation`, `rowl_game_data_packaged`,
  `rowl_player_chapter_prefetch_packaged`. Backlog, auto/skip, accessibility and
  preferences are proven by the native suite on the same engine code; the
  packaged-player tests cover the title screen, the profile write, per-game data
  and the chapter / prefetch path.
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

Status of this plan (2026-10-24): steps 1-3 are done (D02 title flow, D03
backlog, D01c/D01d/D04/D05 profile, auto/skip and preferences wiring). Step 4
is partly open: slot pages still show slots 0-9, controller/touch parity is not
claimed, and the acceptance flow of the P1 contract (item 7) has only a Linux
offscreen smoke on record (`rowl_demo_*_packaged`); a Windows run is not
recorded here.
