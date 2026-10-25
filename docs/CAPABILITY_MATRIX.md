# Capability and Node-Component Matrix

Status reviewed: 2026-10-25 (D07-3, on top of D01-D07-2)
Scope: every `ROWL_ENGINE_CAPABILITY_*` bit and every node-component JSON key, followed from the
editor field to the native engine to the packaged player (`RowlGame`).

Rules of this document:

- Every claim was checked against the code at the commit named in the git history; where this
  document and the code disagree, **code wins** and this file must be patched.
- "Packaged-player proof" names a test that really runs the packaged player (`rowl_player` /
  `RowlGame`) or - when the packaged player is not involved - says so. A row with no test says
  **no proof**; nothing is claimed that no test locks.
- The editor's preview and the packaged player run the same native engine
  (`EngineHost` / `OffscreenRuntimeWorker` -> `NativeBridge` -> `RowlEngineCore`), so a component
  key never behaves differently between the two; only the surrounding player UX (title, backlog,
  auto/skip drivers) exists twice (C# `PlayerViewModel` in the editor, native `engine_player_drivers`
  in the packaged player).

Status words:

| Status | Meaning |
| --- | --- |
| full | Author can set it in the editor, the engine reads it, a test locks it |
| partial | Works, but one link of the chain is missing (no editor field, or no test through the packaged player) |
| native-only | Engine implements it; there is no editor field (only hand-written JSON / C API) |
| authoring-time only | The editor uses it while authoring and bakes a result; the runtime does not read the key |
| unused | Written or collected, but nothing consumes it |

## 1. Capability bits (`RowlEngine_GetCapabilities`)

The editor reads exactly one bit at run time: `PLAYER_PROFILE`
(`editor/Src/Native/NativeProfileBridge.cs`). Every other row has no bit-driven editor decision;
the editor mirrors a few values as constants (`LocalizationService`, `AccessibilityService`,
`MarkupParser`) and hides nothing based on a bit.

