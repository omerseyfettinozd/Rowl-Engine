#!/usr/bin/env python3
"""Contract tests for the Linux `.sh` self-extracting installer (Faz 6 Dilim 4).

Covers tools/export_game.py `self-extracting`: wrap a portable-zip, install
it for real with `sh` into a clean directory, verify (`sha256sum -c` +
`package_assets.verify_package`), uninstall with no leftovers (and no
glob-deletes of user files), reject a 1-byte-flipped payload without
creating the target, and prove byte-identical double generation. The
install path is also exercised with python3 removed from PATH, proving the
stub is pure shell at install time.

NSIS/WiX are explicitly out of scope: this is the Linux `.sh` installer
only, no `.exe` installer is produced or asserted here.
"""

import hashlib
import importlib.util
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "rowl_export_game", ROOT / "tools" / "export_game.py")
EXPORT_GAME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT_GAME)

TOOLS_DIR = str(ROOT / "tools")
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)
import package_assets  # noqa: E402


def run(arguments, **kwargs):
    return subprocess.run(arguments, capture_output=True, text=True,
                          check=False, **kwargs)


def sha256_of(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def write_fixture(fake_root):
    (fake_root / "build" / "bin").mkdir(parents=True)
    (fake_root / "build" / "lib").mkdir(parents=True)
    (fake_root / "packaging").mkdir(parents=True)
    (fake_root / "samples" / "first_light" / "Assets").mkdir(parents=True)
    (fake_root / "build" / "bin" / "rowl_player").write_bytes(b"fake-player")
    (fake_root / "build" / "lib" / "libRowlEngineCore.so").write_bytes(
        b"fake-runtime")
    (fake_root / "packaging" / "THIRD_PARTY_NOTICES.md").write_text(
        "notices\n", encoding="utf-8")
    (fake_root / "samples" / "first_light" / "Assets" / "a.png").write_bytes(
        b"fake-png-bytes-0001")


def payload_line_of(installer):
    text = pathlib.Path(installer).read_text(encoding="utf-8")
    match = re.search(r"^PAYLOAD_LINE=(\d+)$", text, re.M)
    if not match:
        raise SystemExit("installer stub is missing PAYLOAD_LINE")
    return int(match.group(1))


def flip_first_payload_byte(installer, destination):
    """Corrupt exactly 1 payload byte (first base64 char of the payload)."""
    lines = pathlib.Path(installer).read_text(
        encoding="utf-8").splitlines(keepends=True)
    index = payload_line_of(installer) - 1
    row = lines[index]
    if not row.strip():
        raise SystemExit("payload start line is empty, cannot flip a byte")
    replacement = "B" if row[0] == "A" else "A"
    lines[index] = replacement + row[1:]
    pathlib.Path(destination).write_text("".join(lines), encoding="utf-8")


def require_tools(*names):
    missing = [name for name in names if shutil.which(name) is None]
    if missing:
        raise SystemExit(f"missing host tools for installer test: {missing}")


require_tools("sh", "sha256sum", "unzip", "base64", "tail")

with tempfile.TemporaryDirectory() as directory:
    fake_root = pathlib.Path(directory) / "fake"
    fake_root.mkdir()
    write_fixture(fake_root)

    with mock.patch.object(EXPORT_GAME, "ROOT", fake_root), \
         mock.patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "1757971200"}):
        zip_path = fake_root / "rowl-portable.zip"
        EXPORT_GAME.export_portable_zip(output=zip_path)

        first = fake_root / "install-a.sh"
        second = fake_root / "install-b.sh"
        EXPORT_GAME.export_self_extracting(input=zip_path, output=first,
                                           version="9.9-test")
        EXPORT_GAME.export_self_extracting(input=zip_path, output=second,
                                           version="9.9-test")

        # Determinism: same zip + same inputs => byte-identical installer.
        if first.read_bytes() != second.read_bytes():
            raise SystemExit("self-extracting output is not byte-identical")

        # The stub must be valid shell.
        syntax = run(["sh", "-n", str(first)])
        if syntax.returncode != 0:
            raise SystemExit(f"`sh -n` rejected the stub: {syntax.stderr}")

        # Real install into a clean directory.
        prefix = fake_root / "game-install"
        installed = run(["sh", str(first), "--prefix", str(prefix)])
        if installed.returncode != 0:
            raise SystemExit(f"installer failed: {installed.stderr}")
        if not prefix.is_dir():
            raise SystemExit("installer reported success but made no prefix")

        # Installed bytes must equal the zip entries byte-for-byte.
        import zipfile
        with zipfile.ZipFile(zip_path) as archive:
            for name in archive.namelist():
                if (prefix / name).read_bytes() != archive.read(name):
                    raise SystemExit(f"installed {name} differs from zip")

        # `sha256sum -c` must be green inside the installed tree.
        sums = run(["sha256sum", "-c", "SHA256SUMS"], cwd=prefix)
        if sums.returncode != 0:
            raise SystemExit(f"installed SHA256SUMS check failed: {sums.stdout}")

        # The installed demo package must verify.
        if package_assets.verify_package(str(prefix / "game.rowlpkg")) != 0:
            raise SystemExit("installed game.rowlpkg failed verify")

        # The launcher must be executable.
        if not os.access(prefix / "rowl_player", os.X_OK):
            raise SystemExit("installed rowl_player is not executable")

        # Default-prefix form: bare `sh installer` installs ./rowl-game.
        default_cwd = fake_root / "default-cwd"
        default_cwd.mkdir()
        bare = run(["sh", str(first)], cwd=default_cwd)
        if bare.returncode != 0 or not (default_cwd / "rowl-game").is_dir():
            raise SystemExit("default-prefix install failed")
        bare_un = run(["sh", str(first), "uninstall"], cwd=default_cwd)
        if bare_un.returncode != 0 or (default_cwd / "rowl-game").exists():
            raise SystemExit("default-prefix uninstall left leftovers")

        # Uninstall: no leftovers, prefix dir removed when empty.
        uninstalled = run(["sh", str(first), "uninstall",
                           "--prefix", str(prefix)])
        if uninstalled.returncode != 0:
            raise SystemExit(f"uninstall failed: {uninstalled.stderr}")
        if prefix.exists():
            raise SystemExit("uninstall left the prefix behind")

        # Uninstall must not glob-delete: a user file survives.
        prefix.mkdir()
        keep = prefix / "my-save.txt"
        keep.write_text("mine\n", encoding="utf-8")
        run(["sh", str(first), "--prefix", str(prefix)])
        guarded = run(["sh", str(first), "uninstall",
                       "--prefix", str(prefix)])
        if guarded.returncode != 0 or not keep.is_file():
            raise SystemExit("uninstall touched a user file")
        shutil.rmtree(prefix, ignore_errors=True)

        # Corrupt payload (1 byte flip): rejected, target never created.
        corrupt = fake_root / "install-corrupt.sh"
        flip_first_payload_byte(first, corrupt)
        bad_prefix = fake_root / "must-not-exist"
        rejected = run(["sh", str(corrupt), "--prefix", str(bad_prefix)])
        if rejected.returncode == 0:
            raise SystemExit("corrupt-payload install was accepted")
        if bad_prefix.exists():
            raise SystemExit("corrupt-payload install wrote to the target")

        # Pure-shell proof: install with python3 unreachable via PATH.
        farm = fake_root / "tool-farm"
        farm.mkdir()
        for tool in ("tail", "base64", "sha256sum", "unzip", "mkdir", "rm",
                     "mktemp", "chmod", "cp", "rmdir"):
            found = shutil.which(tool)
            if found is None:
                raise SystemExit(f"cannot build no-python PATH farm: {tool}")
            os.symlink(found, farm / tool)
        sealed_env = {"PATH": str(farm), "TMPDIR": str(fake_root)}
        probe = run(["/usr/bin/sh", "-c", "command -v python3"],
                    env=sealed_env)
        if probe.returncode == 0:
            raise SystemExit("no-python PATH farm still finds python3")
        sealed_prefix = fake_root / "sealed-install"
        sealed = run(["/usr/bin/sh", str(first), "--prefix",
                      str(sealed_prefix)], env=sealed_env)
        if sealed.returncode != 0 or not sealed_prefix.is_dir():
            raise SystemExit(f"python3-free install failed: {sealed.stderr}")
        sealed_un = run(["/usr/bin/sh", str(first), "uninstall",
                         "--prefix", str(sealed_prefix)], env=sealed_env)
        if sealed_un.returncode != 0 or sealed_prefix.exists():
            raise SystemExit("python3-free uninstall left leftovers")

        # Corrupt input zip must fail fast at generation time (no output).
        tampered_zip = fake_root / "tampered.zip"
        tampered_zip.write_bytes(zip_path.read_bytes())
        with tampered_zip.open("r+b") as handle:
            handle.seek(200)
            handle.write(b"\x00")
        try:
            EXPORT_GAME.export_self_extracting(
                input=tampered_zip,
                output=fake_root / "tampered.sh", version="9.9-test")
        except ValueError:
            pass
        else:
            raise SystemExit("tampered input zip was wrapped without error")
        if (fake_root / "tampered.sh").exists():
            raise SystemExit("failed generation published an output file")

print("[SelfExtracting] wrap/install/verify/uninstall/reject/determinism all green.")
