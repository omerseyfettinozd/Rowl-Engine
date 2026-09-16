# Rowl Engine Platform Support Contract

Rowl Engine targets a shared C++ runtime with platform-specific hosts and
packages. A UI target or a successful cross-compile alone does **not** mean a
platform is supported: a platform becomes supported only after its release
package and a real-device smoke test pass.

## Multi-runtime event contract

Multiple C API handles may coexist with isolated VFS and runtime state. SDL
video/audio lifetime is leased process-wide, while visible or embedded SDL
windows are registered with one process UI/event thread. The dispatcher pumps
SDL's shared queue once and routes key, pointer, resize, and close events by
window ID; an SDL quit request is broadcast to every registered visible runtime.
Offscreen handles remain independent under their existing per-handle owner
thread contract.

## User data directory contract

The default `PlatformHost` keeps save files and future player profiles outside
the installation/project directory. Linux uses
`$XDG_DATA_HOME/rowl-engine/{saves,profiles}` when `XDG_DATA_HOME` is absolute,
otherwise `~/.local/share/rowl-engine/{saves,profiles}`. Windows uses the
Unicode LocalAppData Known Folder and appends
`rowl-engine\\{saves,profiles}`. Native filesystem paths remain wide on Windows;
the C ABI exposes them as UTF-8 through a required-size query followed by a
caller-owned buffer copy. An explicit editor/project save override continues
to take precedence for backward compatibility.

This is an implemented path and ABI contract, not a Windows device result.
Interactive Windows Unicode save/load remains part of the real-device gate.

## Current status

| Target | Runtime build gate | Package/device gate | Status |
| --- | --- | --- | --- |
| Linux desktop x86_64 | Native CTest, shaderless fallback and GPU-MSDF smoke CI tests | Fresh deterministic `game.rowlpkg` (embedded manifest, license inventory) plus VFS package smoke | CI build/test/package gates green, incl. run 34744730099; interactive GUI proof pending |
| Linux desktop arm64 | Native `ubuntu-24.04-arm` compile job with ELF AArch64 artifact checks | No release package or GUI/device run in this gate | Compile portability only; package and device validation pending |
| Windows desktop x64 | vcpkg CMake/CTest CI job | Fresh standalone package, DLL and shaderless VFS smoke in CI | CI build/test/package gates green, incl. run 34744730099; interactive GUI/input/audio device proof pending |
| macOS desktop arm64 | Native `macos-15` compile job `macos-arm64-compile` (pinned SDL3 3.4.16, Mach-O arm64 `file` checks, artifact upload; no CTest) | No signed package or device run in this gate | D1 compile-gate only; package, signing and device validation pending |
| Android arm64-v8a | Core-only NDK CMake script, never executed here | No APK/AAB is produced; physical-device test pending | Host skeleton only (`EngineActivity`, manifest); evidence-blocked, not supported |
| iOS | Core-only Xcode/CMake arm64 script, never executed here | No signed app is produced; physical-device test pending | Host skeleton only (`Info.plist`, build script); evidence-blocked, not supported |

Android and iOS stay **evidence-blocked**: their host skeletons
exist so the architecture has somewhere to land, but without host
hardware, SDKs, signing, or a physical device there is no build, package,
or smoke proof to claim. macOS left compile-blocked through the Faz 7 D1
gate below but remains device-blocked under the same rule. A target leaves
this state only through the contract at the top of this file.

### Faz 7 D1 — macOS compile gate (CI-only, no device proof)

`.github/workflows/ci.yml` gains `macos-arm64-compile` (`runs-on: macos-15`,
arm64): Homebrew deps plus the same pinned SDL3 3.4.16 tarball as the
`linux-arm64-compile` precedent, plain CMake configure/build,
`file ... | grep -q 'Mach-O.*arm64'` verification and artifact upload. CTest
is deliberately absent (no display server, no code signature on the hosted
runner) — the job name says "compile" on purpose.

Known Darwin gaps, recorded here and NOT fixed in D1 (adaptor skeleton is D2):
- `engine/src/platform/user_data_directories.cpp:80-83` — the `#else`
  HOME-fallback (`~/.local/share/...`) also catches Apple builds and violates
  the Apple convention (`~/Library/Application Support/...`). D2 item, untouched.
- Root `CMakeLists.txt:253` — `if(UNIX AND NOT APPLE)` keeps the GPU-MSDF
  smoke test Linux-only. Irrelevant to the D1 job (no CTest runs there), but
  any future macOS device gate must define the macOS GPU/smoke story.

