# Rowl Engine Platform Support Contract

Rowl Engine targets a shared C++ runtime with platform-specific hosts and
packages. A UI target or a successful cross-compile alone does **not** mean a
platform is supported: a platform becomes supported only after its release
package and a real-device smoke test pass.

## Current status

| Target | Runtime build gate | Package/device gate | Status |
| --- | --- | --- | --- |
| Linux desktop | Native CTest, shaderless fallback and GPU-MSDF smoke CI tests | Fresh `game.rowlpkg` plus VFS package smoke | Local package smoke validated; CI gate configured |
| Windows desktop | vcpkg CMake/CTest CI job | Fresh standalone package, DLL and shaderless VFS smoke in CI | Gate configured; first CI result pending |
| macOS desktop | None yet | None yet | Planned |
| Android arm64-v8a | Core-only NDK CMake script | APK and physical-device test pending | Build path prepared |
| iOS | Core-only Xcode/CMake arm64 script | Signed app and physical-device test pending | Build path prepared |

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
