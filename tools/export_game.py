#!/usr/bin/env python3
"""Build Rowl Engine export targets without overstating produced artifacts.

The Android and iOS host projects are not complete package producers yet. The
commands below therefore build and verify only the native runtime artifacts.
They must not report an APK, AAB, or IPA until those files actually exist.

portable-zip (Faz 6 Dilim 3) — deterministic portable release archive:

  python3 tools/export_game.py portable-zip [--output FILE] [--player FILE]
      [--runtime FILE]... [--package FILE]

  Zip content (flat layout, no installer):
    <player-name>            built launcher binary (rowl_player / rowl_player.exe);
                             discovered under build/, overridable via --player
                             (hardcode yok: parametre veya kesif).
    <runtime-name>           RowlEngineCore shared lib(s); discovered under
                             build/, repeat --runtime to override.
    game.rowlpkg             demo package packed from samples/first_light/Assets
                             (or --package to reuse a prebuilt .rowlpkg).
    game.rowlpkg.sha256      canonical sha256sum sidecar for the demo package,
                             so `package_assets.py verify` passes on extraction.
    THIRD_PARTY_NOTICES.md   copied byte-for-byte from packaging/.
    VERSION                  git-sha + tarih; git yoksa `unknown` + tarih
                             (fail degil).
    SHA256SUMS               canonical `sha256sum` form (`<hash><two-spaces><name>`)
                             hashes of every other zip entry (itself excluded:
                             a self-hash is impossible).

  Determinism: entries written in sorted arcname order with a fixed timestamp
  (1980-01-01, or SOURCE_DATE_EPOCH when set), fixed ZIP_DEFLATED compression
  (level 9), fixed Unix permission bits (0o755 launcher, 0o644 everything
  else), stdlib zipfile/ZipInfo only — no external `zip` binary. Two runs
  produce byte-identical output ONLY under identical inputs: same commit (the
  git-sha lands in VERSION) and same UTC date — pin SOURCE_DATE_EPOCH for
  cross-day reproducibility (it fixes both the zip timestamps and the
  VERSION date).

  Verification (after unzipping to a temp dir):
    1. `sha256sum -c SHA256SUMS`
    2. `python3 tools/package_assets.py verify game.rowlpkg`
    3. THIRD_PARTY_NOTICES.md and VERSION are present.

  NSIS/WiX/installer are explicitly out of scope for this slice (dilim-disi).
"""

import argparse
import datetime
import hashlib
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Fixed zip entry timestamp (pre-1980 dates are invalid in the zip format).
PORTABLE_ZIP_EPOCH_STAMP = (1980, 1, 1, 0, 0, 0)

_PLAYER_CANDIDATES = (
    pathlib.Path("build") / "bin" / "rowl_player",
    pathlib.Path("build") / "bin" / "rowl_player.exe",
)
_RUNTIME_CANDIDATES = (
    pathlib.Path("build") / "lib" / "libRowlEngineCore.so",
    pathlib.Path("build") / "lib" / "libRowlEngineCore.dylib",
    pathlib.Path("build") / "bin" / "RowlEngineCore.dll",
    pathlib.Path("build") / "lib" / "RowlEngineCore.dll",
)
_PORTABLE_DEFAULT_OUTPUT = pathlib.Path("build") / "rowl-portable.zip"
_PORTABLE_SAMPLE_ASSETS = (
    pathlib.Path("samples") / "first_light" / "Assets"
)
_PORTABLE_NOTICES = pathlib.Path("packaging") / "THIRD_PARTY_NOTICES.md"


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


def _portable_zip_stamp():
    """Fixed zip entry timestamp; SOURCE_DATE_EPOCH wins when set and valid."""
    raw = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
    if raw:
        try:
            moment = datetime.datetime.fromtimestamp(
                int(raw), tz=datetime.timezone.utc
            )
            if moment.year >= 1980:
                return (moment.year, moment.month, moment.day,
                        moment.hour, moment.minute, moment.second)
        except (ValueError, OverflowError, OSError):
            pass
    return PORTABLE_ZIP_EPOCH_STAMP


def _portable_version_date():
    """VERSION date string; SOURCE_DATE_EPOCH wins when set and valid."""
    raw = os.environ.get("SOURCE_DATE_EPOCH", "").strip()
    if raw:
        try:
            moment = datetime.datetime.fromtimestamp(
                int(raw), tz=datetime.timezone.utc
            )
            return moment.strftime("%Y-%m-%d")
        except (ValueError, OverflowError, OSError):
            pass
    return datetime.datetime.now(tz=datetime.timezone.utc).strftime("%Y-%m-%d")


