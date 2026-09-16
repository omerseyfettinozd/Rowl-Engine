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

self-extracting (Faz 6 Dilim 4) — Linux POSIX `.sh` self-extracting installer:

  python3 tools/export_game.py self-extracting [--input ZIP] [--output SH]
      [--version VER]

  Wraps a portable-zip (Dilim 3 output) in a single `install-rowl-<ver>.sh`
  file runnable as `sh install-rowl-<ver>.sh [--prefix DIR] [install]` /
  `sh ... uninstall [--prefix DIR]` (default prefix `./rowl-game`).
  The stub is pure POSIX shell (`#!/bin/sh` + `set -u`); install time needs
  only tail, base64, sha256sum, unzip, mkdir, rm, mktemp, chmod, cp, mv,
  rmdir — python3 is NOT required to install.

  Verification (before anything is written to the target):
    1. the embedded base64 payload is decoded into a temp dir,
    2. its SHA-256 must equal the build-time PAYLOAD_SHA256 (mismatch:
       exit 1, the target directory is never created),
    3. the zip is unpacked and its inner SHA256SUMS is re-checked
       (`sha256sum -c`), and only then are files copied into --prefix with
       rowl_player made executable.
  Receipt: after every copy plus chmod succeeds, `$prefix/.rowl-receipt`
  is written atomically (tmp file + `mv`) as `ROWL_SDE_VERSION=<ver>`,
  `PAYLOAD_SHA256=<sha>`, then one installed name per line.
  Rollback: any copy/chmod/receipt-write failure removes the files copied
  so far (`rm -f`) and exits 1; the temp dir is guarded by a single
  `trap 'rm -rf "$tmpdir"' EXIT` (cancelled on success, no double-free).
  Uninstall deletes exactly the receipt-listed names when
  `$prefix/.rowl-receipt` exists, else falls back to the embedded FILES
  list (legacy prefixes), then removes the receipt itself and rmdirs the
  prefix when left empty (user files are never touched). Receipt lines
  are validated before anything is deleted: absolute paths, `..`
  segments and backslashes fail closed (exit 1, nothing removed).
  Tags and entry names must match `^[A-Za-z0-9._-]+$` or generation
  fails with `ValueError` (this also gates the default tag derived from
  the zip VERSION content).

  Determinism: the stub template is fixed; only payload-derived values
  (hash, file list, payload start line) vary, so the same input zip yields
  a byte-identical `.sh`.

  Costs and limits: base64 inflates the installer by ~33% over the zip;
  install requires tail, base64, sha256sum, unzip, mkdir, rm, mktemp,
  chmod, cp, mv, rmdir (a missing tool fails with the corresponding gate
  message, not a stack trace). Input zips with non-flat or unsafe entry
  names (absolute paths, `..` segments) are rejected at build time.

  NSIS/WiX note: those Windows installer stacks stay out of scope — this
  slice ships the Linux `.sh` installer only, no `.exe` installer.
