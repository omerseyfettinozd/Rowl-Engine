# Rich Text Markup Contract — Parser, Token Stream, Fail-Closed Rules (Faz 3 Dilim 2)

Canonical reference for the rich-text markup slice. Sources of truth are the
implementations named below; when this document and code disagree, code wins
and this document must be patched.

- Native rules: `engine/include/rowl/text/markup_parser.hpp` +
  `engine/src/text/markup_parser.cpp` (`Rowl::Text`).
- C ABI: `engine/src/c_api_markup.cpp` (+ declarations in
  `engine/include/rowl/c_api.h`).
- Managed mirror: `editor/Services/MarkupParser.cs` (+ P/Invoke in
  `editor/Src/Native/NativeBridge.cs`).

`engine.cpp`, `window.cpp`, `MainWindowViewModel.cs` and `EngineHost.cs`
gain no logic in this slice: the parser is a stateless, dependency-free
module consumed through pure functions.

## 1. Concepts

| Concept | Owner | Meaning |
|---|---|---|
| markup source | dialogue text | UTF-8 string with inline `<tag>` markers. |
| plain text | parser output | Source minus every *valid* tag; broken tags stay as literal text. Supplies logical text to shaping. |
| token stream | parser output | One logical-scalar `chars[]` entry per Unicode code point of the plain text, carrying the styles active at that point. |
| diagnostic | parser output | Always severity `warning`: what was tolerated and where (byte `offset`/`length` in the source). Never an error, never a crash. |
| capability | C ABI | `CAPABILITY_RICH_TEXT_MARKUP = (1 << 8) [256]`, additive. |

