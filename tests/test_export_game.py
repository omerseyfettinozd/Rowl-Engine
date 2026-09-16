#!/usr/bin/env python3
"""Contract tests for the honest native/mobile export status boundary."""

import contextlib
import importlib.util
import io
import os
import pathlib
import subprocess
import sys
import tempfile
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("rowl_export_game", ROOT / "tools" / "export_game.py")
EXPORT_GAME = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXPORT_GAME)


def run_main(arguments):
    stdout = io.StringIO()
    stderr = io.StringIO()
    with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
        result = EXPORT_GAME.main(arguments)
    return result, stdout.getvalue(), stderr.getvalue()


with tempfile.TemporaryDirectory() as directory:
    fake_root = pathlib.Path(directory)

    with mock.patch.object(EXPORT_GAME, "ROOT", fake_root), mock.patch.object(
        EXPORT_GAME.subprocess,
        "run",
        side_effect=subprocess.CalledProcessError(7, ["android-build"]),
    ):
        code, stdout, stderr = run_main(["android"])
        if code != 7 or "successful" in stdout or "ERROR" not in stderr:
            raise SystemExit("Android subprocess failure was not propagated honestly")

    android_runtime = (
        fake_root / "build" / "android-arm64-v8a" / "lib" / "libRowlEngineCore.so"
    )
    android_runtime.parent.mkdir(parents=True)
    android_runtime.write_bytes(b"runtime")
    with mock.patch.object(EXPORT_GAME, "ROOT", fake_root), mock.patch.object(
        EXPORT_GAME.subprocess, "run"
    ) as run:
        code, stdout, stderr = run_main(["android"])
        command = run.call_args.args[0]
        if code != 0 or stderr or "native runtime build successful" not in stdout:
            raise SystemExit("Android native-only success was not reported")
        if "APK/AAB was not produced" not in stdout or "--asset-path" in command:
            raise SystemExit("Android native build was presented as a package export")

    ios_runtime = fake_root / "build" / "ios-arm64" / "Release" / "libRowlEngineCore.dylib"
    ios_runtime.parent.mkdir(parents=True)
    ios_runtime.write_bytes(b"runtime")
    with mock.patch.object(EXPORT_GAME, "ROOT", fake_root), mock.patch.object(
        EXPORT_GAME.subprocess, "run"
    ) as run:
        code, stdout, stderr = run_main(["ios"])
        command = run.call_args.args[0]
        if code != 0 or stderr or "native runtime build successful" not in stdout:
            raise SystemExit("iOS native-only success was not reported")
        if "IPA was not produced" not in stdout or "--asset-path" in command:
            raise SystemExit("iOS native build was presented as an IPA export")

    ios_runtime.unlink()
    with mock.patch.object(EXPORT_GAME, "ROOT", fake_root), mock.patch.object(
        EXPORT_GAME.subprocess, "run"
    ):
        code, stdout, stderr = run_main(["ios"])
        if code == 0 or "successful" in stdout or "was not produced" not in stderr:
            raise SystemExit("Missing iOS runtime artifact did not fail the export")

