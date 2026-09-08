# Default MSDF atlas

`default.png` and `default.json` are the standalone player's bundled MSDF
atlas. They are generated from `../default.ttf` with
[`msdf-atlas-gen`](https://github.com/Chlumsky/msdf-atlas-gen):

```sh
msdf-atlas-gen -font Assets/fonts/default.ttf -type msdf -format png \
  -dimensions 1024 1024 -pxrange 4 -yorigin top \
  -imageout Assets/fonts/msdf/default.png \
  -json Assets/fonts/msdf/default.json
```

Keep the PNG and JSON together. The runtime requires the generator's
`atlas.distanceRange`, dimensions, glyph `planeBounds`, and `atlasBounds`
fields. The first GPU delivery targets Linux/Vulkan SPIR-V; non-SPIR-V
platform shader packages are deliberately deferred until their host validation.
