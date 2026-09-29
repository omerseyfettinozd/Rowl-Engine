#!/usr/bin/env python3
"""Check that the RowlEngineCore public C ABI only grows additively.

Usage:
    check_abi_additive.py <libRowlEngineCore.so|.dylib|.dll|.lib> <baseline.txt>

Extracts exported symbols from the shared library with a portable tool
chain (no ELF-only assumption), keeps the public C ABI symbols (those
starting with ``RowlEngine_``), and compares the sorted unique set
against a baseline file (one symbol per line, sorted).

Tool selection by detected binary format (magic bytes + suffix):

    ELF (.so)   : ``nm -D --defined-only --format=posix``,
                  then ``llvm-nm -D --defined-only --format=posix``.
    Mach-O (.dylib): ``nm -gU`` (BSD nm; leading '_' stripped),
                  then ``llvm-nm``.
    PE (.dll)   : ``dumpbin /EXPORTS``, then ``llvm-nm``, then ``nm``.
    COFF (.lib/.obj): ``llvm-nm``, then ``nm`` (both read COFF archives
                  and objects; ``dumpbin /EXPORTS`` is DLL-only so it is
                  not in this chain).

``otool -L`` is probed on Mach-O hosts as a diagnostic (it lists linked
libraries, not exports) but is never used for symbol extraction. If no
usable tool is found, or every tool fails (e.g. broken toolchain), the
script exits 1 with a clean ``ERROR:`` line — never a traceback
(portable fallback).

Exit codes:
    0 - ABI is identical to the baseline, or only additions were found.
    1 - A baseline symbol was removed, or usage/environment error
        (missing library, missing baseline, or no working symbol tool).
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

ABI_PREFIX = "RowlEngine_"


def detect_format(library: Path) -> str:
    """Return 'ELF', 'Mach-O', 'PE', 'COFF', or 'unknown' for the given file."""
    try:
        with open(library, "rb") as fh:
            magic = fh.read(8)
    except OSError:
        magic = b""
    if magic.startswith(b"\x7fELF"):
        return "ELF"
    if magic.startswith(b"MZ"):
        return "PE"
    # COFF archives (import/static .lib) start with the ar magic; COFF
    # objects (.obj) start with a machine field (e.g. 0x8664 AMD64, 0x014C
    # I386, 0xAA64 ARM64, little-endian) followed by section counts.
    if magic.startswith(b"!<arch>"):
        return "COFF"
    if len(magic) >= 2 and magic[:2] in (
        b"\x64\x86",  # IMAGE_FILE_MACHINE_AMD64
        b"\x4c\x01",  # IMAGE_FILE_MACHINE_I386
        b"\x64\xaa",  # IMAGE_FILE_MACHINE_ARM64
    ):
        return "COFF"
    # Mach-O magics: 32/64-bit, normal/swapped, plus fat binary (cafebabe).
    if magic[:4] in (
        b"\xcf\xfa\xed\xfe",  # MH_MAGIC_64 LE
        b"\xce\xfa\xed\xfe",  # MH_MAGIC LE
        b"\xfe\xed\xfa\xcf",  # MH_MAGIC BE
        b"\xfe\xed\xfa\xce",  # MH_MAGIC_64 BE
        b"\xca\xfe\xba\xbe",  # FAT_MAGIC
    ):
        return "Mach-O"
    suffix = library.suffix.lower()
    if suffix == ".dll":
        return "PE"
    if suffix in (".lib", ".obj"):
        return "COFF"
    if suffix == ".dylib":
        return "Mach-O"
    if suffix == ".so":
        return "ELF"
    return "unknown"


def _run(cmd: list[str]) -> str | None:
    """Run cmd, returning stdout on success or None on any failure.

    Missing binary, non-zero exit, timeout, and undecodable output all
    map to None — the caller tries the next tool in the chain.
    """
    if shutil.which(cmd[0]) is None:
        return None
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if proc.returncode != 0:
        return None
    return proc.stdout


def _parse_nm_posix(output: str, mach_o: bool = False) -> set[str]:
    """Parse ``nm --format=posix`` / BSD-nm output for ABI symbols.

    Two row shapes occur in the wild:

        POSIX (``--format=posix``): ``RowlEngine_Create T 00001000``
        BSD (macOS default nm):     ``0000000100001000 T _RowlEngine_Create``

    The name column swaps position between the two, so scan every token:
    strip archive-member decorations and, on Mach-O, one leading
    underscore (Apple/x86 cdecl convention), then keep tokens carrying
    the ABI prefix.
    """
    symbols = set()
    for line in output.splitlines():
        for token in line.split():
            name = token.strip("():,;")
            if mach_o and name.startswith("_"):
                name = name[1:]
            if name.startswith(ABI_PREFIX):
                symbols.add(name)
    return symbols


def _parse_dumpbin(output: str) -> set[str]:
    """Parse ``dumpbin /EXPORTS`` output for ABI symbols.

    Export table rows look like::

        ordinal hint RVA      name
              1    0 00001000 RowlEngine_Create
    """
    symbols = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 4:
            continue
        # Column bases: ordinal is decimal, hint and RVA are hexadecimal
        # (dumpbin prints hints like 0A/1F when A-F digits appear).
        try:
            int(parts[0], 10)   # ordinal (decimal)
            int(parts[1], 16)   # hint (hex)
            int(parts[2], 16)   # RVA (hex)
        except ValueError:
            continue
        name = parts[3]
        if name.startswith(ABI_PREFIX):
            symbols.add(name)
    return symbols


def _parse_nm_coff(output: str) -> set[str]:
    """Parse ``llvm-nm`` / ``nm`` output on COFF archives/objects.

    Unlike ``nm --format=posix`` (name-first), llvm-nm prints address-first
    rows (``00000000 T RowlEngine_Create``) and may prefix archive members
    (``lib(member): ...``). This parser scans every whitespace-separated
    token instead of assuming a column, and strips one leading underscore
    (x86 cdecl decoration ``_RowlEngine_Foo``; x64/ARM64 have none).
    """
    symbols = set()
    for line in output.splitlines():
        for token in line.split():
            name = token.strip("():,;")
            if name.startswith("_"):
                name = name[1:]
            if name.startswith(ABI_PREFIX):
                symbols.add(name)
    return symbols


def _otool_available() -> bool:
    """Probe for ``otool -L`` (Mach-O diagnostic only, never extraction)."""
    return shutil.which("otool") is not None


def extract_abi_symbols(library: Path) -> list[str]:
    """Return sorted unique RowlEngine_* exported symbols in library."""
    fmt = detect_format(library)
    tried: list[str] = []
    symbols: set[str] = set()

    if fmt == "PE":
        chain: list[tuple[list[str], str]] = [
            (["dumpbin", "/EXPORTS", str(library)], "dumpbin"),
            (["llvm-nm", "--defined-only", str(library)], "llvm-nm"),
            (["nm", "--defined-only", str(library)], "nm"),
        ]
        for cmd, label in chain:
            tried.append(label)
            out = _run(cmd)
            if out is None:
                continue
            if label == "dumpbin":
                symbols = _parse_dumpbin(out)
            else:
                symbols = _parse_nm_posix(out)
            if symbols:
                break
    elif fmt == "COFF":
        # Windows import/static library or object: dumpbin /EXPORTS is
        # DLL-only, so the chain is llvm-nm then nm (both read COFF).
        chain = [
            (["llvm-nm", "--defined-only", str(library)], "llvm-nm"),
            (["nm", "--defined-only", str(library)], "nm"),
        ]
        for cmd, label in chain:
            tried.append(label)
            out = _run(cmd)
            if out is None:
                continue
            symbols = _parse_nm_coff(out)
            if symbols:
                break
    elif fmt == "Mach-O":
        chain = [
            (["nm", "-gU", str(library)], "nm"),
            (["llvm-nm", "--defined-only", str(library)], "llvm-nm"),
            (["nm", "-g", str(library)], "nm"),
            (["nm", "-g", "-P", str(library)], "nm -P"),
        ]
        for cmd, label in chain:
            tried.append(label)
            out = _run(cmd)
            if out is None:
                continue
            symbols = _parse_nm_posix(out, mach_o=True)
            if symbols:
                break
        # otool -L lists linked libraries, not exports: diagnostic probe
        # only, so a macOS log still shows which native tools were present.
        if not symbols and _otool_available():
            tried.append("otool -L (diagnostic only)")
    else:  # ELF or unknown: ELF-style dynamic listing first.
        chain = [
            (["nm", "-D", "--defined-only", "--format=posix", str(library)], "nm -D"),
            (
                ["llvm-nm", "-D", "--defined-only", "--format=posix", str(library)],
                "llvm-nm -D",
            ),
            (["nm", "--defined-only", "--format=posix", str(library)], "nm"),
        ]
        for cmd, label in chain:
            tried.append(label)
            out = _run(cmd)
            if out is None:
                continue
            symbols = _parse_nm_posix(out)
            if symbols:
                break

    if not symbols:
        tried_str = ", ".join(tried) if tried else "no tools on PATH"
        print(
            f"ERROR: no working symbol tool for '{library}' "
            f"(format: {fmt}; tried: {tried_str}). "
            "Install binutils (`nm`), LLVM (`llvm-nm`), or MSVC "
            "(`dumpbin`) for this platform.",
            file=sys.stderr,
        )
        sys.exit(1)
    return sorted(symbols)


def read_baseline(baseline: Path) -> list[str]:
    """Read baseline file, returning sorted unique non-empty lines."""
    text = baseline.read_text(encoding="utf-8")
    symbols = sorted({line.strip() for line in text.splitlines() if line.strip()})
    return symbols


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(
            f"Usage: {Path(argv[0]).name} <libRowlEngineCore.so|.dylib|.dll> "
            "<baseline.txt>",
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