"""

import argparse
import base64
import datetime
import hashlib
import os
import pathlib
import re
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


_SDE_DEFAULT_INPUT = pathlib.Path("build") / "rowl-portable.zip"
_SDE_PAYLOAD_MARKER = ("# __ROWL_PAYLOAD_B64_BELOW__: base64(portable-zip); "
                       "decoded and hash-verified at install time.\n")

# Shell-safe token: version tags and payload entry names must match this
# (the tag lands inside a double-quoted shell string, names inside an
# unquoted $FILES word-split — anything else is a generation-time error).
_SDE_SAFE_TOKEN = re.compile(r"^[A-Za-z0-9._-]+$")

# Fixed POSIX sh installer stub. Only @@...@@ placeholders vary, and only
# with payload-derived values (hash, file list, payload start line), so the
# same input zip always renders byte-identical output. Install time needs
# tail, base64, sha256sum, unzip, mkdir, rm, mktemp, chmod, cp, mv, rmdir
# only.
_SDE_STUB_TEMPLATE = """#!/bin/sh
# Rowl Engine self-extracting installer (Faz 6 Dilim 4).
#
# Linux-only POSIX sh wrapper around an embedded portable-zip payload.
#
#   sh install-rowl-<ver>.sh [--prefix DIR] [install]
#   sh install-rowl-<ver>.sh uninstall [--prefix DIR]
#
# Install verifies BEFORE touching the target directory:
#   1. the base64 payload below is decoded into a temporary directory,
#   2. its SHA-256 must equal PAYLOAD_SHA256 (mismatch: exit 1, the target
#      directory is never created),
#   3. the zip is unpacked and its inner SHA256SUMS is re-checked with
#      `sha256sum -c`; only then are files copied into --prefix (default
#      ./rowl-game) and the launcher made executable.
#   4. after every copy plus chmod succeeds, $prefix/.rowl-receipt is
#      written atomically (tmp file + mv) as ROWL_SDE_VERSION=<ver>,
#      PAYLOAD_SHA256=<sha>, then one installed name per line; a receipt
#      failure fails the install and rolls back the copied files.
#   Any copy/chmod/receipt failure removes the files copied so far and
#   exits 1; the temp dir is guarded by a single trap on EXIT.
#
# Uninstall deletes exactly the receipt-listed names when
# $prefix/.rowl-receipt exists, else the FILES listed below (legacy
# prefixes, no globs, so user files are never touched), then removes the
# receipt itself and the prefix directory when it is left empty. Every
# receipt line is validated BEFORE anything is deleted (absolute paths,
# `..` segments and backslashes fail closed with exit 1, nothing removed).
#
# Needs only: tail, base64, sha256sum, unzip, mkdir, rm, mktemp, chmod,
# cp, mv, rmdir. No python3 at install time, no root privileges.
# Windows NSIS/WiX installers are a separate, later concern; this slice
# deliberately ships no .exe installer.
set -u
ROWL_SDE_VERSION="@@VERSION@@"
PAYLOAD_SHA256="@@PAYLOAD_SHA256@@"
PAYLOAD_LINE=@@PAYLOAD_LINE@@
FILES="@@FILES@@"
PLAYER="@@PLAYER@@"

usage() {
  echo "usage: sh $0 [--prefix DIR] [install|uninstall]" >&2
  echo "       default prefix: ./rowl-game" >&2
}

fail() {
  echo "rowl-installer: $1" >&2
  exit "${2:-1}"
}

do_install() {
  prefix="$1"
  [ -n "$prefix" ] || fail "empty --prefix (wont install into /)"
  tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/rowl-install-XXXXXX")" || fail "cannot create temp dir"
  trap 'rm -rf "$tmpdir"' EXIT
  payload="$tmpdir/payload.zip"
  staged="$tmpdir/staged"
  if ! tail -n +"$PAYLOAD_LINE" "$0" | base64 -d > "$payload" 2>/dev/null; then
    fail "cannot decode embedded payload"
  fi
  digest="$(sha256sum "$payload")"
  actual="${digest%% *}"
  if [ "$actual" != "$PAYLOAD_SHA256" ]; then
    fail "payload hash mismatch (expected $PAYLOAD_SHA256, got $actual)"
  fi
  mkdir -p "$staged" || fail "cannot create staging dir"
  if ! unzip -q "$payload" -d "$staged" 2>/dev/null; then
    fail "cannot unpack verified payload"
  fi
  if ! (cd "$staged" && sha256sum -c SHA256SUMS); then
    fail "inner SHA256SUMS check failed"
  fi
  mkdir -p "$prefix" || fail "cannot create target directory $prefix"
  installed=""
  for name in $FILES; do
    if ! cp "$staged/$name" "$prefix/$name"; then
      for f in $installed; do
        rm -f "$prefix/$f"
      done
      fail "cannot install $name into $prefix"
    fi
    installed="$installed $name"
  done
  if [ -n "$PLAYER" ]; then
    if ! chmod +x "$prefix/$PLAYER"; then
      for f in $installed; do
        rm -f "$prefix/$f"
      done
      fail "cannot make $PLAYER executable"
    fi
  fi
  if ! {
    echo "ROWL_SDE_VERSION=$ROWL_SDE_VERSION"
    echo "PAYLOAD_SHA256=$PAYLOAD_SHA256"
    for name in $FILES; do
      echo "$name"
    done
  } > "$prefix/.rowl-receipt.tmp"; then
    for f in $installed; do
      rm -f "$prefix/$f"
    done
    rm -f "$prefix/.rowl-receipt.tmp"
    fail "cannot write receipt into $prefix"
  fi
  if ! mv "$prefix/.rowl-receipt.tmp" "$prefix/.rowl-receipt"; then
    for f in $installed; do
      rm -f "$prefix/$f"
    done
    rm -f "$prefix/.rowl-receipt.tmp"
    fail "cannot write receipt into $prefix"
  fi
  trap - EXIT
  rm -rf "$tmpdir"
  echo "rowl-installer: installed $ROWL_SDE_VERSION into $prefix"
}

