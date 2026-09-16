#!/usr/bin/env python3
"""Contract tests for the Linux `.sh` self-extracting installer (Faz 6 Dilim 4+6).

Covers tools/export_game.py `self-extracting`: wrap a portable-zip, install
it for real with `sh` into a clean directory, verify (`sha256sum -c` +
`package_assets.verify_package`), check the atomic `.rowl-receipt`
(version + payload-sha + exact FILES set), uninstall via the receipt with
no leftovers (and no glob-deletes of user files), fall back to the
embedded FILES list when the receipt is missing (legacy prefix), roll
back a half-install with no leftovers and no receipt, reject a
1-byte-flipped payload without creating the target (and without a
receipt), and prove byte-identical double generation. The install path is
also exercised with python3 removed from PATH, proving the stub is pure
shell at install time.

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

        # (a) Receipt exists with version + payload-sha + exact FILES set.
        stub_text = first.read_text(encoding="utf-8")
        sha_match = re.search(r'^PAYLOAD_SHA256="([0-9a-f]{64})"$',
                              stub_text, re.M)
        files_match = re.search(r'^FILES="(.*)"$', stub_text, re.M)
        if not sha_match or files_match is None:
            raise SystemExit("installer stub is missing PAYLOAD/FILES lines")
        stub_files = files_match.group(1).split()
        import zipfile
        with zipfile.ZipFile(zip_path) as archive:
            zip_names = sorted(archive.namelist())
        if sorted(stub_files) != zip_names:
            raise SystemExit("stub FILES list differs from zip entries")
        receipt = prefix / ".rowl-receipt"
        if not receipt.is_file():
            raise SystemExit("install wrote no .rowl-receipt")
        if (prefix / ".rowl-receipt.tmp").exists():
            raise SystemExit("receipt tmp file was left behind")
        receipt_lines = receipt.read_text(encoding="utf-8").splitlines()
        if receipt_lines[:2] != ["ROWL_SDE_VERSION=9.9-test",
                                 f"PAYLOAD_SHA256={sha_match.group(1)}"]:
            raise SystemExit(f"receipt header wrong: {receipt_lines[:2]!r}")
        if sorted(receipt_lines[2:]) != zip_names:
            raise SystemExit("receipt file set differs from FILES")


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
        # (b) Receipt-driven uninstall: receipt + all installed files gone,
        # user file survives, uninstall exits 0.
        prefix.mkdir()
        keep = prefix / "my-save.txt"
        keep.write_text("mine\n", encoding="utf-8")
        reinstall = run(["sh", str(first), "--prefix", str(prefix)])
        if reinstall.returncode != 0:
            raise SystemExit(f"reinstall failed: {reinstall.stderr}")
        if not (prefix / ".rowl-receipt").is_file():
            raise SystemExit("reinstall wrote no .rowl-receipt")
        guarded = run(["sh", str(first), "uninstall",
                       "--prefix", str(prefix)])
        if guarded.returncode != 0 or not keep.is_file():
            raise SystemExit("uninstall touched a user file")
        if (prefix / ".rowl-receipt").exists():
            raise SystemExit("uninstall left .rowl-receipt behind")
        for name in stub_files:
            if (prefix / name).exists():
                raise SystemExit(f"uninstall left {name} behind")
        if (prefix / ".rowl-receipt.tmp").exists():
            raise SystemExit("uninstall left a receipt tmp file")
        shutil.rmtree(prefix, ignore_errors=True)

        # (c) Legacy uninstall: receipt deleted by hand falls back to the
        # embedded FILES list, still exits 0 with no leftovers.
        legacy_prefix = fake_root / "legacy-install"
        legacy_install = run(["sh", str(first), "--prefix",
                              str(legacy_prefix)])
        if legacy_install.returncode != 0:
            raise SystemExit(f"legacy setup install failed: "
                             f"{legacy_install.stderr}")
        (legacy_prefix / ".rowl-receipt").unlink()
        legacy_un = run(["sh", str(first), "uninstall",
                         "--prefix", str(legacy_prefix)])
        if legacy_un.returncode != 0:
            raise SystemExit(f"legacy uninstall failed: {legacy_un.stderr}")
        for name in stub_files:
            if (legacy_prefix / name).exists():
                raise SystemExit(f"legacy uninstall left {name} behind")
        if legacy_prefix.exists():
            raise SystemExit("legacy uninstall left the prefix behind")

        # (d) Half-install rollback: block the LAST FILES entry with a
        # read-only directory so every earlier file is copied first and
        # then the copy fails. Install must exit non-zero and leave no
        # newly copied file and no receipt behind.
        # (cp of a file onto a plain directory would succeed by copying
        # inside it, so the blocker is chmod-555 to force EACCES.)
        blocker_name = stub_files[-1]
        half_prefix = fake_root / "half-install"
        half_prefix.mkdir()
        blocker = half_prefix / blocker_name
        blocker.mkdir(parents=True)
        os.chmod(blocker, 0o555)
        half = run(["sh", str(first), "--prefix", str(half_prefix)])
        if half.returncode == 0:
            os.chmod(blocker, 0o755)
            raise SystemExit("blocked install was accepted")
        if (half_prefix / ".rowl-receipt").exists() or \
                (half_prefix / ".rowl-receipt.tmp").exists():
            os.chmod(blocker, 0o755)
            raise SystemExit("failed install left a receipt behind")
        leftovers = sorted(
            p.name for p in half_prefix.iterdir() if p.name != blocker_name)
        if leftovers:
            os.chmod(blocker, 0o755)
            raise SystemExit(f"rollback left files behind: {leftovers}")
        nested = list(blocker.iterdir())
        if nested:
            os.chmod(blocker, 0o755)
            raise SystemExit(f"blocked copy wrote inside blocker: {nested}")
        os.chmod(blocker, 0o755)
        shutil.rmtree(half_prefix, ignore_errors=True)

        # Corrupt payload (1 byte flip): rejected, target never created.
        # (e) No receipt is created on a rejected payload either.
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
                     "mktemp", "chmod", "cp", "mv", "rmdir"):
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

        # (R-a) Evil --version tag is rejected at generation time.
        evil_tag = 'v1";touch /tmp/pwned;echo "'
        try:
            EXPORT_GAME.export_self_extracting(
                input=zip_path, output=fake_root / "evil-tag.sh",
                version=evil_tag)
        except ValueError:
            pass
        else:
            raise SystemExit("evil version tag was wrapped without error")
        if (fake_root / "evil-tag.sh").exists():
            raise SystemExit("evil-tag generation published an output file")

        # (R-a2) The default tag (from the zip VERSION content) passes the
        # same gate: a poisoned commit line must fail generation too.
        poisoned_zip = fake_root / "poisoned-version.zip"
        with zipfile.ZipFile(zip_path) as archive:
            entries = {name: archive.read(name)
                       for name in archive.namelist() if name != "SHA256SUMS"}
        entries["VERSION"] = b"rowl portable-zip\ncommit x\";touch /tmp/pwned\n" \
            b"date 2026-09-16\n"
        entries["SHA256SUMS"] = "".join(
            f"{hashlib.sha256(entries[n]).hexdigest()}  {n}\n"
            for n in sorted(entries) if n != "SHA256SUMS").encode("utf-8")
        with zipfile.ZipFile(poisoned_zip, "w",
                             compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as archive:
            for name in sorted(entries):
                archive.writestr(name, entries[name])
        try:
            EXPORT_GAME.export_self_extracting(
                input=poisoned_zip,
                output=fake_root / "poisoned.sh")
        except ValueError:
            pass
        else:
            raise SystemExit("poisoned VERSION tag was wrapped without error")
        if (fake_root / "poisoned.sh").exists():
            raise SystemExit(
                "poisoned-tag generation published an output file")

        # (R-b) Poisoned receipt (`../escape.txt`) makes uninstall
        # fail-closed: exit non-zero, the outside canary survives, and
        # nothing inside the prefix is deleted either.
        esc_prefix = fake_root / "escape-test"
        esc_install = run(["sh", str(first), "--prefix", str(esc_prefix)])
        if esc_install.returncode != 0:
            raise SystemExit(f"escape setup install failed: "
                             f"{esc_install.stderr}")
        canary = fake_root / "escape.txt"
        canary.write_text("do-not-touch\n", encoding="utf-8")
        with (esc_prefix / ".rowl-receipt").open("a",
                                                 encoding="utf-8") as handle:
            handle.write("../escape.txt\n")
        esc_un = run(["sh", str(first), "uninstall",
                      "--prefix", str(esc_prefix)])
        if esc_un.returncode == 0:
            raise SystemExit("poisoned-receipt uninstall was accepted")
        if canary.read_text(encoding="utf-8") != "do-not-touch\n":
            raise SystemExit("poisoned-receipt uninstall escaped the prefix")
        if not (esc_prefix / "game.rowlpkg").is_file():
            raise SystemExit("poisoned-receipt uninstall was not fail-closed")
        if not (esc_prefix / ".rowl-receipt").is_file():
            raise SystemExit("poisoned-receipt uninstall deleted the receipt")
        shutil.rmtree(esc_prefix, ignore_errors=True)
        canary.unlink(missing_ok=True)

        # (R-c) Space-named zip entries are rejected at generation time
        # (the stub iterates $FILES with word-splitting).
        spaced_zip = fake_root / "spaced.zip"
        with zipfile.ZipFile(zip_path) as archive:
            spentries = {name: archive.read(name)
                         for name in archive.namelist()
                         if name != "SHA256SUMS"}
        spentries["evil file.txt"] = b"evil-bytes"
        spentries["SHA256SUMS"] = "".join(
            f"{hashlib.sha256(spentries[n]).hexdigest()}  {n}\n"
            for n in sorted(spentries) if n != "SHA256SUMS").encode("utf-8")
        with zipfile.ZipFile(spaced_zip, "w",
                             compression=zipfile.ZIP_DEFLATED,
                             compresslevel=9) as archive:
            for name in sorted(spentries):
                archive.writestr(name, spentries[name])
        try:
            EXPORT_GAME.export_self_extracting(
                input=spaced_zip, output=fake_root / "spaced.sh",
                version="9.9-test")
        except ValueError:
            pass
        else:
            raise SystemExit("space-named entry was wrapped without error")
        if (fake_root / "spaced.sh").exists():
            raise SystemExit(
                "space-named generation published an output file")

print("[SelfExtracting] wrap/install/verify/uninstall/reject/determinism all green.")