The output model is a **flat logical token stream, not a tree**. `MarkupChar`
entries are Unicode scalars and preserve linear source order; they are not the
final typewriter reveal unit. Dilim 3 combines UAX #29 extended-grapheme
boundaries with HarfBuzz shaping clusters into `reveal_index` groups. Combining
marks, emoji ZWJ sequences/modifiers and ligatures therefore reveal atomically.
Styles are independent flags/stacks; nesting *order* is insignificant (each
closing tag pops its own kind's stack).

Before shaping-run construction, every extended grapheme cluster is normalized
to the first scalar's typography/effect style. Pauses found anywhere inside the
cluster accumulate before that cluster; the first scalar's speed applies to the
whole cluster. A HarfBuzz cluster spanning multiple graphemes receives one
shared `reveal_index`. Dilim 3 owns this normalization and the final mapping;
this parser deliberately retains the original scalar-level data.

## 2. Supported tags

Tag names and attribute keys are **case-insensitive** (`<B>`, `<Color>`).
Values may be unquoted, single- or double-quoted
(`<color=red>`, `<color='red'>`, `<color="red">`). Whitespace around names,
`=` and values is ignored.

### 2.1 Formatting

| Tag | Meaning |
|---|---|
| `<b>` … `</b>` | bold on |
| `<i>` … `</i>` | italic on |
| `<u>` … `</u>` | underline on |
| `<color=VALUE>` … `</color>` | text color override; innermost wins |
| `<size=N>` … `</size>` | local size override (abstract units); innermost wins |

- `<b>`, `<i>`, `<u>` take **no** attributes: `<b foo=1>` is literal + warning.
- `N` is a float in **(0, 512]**; `0`, negatives, non-numbers are literal + warning.

### 2.2 Color values

`<color=#RRGGBB|#RGB|named>`:

- `#RRGGBB`: six hex digits (`#FF0000`).
- `#RGB`: three hex digits, nibble-doubled (`#F00` ≡ `#FF0000`).
- Named (case-insensitive): `black #000000`, `white #FFFFFF`,
  `red #FF0000`, `green #00FF00`, `lime #00FF00`, `blue #0000FF`,
  `navy #000080`, `yellow #FFFF00`, `cyan #00FFFF`, `aqua #00FFFF`,
  `magenta #FF00FF`, `fuchsia #FF00FF`, `gray/grey #808080`,
  `orange #FFA500`, `purple #800080`, `pink #FFC0CB`, `brown #A52A2A`,
  `teal #008080`, `olive #808000`.

Anything else (`#GGG`, `#12345`, `blurple`, empty) is literal + warning.
Alpha is always 255; `#RRGGBBAA` forms are **not** accepted.

### 2.3 Structure

| Input | Meaning |
|---|---|
| `<br>`, `<br/>`, `<br />` (any case) | line break: one `\n` char in the plain text carrying current styles |
| literal newline | `\r\n` and lone `\r` normalize to a single `\n` each |

`<br>` takes no attributes; `</br>` is malformed (literal + warning).

### 2.4 Typewriter & timing

| Tag | Meaning |
|---|---|
| `<speed=F>` … `</speed>` | typewriter speed multiplier from this point; `</speed>` restores the outer value |
| `<pause=F>` | zero-width event: wait `F` seconds before the **next** character |

- `speed` must be in **(0, 100]** (`1.0` = normal). `0`, negatives,
  non-numbers are literal + warning. Unclosed `<speed>` applies to end of
  input + one warning.
- `pause` must be in **[0, 60]**. Pauses accumulate onto the following
  character's `pause_before`; consecutive pauses sum. A pause with no
  following character accumulates into `trailing_pause`. `<pause>` has no
  closing tag: `</pause>` is malformed (literal + warning).
- Only the `=` form is valid (`<speed=2.0>`); `<speed 2.0>` is literal +
  warning. A trailing `/` is tolerated (`<pause=1.0/>`).

### 2.5 Dynamic effect pre-tags

| Tag | Meaning |
|---|---|
| `<shake intensity=F>` … `</shake>` | per-character shake with intensity |
| `<wave speed=F amplitude=F>` … `</wave>` | per-character wave with speed + amplitude |

- Keys are order-free but **exact**: missing, extra, duplicated or
  non-numeric params are literal + warning (no silent defaults;
  bare `<shake>` is literal). Both tags require their full param set.
- Every param must be in **[0, 100]**.
- Unclosed effects apply to end of input + one warning.

## 3. Error tolerance (fail-closed / graceful fallback)

The parser **never crashes, never throws across the ABI, always
terminates**. Rules:

1. **Valid open, unclosed at end of input** — consumed as formatting, effect
   runs to end of input, **one warning** per unclosed tag
   (`unclosed <b> applies to end of input; kept styling`).
   Example: `<b>hello` → plain `hello`, all bold, 1 warning.
2. **Unknown tag** (`<dragon>`), **bad value** (`<color=#GGG>`,
   `<size=0>`, `<shake>`), **stray close** (`</b>` with no open `<b>`),
   **malformed close** (`</br>`, `</pause>`, `</b foo>`), **empty tag**
   (`<>`), **unterminated attempt** (`<b hello`) — the raw bytes are
   **preserved as literal text** with current styles, plus **one warning**
   each. A failed open leaves no style frame, so its later `</…>` warns
   again as stray.
3. **Not a tag at all**: `<` followed by anything other than a letter,
   `/` or `>` (`a < b`, `<3`, lone `<` at end) is silent literal text with
   **no** warning. Exception: `<>` is an empty-tag attempt and warns as
   malformed (still literal). A backslash escapes a tag-looking delimiter:
   `\<b>` produces literal `<b>` with no warning and does not enable bold.
   Backslashes not immediately followed by `<` remain literal.
4. **Interleaved closes** close per-kind, not per-tree:
   `<b><i></b></i>` pops bold then italic with **no** warning.
5. **Invalid UTF-8** runs become one `U+FFFD` each + one warning; valid
   bytes around them survive. (Managed: lone UTF-16 surrogates behave the
   same.)
6. At most **128 warnings** are stored; overflow is counted in
   `omitted_diagnostics`.
7. Input larger than **256 KiB** is rejected at the entry gates
   (native: `INVALID_ARGUMENT`; managed: empty document + one warning).

## 4. Output model

- `plain_text`: valid tags removed, `\n` per break/newline, literals kept.
  Dilim 3 shapes this text and assigns its final reveal positions.
- `chars[]`: exactly one logical scalar entry per Unicode code point of
  `plain_text`; these entries are Dilim 3 shaping input, not final reveal units
  (concatenating `text` reproduces `plain_text`). Each entry:
  `text`, `line_break`, `bold`, `italic`, `underline`,
  `color` (`#RRGGBB` or null), `size` (number or null),
  `speed` (default `1.0`), `pause_before` (default `0`),
  `shake` + `shake_intensity`, `wave` + `wave_speed` + `wave_amplitude`
  (param fields null when the effect is off).
- `diagnostics[]`: `{message, offset, length}` warnings in source order.
- `trailing_pause`: seconds parked after the last character.
- `omitted_diagnostics`: warnings dropped by the 128 cap.

## 5. C ABI (additive; older entry points untouched)

| Entry point | Contract |
|---|---|
| `RowlEngine_ParseMarkup(markup, buffer, size, outRequired)` | UTF-8 JSON document (`plain_text`, `char_count`, `chars`, `diagnostics`, `trailing_pause`, `omitted_diagnostics`) via the caller-buffer contract (null/0 = size query; undersized = `BUFFER_TOO_SMALL`, buffer cleared). |
| `RowlEngine_StripMarkup(markup, buffer, size, outRequired)` | Plain text only; same validation and buffer contract. |
| `RowlEngine_GetCapabilities` | Now includes `CAPABILITY_RICH_TEXT_MARKUP`; all older flags intact. |

Both markup calls are **handle-free pure helpers**: no engine instance, no
thread affinity, no file I/O, thread-safe. Null markup, null
`outRequiredSize`, non-zero size with null buffer, or input over 256 KiB
returns `INVALID_ARGUMENT`. No C++ exception ever crosses the boundary.

Implementation ownership: `engine/include/rowl/text/` +
`engine/src/text/markup_parser.cpp` (rules) and
`engine/src/c_api_markup.cpp` (ABI).

## 6. C# bridge

- P/Invoke: `NativeBridge.RowlEngine_ParseMarkup / RowlEngine_StripMarkup`
  (`editor/Src/Native/NativeBridge.cs`).
- Logic: `editor/Services/MarkupParser.cs` — a dependency-free managed port
  of the same contract (`Parse` / `Strip` / `ToJson` / `ParseJson`), plus
  `TryParseNativeJson` / `TryStripNative` / `ParseNativeOrManagedJson`
  which prefer native output and silently fall back to managed parsing when
  the library is missing or rejects the input. Never throws
  (null/oversized/hostile input degrades to empty content + warning).
  `EngineHost` and `MainWindowViewModel` are untouched.

## 7. Limits

- Input: 256 KiB (UTF-8 bytes) at the native and managed gates.
- Stored warnings: 128 (`omitted_diagnostics` counts the rest).
- `size` [1, 512]; `speed` (0, 100]; `pause` [0, 60]; effect params [0, 100].
- Locale-independent number parsing (ASCII `.` decimal only; `nan`/`inf`
  spellings rejected).

## 8. Test matrix

| Gate | Coverage |
|---|---|
| `tests/test_markup_parser.cpp` (CTest, inside `rowl_native_tests`) | All tags + nesting, color/size forms, `<br>` variants, CRLF normalization, locale-independent speed/pause numbers, pause attach + trailing accumulation, shake/wave params incl. order/quotes/missing, unclosed/broken/stray/unterminated literal fallback + counts, escaped/silent `<`, Unicode code-point alignment (TR/CJK/emoji), invalid UTF-8, empty/tags-only, hostile fuzz with plain/char consistency, capability bits, null/argument guards, 256 KiB rejection, caller-buffer contract, Parse JSON schema. |
| `editor/Tests/EditorMarkupParserSlice2Tests.cs` (xUnit) | Managed contract mirror, JSON keys, capability value, null/empty/oversized fail-closed, required native parity, UTF-8 diagnostic-span parity after non-ASCII text, escaped `<`, hostile-input sweep. |

## 9. Explicitly out of scope (later Faz 3 slices)

Shaping/rendering (FreeType/HarfBuzz/FriBidi/libunibreak), shared
measure/typewriter/render glyph array, RTL/BiDi reordering, font fallback
and license/glyph-coverage checks, editor translation table,
pseudo-locale, CSV/JSON exchange, per-node localized dialogue resolution.
The `speed`/`pause`/`shake`/`wave` tokens are *data for* those later
stages; this slice wires them to no renderer.
