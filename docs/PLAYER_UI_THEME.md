# Player-interface theme: `Assets/json/ui_theme.json` (T1, format 1)

A game restyles the UI the engine itself draws (title screen, pause menu, save pages, Preferences,
History, Ekstralar pages, achievement toast, AUTO/SKIP badges) and sets project-wide DEFAULTS for the
dialogue box and the choice buttons in one file. The file is read from the project VFS (loose project and
`.rowlpkg` alike; candidates `json/ui_theme.json`, `ui_theme.json`, `Assets/json/ui_theme.json`,
`Assets/ui_theme.json`) when `RowlEngine_SetProjectDirectory` mounts the project. The packager takes the
whole `Assets/` folder, so the file travels in the package with no extra step.

**No file, or a file without a key, means the look the engine had before the theme existed, pixel for
pixel** (every default below is the former constant). The theme is not part of the frame hash; changing
it invalidates the cached frame and the baked AUTO/SKIP/toast state.

Not covered yet (later slices): images and 9-slice frames, layout (positions, sizes, row pitch), label
texts, which rows are listed, durations, sounds, and the editor window.

## Rules

- Colours are `"#RRGGBB"` or `"#RRGGBBAA"` (nothing else; `#RGB` and unprefixed forms are refused).
- Numbers must be finite; one outside its range is **clamped** (and reported). `px` values are virtual
  pixels of the 1920x1080 canvas unless said otherwise; the player's text scale still applies.
- A bad value keeps that field's default and adds one diagnostic; a whole file that is too large (over
  256 KiB), not a JSON object, or without `"format": 1` is ignored (one diagnostic, built-in theme). Unknown
  keys are ignored. At most 32 diagnostics are kept (a 33rd line says more were omitted). They are logged
  (`[UiTheme]`) and returned by `RowlEngine_GetUiThemeJson`.
- Font paths are VFS-relative `.ttf` / `.otf` (no absolute path, no `..`).
- **Inheritance:** some fields follow another unless the file names them, so one `menu.title` recolours the
  chrome. The "inherits" column below lists them; the inherited colour keeps the field's own alpha.

## `menu` (all menu pages, History included)

| key | default | meaning | inherits |
|---|---|---|---|
| `dim` | `#000000A0` | veil over the frozen story frame | |
| `panel_fill` | `#0F0F1AEB` | panel background | |
| `panel_border` | `#38BDF8` | panel border colour | |
| `panel_border_width` | 1 | border thickness, physical px, 0-8 (0 = none) | |
| `title` | `#38BDF8` | page title | |
| `text` | `#F1F5F9` | row label | |
| `dim_text` | `#94A3B8` | row value, detail line, counter, voice mark | |
| `disabled_text` | `#94A3B8` | label of a disabled row | `dim_text` |
| `selected_fill` | `#0E7490` | selected row background | |
| `selected_bar` | `#38BDF8` | bar on the selected row's left edge | |
| `selected_bar_width` | 6 | bar width, 0-64 (0 = none) | |
| `selected_text` | `#38BDF8` | label of the selected row | `title` |
| `hint` | `#94A3B8` | key-hint line at the panel bottom | `dim_text` |
| `thumb_border` | `#38BDF8` | save-slot thumbnail frame | `panel_border` |
| `history_speaker` | `#38BDF8` | speaker name in History | `title` |
| `title_px` `row_px` `value_px` `detail_px` `hint_px` `history_speaker_px` | 40 24 24 16 19 20 | text sizes, 8-120 (the text scale rule is unchanged: title/hint/detail stay at design size, rows grow only as far as the row box holds them) | |

## `extras` (gallery, achievements, full-screen viewer)

| key | default | meaning | inherits |
|---|---|---|---|
| `cell_fill` | `#080A12` | unlocked gallery cell background | |
| `locked_fill` | `#161928` | locked gallery cell box | |
| `cell_border_selected` | `#38BDF8` | selected cell border | `menu.selected_bar` |
| `cell_border` | `#475569` | unlocked cell border | |
| `locked_border` | `#2E384E` | locked cell border | |
| `plate` | `#16192A` | achievement plate | |
| `plate_selected` | `#0E7490` | selected plate and selected gallery cell | `menu.selected_fill` |
| `plate_bar` | `#38BDF8` | bar on the selected plate | `menu.selected_bar` |
| `title_selected` | `#38BDF8` | selected achievement title | `menu.title` |
| `text` | `#F1F5F9` | unlocked titles, captions | `menu.text` |
| `dim_text` | `#94A3B8` | locked texts, dates, the "?" mark | `menu.dim_text` |
| `description` | `#CBD5E1` | unlocked achievement description | |
| `viewer_dim` | `#000000F0` | full-screen viewer backdrop | |
| `viewer_frame` | `#38BDF8` | frame around the viewed picture | `menu.panel_border` |
| `viewer_title` | `#F1F5F9` | viewer title | `menu.text` |
| `mark_px` `caption_px` `viewer_title_px` | 44 16 30 | sizes (mark up to 200) | |

## `toast` and `badges`

`toast`: `fill` (`#0F0F1AD7`, inherits `menu.panel_fill`), `text` (`menu.text`), `accent` (the "Basarim:" head,
`menu.title`), `rule` (`#38BDF8BE`, inherits `menu.title`), `rule_px` 2 (0-16, 0 = no rule), `px` 26,
`height` 56, `margin_top` 28, `pad_x` 30, `max_text_width` 1300.

