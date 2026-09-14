# Text Shaping Pipeline Contract

Status: Faz 3 / Dilim 3  
Native namespace: `Rowl::Text`  
Public entry point: `TextShaper::shapeMarkup`

## 1. Purpose and ownership

The shaping service converts the flat rich-text stream from
`parseMarkup()` into one immutable `ShapedText` layout. That layout is the
single authority for:

- visual-order glyph indices and positions;
- line width, total bounds and alignment;
- reveal/typewriter grouping and timing;
- software FreeType rasterization.

The renderer must not re-count UTF-8 bytes, UTF-16 code units or Unicode
scalars to decide visibility. `Window` creates one `ShapedText`, evaluates its
reveal state and passes that same object to `renderShapedText()`.

Implementation and policy live under `engine/src/text`; `engine.cpp` and
`window.cpp` remain orchestration points. The editor ViewModel owns no shaping
logic.

## 2. Pipeline order

1. The tolerant markup parser produces UTF-8 `plainText` and one `MarkupChar`
   per logical Unicode scalar.
2. libunibreak applies UAX #29 extended-grapheme boundaries. Combining marks,
   variation selectors, emoji modifiers and ZWJ sequences are initially one
   reveal group.
3. FriBidi resolves paragraph direction and embedding levels. Logical runs are
   shaped separately and sorted into visual run order; logical scalar indices
   remain attached to every glyph.
4. HarfBuzz shapes each directional run with monotone-grapheme clusters.
   Glyph IDs, advances and offsets come directly from HarfBuzz. If one
   HarfBuzz cluster spans multiple graphemes (for example a font ligature),
   those graphemes are merged into one reveal group.
5. libunibreak applies UAX #14 line-break opportunities. Wrapping never splits
   a reveal group. Each selected line is shaped again so line-local BiDi order
   and contextual shaping are authoritative.
6. FreeType rasterizes the HarfBuzz glyph IDs. Measurement uses the advances
   already stored in the same `ShapedText`; it never independently measures
   code points.

Explicit LF starts a new line. Automatic wrapping is disabled when
`maxWidth <= 0`.

## 3. Logical, visual and reveal indices

`logicalScalar` indexes the markup parser's logical scalar stream.
`ShapedGlyph` order is visual drawing order within each line. `revealIndex`
indexes `ShapedText::revealUnits` in logical reading order.

All glyphs belonging to a combining sequence, emoji ZWJ sequence or shaped
ligature share one reveal index. Rendering a reveal limit therefore displays
all or none of that cluster. BiDi reordering never changes reveal identity.

When style or timing boundaries occur inside a merged cluster:

- visual style and `speed` come from the first logical scalar;
- every `pause_before` in the cluster is summed before that reveal;
- the cluster is never split to preserve the boundary.

This is the normalization policy established by the rich-text markup
contract.

## 4. Typewriter timeline

`evaluateReveal(shaped, elapsedSeconds, baseMillisecondsPerUnit)` walks only
`shaped.revealUnits`:

```text
unit duration = pause_before + (base duration / speed multiplier)
```

A unit becomes visible after its duration is consumed. Trailing markup pauses
do not add a glyph or reveal unit, but they delay `complete=true`. Click-to-
complete, voice-blip cadence and software rendering use reveal-unit indices;
the legacy field name `lastBlipCodepointIndex` is retained only for source and
save compatibility.

## 5. Backends and fallback

With `ROWL_ENABLE_TEXT_SHAPING=ON`, CMake asks pkg-config for all four packages
as one optional feature set: `freetype2`, `harfbuzz`, `fribidi` and
`libunibreak`. The advanced backend is enabled only when all are present.

If the option is disabled or any package is absent, configuration succeeds and
`FontRenderer` retains its existing stb_truetype measurement, wrapping and
rasterization path. The fallback reveal model still groups common combining,
variation-selector, emoji-modifier and ZWJ sequences, but it does not claim
full BiDi or font-dependent ligature shaping. GPU MSDF availability is
independent; shaderless rendering can still use the advanced CPU shaping path.

## 6. C ABI

Capability bit `ROWL_ENGINE_CAPABILITY_TEXT_SHAPING` is `1 << 9` (`512`).

`RowlEngine_ShapeMarkup` is handle-free and reads caller-owned font bytes only
during the call. It returns deterministic UTF-8 JSON containing:

- `backend`, `plain_text`, `width`, `height`, `trailing_pause`;
- visual-order `glyphs` with glyph/logical/reveal/line indices and positions;
- `lines` with glyph ranges, width and baseline;
- logical `reveal_units` with scalar span, pause, speed and representative
  code point.

It follows the standard caller-buffer contract: `NULL/0` queries size, the
required size includes the final NUL, and undersized buffers return
`ROWL_RESULT_BUFFER_TOO_SMALL`. Markup is bounded at 256 KiB, font data at
32 MiB and the optional language tag at 128 bytes. Invalid/non-finite sizes,
invalid pointers and malformed fonts fail without retaining state.

The C# bridge pins the font byte array for exactly the two caller-buffer calls;
no unmanaged pointer escapes.

## 7. Concurrency and lifetime

`ShapedText` owns all returned strings and vectors. A `TextShaper` owns its
font bytes and native font objects. One instance is intended for its owning
renderer thread; callers must not concurrently mutate/reload and shape on the
same instance. Separate instances share no mutable shaping state.
