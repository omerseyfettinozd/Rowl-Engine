#!/usr/bin/env python3
"""Contract tests for the honest native/mobile export status boundary."""

import contextlib
import importlib.util
import io
import pathlib
import subprocess
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

print("Export tool contract tests passed.")
