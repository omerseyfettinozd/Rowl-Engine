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

Skins (images, 9-slice frames, backgrounds, cursor, slider, continue mark; T2) are described in
"Skins" below. Not covered yet (later slices): layout (positions, sizes, row pitch; T2b), label texts,
which rows are listed, durations, UI sounds, and the editor window.

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
`story`, `fonts` and every skin key cannot be overridden. Without it the setting changes the glyph halo
only, as before. **Skins step aside while High Contrast is on** (a picture behind text is what the setting
exists to avoid): pictures, "none" panels, the title background / logo are dropped and the surfaces draw
their plain colours, whether or not the file has a `high_contrast` object. `"high_contrast": {"keep_skins":
true}` keeps them.

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

## Skins (T2): any surface wears the author's picture

A skin dresses one surface. Every surface takes the same object:

```json
"panel_skin": {
  "image": "ui/save_frame.png",       // VFS path (.png .jpg .jpeg .bmp .tga .gif); "" = no picture
  "mode": "nine_slice",               // nine_slice | stretch | tile | fit | cover | center
  "slice": [24, 24, 24, 24],          // nine_slice insets l,t,r,b in IMAGE pixels (one number = all four)
  "slice_scale": 1.0,                 // on-screen size of the image: edges, tiles, centred picture (0.05-16)
  "tint": "#FFFFFFFF", "opacity": 1,  // multiply the picture
  "filter": "linear",                 // or "nearest" (pixel art)
  "fill": "#102030", "border": "#FFD27A", "border_width": 2, "corner_radius": 8,  // colours of the surface itself
  "padding": [24, 28, 24, 12],        // content inset l,t,r,b, virtual px
  "selected": { "image": "ui/row_hi.png" }   // state variants (below)
}
```

- A bare string is the image (`"panel_skin": "ui/frame.png"`); `"none"` / `false` means "draw no panel".
- **With a picture the surface's own colours are not drawn** (the picture is the frame) unless the skin names
  `fill` / `border`; without a picture, or when the picture is missing / unreadable / over 8192 px, the surface
  draws exactly as it did before skins existed (and `GetUiThemeJson` lists the problem). A skin with no
  picture and no colour (only `padding`) changes no pixel.
- `nine_slice`: corners are drawn 1:1 (times `slice_scale`), edges stretch along one axis, the centre fills.
  `slice_scale` 2 on a half-size window shows the image's pixels 1:1. A frame smaller than its own corners
  shrinks the corners alike. `tile` repeats the picture at its (scaled) size, `cover` fills and crops, `fit`
  keeps the aspect inside the rectangle, `center` draws it at its size.
- `border_width` is physical pixels (like `menu.panel_border_width`); `corner_radius` and `padding` are
  virtual pixels of the 1920x1080 canvas.
- State variants (`selected`, `focused`, `hover`, `pressed`, `disabled`, `locked`, `empty`, `corrupt`) start as
  a copy of the normal skin and change only the keys they name. `selected`, `focused` and `hover` stand in for
  one another (the engine does not tell hover from keyboard focus: pointer hover moves the focus).

### Surfaces