`badges` (AUTO / SKIP capsules): `auto_fill` (`#0F0F1AC8`, `menu.panel_fill`), `auto_text` (`menu.title`),
`skip_fill` (`#0E7490D2`, `menu.selected_fill`), `skip_text` (`menu.text`), `px` 24, `width` 116, `height` 44,
`gap` 12, `margin_right` 40, `margin_top` 28. The badge words are not themed here (texts: a later slice).

## `scene`

`letterbox` `#0B0F19` (outside the 16:9 canvas), `clear` `#1A1A24` (frame clear), `missing_background`
`#141826` (background texture not found), `placeholder_fill` `#1E293BDC` / `placeholder_border` `#38BDF8`
(character sprite not found).

## `story`: project-wide defaults of the dialogue box and the choice buttons

**Precedence: a key written in the node's component > this theme > the built-in default.** The component
JSON is never changed; the engine only fills the keys the component does not carry. A theme reload moves
exactly the fields no component wrote.

`story.dialogue`: `box_color` `#0F0F1A`, `box_opacity` 0.88, `border_color` `#00F0FF`, `border_thickness` 2
(0-16), `text_color` `#F1F5F9`, `font_size` 24, `speaker_font_size` 20, `speaker_color` `#38BDF8`,
`text_alignment` `Left` (Left / Center / Right), `corner_radius` 8 (0-64).

`story.choice`: `background_color` `#1E293B`, `hover_color` `#0EA5E9`, `border_color` `#38BDF8`, `text_color`
`#FFFFFF`, `opacity` 1, `font_size` 22, `text_alignment` `Center`, `border_thickness` 2, `corner_radius` 8.

`story.speaker_tag` (the name badge above the dialogue box): `text_color` `#FFFFFF`, `border_color`
`#FFFFFFB4`, `border_width` 1 (physical px, 0-8).

`story.choice_focus` (marks of the keyboard / mouse focused button): `inner_color` (default: the button's text
colour), `bar_color` (default: the button's border colour), `bar_width` 6, `inset` 3.

`story.honor_shape_fields` (default `false`): components carry `corner_radius` (dialogue, choice) and the
choice's `border_thickness`, but the renderer never drew them, and the editor writes `8` / `2` into every
component (the sample `kayip_tablo` writes `10`). They stay undrawn unless this is `true`; then the dialogue
box and the buttons get rounded corners (`corner_radius` virtual px) and a border of `round(border_thickness)`
physical pixels (a thickness up to 1.5 stays 1 px, 0 = no border; at most 16). Without the flag the dialogue
border keeps its old rule (1 ring, 2 rings above 1.5).

Notes: `box_opacity` is carried for the editor but the renderer takes the box alpha from the colour's own
alpha channel only (`"#RRGGBBAA"`); the number alone never changed a pixel (existing behaviour, unchanged so
the old frames stay identical). A colour with an alpha channel blends over the scene.

## `fonts`

`menu` (menus, badges, toast, Ekstralar), `dialogue` (dialogue text; also the default font of the choice
buttons, a button's own `font_family` still wins) and `speaker` (name tag): VFS paths of `.ttf` / `.otf`.
Empty or unreadable = the project font (`fonts/default.ttf`), with a diagnostic when unreadable. Measuring
and drawing use the same renderer instance, the player's text scale and high-contrast setting apply to all
of them. A themed font is not in the MSDF atlas, so text in it is drawn with the TrueType path.

## `high_contrast`

Optional object with the same `menu`, `extras`, `toast`, `badges`, `scene` keys: while the player's
High Contrast setting is on, these values replace the theme's (inheritance runs again over the result).
`story` and `fonts` cannot be overridden. Without it the setting changes the glyph halo only, as before.

## Example (dark coffee and gold)

```json
{
  "format": 1,
  "menu": {"dim": "#000000B8", "panel_fill": "#1A120BF0", "panel_border": "#C9A86A", "panel_border_width": 2,
           "title": "#E8C98A", "text": "#F2E6D0", "dim_text": "#A8946F", "selected_fill": "#4A3320",
           "selected_bar": "#C9A86A", "selected_bar_width": 8},
  "extras": {"locked_fill": "#2A1E14", "cell_border": "#8A6F45", "plate": "#241910"},
  "story": {"dialogue": {"box_color": "#14110D", "border_color": "#C9A86A", "text_color": "#F2E6D0"},
            "choice": {"background_color": "#2A1E14", "hover_color": "#5A3E24", "border_color": "#C9A86A"},
            "speaker_tag": {"text_color": "#14110D", "border_color": "#E8C98A"}},
  "fonts": {"menu": "fonts/menu.ttf"},
  "high_contrast": {"menu": {"panel_fill": "#000000", "text": "#FFFFFF"}}
}
```

## C API (`ROWL_ENGINE_CAPABILITY_UI_THEME` = 536870912)

- `RowlEngine_GetUiThemeJson(handle, buf, size, &required)`: `{"format","loaded","source","diagnostics",
  "theme":{every key, resolved},"high_contrast":{..}|null}`; `source` is the VFS path, `(inline)` or empty.
- `RowlEngine_ReloadUiTheme(handle)`: re-reads the file (editor live preview after a save) and repaints the
  next frame; a missing file restores the built-in look.
- `RowlEngine_SetUiThemeJson(handle, json)`: installs a document without a file (preview of unsaved edits);
  a document that is not usable returns `ROWL_RESULT_VALIDATION_ERROR` and the theme in force stays.

## Authoring note for the editor (T4)

The editor's dialogue and choice components serialise **every** field on save (`font_size`, `text_color`,
`box_color`, `corner_radius`, ...). Such a node always "writes" the key, so the theme default is never seen on
it. For the theme to take effect the editor has to write only the fields the author changed (and drop a key
when it is reset to "default").
