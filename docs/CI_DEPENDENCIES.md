# CI Bağımlılık Sabitleme Kaydı (T0)

Bu dosya CI'nın o günkü gerçek girdi sürümlerini repoda sabitler.
Amaç: "CI'da çalışıyordu" cümlesinin hangi imaj/paketle kurulduğunu
bilmek; ortam kayması 3 fix turu yemeden yakalanır.

Snapshot: 2026-09-17 — CI-14 `35270524431` 5/5 yeşil.
Kaynak: `.github/workflows/ci.yml` (bu dosyayla çelişirse CI dosyası
gerçektir; bu kayıt o zaman güncellenir, tersi değil).

## Sabit pin'ler

| Bağımlılık | Sürüm / kaynak | Kullanıldığı job |
|---|---|---|
| SDL3 | `release-3.4.16` tarball (libsdl-org) | linux, sanitizer, linux-arm64, macos-arm64 (kaynakten derlenir) / windows (vcpkg `sdl3` portu) |
| .NET SDK | `10.0.x` (`actions/setup-dotnet@v4`) | linux, windows |
| vcpkg triplet | `x64-windows` (classic) | windows |
| vcpkg portları | `sdl3 zstd lua nlohmann-json libogg libvorbis libpng libwebp freetype harfbuzz fribidi libunibreak` | windows |
| choco | `pkgconfiglite`, `ffmpeg` | windows |
| MSVC setup | `ilammy/msvc-dev-cmd@v1` (arch x64) | windows |
| actions/checkout | `v4` | tümü |
| actions/upload-artifact | `v4` | linux, linux-arm64, macos-arm64 |
| cmake / ninja | runner-imajı sağlar (sürüm pin'lenmez, parity scripti varlığı kontrol eder) | tümü |

## Linux apt kümeleri (ubuntu-24.04)

- linux/sanitizer: `build-essential cmake curl ninja-build pkg-config
  liblua5.4-dev libzstd-dev libvorbis-dev libogg-dev libpng-dev
  libwebp-dev ffmpeg libfreetype-dev libharfbuzz-dev libfribidi-dev
  libunibreak-dev libvulkan-dev glslc mesa-vulkan-drivers xvfb`
  + SDL X11/Wayland dev paketleri (tam liste ci.yml'de).
- linux-arm64: üsttekinin `ffmpeg`/shaping-dışı varyantı + `file`
  (ci.yml'deki liste esastır).

## macOS brew (macos-15)

`lua zstd libvorbis libogg libpng webp pkg-config cmake ninja`.

## Davranış pin'leri (sürüm değil, kapı)

- Windows `ROWL_PERF_FLOOR=report` (fail kapısı Linux-only — Faz 4.5 D1).
- Windows ctest `--timeout 300` (`rowl_native_tests` TIMEOUT 1500).
- Sanitizer PR'da hızlı alt-küme (`ROWL_SKIP_LONG_TESTS=1`), tam süit
  nightly'da (`sanitizer-nightly`, schedule).
- `sanitizer` işi ASan+UBSan, `detect_leaks=0` (LSan follow-up).

## Bakım

Bu listedeki HERHANGİ bir girdi değiştiğinde (ci.yml editi) bu dosya
aynı commit'te güncellenir. `tools/check_parity.sh` yerelde ana
araçların varlığını/sürüm ailesini doğrular.
