#!/usr/bin/env python3
"""Package a sample project and run it through the release contract.

Builds a portable release layout from the selected sample (packaged VFS,
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
import uuid
import wave


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"
VERIFIER = ROOT / "tools" / "verify_release_package.py"
SAMPLE = ROOT / "samples" / "first_light"


def run(*args, env=None):
    return subprocess.run([str(a) for a in args], capture_output=True,
                          text=True, check=False, env=env)


# Fixture checksums are canonical LF (see .gitattributes). A Windows CRLF
# checkout must still verify, so text entries are normalized before hashing.
# This bit CI runs 34694768878/34694916995/34695172306, where project.rowlproj
# hashed as CRLF and failed rowl_demo_second_signal_packaged on Windows only.
TEXT_CHECKSUM_SUFFIXES = frozenset({".json", ".rowlproj"})


def canonical_checksum_bytes(relative, data):
    if pathlib.PurePosixPath(relative).suffix.lower() in TEXT_CHECKSUM_SUFFIXES:
        return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
    return data


def manifest_file(sample_dir, relative):
    relative_path = pathlib.PurePosixPath(relative)
    if relative_path.is_absolute() or ".." in relative_path.parts:
        raise ValueError(f"unsafe Golden Project path: {relative}")
    return sample_dir.joinpath(*relative_path.parts)


def validate_productization_cases(sample_dir, manifest, checksums):
    """Validate the v2 fixture data contract without claiming runtime locale support."""
    cases = manifest.get("productization_cases")
    if cases is None:
        return
    if not isinstance(cases, dict):
        raise ValueError("Golden Project productization cases must be an object")

    project = json.loads((sample_dir / "project.rowlproj").read_text(encoding="utf-8"))
    graph = json.loads(
        (sample_dir / "Assets" / "json" / "full_story_graph.json").read_text(encoding="utf-8")
    )
    content_ids = []
    for node in graph.get("nodes", []):
        for game_object in node.get("objects", []):
            for component in game_object.get("components", []):
                if component.get("type") != "dialogue":
                    continue
                content_id = component.get("data", {}).get("content_id")
                if not isinstance(content_id, str):
                    raise ValueError("Golden Project dialogue is missing content_id")
                try:
                    uuid.UUID(content_id)
                except ValueError as error:
                    raise ValueError(f"invalid dialogue content_id: {content_id}") from error
                content_ids.append(content_id)
    if not content_ids or len(content_ids) != len(set(content_ids)):
        raise ValueError("Golden Project dialogue content_ids must be present and unique")

    localization = cases.get("localization")
    if not isinstance(localization, dict):
        raise ValueError("Golden Project localization case is missing")
    default_locale = localization.get("default_locale")
    supported_locales = localization.get("supported_locales")
    catalogs = localization.get("catalogs")
    if (not isinstance(default_locale, str) or not isinstance(supported_locales, list)
            or default_locale not in supported_locales or not isinstance(catalogs, dict)):
        raise ValueError("invalid Golden Project localization declaration")
    if project.get("defaultLocale") != default_locale or project.get("supportedLocales") != supported_locales:
        raise ValueError("project locale declaration differs from Golden Project contract")
    for locale in supported_locales:
        relative = catalogs.get(locale)
        if not isinstance(relative, str) or relative not in checksums:
            raise ValueError(f"locale catalog is not checksummed: {locale}")
        catalog = json.loads(manifest_file(sample_dir, relative).read_text(encoding="utf-8"))
        entries = catalog.get("entries")
        if catalog.get("schema_version") != 1 or catalog.get("locale") != locale or not isinstance(entries, dict):
            raise ValueError(f"invalid locale catalog: {locale}")
        missing = sorted(set(content_ids) - set(entries))
        if missing:
            raise ValueError(f"locale catalog {locale} misses content_ids: {', '.join(missing)}")
        for content_id in content_ids:
            entry = entries[content_id]
            if not isinstance(entry, dict) or not isinstance(entry.get("speaker"), str) \
                    or not isinstance(entry.get("text"), str) or not isinstance(entry.get("alt_text"), str):
                raise ValueError(f"invalid locale entry: {locale}/{content_id}")

    long_audio = cases.get("long_audio")
    if not isinstance(long_audio, dict):
        raise ValueError("Golden Project long-audio case is missing")
    audio_relative = long_audio.get("path")
    minimum_duration = long_audio.get("minimum_duration_seconds")
    if not isinstance(audio_relative, str) or audio_relative not in checksums \
            or not isinstance(minimum_duration, (int, float)) or minimum_duration <= 0:
        raise ValueError("invalid Golden Project long-audio declaration")
    with wave.open(str(manifest_file(sample_dir, audio_relative)), "rb") as audio:
        frame_width = audio.getnchannels() * audio.getsampwidth()
        actual_frames = len(audio.readframes(audio.getnframes())) // frame_width
        duration = actual_frames / audio.getframerate()
    if duration < minimum_duration:
        raise ValueError(f"Golden Project long audio is only {duration:.3f} seconds")

    unicode_case = cases.get("unicode_path")
    unicode_relative = unicode_case.get("path") if isinstance(unicode_case, dict) else None
    if not isinstance(unicode_relative, str) or not any(ord(char) > 127 for char in unicode_relative) \
            or unicode_relative not in checksums or not manifest_file(sample_dir, unicode_relative).is_file():
        raise ValueError("invalid Golden Project Unicode-path declaration")

    corrupt_case = cases.get("corrupt_asset")
    corrupt_source = corrupt_case.get("source_path") if isinstance(corrupt_case, dict) else None
    if not isinstance(corrupt_source, str) or corrupt_source not in checksums \
            or corrupt_case.get("mutation") != "truncate_to_16_bytes" \
            or corrupt_case.get("expected_diagnostic") != "asset_decode_error":
        raise ValueError("invalid Golden Project corrupt-asset case")


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
            candidate = manifest_file(sample_dir, relative)
            if not candidate.is_file():
                raise ValueError(f"missing Golden Project file: {relative}")
            actual = hashlib.sha256(
                canonical_checksum_bytes(relative, candidate.read_bytes())
            ).hexdigest()
            if actual != expected:
                raise ValueError(
                    f"Golden Project checksum mismatch for {relative}: {actual}"
                )
        validate_productization_cases(sample_dir, manifest, checksums)
    except (OSError, ValueError, TypeError, KeyError, json.JSONDecodeError, wave.Error) as error:
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
        shutil.copy2(ROOT / "packaging" / "THIRD_PARTY_NOTICES.md",
                     release / "THIRD_PARTY_NOTICES.md")
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

    print(f"[DemoPackaged] {sample_dir.name} release packages, verifies, and renders.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
