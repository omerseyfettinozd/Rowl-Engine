#!/usr/bin/env python3
"""Package the First Light sample and run it through the release contract.

Builds a portable release layout from samples/first_light (packaged VFS,
no loose Assets), validates it with tools/verify_release_package.py, then
runs the standalone player --package-smoke-test against it: one real frame
rendered from the packaged story graph, offscreen, on every platform.
"""

import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
VERIFIER = ROOT / "tools" / "verify_release_package.py"
SAMPLE = ROOT / "samples" / "first_light"


def run(*args, env=None):
    return subprocess.run([str(a) for a in args], capture_output=True,
                          text=True, check=False, env=env)


def validate_golden_manifest(sample_dir):
    """Reject accidental fixture drift before comparing platform results."""
    manifest_path = sample_dir / "golden_project.json"
    if not manifest_path.is_file():
        return
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema_version") != 1 or not manifest.get("fixture_id"):
            raise ValueError("unsupported or missing Golden Project identity")
        checksums = manifest.get("sha256")
        if not isinstance(checksums, dict) or not checksums:
            raise ValueError("Golden Project has no checksum map")
        for relative, expected in checksums.items():
            relative_path = pathlib.PurePosixPath(relative)
            if relative_path.is_absolute() or ".." in relative_path.parts:
                raise ValueError(f"unsafe Golden Project path: {relative}")
            candidate = sample_dir.joinpath(*relative_path.parts)
            if not candidate.is_file():
                raise ValueError(f"missing Golden Project file: {relative}")
            actual = hashlib.sha256(candidate.read_bytes()).hexdigest()
            if actual != expected:
                raise ValueError(
                    f"Golden Project checksum mismatch for {relative}: {actual}"
                )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid Golden Project manifest: {error}") from error


def main():
    if len(sys.argv) != 4:
        print(f"usage: {sys.argv[0]} <player-bin> <engine-lib> <sample-dir>",
              file=sys.stderr)
        return 2
    player_bin = pathlib.Path(sys.argv[1])
    engine_lib = pathlib.Path(sys.argv[2])
    sample_dir = pathlib.Path(sys.argv[3])
    for path in (player_bin, engine_lib, sample_dir / "project.rowlproj",
                 sample_dir / "Assets" / "json" / "full_story_graph.json"):
        if not path.is_file():
            print(f"missing input: {path}", file=sys.stderr)
            return 2
    try:
        validate_golden_manifest(sample_dir)
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="rowl-demo-packaged-") as directory:
        release = pathlib.Path(directory) / "release"
        (release / "Assets" / "packages").mkdir(parents=True)
        (release / "mods").mkdir(parents=True)

        package = release / "Assets" / "packages" / "game.rowlpkg"
        pack = run(sys.executable, PACKAGER, sample_dir / "Assets", package)
        if pack.returncode != 0:
            print(f"packager failed: {pack.stdout}{pack.stderr}", file=sys.stderr)
            return 1

        player_name = "RowlGame.exe" if os.name == "nt" else "RowlGame"
        shutil.copy2(player_bin, release / player_name)
        shutil.copy2(engine_lib, release / engine_lib.name)
        shutil.copy2(sample_dir / "project.rowlproj", release / "project.rowlproj")
        (release / "mods" / "README.md").write_text("# Rowl Engine mods\n", encoding="utf-8")
        (release / "README.txt").write_text(
            "ROWL ENGINE - FIRST LIGHT SAMPLE RELEASE\n", encoding="utf-8")
        (release / "run_game.sh").write_text(
            "#!/bin/sh\nexec ./%s \"$@\"\n" % player_name, encoding="utf-8")

        verify = run(sys.executable, VERIFIER, release)
        if verify.returncode != 0:
            print(f"release verifier failed: {verify.stdout}{verify.stderr}",
                  file=sys.stderr)
            return 1

        env = dict(os.environ)
        env["SDL_AUDIODRIVER"] = "dummy"
        smoke = run(release / player_name, "--project", release,
                    "--package-smoke-test", env=env)
        if smoke.returncode != 0 or "Package smoke frame rendered" not in smoke.stdout:
            print(f"package smoke failed (exit {smoke.returncode}): "
                  f"{smoke.stdout}{smoke.stderr}", file=sys.stderr)
            return 1

    print("[DemoPackaged] First Light release packages, verifies, and renders.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