# --- Faz 6 Dilim 3: portable-zip deterministic contract ---
with tempfile.TemporaryDirectory() as directory:
    fake_root = pathlib.Path(directory)
    (fake_root / "build" / "bin").mkdir(parents=True)
    (fake_root / "build" / "lib").mkdir(parents=True)
    (fake_root / "packaging").mkdir(parents=True)
    (fake_root / "samples" / "first_light" / "Assets").mkdir(parents=True)
    (fake_root / "build" / "bin" / "rowl_player").write_bytes(b"fake-player")
    (fake_root / "build" / "lib" / "libRowlEngineCore.so").write_bytes(b"fake-runtime")
    (fake_root / "packaging" / "THIRD_PARTY_NOTICES.md").write_text(
        "notices\n", encoding="utf-8")
    (fake_root / "samples" / "first_light" / "Assets" / "a.png").write_bytes(
        b"fake-png-bytes-0001")

    with mock.patch.object(EXPORT_GAME, "ROOT", fake_root):
        out_a = fake_root / "a.zip"
        out_b = fake_root / "b.zip"
        EXPORT_GAME.export_portable_zip(output=out_a)
        EXPORT_GAME.export_portable_zip(output=out_b)
        if out_a.read_bytes() != out_b.read_bytes():
            raise SystemExit("portable-zip is not byte-identical across runs")

        # SOURCE_DATE_EPOCH pins the zip timestamps AND the VERSION date:
        # same epoch -> identical bytes with the epoch date inside VERSION;
        # a different epoch date -> different bytes (date is really captured).
        import zipfile as zipfile_sde
        with mock.patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "1234567890"}):
            out_sde1 = fake_root / "sde1.zip"
            out_sde2 = fake_root / "sde2.zip"
            EXPORT_GAME.export_portable_zip(output=out_sde1)
            EXPORT_GAME.export_portable_zip(output=out_sde2)
            if out_sde1.read_bytes() != out_sde2.read_bytes():
                raise SystemExit("portable-zip is not identical under pinned SDE")
            with zipfile_sde.ZipFile(out_sde1) as pinned:
                version_text = pinned.read("VERSION").decode("utf-8")
            if "2009-02-13" not in version_text:
                raise SystemExit("pinned SDE date did not reach VERSION: "
                                 + version_text)
        with mock.patch.dict(os.environ, {"SOURCE_DATE_EPOCH": "1600000000"}):
            out_sde3 = fake_root / "sde3.zip"
            EXPORT_GAME.export_portable_zip(output=out_sde3)
            if out_sde3.read_bytes() == out_sde1.read_bytes():
                raise SystemExit("different SDE date unexpectedly gave same bytes")

        code, stdout, stderr = run_main(
            ["portable-zip", "--output", str(fake_root / "c.zip")])
        if code != 0 or not (fake_root / "c.zip").is_file():
            raise SystemExit("portable-zip CLI target failed: " + stderr)
        if (fake_root / "c.zip").read_bytes() != out_a.read_bytes():
            raise SystemExit("portable-zip CLI output differs from API output")

        import hashlib
        import zipfile

        extract = fake_root / "extracted"
        extract.mkdir()
        with zipfile.ZipFile(out_a) as archive:
            names = archive.namelist()
            if names != sorted(names):
                raise SystemExit("portable-zip entries are not in sorted order")
            archive.extractall(extract)

        sums = (extract / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
        covered = {}
        for line in sums:
            digest, name = line.split("  ")
            covered[name] = digest
        for name, digest in covered.items():
            actual = hashlib.sha256((extract / name).read_bytes()).hexdigest()
            if actual != digest:
                raise SystemExit(f"SHA256SUMS mismatch for extracted {name}")
        if set(covered) != set(names) - {"SHA256SUMS"}:
            raise SystemExit("SHA256SUMS does not cover every zip entry")

        for required in ("THIRD_PARTY_NOTICES.md", "VERSION",
                         "game.rowlpkg", "game.rowlpkg.sha256"):
            if not (extract / required).is_file():
                raise SystemExit(f"portable-zip is missing {required}")

        tools_dir = str(ROOT / "tools")
        if tools_dir not in sys.path:
            sys.path.insert(0, tools_dir)
        import package_assets
        if package_assets.verify_package(str(extract / "game.rowlpkg")) != 0:
            raise SystemExit("extracted game.rowlpkg failed verify")

        # Tamper probe (locked): 1 flipped byte must break the SHA256SUMS check.
        tampered = bytearray((extract / "game.rowlpkg").read_bytes())
        if len(tampered) <= 25:
            raise SystemExit("tamper probe needs a package over 25 bytes")
        tampered[25] ^= 1
        if hashlib.sha256(bytes(tampered)).hexdigest() == covered["game.rowlpkg"]:
            raise SystemExit("SHA256SUMS tamper probe did not go red")

    # export_pc staging must be untouched by the portable-zip path.
    if (fake_root / "build" / "export_pc").exists():
        raise SystemExit("portable-zip disturbed the export_pc staging directory")

print("Export tool contract tests passed.")
