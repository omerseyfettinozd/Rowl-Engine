# Hex Color Parsing — Unification (Faz 4.5 Dilim 2)

Single source of truth: `engine/include/rowl/text/hex_color.hpp`
(header-only, no C ABI change, no new translation unit).

## API

- `bool tryParseHexColor(input, HexColor&)`: strict superset parse.
  Success fills `r/g/b/a` (`a = 255` when the form carries no alpha);
  failure returns `false` and leaves `out` untouched.
- `bool tryParseHexColor(input, r, g, b, a)`: byte-reference twin.
- `HexColor parseHexColor(input, fallback, ok = nullptr)`: explicit-fallback
  wrapper. Valid input returns the parsed color (`ok = true`); malformed
  input returns `fallback` (`ok = false`). There is no implicit fallback —
  every caller passes its own. No silent garbage colors.

## Accepted union

Optional single leading `#`, then exactly **3 / 4 / 6 / 8** hex digits
(case-insensitive, surrounding ASCII whitespace trimmed):

| Form       | Example       | Result               |
|------------|---------------|----------------------|
| `#RGB`     | `#F00`        | `(255, 0, 0, 255)`   |
| `#RGBA`    | `#F008`       | `(255, 0, 0, 0x88)`  |
| `#RRGGBB`  | `#10B981`     | `(0x10, 0xB9, 0x81, 255)` |
| `#RRGGBBAA`| `#10B981CC`  | `(0x10, 0xB9, 0x81, 0xCC)` |
| bare forms | `FFF`, `10B981` | same as with `#`    |

Rejected (fallback + failure signal): empty, `#`, truncated (`#12`,
`#12345`, `#1234567`), non-hex digits (`#GGG`, `#FF00GG`), `0x…` prefixes,
`##FFF`, embedded whitespace. Rationale: the old `stoul`/`strtoul`
call sites parsed only a digit *prefix* and silently kept the garbage
(e.g. `#FF00GG` produced a truncated color); the unified parser rejects
the whole input instead.

## Call-site migration

| Site | Old behavior | New behavior |
|------|--------------|--------------|
| `render/window.cpp` (`parseHexColor`, fallback **white** + `defaultA`) | `stoul`, 6/8 digits only, silent garbage on trailing junk | same white fallback, now with `ok` signal; additionally accepts `#RGB`/`#RGBA` |
| `render/transition_manager.cpp` (`parseHexColor`, fallback **black** `0,0,0,255`) | `strtoul`, 3/4/6/8 digits, silent prefix garbage | same black fallback, now with `ok` signal |
| `text/markup_parser.cpp` (`tryParseHexColor` → `Rgba`) | `#RGB`/`#RRGGBB`, `#` required | unchanged subset: `#` still required, only 3/6 digits, `a` always 255 (contract §2.2). Digit validation is shared with the header. |

`window.cpp` scope note: only the hex helper was touched; vignette and
all other `window.cpp` logic are Dilim 4 scope and were not modified.

Verified non-sites (read, no hex parsing, unchanged): `scripting/
lua_sandbox.cpp` (decimal `from_chars` for sandbox numbers only) and
`player/main.cpp` (decimal window-dimension / quick-slot parsing only).

## Markup contract

`docs/RICH_TEXT_MARKUP_CONTRACT.md` §2.2 is unchanged: `<color>` still
accepts `#RGB | #RRGGBB | named`, alpha forms stay literal + warning.

## Tests (no new TU, no harness changes)

- `tests/test_markup_parser.cpp::testHexColorUnification` — header golden
  matrix (`#FFF`/alpha/empty/malformed), untouched-output sentinel,
  wrapper `ok` signal, markup-level goldens (alpha/truncated/empty stay
  literal, `#FFF`/`#RRGGBB` parse identically).
- `tests/test_camera_and_transition_pipeline.cpp::testTransitionHexUnification`
  — black-fallback flavor matrix plus `fade_color` smoke for every hex class.
