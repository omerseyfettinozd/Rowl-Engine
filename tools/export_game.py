#!/usr/bin/env python3
"""Build Rowl Engine export targets without overstating produced artifacts.

The Android and iOS host projects are not complete package producers yet. The
commands below therefore build and verify only the native runtime artifacts.
They must not report an APK, AAB, or IPA until those files actually exist.
"""

import argparse
import pathlib
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]


def _run(command):
    subprocess.run(command, cwd=ROOT, check=True)


def _require_file(path, description):
    if not path.is_file():
        raise FileNotFoundError(f"{description} was not produced: {path}")
    return path


def export_pc():
    """Create a developer staging directory, not a verified release package."""
    print("[Export Tool] Creating PC developer staging directory...")
    out_dir = ROOT / "build" / "export_pc"
    out_dir.mkdir(parents=True, exist_ok=True)

    player_candidates = (
        ROOT / "build" / "bin" / "rowl_player",
        ROOT / "build" / "bin" / "rowl_player.exe",
    )
    runtime_candidates = (
        ROOT / "build" / "lib" / "libRowlEngineCore.so",
        ROOT / "build" / "lib" / "libRowlEngineCore.dylib",
        ROOT / "build" / "bin" / "RowlEngineCore.dll",
        ROOT / "build" / "lib" / "RowlEngineCore.dll",
    )
    player = next((path for path in player_candidates if path.is_file()), None)
    runtime = next((path for path in runtime_candidates if path.is_file()), None)
    if player is None or runtime is None:
        raise FileNotFoundError(
            "PC staging requires a built rowl_player and RowlEngineCore runtime"
        )

    shutil.copy2(player, out_dir / player.name)
    shutil.copy2(runtime, out_dir / runtime.name)
    print(f"[Export Tool] PC developer staging created: {out_dir}")
    print("[Export Tool] This is not a verified standalone release package.")


def export_android():
    """Build and verify the Android arm64 native runtime only."""
    print("[Export Tool] Building Android arm64-v8a native runtime...")
    _run(["bash", str(ROOT / "packaging" / "android" / "build.sh")])
    runtime = _require_file(
        ROOT / "build" / "android-arm64-v8a" / "lib" / "libRowlEngineCore.so",
        "Android native runtime",
    )
    print(f"[Export Tool] Android native runtime build successful: {runtime}")
    print("[Export Tool] APK/AAB was not produced; the Android app host is pending.")


def export_ios():
    """Build and verify an iOS native runtime artifact only."""
    print("[Export Tool] Building iOS arm64 native runtime...")
    _run(["bash", str(ROOT / "packaging" / "ios" / "build.sh")])

    build_root = ROOT / "build" / "ios-arm64"
    candidates = []
    if build_root.is_dir():
        for pattern in ("libRowlEngineCore.a", "libRowlEngineCore.dylib"):
            candidates.extend(build_root.rglob(pattern))
    if not candidates:
        raise FileNotFoundError(
            f"iOS native runtime was not produced under: {build_root}"
        )

    runtime = sorted(candidates)[0]
    print(f"[Export Tool] iOS native runtime build successful: {runtime}")
    print("[Export Tool] IPA was not produced; app host and signing are pending.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", default="pc", choices=("pc", "android", "ios"))
    args = parser.parse_args(argv)

    exporters = {
        "pc": export_pc,
        "android": export_android,
        "ios": export_ios,
    }
    try:
        exporters[args.target]()
    except (FileNotFoundError, OSError, subprocess.CalledProcessError) as error:
        print(f"[Export Tool] ERROR: {error}", file=sys.stderr)
        return error.returncode if isinstance(error, subprocess.CalledProcessError) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
