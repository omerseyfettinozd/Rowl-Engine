# Rowl Engine — Üçüncü Taraf Lisans Envanteri

Bu dosya, Rowl Engine'in bağladığı, gömdüğü veya dış proses olarak
çağırdığı tüm üçüncü taraf bileşenlerin lisans envanteridir.
`tests/test_third_party_licenses.py` bu tabloyu makineyle doğrular
(bütünlük kapısı).

Kural: yeni bir bağımlılık ekleyen, bu tabloya bir satır eklemek
zorundadır; tablo ile gerçek (dosya başlığı / FetchContent / nuspec)
uyuşmazsa test kırmızıya döner.

## GPL Bulgusu (Faz 6 Dilim 9)

**Motor ikili dosyasında (RowlEngineCore / rowl_player) GPL ile
bağlantı (link) YOKTUR.** Kanıtlar:

- `ffmpeg`, yalnızca harici proses olarak çağrılır (link yok):
  sözleşme `docs/MEDIA_CONVERTERS_CONTRACT.md` ("Decoding is an
  **external-process call only** — nothing is linked (ffmpeg is GPL;
  the engine only spawns it)"), kod gerçeği
  `editor/Services/MediaConverterService.cs`
  (`BuildFfmpegDecodeStartInfo`, `ProcessStartInfo` ile
  `FileName = ffmpegPath`; dosyada "ffmpeg GPL'e link YOK" notu).
- `flac` aynı şekilde yalnızca harici proses (`flac -d -c ...`
  sözleşmede; link yok).
- Host dönüştürücü araçları (`tools/rowl_oggenc.c`,
  `tools/rowl_webp2png.c`) ffmpeg CLI **encode** yolu içermez
  (kaynak başlığında yazar; `tests/test_converters.py` bunu grepler).
- Dağıtım uyarısı: oyun paketiyle birlikte bir `ffmpeg` ikili dosyası
  dağıtılırsa o ikili dosyanın GPL yükümlülükleri doğar — bu, motorun
  değil, paketleyenin sorumluluğudur.

## Envanter Tablosu

| Kütüphane | Sürüm | Lisans (SPDX) | Kaynak-kanıt (dosya:satır veya URL) |
|---|---|---|---|
| stb_image (gömülü) | v2.30 | MIT OR Unlicense | engine/include/thirdparty/stb_image.h:1 (sürüm), engine/include/thirdparty/stb_image.h:7952 (ALTERNATIVE A - MIT), engine/include/thirdparty/stb_image.h:7970 (ALTERNATIVE B - Public Domain) |
| stb_truetype (gömülü) | v1.26 | MIT OR Unlicense | engine/include/thirdparty/stb_truetype.h:1 (sürüm), engine/include/thirdparty/stb_truetype.h:5043 (ALTERNATIVE A - MIT), engine/include/thirdparty/stb_truetype.h:5062 (public domain) |
| nlohmann/json (FetchContent) | v3.11.3 | MIT | engine/CMakeLists.txt:59-64 (FetchContent bloğu: GIT_REPOSITORY https://github.com/nlohmann/json.git + GIT_TAG v3.11.3) |
| rowl_sha256.h (Rowl-yazımı, header-only) | v1.0.0-araç-içi | Unlicense | tools/rowl_sha256.h:2 (public-domain SHA-256 beyanı) |
| SDL3 (sistem) | 3.4.16 | Zlib | /usr/share/licenses/sdl3/LICENSE (zlib metni); sürüm: pkg-config sdl3 |
| Lua (sistem) | 5.5.1 | MIT | /usr/share/licenses/lua/LICENSE; sürüm: /usr/lib/liblua5.5.so (CMakeCache LUA_LIBRARY), lua -v |
| zstd (sistem) | 1.5.7 | BSD-3-Clause | /usr/include/zstd.h: ZSTD_VERSION_MAJOR 1 MINOR 5; paket: zstd 1.5.7 |
| libogg (sistem) | 1.3.6 | BSD-3-Clause | /usr/share/licenses/libogg/; paket: libogg 1.3.6 |
| libvorbis — vorbisfile/vorbisenc (sistem) | 1.3.7 | BSD-3-Clause | /usr/share/licenses/libvorbis/; sürüm: pkg-config vorbisfile |
| FreeType2 (sistem, shaping) | 2.14.3 | FTL | /usr/share/licenses/freetype2/; paket: freetype2 2.14.3 |
| HarfBuzz (sistem, shaping) | 14.4.0 | MIT | /usr/share/licenses/harfbuzz/; sürüm: pkg-config harfbuzz |
| FriBidi (sistem, shaping) | 1.0.16 | LGPL-2.1-or-later | paket: fribidi 1.0.16 (dinamik link; kaynak değişikliği yok) |
| libunibreak (sistem, shaping) | 7.0 | Zlib | /usr/share/licenses/libunibreak/; paket: libunibreak 7.0 |
| libpng (sistem, host araç) | 1.6.58 | Libpng | /usr/share/licenses/libpng/; sürüm: pkg-config libpng |
| libwebpdecoder (sistem, host araç) | 1.6.0 | BSD-3-Clause | /usr/share/licenses/libwebp/; sürüm: pkg-config libwebpdecoder |
| ffmpeg (DIŞ PROSES — link yok) | 9.0.1 (ortam) | GPL (yalnızca çağrılan ikili dosya; motora bulaşmaz) | docs/MEDIA_CONVERTERS_CONTRACT.md:30, editor/Services/MediaConverterService.cs:30 (link YOK) |
| flac CLI (DIŞ PROSES — link yok) | 1.5.0 (ortam) | GPL-2.0-or-later (CLI; dış proses, link yok — libFLAC BSD) | docs/MEDIA_CONVERTERS_CONTRACT.md:34, paket: flac 1.5.0, lisans beyanı: https://xiph.org/flac/ (araçlar GPL, kütüphaneler BSD) |
| Avalonia (NuGet) | 11.3.11 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: avalonia/11.3.11/avalonia.nuspec (license type=expression MIT) |
| Avalonia.Desktop (NuGet) | 11.3.11 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: avalonia.desktop/11.3.11/*.nuspec (MIT) |
| Avalonia.Headless (NuGet) | 11.3.11 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: avalonia.headless/11.3.11/*.nuspec (MIT) |
| Avalonia.Themes.Fluent (NuGet) | 11.3.11 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: avalonia.themes.fluent/11.3.11/*.nuspec (MIT) |
| Avalonia.Fonts.Inter (NuGet) | 11.3.11 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: avalonia.fonts.inter/11.3.11/*.nuspec (MIT) |
| CommunityToolkit.Mvvm (NuGet) | 8.4.0 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: communitytoolkit.mvvm/8.4.0/communitytoolkit.mvvm.nuspec (MIT) |
| Tmds.DBus.Protocol (NuGet) | 0.21.3 | MIT | editor/RowlEngine.Editor.csproj: PackageReference; NuGet önbelleği: tmds.dbus.protocol/0.21.3/tmds.dbus.protocol.nuspec (MIT) |

Satır sayısı: 24.

## Yükümlülük Notları (lisans türüne göre)

- **MIT / Zlib / BSD-3-Clause / Libpng / Unlicense / FTL**
  (koşul-atıf): ikili dağıtımda lisans metninin korunması yeterlidir;
  kaynak açma zorunluluğu doğurmaz. `stb_*` çift lisanslıdır
  (MIT ya da Unlicense — ikisi de atıf-dışı serbesttir).
- **LGPL-2.1-or-later (yalnızca FriBidi)**: dinamik bağlanır, motor
  FriBidi kaynağını değiştirmez; bu kullanımda kaynak-açma tetiklenmez.
  FriBidi statik bağlanırsa veya yamalanırsa bu not güncellenmelidir.
- **GPL (yalnızca harici `ffmpeg` / `flac` CLI prosesleri)**: motora link OLMADIĞI için
  motor ikilisine GPL bulaşmaz. Bu ikili dosyalardan birini oyunla birlikte
  dağıtan, o dosyanın GPL yükümlülüklerine (kaynak teklifi) kendisi uyar.
- **NuGet (MIT)**: paket lisansları yerel nuspec'lerden doğrulanmıştır;
  doğrulanamayan bir paket `NUGET-LISANS-BAK` işaretlenir ve test onu
  atlar (ama sayar — sessiz geçme yoktur).
