#!/usr/bin/env python3
"""Check that the RowlEngineCore public C ABI only grows additively.

Usage:
    check_abi_additive.py <libRowlEngineCore.so> <baseline.txt>

Extracts dynamic symbols from the shared library with
``nm -D --defined-only --format=posix``, keeps the public C ABI symbols
(those starting with ``RowlEngine_``), and compares the sorted unique set
against a baseline file (one symbol per line, sorted).

Exit codes:
    0 - ABI is identical to the baseline, or only additions were found.
    1 - A baseline symbol was removed, or usage/environment error
        (missing library, missing baseline, or ``nm`` failure).
"""

import subprocess
import sys
from pathlib import Path

ABI_PREFIX = "RowlEngine_"


def extract_abi_symbols(library: Path) -> list[str]:
    """Return sorted unique RowlEngine_* dynamic symbols defined in library."""
    try:
        proc = subprocess.run(
            ["nm", "-D", "--defined-only", "--format=posix", str(library)],
            capture_output=True,
            text=True,
            check=False,
        )
    except FileNotFoundError:
        print("ERROR: `nm` tool not found on PATH.", file=sys.stderr)
        sys.exit(1)
    if proc.returncode != 0:
        print(
            f"ERROR: `nm` failed on '{library}': {proc.stderr.strip()}",
            file=sys.stderr,
        )
        sys.exit(1)
    symbols = set()
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        name = line.split()[0]
        if name.startswith(ABI_PREFIX):
            symbols.add(name)
    return sorted(symbols)


def read_baseline(baseline: Path) -> list[str]:
    """Read baseline file, returning sorted unique non-empty lines."""
    text = baseline.read_text(encoding="utf-8")
    symbols = sorted({line.strip() for line in text.splitlines() if line.strip()})
    return symbols


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(
            f"Usage: {Path(argv[0]).name} <libRowlEngineCore.so> <baseline.txt>",
            file=sys.stderr,
        )
        return 1
    library = Path(argv[1])
    baseline = Path(argv[2])
    if not library.is_file():
        print(f"ERROR: library not found: '{library}'", file=sys.stderr)
        return 1
    if not baseline.is_file():
        print(f"ERROR: baseline file not found: '{baseline}'", file=sys.stderr)
        return 1

    current = extract_abi_symbols(library)
    expected = read_baseline(baseline)

    current_set = set(current)
    expected_set = set(expected)

    removed = sorted(expected_set - current_set)
    added = sorted(current_set - expected_set)

    if removed:
        for symbol in removed:
            print(f"ERROR: ABI symbol removed: {symbol}", file=sys.stderr)
        print(
            f"ERROR: {len(removed)} symbol(s) removed from the public C ABI. "
            "Removals are breaking changes and are not allowed.",
            file=sys.stderr,
        )
        return 1

    if added:
        for symbol in added:
            print(f"WARNING: ABI symbol added: {symbol}")
        print(
            f"WARNING: {len(added)} new ABI symbol(s) detected. "
            "This is allowed (additive change); regenerate the baseline "
            "to accept the new symbols."
        )
        return 0

    print(f"OK: ABI matches baseline ({len(expected)} symbols).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
