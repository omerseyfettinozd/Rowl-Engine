# Media Converters Contract (Faz 5 Dilim 5)

Source media that the runtime cannot ingest directly (MP3, FLAC, WebP) is
converted on the **host** into runtime formats (OGG Vorbis, PNG) by two
deterministic C tools. The runtime itself never converts: `OggStreamSource`
decodes OGG via vorbisfile, images decode via stb_image.

## Tools

| Tool | Input | Output | Libraries (linked) |
|---|---|---|---|
| `tools/rowl_oggenc` | raw s16le PCM (file or stdin) + `--rate`/`--channels` | `.ogg` (Vorbis `-q 4`) | libvorbisenc 1.3.7, libvorbis, libogg |
| `tools/rowl_webp2png` | `.webp` file | `.png` (8-bit RGBA) | libwebpdecoder 1.6.0, libpng 1.6.x |

Both are host-build tools (`ROWL_BUILD_TOOLS`, default ON except when
cross-compiling). They never ship in the game package, never link
`RowlEngineCore`, and never touch `Rowl::Audio`.

### Flags

- `rowl_oggenc [--rate HZ] [--channels N] -o OUT.ogg [--sidecar FILE] [IN.pcm|-]`
  (defaults: 44100 Hz, 2 ch). `--version` prints the vendor string.
  There is **no quality flag in v1**: quality is pinned to `-q 4`.
- `rowl_webp2png -o OUT.png [--sidecar FILE] IN.webp`.
  `--version` prints the converter version.

## Source decode (MP3 / FLAC → PCM)

Decoding is an **external-process call only** — nothing is linked
(ffmpeg is GPL; the engine only spawns it):

- `ffmpeg -hide_banner -loglevel error -i IN -ar 44100 -ac 2 -sample_fmt s16 -f s16le OUT.pcm`
  (dosyaya yazar — kod gerçeği `MediaConverterService.BuildFfmpegDecodeStartInfo`;
  stdout varyantı `-f s16le -` kullanılmaz)
- or `flac -d -c --force-raw-format --sign=signed --endian=little`

The resulting PCM bytes are the `source_sha256` input of the OGG sidecar.

## Determinism rules (both tools)

1. Same input bytes → byte-identical output (SHA-256 equal across runs,
   machines, and stdin-vs-file feeding).
2. `rowl_oggenc`: fixed vendor string `RowlEngine rowl_oggenc <semver>`,
   fixed quality `-q 4`, fixed Ogg serial = big-endian uint32 of
   `SHA-256(pcm_input)[0..3]`, fixed 4096-frame analysis blocks.
3. `rowl_webp2png`: fixed libpng writer — 8-bit RGBA, non-interlaced,
   compression level 6 / mem 8 / default strategy, and **never**
   `tIME`, `tEXt`/`zTXt`/`iTXt`, `pHYs` (DPI), or `iCCP`. Output chunks
   are exactly `IHDR / IDAT* / IEND`.
4. ffmpeg CLI **encode** usage is banned inside both tools (its output is
   not byte-deterministic); `test_converters.py` greps the sources for it.
5. PNG writer selection: libpng was chosen after the double-write probe
   (same WebP → two runs → equal SHA-256, IHDR/IDAT/IEND only); no
   embedded writer was needed.

## Sidecar schema (`<output>.rowlconv.json`)

Written by the tool itself when `--sidecar` is passed. Audio:

```json
{
  "source_sha256": "<hex of raw s16le PCM input>",
  "converter_name": "rowl_oggenc",
  "converter_version": "1.0.0",
  "settings": {"quality_q": 4, "sample_rate_hz": 44100, "channels": 2, "serial": 464258810},
  "output_sha256": "<hex of .ogg bytes>",
  "created_by": "rowl_oggenc 1.0.0"
}
```

Image: same shape with `"converter_name": "rowl_webp2png"` and
`"settings": {"png_writer": "libpng", "dpi": null}` (`dpi` is null
because the writer never emits `pHYs`).

`settings` is REQUIRED (object; the C# reader rejects settings-less
sidecars as invalid). Optional top-level `"source_path"` carries the
SourceAssets-relative source path (e.g. `"sfx/theme.mp3"`): in practice
the tools never write it — the C# convert lane stamps it right after the
tool writes the sidecar (`MediaConverterService.StampSourcePath`, hash-excluded
metadata: `output_sha256` is never recomputed, determinism unaffected).
The linter and the packager (`converted_from.path`) read the stamp first
and fall back to stem search when absent.

## Provenance C API

Capability `ROWL_ENGINE_CAPABILITY_CONVERTER_PROVENANCE` (`131072`):

`RowlEngine_GetAssetProvenanceJson(handle, assetPathUtf8, buffer,
bufferSize, outRequiredSize)` reads `<path>.rowlconv.json` through the
active VFS and copies it with the standard caller-buffer contract
(NULL/0 size query, undersized buffer clears + `BUFFER_TOO_SMALL`).

- Sidecar missing → `ROWL_RESULT_FILE_NOT_FOUND`. This is the **normal**
  state for assets that were never converter-produced, not an error.
- Sidecar present but not JSON / schema keys missing →
  `ROWL_RESULT_PARSE_ERROR`.
- Asset path bounded (NUL within 256 KiB + 1), else `INVALID_ARGUMENT`;
  dead handle → `INVALID_HANDLE`.

## Test matrix

| Area | Test | Location |
|---|---|---|
| OGG determinism (file/stdin ×2, SHA equal, serial/vendor/sidecar) | converter tools | `tests/test_converters.py` |
| MP3/FLAC documented decode commands | converter tools | `tests/test_converters.py` |
| OGG round-trip (vorbisfile decode = same lib as `OggStreamSource`; duration ±1 %, mid-section correlation > 0.90, RMS ratio in [0.5, 2]) | converter tools | `tests/test_converters.py` |
| PNG determinism + chunk hygiene + pixel equality + stb_image open | converter tools | `tests/test_converters.py` |
| No-ffmpeg-CLI hygiene grep | converter tools | `tests/test_converters.py` |
| Provenance present / missing / corrupt + size-query / undersized + null-handle + capability bit | native suite | `tests/test_converter_provenance.cpp` |
| ABI additivity (203 → 204 symbols, 0 removed) | ABI gate | `tools/abi_baseline.txt` + `tests/test_abi_tools.py` |

Known non-goals (v1): no quality/resample knobs on the tools, no
in-engine conversion, no MP3/FLAC/WebP runtime ingestion. The
`[converter-required]` pack gate in `tools/package_assets.py` was lifted
by this slice: converted assets pack with a `converted_from` manifest
entry (source path + hash); sidecar-less `.ogg`/`.png` still pack as
normal assets (fail-open, no break).