def _portable_git_sha():
    """Short git HEAD sha; `unknown` when git is absent or fails (not fatal)."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=ROOT, capture_output=True, text=True, check=False,
        )
    except OSError:
        return "unknown"
    sha = (result.stdout or "").strip()
    return sha if result.returncode == 0 and sha else "unknown"


def _portable_resolve_player(explicit):
    if explicit is not None:
        return _require_file(pathlib.Path(explicit), "Portable-zip launcher")
    for candidate in _PLAYER_CANDIDATES:
        if (ROOT / candidate).is_file():
            return ROOT / candidate
    raise FileNotFoundError(
        "Portable-zip requires a built rowl_player "
        f"(looked under {ROOT / 'build'}; pass --player to override)"
    )


def _portable_resolve_runtimes(explicit):
    if explicit:
        paths = [explicit] if isinstance(explicit, (str, pathlib.Path)) else list(explicit)
        return [_require_file(pathlib.Path(p), "Portable-zip runtime library")
                for p in paths]
    found = [ROOT / c for c in _RUNTIME_CANDIDATES if (ROOT / c).is_file()]
    if not found:
        raise FileNotFoundError(
            "Portable-zip requires a built RowlEngineCore runtime library "
            f"(looked under {ROOT / 'build'}; pass --runtime to override)"
        )
    return found


def _portable_load_packager():
    """Lazy `package_assets.pack_directory` import (tools-dir tolerant)."""
    try:
        from package_assets import pack_directory
        return pack_directory
    except ImportError:
        tools_dir = str(pathlib.Path(__file__).resolve().parent)
        if tools_dir not in sys.path:
            sys.path.insert(0, tools_dir)
        from package_assets import pack_directory
        return pack_directory


def _portable_demo_package(explicit, work_dir):
    """Return (arcname, bytes, sidecar_bytes) for the demo .rowlpkg."""
    arcname = "game.rowlpkg"
    if explicit is not None:
        source = _require_file(pathlib.Path(explicit), "Portable-zip demo package")
        data = source.read_bytes()
    else:
        assets = ROOT / _PORTABLE_SAMPLE_ASSETS
        if not assets.is_dir():
            raise FileNotFoundError(
                f"Portable-zip demo assets were not found: {assets}"
            )
        target = pathlib.Path(work_dir) / arcname
        _portable_load_packager()(str(assets), str(target))
        data = target.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    sidecar = f"{digest}  {arcname}\n".encode("utf-8")
    return arcname, data, sidecar


def export_portable_zip(output=None, player=None, runtime=None, package=None):
    """Build the deterministic portable-zip release archive.

    Never touches the `export_pc` staging directory. Returns the zip path.
    """
    print("[Export Tool] Building deterministic portable-zip...")
    player_path = _portable_resolve_player(player)
    runtime_paths = _portable_resolve_runtimes(runtime)
    notices_path = _require_file(
        ROOT / _PORTABLE_NOTICES, "Portable-zip third-party notices"
    )

    out_path = pathlib.Path(output) if output is not None else ROOT / _PORTABLE_DEFAULT_OUTPUT
    stamp = _portable_zip_stamp()
    version_text = (
        "rowl portable-zip\n"
        f"commit {_portable_git_sha()}\n"
        f"date {_portable_version_date()}\n"
    )

    with tempfile.TemporaryDirectory(prefix="rowl-portable-zip-") as work_dir:
        pkg_arc, pkg_data, pkg_sidecar = _portable_demo_package(package, work_dir)

        entries = {
            player_path.name: player_path.read_bytes(),
            pkg_arc: pkg_data,
            f"{pkg_arc}.sha256": pkg_sidecar,
            _PORTABLE_NOTICES.name: notices_path.read_bytes(),
            "VERSION": version_text.encode("utf-8"),
        }
        for runtime_path in runtime_paths:
            if runtime_path.name in entries:
                raise FileExistsError(
                    f"Portable-zip entry name collision: {runtime_path.name}"
                )
            entries[runtime_path.name] = runtime_path.read_bytes()

        lines = [
            f"{hashlib.sha256(entries[name]).hexdigest()}  {name}\n"
            for name in sorted(entries)
        ]
        entries["SHA256SUMS"] = "".join(lines).encode("utf-8")

        out_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = out_path.with_name(f"{out_path.name}.tmp-{os.getpid()}")
        try:
            with zipfile.ZipFile(str(temporary), "w",
                                 compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=9) as archive:
                for name in sorted(entries):
                    info = zipfile.ZipInfo(name, date_time=stamp)
                    info.create_system = 3
                    info.compress_type = zipfile.ZIP_DEFLATED
                    mode = 0o755 if name == player_path.name else 0o644
                    info.external_attr = mode << 16
                    archive.writestr(info, entries[name])
            os.replace(temporary, out_path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)

    print(f"[Export Tool] Portable-zip created: {out_path}")
    return out_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", default="pc",
                        choices=("pc", "android", "ios", "portable-zip"))
    parser.add_argument("--output", default=None,
                        help="portable-zip output path (default: build/rowl-portable.zip)")
    parser.add_argument("--player", default=None,
                        help="portable-zip launcher binary override")
    parser.add_argument("--runtime", default=None, action="append",
                        help="portable-zip runtime library override (repeatable)")
    parser.add_argument("--package", default=None,
                        help="portable-zip demo .rowlpkg override")
    args = parser.parse_args(argv)

    exporters = {
        "pc": export_pc,
        "android": export_android,
        "ios": export_ios,
    }
    try:
        if args.target == "portable-zip":
            export_portable_zip(output=args.output, player=args.player,
                                runtime=args.runtime, package=args.package)
        else:
            if args.output is not None or args.player is not None \
                    or args.runtime is not None or args.package is not None:
                print("[Export Tool] ERROR: --output/--player/--runtime/--package "
                      "apply only to the portable-zip target", file=sys.stderr)
                return 2
            exporters[args.target]()
    except (FileNotFoundError, FileExistsError, OSError,
            subprocess.CalledProcessError) as error:
        print(f"[Export Tool] ERROR: {error}", file=sys.stderr)
        return error.returncode if isinstance(error, subprocess.CalledProcessError) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
