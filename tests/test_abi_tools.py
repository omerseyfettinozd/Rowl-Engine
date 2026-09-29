#!/usr/bin/env python3
"""Contract tests for the ABI additivity checker."""

import importlib.util
import pathlib
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL = ROOT / "tools" / "check_abi_additive.py"
LIBRARY = ROOT / "build" / "lib" / "libRowlEngineCore.so"
BASELINE = ROOT / "tools" / "abi_baseline.txt"

FAKE_REMOVED_SYMBOL = "RowlEngine_ProveRemovalRed"


def _load_tool_module():
    """Import check_abi_additive.py as a module without running its main()."""
    spec = importlib.util.spec_from_file_location("check_abi_additive", TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _test_dumpbin_parses_hex_hints() -> None:
    """dumpbin hint/RVA columns are hex: rows with A-F digits must parse."""
    module = _load_tool_module()
    output = "\n".join(
        [
            "Microsoft (R) COFF/PE Dumper Version 14.00",
            "",
            "    ordinal hint RVA      name",
            "",
            "          1    0 00001000 RowlEngine_Create",
            "          2    A 00001010 RowlEngine_Destroy",
            "          3   E0 00001020 RowlEngine_GetVersion",
            "          4   1F 00001030 RowlEngine_Foo",
            "          5  ABC 00001040 RowlEngine_Bar",
            "          6  g99 00001050 RowlEngine_NotASymbol",
            "          7    9 00001060 RowlEngine_End",
        ]
    )
    symbols = module._parse_dumpbin(output)
    expected = {
        "RowlEngine_Create",
        "RowlEngine_Destroy",
        "RowlEngine_GetVersion",
        "RowlEngine_Foo",
        "RowlEngine_Bar",
        "RowlEngine_End",
    }
    if symbols != expected:
        raise SystemExit(
            "dumpbin hex-hint rows were not parsed correctly: "
            f"got {sorted(symbols)}, expected {sorted(expected)}"
        )


def _test_nm_posix_parses_bsd_and_posix_rows() -> None:
    """BSD nm (address-first) and POSIX nm (name-first) both parse."""
    module = _load_tool_module()
    output = "\n".join(
        [
            # macOS BSD nm default output: ADDRESS TYPE NAME.
            "0000000100001000 T _RowlEngine_Create",
            "                 T _RowlEngine_Destroy",
            "0000000100001020 T _RowlEngine_GetVersion",
            "# Symbol table",
            "/tmp/lib.dylib(dylib.7):",
            "0000000100001030 (brx) _RowlEngine_Bar",
            "                 U _printf",
            # POSIX --format=posix output: NAME TYPE ADDRESS.
            "RowlEngine_Posix T 00001000",
            "RowlEngine_Posix2 D 00001010",
            "_notabi T 00001020",
        ]
    )
    symbols = module._parse_nm_posix(output, mach_o=True)
    expected = {
        "RowlEngine_Create",
        "RowlEngine_Destroy",
        "RowlEngine_GetVersion",
        "RowlEngine_Bar",
        "RowlEngine_Posix",
        "RowlEngine_Posix2",
    }
    if symbols != expected:
        raise SystemExit(
            "nm POSIX/BSD rows were not parsed correctly: "
            f"got {sorted(symbols)}, expected {sorted(expected)}"
        )
    # ELF/PE path (mach_o=False) must keep the leading underscore: the
    # decorated name does not match the bare ABI prefix.
    elf_symbols = module._parse_nm_posix("_RowlEngine_Underscored T 00001000")
    if elf_symbols != set():
        raise SystemExit(
            "mach_o=False unexpectedly matched underscore-decorated name: "
            f"got {sorted(elf_symbols)}"
        )
    elf_plain = module._parse_nm_posix("RowlEngine_Plain T 00001000")
    if elf_plain != {"RowlEngine_Plain"}:
        raise SystemExit(
            "mach_o=False failed to parse plain POSIX row: "
            f"got {sorted(elf_plain)}"
        )


if not LIBRARY.is_file():
    print(f"skipping ABI tool tests: library not found at '{LIBRARY}'.")
    raise SystemExit(0)

if not BASELINE.is_file():
    raise SystemExit(f"ABI baseline not found at '{BASELINE}'")

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)

    _test_dumpbin_parses_hex_hints()
    _test_nm_posix_parses_bsd_and_posix_rows()

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
