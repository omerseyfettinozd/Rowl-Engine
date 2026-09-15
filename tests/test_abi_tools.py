#!/usr/bin/env python3
"""Contract tests for the ABI additivity checker."""

import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "check_abi_additive.py"
LIBRARY = ROOT / "build" / "lib" / "libRowlEngineCore.so"
BASELINE = ROOT / "tools" / "abi_baseline.txt"

FAKE_REMOVED_SYMBOL = "RowlEngine_ProveRemovalRed"


if not LIBRARY.is_file():
    print(f"skipping ABI tool tests: library not found at '{LIBRARY}'.")
    raise SystemExit(0)

if not BASELINE.is_file():
    raise SystemExit(f"ABI baseline not found at '{BASELINE}'")

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)

    identical = subprocess.run(
        [sys.executable, str(TOOL), str(LIBRARY), str(BASELINE)],
        capture_output=True, text=True, check=False)
    if identical.returncode != 0 or "OK" not in (identical.stdout + identical.stderr):
        raise SystemExit("identical ABI baseline was not accepted with OK")

    removal_baseline = directory / "removal-baseline.txt"
    removal_baseline.write_text(
        BASELINE.read_text(encoding="utf-8").rstrip("\n") + "\n" + FAKE_REMOVED_SYMBOL + "\n",
        encoding="utf-8")
    removed = subprocess.run(
        [sys.executable, str(TOOL), str(LIBRARY), str(removal_baseline)],
        capture_output=True, text=True, check=False)
    if removed.returncode != 1 or "ABI symbol removed" not in (
            removed.stdout + removed.stderr) or FAKE_REMOVED_SYMBOL not in (
            removed.stdout + removed.stderr):
        raise SystemExit("ABI symbol removal was not rejected with exit 1")

    baseline_symbols = [
        line.strip()
        for line in BASELINE.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    if not baseline_symbols:
        raise SystemExit("ABI baseline is empty")
    dropped_symbol = baseline_symbols[0]
    addition_baseline = directory / "addition-baseline.txt"
    addition_baseline.write_text(
        "\n".join(s for s in baseline_symbols if s != dropped_symbol) + "\n",
        encoding="utf-8")
    added = subprocess.run(
        [sys.executable, str(TOOL), str(LIBRARY), str(addition_baseline)],
        capture_output=True, text=True, check=False)
    if added.returncode != 0 or "WARNING" not in (
            added.stdout + added.stderr) or dropped_symbol not in (
            added.stdout + added.stderr):
        raise SystemExit("ABI symbol addition was not accepted with WARNING")
