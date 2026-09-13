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

## Current status

| Target | Runtime build gate | Package/device gate | Status |
| --- | --- | --- | --- |
| Linux desktop x86_64 | Native CTest, shaderless fallback and GPU-MSDF smoke CI tests | Fresh deterministic `game.rowlpkg` (embedded manifest, license inventory) plus VFS package smoke | CI build/test/package gates green, incl. run 34744730099; interactive GUI proof pending |
| Windows desktop x64 | vcpkg CMake/CTest CI job | Fresh standalone package, DLL and shaderless VFS smoke in CI | CI build/test/package gates green, incl. run 34744730099; interactive GUI/input/audio device proof pending |
| macOS desktop | None — never built or tested; no host hardware available | None yet | Skeleton only (dylib resolve paths); evidence-blocked, not supported |
| Android arm64-v8a | Core-only NDK CMake script, never executed here | No APK/AAB is produced; physical-device test pending | Host skeleton only (`EngineActivity`, manifest); evidence-blocked, not supported |
| iOS | Core-only Xcode/CMake arm64 script, never executed here | No signed app is produced; physical-device test pending | Host skeleton only (`Info.plist`, build script); evidence-blocked, not supported |

macOS, Android, and iOS stay **evidence-blocked**: their host skeletons
exist so the architecture has somewhere to land, but without host
hardware, SDKs, signing, or a physical device there is no build, package,
or smoke proof to claim. A target leaves this state only through the
contract at the top of this file.

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
build type, OS/device identity, fixture identity, VFS I/O, JSON update, first
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