do_uninstall() {
  prefix="$1"
  [ -n "$prefix" ] || fail "empty --prefix (wont touch /)"
  if [ ! -d "$prefix" ]; then
    echo "rowl-installer: nothing to remove ($prefix absent)"
    return 0
  fi
  uninstall_list="$FILES"
  if [ -f "$prefix/.rowl-receipt" ]; then
    uninstall_list="$(tail -n +3 "$prefix/.rowl-receipt" 2>/dev/null)"
  fi
  for name in $uninstall_list; do
    case "$name" in
      "") continue ;;
      /*|../*|*/../*|*/..|..|*\\*)
        fail "unsafe entry in install receipt: $name" ;;
    esac
  done
  for name in $uninstall_list; do
    rm -f "$prefix/$name"
  done
  rm -f "$prefix/.rowl-receipt"
  rmdir "$prefix" 2>/dev/null || true
  echo "rowl-installer: uninstalled $prefix"
}

prefix="./rowl-game"
action="install"
while [ $# -gt 0 ]; do
  case "$1" in
    --prefix)
      if [ $# -lt 2 ]; then
        usage
        exit 2
      fi
      prefix="$2"
      shift 2
      ;;
    --prefix=*)
      prefix="${1#--prefix=}"
      shift
      ;;
    install)
      action="install"
      shift
      ;;
    uninstall)
      action="uninstall"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

if [ "$action" = "uninstall" ]; then
  do_uninstall "$prefix"
else
  do_install "$prefix"
fi
# Stop here: everything below this line is the base64 payload, not shell.
exit 0
"""


def _sde_parse_sums(text):
    """Parse canonical `sha256sum` output into {name: digest} (strict)."""
    covered = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        digest, sep, name = line.partition("  ")
        if not sep or len(digest) != 64 or not name:
            raise ValueError(
                "Self-extracting input has a malformed SHA256SUMS line: "
                f"{line!r}"
            )
        covered[name] = digest
    return covered


def _sde_default_tag(version_text):
    """Derive the default release tag from the zip's VERSION content."""
    commit = "unknown"
    date = "unknown"
    for line in version_text.splitlines():
        if line.startswith("commit "):
            commit = line.split(None, 1)[1].strip() or "unknown"
        elif line.startswith("date "):
            date = line.split(None, 1)[1].strip() or "unknown"
    return f"{date}-{commit}"