D2 preview: the macOS adaptor skeleton (user-data directory, packaging and
signing shape) lands in D2; production code stays untouched in D1.

## Faz 4.5 — Wayland explicitly unsupported (release note)

Wayland sessions are **not supported** and fail closed, never silently:

- `Window::initializeEmbedded` rejects a Wayland SDL video driver up front
  (`SDL_GetCurrentVideoDriver() == "wayland"` → log + video-lease cleanup +
  `return false`). No crash, no silent X11-handle fallback.
- The Linux embedded path compiles X11 handle semantics only under
  `#elif defined(__linux__)`; any other platform hits a final `#else` that
  logs, releases the video lease, and returns `false`.
- Real Wayland support is a **Faz 5** item whose prerequisite is
  Avalonia-handle detection on the editor side (docs-only in this slice:
  `EngineHost.cs` / `MainWindowViewModel.cs` / `editor/` untouched).

## Mobile host gate (Faz 6/7)

Desktop hardening in Faz 4.5 is mobile-safe by construction and blocks no
future mobile integration:

- `PlatformHost` exposes defaulted virtuals only (no pure virtuals), so a
  future Android/iOS shell adopts the base incrementally and degrades into
  safe desktop defaults (Active lifecycle, granted audio focus, empty input).
- Touch input has exactly one live path: `Window::pollEvents` pairs
  `FINGER_DOWN/UP` per finger, maps viewport-relatively, and classifies via
  `MobileInput::classifyTouchGesture`. The dead stateless `processSdlEvent`
  SDL switch was deleted in Faz 4.5 Dilim 4 (it duplicated normalization with
  a hardcoded 1920x1080 canvas).
- The `/system/fonts` mount is an optional, exists()-guarded, list-only
  fallback that resolves strictly after the project VFS; absent directories
  skip silently.
- The Faz 6/7 gate for claiming mobile support: a native host injecting a
  real `PlatformHost` + touch end-to-end proof on a physical device, through
  the contract at the top of this file.

## Renderer rule

The SDL/font renderer fallback is the baseline renderer. GPU-MSDF is an
optional optimization and must never be required to configure, build, package,
or render a game. A new GPU backend needs all of the following before it is
enabled by default for a target:

1. a target-native shader artifact and packaging rule;
2. a capability check at runtime;
3. a tested fallback to the baseline renderer; and
4. a CI build gate plus a real-device smoke test.

`ROWL_ENABLE_GPU_MSDF=OFF` is the required shaderless build configuration.
Linux enables the current Vulkan/SPIR-V artifact by default; other targets keep
it off until their artifact and validation exist.

## Low-end performance rule

Before 1.0, each supported release target must define and meet a measured
budget on its lowest supported device:

| Metric | Release criterion |
| --- | --- |
| Frame time | Stable 60 FPS target where the device supports it; otherwise a documented frame cap without sustained stutter |
| Memory | Peak runtime and texture-cache usage remain inside the device-specific budget |
| Startup | Story graph, core assets, and first frame load within the target-specific budget |
| Assets | Missing/corrupt/oversized assets fail safely and do not leave stale render or audio state |

`rowl_tests --benchmark-json <path>` writes a versioned JSON report containing
build type, OS/device identity, CPU architecture/model, fixture identity, VFS I/O, JSON update, first
frame, steady frame, texture cache and process memory values.
`tools/compare_benchmarks.py` rejects different OS/machine/CPU/build-type or
fixture identities, then reports percentage deltas for compatible reports.
These numbers are baselines, not universal pass/fail thresholds; compare
like-for-like Release builds on the same device.

## Texture-cache rule

The runtime uses a 64 MiB decoded-RGBA texture-cache budget by default. When a
new texture needs space, it evicts least-recently-used non-atlas textures first.
A texture that cannot fit by itself is rejected without evicting live state.
Hosts can select a lower device-profile budget through
`RowlEngine_SetTextureCacheBudgetBytes` (values below 1 MiB clamp to 1 MiB).
The MSDF atlas remains pinned while GPU text rendering is active.

`RowlEngine_GetTextureCacheBudgetBytes` and
`RowlEngine_GetTextureCacheEvictionCount` expose the active budget and the
number of LRU evictions since the last cache clear. Missing-texture lookups are
also capped at 512 cached paths, so malformed or generated content cannot grow
the negative cache without bound.
