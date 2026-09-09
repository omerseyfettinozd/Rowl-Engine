# Rowl Engine Platform Support Contract

Rowl Engine targets a shared C++ runtime with platform-specific hosts and
packages. A UI target or a successful cross-compile alone does **not** mean a
platform is supported: a platform becomes supported only after its release
package and a real-device smoke test pass.

## Current status

| Target | Runtime build gate | Package/device gate | Status |
| --- | --- | --- | --- |
| Linux desktop | Native CTest and GPU-MSDF smoke test in CI | Local standalone player test | Validated development target |
| Windows desktop | vcpkg CMake/CTest CI job | Standalone package smoke test pending | Build gate configured |
| macOS desktop | None yet | None yet | Planned |
| Android arm64-v8a | NDK script exists | APK and physical-device test pending | Planned |
| iOS | None yet | None yet | Planned |

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

The native test executable prints repeatable VFS, JSON ingestion, render-frame,
and texture-cache measurements. These numbers are baselines, not universal
pass/fail thresholds; compare like-for-like Release builds on the same device.