| Capability (value) | Editor surface | JSON key / API | Native | Packaged-player proof | Status |
| --- | --- | --- | --- | --- | --- |
| `RESULT_CODES` (1) | `NativeBridge` result enum | `RowlEngine_ResultCode` returns | `c_api_*.cpp` | indirect: every `playerQueryText` call in the player; `rowl_demo_first_light_packaged` | full |
| `CALLER_BUFFERS` (2) | `NativeBridge` string marshalling | `*Utf8(buffer, size, &required)` | `copyUtf8ToCaller` | indirect: `rowl_demo_first_light_packaged` (identity / profile blocks) | full |
| `USER_DATA_DIRECTORIES` (4) | - | `GetSaveDirectoryUtf8`, `GetProfileDirectoryUtf8` | `game_data_layout.cpp`, `platform/user_data_directories` | `rowl_packaged_game_isolation`, `rowl_game_data_packaged` | full |
| `GRAPH_VNEXT` (8) | Inspector chapter / groups | `format_version` 5, `chapter_id`, `chapters[]`, `groups`, `subgraphs` | `story_graph_vnext.cpp` | `rowl_player_chapter_prefetch_packaged` (packaged v5 graph with two chapters); native `test_graph_vnext.cpp` | full |
| `PLAYER_LOOP` (16) | `PlayerViewModel` read tracking | `GetActiveDialogueContentIdsJson`, `Profile*` read set | `engine.cpp`, `player_profile.cpp` | `rowl_demo_first_light_packaged` (smoke: first line marked read, `read_count`) | full |
| `SAVE_METADATA` (32) | `PlayerSaveLoadView` | slot JSON `chapter_id`, `chapter_title`, `summary`, thumbnail | `engine.cpp`, `game_state.cpp`, `save_metadata.cpp` | none through the packaged player; native `test_chapter_boundary_persistence.cpp`, `test_game_state.cpp` | partial (no packaged proof) |
| `PLAYER_CHOICES` (64) | Choice component | `SelectChoice`, `GetChoices*` | `engine.cpp` | none through the packaged player; native `test_choice_input_guard.cpp` | partial (no packaged proof) |
| `LOCALIZATION` (128) | `LocalizationService` | `locales/*.json` catalogs, `SetLocale` | `localization_manager.cpp` | `rowl_demo_second_signal_packaged` checks the catalog data only; runtime switching is native `test_localization.cpp`, `test_player_ui_language.cpp` | partial (no packaged proof of switching) |
| `RICH_TEXT_MARKUP` (256) | `MarkupParser` (editor mirror) | `<color=...>`, `<b>` in dialogue text | `markup_parser.cpp` | none through the packaged player; native `test_markup_parser.cpp` | partial (no packaged proof) |
| `TEXT_SHAPING` (512) | - | automatic (HarfBuzz / FriBidi) | `text_shaper.cpp` | none through the packaged player; native `test_text_shaping.cpp` | native-only |
| `ACCESSIBILITY` (1024) | `AccessibilityService` | `SetTextScale`, contrast, reduced motion | `c_api_accessibility.cpp`, `engine_pause_prefs.cpp` | none through the packaged player; native `test_accessibility.cpp`, `test_player_preferences_menu.cpp` | partial (no packaged proof) |
| `LONG_AUDIO_CONTRACT` (2048) | `AudioStreamingBadge` | header probe + warning | `audio_engine.cpp` | `rowl_demo_second_signal_packaged` validates the long-audio fixture data; native `test_audio_engine.cpp` | partial |
| `CAMERA_ROTATION_IGNORED` (4096) | Camera rotation control is disabled (`IsEnabled=False`, hard-coded, not driven by the bit) | `camera.rotation` | `Camera2D::setRotation` is a no-op (docs/DATA_FORMATS_AND_MIGRATION.md section 8) | none through the packaged player; native `test_camera_and_transition_pipeline.cpp` | unused (rotation is deliberately ignored; the key is a dead store) |
| `AUDIO_STREAMING` (8192) | `AudioStreamingBadge` / service | automatic for long OGG BGM | `audio_engine.cpp` | none through the packaged player; native `test_audio_streaming.cpp`, `test_chapter_boundary_persistence.cpp` (streamed BGM across a chapter boundary, save / load / rewind) | partial (no packaged proof) |
| `AUDIO_MIXER_POLYPHONY` (16384) | Pause menu volumes (native) | bus gains, SFX polyphony, fades | `audio_engine.cpp` | none through the packaged player (preferences are exercised by the smoke profile write only); native `test_audio_mixer.cpp` | partial |
| `CHARACTER_LAYERS` (32768) | Character component layers | `character.layers{body,face,outfit,accessory}` | `character_layers.cpp` | none through the packaged player; native `test_character_layers.cpp` | full in editor + native (no packaged proof) |
| `PREFETCH_CHAPTERS` (65536) | Play Mode only: `PrefetchChapterFeedService` | `json/chapters/*`, `PrefetchChapterAssets`, `PumpPrefetch`, `FeedChaptersFromVfs` | `prefetch.cpp`, `chapter_loader.cpp`, `c_api_prefetch_chapters.cpp` | `rowl_player_chapter_prefetch_packaged` (packaged + loose); native `test_prefetch_chapters.cpp`, `test_chapter_boundary_persistence.cpp` | full (D06-3: packaged player prefetches; warm-up only) |
| `CONVERTER_PROVENANCE` (131072) | Media converter service | `<asset>.rowlconv.json`, manifest `converted_from` | `c_api_provenance.cpp` | `rowl_converter_tool_tests` (tools), native `test_converter_provenance.cpp`; not a player feature | native-only (tooling) |
| `MSDF_RENDER` (262144) | - | shader availability | `msdf_renderer.cpp` (only when `ROWL_GPU_MSDF_SHADER_AVAILABLE`) | `rowl_player_gpu_msdf_smoke` (only with Xvfb + Vulkan software driver) | native-only |
| `TRANSITION_REPERTOIRE` (524288) | D06-2: `TransitionKindCatalog` lists all 13 kinds from `GetSupportedTransitionKindsJson` | `transition.kind` | `transition_manager.cpp` | none through the packaged player; native `test_e2a_transition_spritesheet_red_probe.cpp` (`e2a_repertoire`), editor `EditorD062AuthoringSurfaceTests` | partial (no packaged proof) |
| `SPRITE_SHEET` (1048576) | none | `character.sheet_cols/rows/fps/loop/start` | `engine.cpp` (character branch) | none through the packaged player; native `test_e2a_transition_spritesheet_red_probe.cpp` | native-only |
| `CHARACTER_FX` (2097152) | none | C API tween / emphasis / lip-sync helpers | `c_api_character_fx.cpp` | native `test_e2b_character_fx_red_probe.cpp` (`e2b_character_fx_red_probe`); no node key reads it | native-only |
| `GAME_IDENTITY` (4194304) | `NativeBridge` identity calls; build writes `rowl/game.json` | `project.rowlproj project_uuid`, `rowl/game.json` | `platform/game_identity.cpp` | `rowl_game_data_packaged`, `rowl_packaged_game_isolation`, `rowl_demo_*_packaged` | full |
| `GAME_DATA` (8388608) | `NativeBridge` | per-game data folder, legacy save migration | `game_data_layout.cpp` | `rowl_game_data_packaged`, `rowl_packaged_game_isolation` | full |
| `PLAYER_PROFILE` (16777216) | `NativeProfileBridge` (the only bit the editor reads) | `player-profile.json` (v1) | `player_profile.cpp` | `rowl_demo_first_light_packaged` (smoke profile write + flush), `rowl_packaged_game_isolation` | full |
| `TITLE_SCREEN` (33554432) | `NativeBridge.OpenTitleScreen` | pause-menu mode 3 / 4 | `engine_title_menu.cpp` | `rowl_demo_first_light_packaged` ("Package smoke title: ok") | full |
| `BACKLOG_REPLAY` (67108864) | Editor player backlog panel (C#) | history JSON `voice`, `ReplayBacklogVoice`, `OpenBacklog` | `engine_backlog.cpp` | none through the packaged player; native `test_backlog_replay.cpp`, `test_backlog_screen.cpp` | partial (no packaged proof) |
| `PLAYER_AUTO_SKIP` (134217728) | Editor player auto / skip drivers (C#, separate implementation) | `SetAutoMode`, `SetSkipActive`, profile `auto_*`, `skip_mode` | `engine_player_drivers.cpp` | none through the packaged player; native `test_auto_skip.cpp`, `test_auto_skip_ui.cpp` | partial (no packaged proof; two driver implementations, no parity test) |
| `EXTRAS_UNLOCKS` (268435456) | D07-3: `unlock` component card, extras catalog editor window ("..." menu > Extras catalog), project lint, `NativeProfileBridge.GetExtrasJson` (parity check only); native title "Ekstralar" pages + achievement toast since D07-2 (`RowlEngine_OpenExtras`, pause-menu modes 6-9). The editor `PlayerWindow` has **no** Ekstralar screens (the packaged native player has them) | `Assets/json/extras.json`, profile `unlocks`, `RowlEngine_GetExtrasJson` / `ProfileUnlock` / `ProfileIsUnlocked` | `extras_catalog.cpp`, `engine_extras.cpp`, `engine_extras_menu.cpp`, `window_extras_overlay.cpp`, `player_profile.cpp`, `c_api_extras.cpp` | none through the packaged player; native `test_extras_unlocks.cpp`, `test_extras_screens.cpp` (includes the shipped `kayip_tablo` catalog); editor `EditorD073ExtrasAuthoringTests` (managed catalog rules agree with the native engine on 17 documents) | native screens + editor authoring (no packaged proof); editor PlayerWindow: Ekstralar yok (native oyuncuda var) |
| `UI_THEME` (536870912) | none yet (the editor "Oyuncu Arayuzu" window is T3; the dialogue / choice components still serialise every field, so a theme default does not show on editor-saved nodes until T4) | `Assets/json/ui_theme.json`, `RowlEngine_GetUiThemeJson` / `ReloadUiTheme` / `SetUiThemeJson` | `ui_theme.cpp`, `engine_ui_theme.cpp`, `window_skin.cpp` (T2 skins), `window_pause_overlay.cpp`, `window_extras_overlay.cpp`, `window.cpp`, `c_api_ui_theme.cpp` | none in the editor; native `test_ui_theme.cpp`, `test_ui_skin.cpp`; packaged `test_ui_theme_packaged.py` | native + packaged proof; no editor surface |

## 2. Node components (JSON `components[].type` and `data` keys)

Native reads the keys in `Engine::updateSceneFromComponents` (`engine/src/core/engine.cpp`) and the
helpers it calls. "Editor" means the field written by `editor/ViewModels/Components/*`.

| Component / key | Editor field | JSON key | Native read | Packaged-player proof | Status |
| --- | --- | --- | --- | --- | --- |
| `dialogue` (speaker, text, box geometry, typewriter, colors, borders, blip sounds, auto advance) | `DialogueComponentViewModel` | `speaker`, `dialogue`, `x`, `y`, `width`, `height`, `scale`, `typewriter_enabled`, `text_speed`, `font_size`, `speaker_font_size`, `text_color`, `speaker_color`, `text_alignment`, `box_opacity`, `box_color`, `border_color`, `border_thickness`, `corner_radius`, `custom_box_texture`, `typewriter_sound`, `voice_blip_*`, `auto_advance`, `auto_advance_delay` | yes | `rowl_demo_first_light_packaged` / `rowl_demo_second_signal_packaged` render a packaged dialogue frame | full |
| `dialogue.content_id` | assigned by the editor on save (identity of a line) | `content_id` | yes (read marks, backlog, localization) | `rowl_demo_second_signal_packaged` validates unique ids; read-mark smoke in `rowl_demo_first_light_packaged` | full |
| `speaker`, `dialogue_box` (legacy aliases) | map to `DialogueComponentViewModel` | `speaker` / `dialogue` / `content_id`; `x`, `y`, `width`, `height` | yes | none | full (legacy; no packaged proof) |
| `background` | `BackgroundComponentViewModel` | `texture`, `x`, `y`, `width`, `height`, `rotation`, `parallax_x`, `parallax_y`, `opacity` | yes | `rowl_demo_*_packaged` (frame from VFS) | full |
| `background.scale` | written | `scale` | **not read** (verified: no read in the background branch) | none | unused (editor layout value only) |
| `character` (sprite, geometry, rotation, scale, blip) | `CharacterComponentViewModel` | `sprite`, `x`, `y`, `width`, `height`, `rotation`, `scale`, `scale_x`, `scale_y`, `voice_blip_sound`, `voice_blip_pitch`, `voice_blip_variance`, `voice_blip_cadence` | yes | `rowl_demo_*_packaged` | full |
| `character.layers` | slots body / face / outfit / accessory | `layers{...}` | yes (`character_layers.cpp`; layers win over `sprite`) | none (native `test_character_layers.cpp`) | full (no packaged proof) |
| `character.expressions` / `slots` / `name` | expression editor | `expressions[{name,slots}]` | **not read**: the editor bakes the chosen expression into `layers` while authoring; the preset library exists only in the C API | none | authoring-time only (the array is dead data at run time; the runtime cannot switch expression) |
| `character.position`, `character.maintain_aspect_ratio` | written | `position`, `maintain_aspect_ratio` | **not read** (verified: no read anywhere in `engine/src`) | none | unused (editor placement helper) |
| `character.sheet_cols/rows/fps/loop/start` | none | `sheet_*` | yes (sprite sheet) | none (native `test_e2a_transition_spritesheet_red_probe.cpp`) | native-only |
| `audio.bgm_track`, `bgm_transition`, `bgm_transition_duration_seconds`, `volume`, `dsp_filter` | `AudioComponentViewModel` | same keys; project default `default_bgm_transition(_duration_seconds)` in `project.rowlproj` | yes | none through the packaged player; native `test_chapter_boundary_persistence.cpp` (BGM across chapters, save / load / rewind) | full in editor + native (no packaged proof) |
| `audio.sfx_track` | `AudioComponentViewModel` | `sfx_track` | yes (once per real node entry) | none through the packaged player; native `test_chapter_boundary_persistence.cpp`, `test_backlog_replay.cpp` | full in editor + native (no packaged proof) |
| `audio.voice_track` | D06-2: "Voice" field + file picker | `voice_track` | yes (Voice channel, history row, no blips) | none through the packaged player; native `test_backlog_replay.cpp`, editor `EditorD062AuthoringSurfaceTests` | full in editor + native (no packaged proof) |
| `audio.ambience_track` | none | `ambience_track` | **no node playback**; read only by the prefetch collector (`prefetch.cpp`). Ambience / UI buses are volume sliders only (`c_api_audio.cpp`; `docs/AUDIO_MIXER_CONTRACT.md` removed per-node ambience beds on purpose) | none | unused (kept in the collector because `test_prefetch_chapters.cpp` locks 16 assets / `audio-ambience`; removing it is a contract change) |
| `choice.options[]` (text, enabled, condition, geometry, style colors, images) | `ChoiceComponentViewModel` | `option_id`, `text`, `enabled`, `condition`, `x`, `y`, `width`, `height`, `font_size`, `opacity`, `border_thickness`, `corner_radius`, `text_color`, `background_color`, `hover_color`, `border_color`, `text_alignment`, `font_family`, `normal_image`, `background_image` | yes | none through the packaged player; native `test_choice_input_guard.cpp` | full in editor + native (no packaged proof) |
| `choice.target_node_id`, `style_preset`, `anchor`, `layout`, `padding`, `title`, `hover_image`, `pressed_image`, `disabled_color`, `disabled_image` | written | same | **not read** (routing is `next_nodes[].option_id`; verified: no read in `engine/src`) | none | unused (editor layout / bookkeeping) |
| `variable` | `VariableComponentViewModel` | `key`, `value`, `operation` (`set` / `add`) | yes (real node entry only; load / rewind restore saved variables) | none through the packaged player; native `test_chapter_boundary_persistence.cpp`, `test_condition_component.cpp` | full in editor + native (no packaged proof) |
| `condition` | `ConditionComponentViewModel` (+ lint) | `expression`, `fail_target_node_id` | yes since D06-1 (real entry; Lua expression; invalid target warns once) | none through the packaged player; native `test_condition_component.cpp`, `docs/DATA_FORMATS_AND_MIGRATION.md` section 11 | full in editor + native (no packaged proof) |
| `unlock` | D07-3: `UnlockComponentViewModel` card (kind picker, id picked from the project catalog, free text without one; an id the catalog lacks is kept and flagged); not pushed to the edit-time preview (selecting a node must not write the profile) | `kind` (`cg` / `music` / `achievement`), `id` | yes since D07-1 (real entry only; id must be in `extras.json`, else one warning per node; load / rewind / skipped node unlock nothing) | none through the packaged player; native `test_extras_unlocks.cpp`, editor `EditorD073ExtrasAuthoringTests` | full in editor + native (no packaged proof) |
| any component type the editor has no view model for | D07-3: `UnknownComponentViewModel` keeps `type`, `id`, `enabled` and the raw `data` through open + save (before D07-3 the loader dropped it and the next save deleted it) and shows a read-only card | as written by the other tool | n/a | editor `EditorD073UnknownComponentTests` | kept verbatim |
| `script` | `ScriptComponentViewModel` | `path`, `code` | yes (sandboxed Lua) | none through the packaged player; native `test_lua_sandbox.cpp` | full in editor + native (no packaged proof) |
| `camera` (position, zoom, pan, easing, shake) | `CameraComponentViewModel` | `x`, `y`, `zoom`, `pan_duration`, `zoom_duration`, `easing`, `shake_*` | yes | none through the packaged player; native `test_camera_and_transition_pipeline.cpp` | full in editor + native (no packaged proof) |
| `camera.rotation` | control disabled | `rotation` | read, then ignored on purpose | none | unused (see `CAMERA_ROTATION_IGNORED`) |
| `transition` (kind, duration, color, flash, tint, vignette) | `TransitionComponentViewModel`; D06-2 lists all 13 native kinds | `kind`, `duration`, `color`, `flash_*`, `tint_*`, `vignette_*` | yes (13 kinds: crossfade, fade_black, fade_white, fade_color, wipe_left, wipe_right, dissolve, push_*, iris_*) | none through the packaged player; native `test_camera_and_transition_pipeline.cpp`, `test_e2a_transition_spritesheet_red_probe.cpp` | full in editor + native (no packaged proof) |

### Reading the table

- **Packaged-player proof today** is limited to: the offscreen smoke (a frame from the packaged
  graph, profile write, title screen) in `rowl_demo_first_light_packaged` /
  `rowl_demo_second_signal_packaged`, per-game data isolation in `rowl_packaged_game_isolation` /
  `rowl_game_data_packaged`, and (D06-3) the chapter / prefetch path in
  `rowl_player_chapter_prefetch_packaged`. Everything else is proven by the native `rowl_tests`
  suite on the same engine code, not through the packaged binary. That is stated per row rather
  than assumed.
- The sample `samples/kayip_tablo` uses only background, dialogue, character (body only),
  variable, transition, audio (BGM crossfade), choice, camera, one script and (D07-1) four
  `unlock` achievements at its endings, with an `extras.json` of 10 CG, 5 music items and 4 achievements. It does **not**
  exercise chapters, `condition`, `voice_track`, `ambience_track`, `sfx_track`, expressions or
  sprite sheets, so it cannot serve as evidence for those rows.
- Editor-only keys that the engine never reads (`background.scale`, `character.position`,
  `character.maintain_aspect_ratio`, the choice layout / style keys, `character.expressions`) are
  harmless: unknown keys are tolerated on load. They are listed so an author does not expect an
  in-game effect.

### Open items found while building this matrix

1. `audio.ambience_track` has no playback and no editor field; it only inflates the prefetch
   list. Decision needed: implement node ambience (loop + fade) or drop the key from
   `collectNodeAssets` together with its test.
2. `character.expressions` is dead data at run time. Decision needed: keep the "bake at author
   time" rule (and say so in the editor) or add a runtime expression selector.
3. Rewind granularity (fixed in F1): the player's Backspace / swipe-back now rewinds one NODE
   (`Engine::rewindNodes`, `RowlEngine_RewindNodes`): it lands on the last state link of the previous
   node, i.e. after all of that node's entry commits (variables, script, music, history line).
   `RowlEngine_Rewind(steps)` stays the raw state-link step for hosts that address single links
   (the editor's timeline); a node with entry effects owns several links there.
4. Auto / Skip exist twice (C# editor drivers, native player drivers) with no parity test.