| key | what it dresses | states used |
|---|---|---|
| `menu.panel_skin` | the panel behind every menu page | normal |
| `menu.row_skin` | rows of every page | normal, selected, disabled |
| `menu.overlay_skin` | a full-window picture over the veil (`menu.dim`) | normal |
| `screens.<page>.panel_skin` / `row_skin` | that page only; `"none"` = no panel | as above |
| `screens.<page>.dim`, `title_color`, `title_px`, `title_hidden`, `hint_hidden`, `counter_color`, `counter_px` | page veil colour, title and counter text, hide the title / hint line | |
| `slots.row_skin` | rows of the Save / Load pages | normal (filled), selected, empty, corrupt |
| `slots.thumb_frame_skin` | frame around the thumbnail box (`padding` grows it outward) | normal |
| `slots.thumb_placeholder`, `slots.new_image` | picture where the slot has no thumbnail / "new save" for an empty slot on the Save page | image, mode |
| `title_screen.background` | full-window picture on the title and on the pages opened from it (default `cover`) | image, mode, `fill` |
| `title_screen.logo` + `logo_x/y/w/h` | logo on the title page (virtual px rectangle) | image, mode |
| `title_screen.music` | VFS audio (.ogg .wav .mp3 .flac), plays while the title is up, stops with it unless the story brings its own BGM | |
| `title_screen.dim`, `hide_story_frame` | veil on the title (none while a background shows) / hide the story frame behind the title even without a picture (plain `background.fill`, black by default) | |
| `story.dialogue.skin` | the dialogue box; a component's own `custom_box_texture` wins over the theme's image; `padding` is where the text sits (default `[24, 28, 24, 8]`) | normal |
| `story.speaker_tag.skin`, `offset_x`, `offset_y` | the name tag; offset moves it from its place (virtual px) | normal |
| `story.choice.skin` | the option buttons; `padding` left / right is the text inset (default 12) | normal, focused, disabled |
| `story.choice.show_disabled`, `disabled_text` | draw options whose condition is false (dimmed, never selectable); default `false` = hidden as before | |
| `story.continue_indicator` | `skin` (picture), `width`, `height`, `offset_x/y` (from the box's bottom-right corner), `animation` (`none` / `bounce` / `blink`), `period`, `amplitude`: a mark when the line has finished typing and no options are up; still under "Reduce Motion" | |
| `toast.skin`, `toast.anchor`, `toast.margin_x` | achievement toast; anchors `top_center` (default), `top_left`, `top_right`, `bottom_*`; `margin_top` is the distance from the anchored top / bottom edge | normal |
| `badges.auto_skin`, `skip_skin`, `badges.anchor` | AUTO / SKIP capsules; anchors `top_right` (default), `top_left`, `bottom_right`, `bottom_left`; they stack away from the edge | normal |
| `extras.cell_skin` | gallery cell (over the picture: draw it as a frame with a transparent centre) | normal, locked, selected (selected = plate behind the cell) |
| `extras.locked_image` | picture inside a locked cell instead of "?" | image, mode |
| `extras.plate_skin` | achievement plate; `padding[0]` is the text inset | normal, locked, selected |
| `extras.locked_icon`, `icon_size` | icon of a locked achievement; an unlocked one shows its catalog `icon` (see below), 16-96 px | |
| `extras.viewer_frame_skin` | frame around the full-screen picture (`padding` grows it outward) | normal |
| `cursor` | `image` (PNG, 512 px max), `hotspot_x/y` (image px), `scale` (0.25-4): the pointer on the game window; offscreen / embedded surfaces keep the host's | |
| `preferences.value_style` | `"slider"`: volume / text speed / auto delay rows show a track with a knob (click or drag the track; keys still step); `"text"` (default) is the "100%" text | |
| `preferences.track_skin`, `fill_skin`, `knob_skin`, `track_height`, `knob_size`, `show_value` | the slider's parts (without pictures plain bars in the menu colours); knob `pressed` while dragging | |

History entries wear `screens.history.row_skin` (normal / selected). Music-room rows are generic rows
(`screens.music.row_skin`). With a `selected` row skin the selected row shows that picture instead of the cyan
fill and the bar; with only a normal row picture the selected row keeps it and gets the bar alone.

`extras.json` achievements may carry an optional `"icon": "ui/star.png"` (a VFS image). A locked achievement
never reveals it (the view carries no path for it); the theme's `extras.locked_icon` stands in.

The option JSON's `hover_image` / `disabled_image` (and `normal_image`, as before) are honoured: an option's
own state image wins over the theme's skin for that state. `pressed_image` is read but selection fires on
press, so the state never shows on a button (the slider knob does use `pressed` while dragging).

### Recipe: use your own frame picture for the save screen

1. Put the picture in the project, e.g. `Assets/ui/save_frame.png` (it travels in the package with the rest of
   `Assets/`). Draw it with a transparent or decorative centre and edges that survive stretching.
2. Note the width of the corner decoration in image pixels, say 32.
3. In `Assets/json/ui_theme.json` (format 1):
   ```json
   {"format": 1,
    "screens": {"save": {"panel_skin": {"image": "ui/save_frame.png", "mode": "nine_slice",
                                         "slice": [32, 32, 32, 32]}},
                "load": {"panel_skin": {"image": "ui/save_frame.png", "mode": "nine_slice",
                                         "slice": [32, 32, 32, 32]}}}}
   ```
4. Run the game (or `RowlEngine_ReloadUiTheme` in a host). Corners stay as drawn, edges stretch, the centre
   fills; the old accent border and panel colour are gone. Adjust `slice_scale` to make the frame bigger or
   smaller on screen, `tint` to recolour it.
5. Check `RowlEngine_GetUiThemeJson(...)["diagnostics"]`: a misspelt path or an unreadable picture is named
   there and the panel simply keeps its old colours.
6. Want no frame at all on a page? `"panel_skin": "none"`, then colour the text with `menu.text` /
   `screens.<page>.title_color` so it reads over the story frame.

### Not in T2

Layout is still the engine's fixed grid (panel 880x860 at 520,110; row pitch; thumbnail box; gallery columns;
achievement rows). Moving or resizing those, the slot "card grid" and the text of the menu labels are the next
slice (T2b); hit-testing will then come from the same runtime layout as drawing.

## Authoring note for the editor (T4)

The editor's dialogue and choice components serialise **every** field on save (`font_size`, `text_color`,
`box_color`, `corner_radius`, ...). Such a node always "writes" the key, so the theme default is never seen on
it. For the theme to take effect the editor has to write only the fields the author changed (and drop a key
when it is reset to "default").
