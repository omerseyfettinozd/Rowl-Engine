# Rowl Engine — Third-Party License Inventory

Standalone Rowl Engine releases (`RowlGame` + `RowlEngineCore` + canonical
`Assets/packages/game.rowlpkg`) bundle or link the open-source components
below. Licenses are named as declared upstream; the full license texts ship
with their packages (system packages, vcpkg ports, or NuGet `.nupkg`
`THIRD-PARTY-NOTICES` files) and apply in addition to Rowl Engine's own
terms. This file is copied byte-for-byte into every standalone build by
`ProjectBuildService` and required there by `tools/verify_release_package.py`.

## Native runtime (C++ engine)

| Component | Version pin | License (upstream) | Upstream |
|---|---|---|---|
| SDL3 | CI builds 3.4.16 from source / vcpkg `sdl3` | zlib | https://github.com/libsdl-org/SDL |
| Lua | `find_package(Lua REQUIRED)` (bu makine: 5.5.1; CI: liblua5.4-dev, vcpkg `lua` — dağıtım-paketi CI pinine göre değişir) | MIT | https://www.lua.org/license.html |
| Zstd | system libzstd / vcpkg `zstd` | BSD-3-Clause (dual BSD/GPLv2, BSD applies) | https://github.com/facebook/zstd |
| nlohmann/json | `v3.11.3` (pinned `FetchContent` fallback) | MIT | https://github.com/nlohmann/json |
| Ogg/Vorbis (`vorbisfile`) | system libvorbis / vcpkg `libvorbis` | BSD-3-Clause (Xiph) | https://xiph.org/vorbis/ |
| stb (`stb_image.h`, `stb_truetype.h`, vendored under `engine/include/thirdparty/`) | vendored snapshot | MIT | https://github.com/nothings/stb |
| FreeType2 (system, text shaping) | system freetype2 (bu makine: 2.14.3) | FTL | https://freetype.org |
| HarfBuzz (system, text shaping) | system harfbuzz (bu makine: 14.4.0) | MIT | https://harfbuzz.github.io |
| FriBidi (system, text shaping) | system fribidi (bu makine: 1.0.16; dinamik link, kaynak değişikliği yok) | LGPL-2.1-or-later | https://github.com/fribidi/fribidi |
| libunibreak (system, text shaping) | system libunibreak (bu makine: 7.0) | zlib | https://github.com/adah1972/libunibreak |
| libpng (system, host tool `rowl_webp2png`) | system libpng (bu makine: 1.6.58) | Libpng | http://www.libpng.org/pub/png/libpng.html |
| libwebpdecoder (system, host tool `rowl_webp2png`) | system libwebp (bu makine: 1.6.0) | BSD-3-Clause | https://developers.google.com/speed/webp |
| ffmpeg (DIŞ PROSES — link yok) | ortam ikili dosyası (bu makine: 9.0.1) | GPL (yalnızca çağrılan ikili dosya; motora bulaşmaz) | `docs/MEDIA_CONVERTERS_CONTRACT.md` |
| flac CLI (DIŞ PROSES — link yok) | ortam ikili dosyası (bu makine: 1.5.0) | GPL-2.0-or-later (CLI; dış proses, link yok — libFLAC BSD) | `docs/MEDIA_CONVERTERS_CONTRACT.md` |

## Editor (C# / Avalonia, ships in editor and tooling builds)

| Component | Version pin | License (upstream) | Upstream |
|---|---|---|---|
| Avalonia (+ Desktop, Themes.Fluent, Fonts.Inter) | 11.3.11 | MIT | https://github.com/AvaloniaUI/Avalonia |
| CommunityToolkit.Mvvm | 8.4.0 | MIT (.NET Foundation) | https://github.com/CommunityToolkit/dotnet |
| Tmds.DBus.Protocol | 0.21.3 | MIT | https://github.com/tmds/Tmds.DBus |
| SkiaSharp / HarfBuzzSharp (transitive via Avalonia) | via Avalonia 11.3.11 | MIT | https://github.com/mono/SkiaSharp |
| .NET runtime | 10.0.x | MIT | https://github.com/dotnet/runtime |

## Pack-time tooling (not shipped in releases)

- `zstandard` Python package (optional; without it the packer stores entries
  raw): BSD-3-Clause, https://github.com/indygreg/python-zstandard.
- Test-only packages (xunit, `Microsoft.NET.Test.Sdk`, coverlet) never ship
  in a release and are therefore not inventoried here.

## Notes

- SDL3's zlib license, the MIT licenses, and the BSD licenses all permit
  commercial redistribution provided their copyright/license notices are
  preserved — which is what this file, together with the upstream notice
  files referenced above, does.
- FriBidi is LGPL-2.1-or-later and links dynamically with no source
  changes; ffmpeg/flac are external processes only (never linked), so
  neither GPL condition touches the engine binary. The full inventory and
  obligation notes live in the root `THIRD_PARTY_LICENSES.md`.
- If a dependency version changes, update this table in the same commit.
