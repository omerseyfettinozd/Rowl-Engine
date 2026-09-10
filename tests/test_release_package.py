#!/usr/bin/env python3
"""Contract tests for portable release package verification."""

import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
VERIFIER = ROOT / "tools" / "verify_release_package.py"


def run_verifier(release_root):
    return subprocess.run([sys.executable, str(VERIFIER), str(release_root)],
                          capture_output=True, text=True, check=False)


def require_rejection(release_root, message):
    result = run_verifier(release_root)
    if result.returncode == 0 or message not in result.stderr:
        raise SystemExit(f"expected verifier rejection containing {message!r}: "
                         f"stdout={result.stdout!r}, stderr={result.stderr!r}")


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)
    release = root / "release"
    package = release / "Assets" / "packages" / "game.rowlpkg"
    package.parent.mkdir(parents=True)
    subprocess.run([sys.executable, str(PACKAGER), str(ROOT / "Assets"), str(package)], check=True)
    (release / "mods").mkdir()
    (release / "mods" / "README.md").write_text("# mods\n", encoding="utf-8")
    (release / "README.txt").write_text("release\n", encoding="utf-8")
    (release / "RowlGame").write_bytes(b"player")
    (release / "libRowlEngineCore.so").write_bytes(b"runtime")

    valid = run_verifier(release)
    if valid.returncode != 0 or "Valid release" not in valid.stdout:
        raise SystemExit("valid release package was rejected: " + valid.stderr)

    missing_runtime = root / "missing-runtime"
    shutil.copytree(release, missing_runtime)
    (missing_runtime / "libRowlEngineCore.so").unlink()
    require_rejection(missing_runtime, "missing native RowlEngineCore runtime library")

    corrupt_package = root / "corrupt-package"
    shutil.copytree(release, corrupt_package)
    (corrupt_package / "Assets" / "packages" / "game.rowlpkg").write_bytes(b"ROWL")
    require_rejection(corrupt_package, "package is shorter than its v1 header")

    loose_assets = root / "loose-assets"
    shutil.copytree(release, loose_assets)
    (loose_assets / "Assets" / "images").mkdir()
    require_rejection(loose_assets, "release Assets contains loose content")

    unsafe_mod = root / "unsafe-mod"
    shutil.copytree(release, unsafe_mod)
    outside = root / "outside.txt"
    outside.write_text("outside", encoding="utf-8")
    try:
        (unsafe_mod / "mods" / "override.txt").symlink_to(outside)
    except OSError as error:
        raise SystemExit("test host cannot create a symbolic link: " + str(error))
    require_rejection(unsafe_mod, "mods override contains a symbolic link")