def export_self_extracting(input=None, output=None, version=None):
    """Wrap a portable-zip in a POSIX sh self-extracting installer.

    The input zip is verified against its own SHA256SUMS first (fail fast
    on corrupt input). Returns the installer path.
    """
    print("[Export Tool] Building self-extracting installer...")
    in_path = (pathlib.Path(input) if input is not None
               else ROOT / _SDE_DEFAULT_INPUT)
    blob = _require_file(in_path, "Self-extracting input portable-zip").read_bytes()
    try:
        with zipfile.ZipFile(str(in_path)) as archive:
            names = archive.namelist()
            if sorted(names) != list(names):
                raise ValueError(
                    "Self-extracting input zip entries are not in sorted order"
                )
            try:
                sums_raw = archive.read("SHA256SUMS").decode("utf-8")
                version_raw = archive.read("VERSION").decode("utf-8")
            except KeyError as error:
                raise ValueError(
                    f"Self-extracting input zip is missing {error}"
                ) from error
            covered = _sde_parse_sums(sums_raw)
            if set(covered) != set(names) - {"SHA256SUMS"}:
                raise ValueError(
                    "Self-extracting input SHA256SUMS does not cover every entry"
                )
            for name, digest in covered.items():
                actual = hashlib.sha256(archive.read(name)).hexdigest()
                if actual != digest:
                    raise ValueError(
                        f"Self-extracting input fails its own SHA256SUMS: {name}"
                    )
            for name in names:
                parts = name.split("/")
                if (not name or name.startswith("/") or "\\" in name
                        or any(part in ("", ".", "..") for part in parts)
                        or _SDE_SAFE_TOKEN.match(name) is None):
                    raise ValueError(
                        f"Self-extracting input has an unsafe entry name: {name!r}"
                    )
    except zipfile.BadZipFile as error:
        raise ValueError(
            f"Self-extracting input is not a readable zip: {error}"
        ) from error

    tag = version if version is not None else _sde_default_tag(version_raw)
    if _SDE_SAFE_TOKEN.match(tag) is None:
        raise ValueError(
            f"Self-extracting version tag has unsafe characters: {tag!r}"
        )
    player = next(
        (name for name in sorted(names) if name.startswith("rowl_player")), ""
    )
    stub = (_SDE_STUB_TEMPLATE
            .replace("@@VERSION@@", tag)
            .replace("@@PAYLOAD_SHA256@@", hashlib.sha256(blob).hexdigest())
            .replace("@@FILES@@", " ".join(sorted(names)))
            .replace("@@PLAYER@@", player))
    header = stub + _SDE_PAYLOAD_MARKER
    payload_line = header.count("\n") + 1  # first base64 line (1-based)
    header = header.replace("@@PAYLOAD_LINE@@", str(payload_line))

    out_path = (pathlib.Path(output) if output is not None
                else ROOT / "build" / f"install-rowl-{tag}.sh")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = out_path.with_name(f"{out_path.name}.tmp-{os.getpid()}")
    try:
        with open(temporary, "wb") as handle:
            handle.write(header.encode("utf-8"))
            handle.write(base64.encodebytes(blob))
        os.chmod(temporary, 0o755)
        os.replace(temporary, out_path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)

    print(f"[Export Tool] Self-extracting installer created: {out_path}")
    return out_path


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", default="pc",
                        choices=("pc", "android", "ios", "portable-zip",
                                 "self-extracting"))
    parser.add_argument("--output", default=None,
                        help="portable-zip output path (default: build/rowl-portable.zip); "
                             "self-extracting output path "
                             "(default: build/install-rowl-<ver>.sh)")
    parser.add_argument("--player", default=None,
                        help="portable-zip launcher binary override")
    parser.add_argument("--runtime", default=None, action="append",
                        help="portable-zip runtime library override (repeatable)")
    parser.add_argument("--package", default=None,
                        help="portable-zip demo .rowlpkg override")
    parser.add_argument("--input", default=None,
                        help="self-extracting input portable-zip "
                             "(default: build/rowl-portable.zip)")
    parser.add_argument("--version", default=None,
                        help="self-extracting release tag override "
                             "(default: <date>-<commit> from the zip VERSION)")
    args = parser.parse_args(argv)

    exporters = {
        "pc": export_pc,
        "android": export_android,
        "ios": export_ios,
    }
    try:
        if args.target == "portable-zip":
            if args.input is not None or args.version is not None:
                print("[Export Tool] ERROR: --input/--version "
                      "apply only to the self-extracting target", file=sys.stderr)
                return 2
            export_portable_zip(output=args.output, player=args.player,
                                runtime=args.runtime, package=args.package)
        elif args.target == "self-extracting":
            if args.player is not None or args.runtime is not None \
                    or args.package is not None:
                print("[Export Tool] ERROR: --player/--runtime/--package "
                      "apply only to the portable-zip target", file=sys.stderr)
                return 2
            export_self_extracting(input=args.input, output=args.output,
                                   version=args.version)
        else:
            if args.output is not None or args.player is not None \
                    or args.runtime is not None or args.package is not None \
                    or args.input is not None or args.version is not None:
                print("[Export Tool] ERROR: --output/--player/--runtime/--package/--input/--version "
                      "apply only to the portable-zip/self-extracting targets",
                      file=sys.stderr)
                return 2
            exporters[args.target]()
    except (FileNotFoundError, FileExistsError, OSError,
            subprocess.CalledProcessError, ValueError) as error:
        print(f"[Export Tool] ERROR: {error}", file=sys.stderr)
        return error.returncode if isinstance(error, subprocess.CalledProcessError) else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
