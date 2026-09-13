# Rowl Engine Production Platform Gates (1.0)

What "supported in production" means per target, and the current evidence.
Volatile per-commit CI run numbers live in the session log, not here; this
file records gate definitions and the durable local evidence.

## Gate definitions

A target is production-green only when **all** of these hold for it:

1. **Runtime build gate** — the native runtime and player compile cleanly
   for the target.
2. **Test gate** — the target's test suite passes (native CTest on
   Linux/Windows CI; device smoke where a device exists).
3. **Package gate** — a clean-room standalone release
   (`RowlGame`/`RowlGame.exe`, native `RowlEngineCore` library, canonical
   `Assets/packages/game.rowlpkg` with embedded manifest, `project.rowlproj`,
   `THIRD_PARTY_NOTICES.md`, launchers, `mods/`, `README.txt`) passes
   `tools/verify_release_package.py`.
4. **Smoke gate** — the packaged release renders its package-smoke frame
   (`RowlGame --project <release> --package-smoke-test` prints
   `Package smoke frame rendered`) with no missing-dependency errors.

## Linux x86_64 — GREEN (local Release gate)

- Build: `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release`
  + `cmake --build build-release --parallel 4`, clean, 58/58 targets.
- Clean-room release assembled in a fresh directory (no repo leakage):
  - `RowlGame` (player, reports v1.0.0), `libRowlEngineCore.so`,
    `game.rowlpkg` (13 entries), `project.rowlproj`,
    `THIRD_PARTY_NOTICES.md`, `run_game.sh`, `mods/README.md`, `README.txt`.
  - `ldd` on player and library: **0 missing** dependencies.
  - `verify_release_package.py`: `Valid release: game.rowlpkg contains
    13 entries including json/full_story_graph.json.`
  - Smoke: `Package smoke frame rendered` (exit 0, `SDL_AUDIODRIVER=dummy`).
- The same `Assets/` tree packed from the Debug tree hashes identically
  (`sha256 97fd13a8…b6a82`), confirming the determinism contract across
  build directories.

## Windows x64 — GREEN via CI (per-commit gate)

- Gated by the `windows` job of the `Build and test` workflow (vcpkg
  `sdl3/zstd/lua/nlohmann-json/libvorbis`, MSVC CMake configure + build,
  full CTest, standalone-package assembly, `verify_release_package.py`,
  shaderless VFS smoke with the packaged player).
- Policy: every `main` push must show `linux: success` **and**
  `windows: success` before its work is closed; the observed run numbers
  are recorded in the second-brain session log.
- Not claimed: interactive GUI/input/audio-device proof on real Windows
  hardware (tracked as pending, same as Linux GUI proof).

## Evidence-blocked targets (not supported — no illusion)

- **Android arm64-v8a:** host has SDK + build-tools + platform-tools
  (`adb` present) and Java, but **no NDK** (`ANDROID_NDK_HOME` unset, no
  `Sdk/ndk/*`) and no release keystore — `packaging/android/build.sh`
  aborts at NDK resolution, so no native runtime, no APK/AAB, no device
  run is possible here. `export_game.py android` is documented to stop at
  the native runtime and does not claim a package.
- **iOS:** `packaging/ios/build.sh` requires Darwin (`uname -s` guard);
  this host is Linux with no Xcode, no signing identity, no device — no
  artifact and no signed archive possible here.
- **macOS desktop:** no Apple host or toolchain on this machine; only
  dylib resolve paths exist in the editor project. No build, no package,
  no notarization proof.
- These targets stay blocked until hardware, SDKs/NDK, signing, and a
  physical device exist; the skeleton code (`EngineActivity`, manifest,
  `Info.plist`, build scripts) is kept only as a landing place.

## Clean-machine note

"Clean machine" above means a fresh directory with only the release files
plus system libraries (`ldd` clean). A truly foreign distribution is still
unproven; first install on a never-touched distro remains future work and
is tracked as such — the `ldd` + verifier + smoke triple is the current
evidence, not a claim beyond it.
