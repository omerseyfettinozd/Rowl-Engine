#!/usr/bin/env python3
"""Verifier mirror probes: reader fail-closed agreement (W8-f2).

Four sub-probes in one file, all on synthetic ROWL v1 packages built with
struct HEADER/ENTRY (no packer subprocess, no zstd needed). The verifier
module is imported directly and read_package_entries is called:

  P1 overlapping payloads: two entries each valid alone, but their
     [offset, offset+compressed_size) ranges intersect -> exit 1 with
     an overlapping payload diagnostic (reader rowlpkg_reader.cpp:358-364
     sorts payloadRanges and rejects start < prev_end; equality is fine).
  P2 path-length cap: a ~5000-byte path is accepted pre-fix (16 KiB) and
     rejected post-fix (reader :298 allows at most 4096). A 4096-byte
     path stays valid as the boundary control.
  P3 canonical duplicate: entries a/./b and a/b look distinct to the
     plain backslash-normalized key but collapse to one canonical key
     (lexically_normal equivalence) -> unsafe or duplicate rejection.
     Distinct a/b + a/c stays valid as the control.
  P4 manifest cross-check: 4a forges a flags=1 record whose
     compressed_size field carries the uncompressed value (the ignored
     field pre-fix: exit 0 hole) -> post-fix manifest size mismatch,
     plus a flags-field forge -> flags mismatch. 4b keeps a legacy
     record without compressed_sha256 at exit 0 + legacy-unverified
     WARN (warn-open preserved, no silent trust).

Usage: run all probes, or pass one probe number (1-4) to run a single
sub-probe (used for per-is RED-lock evidence).
"""

import contextlib
import hashlib
import io
import json
import pathlib
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import verify_release_package as verifier

HEADER = struct.Struct("<4sHIQ")
ENTRY = struct.Struct("<QIQQQI")
MANIFEST_PATH = "rowl/manifest.json"

FAILURES = []


def check(name, condition, detail):
    print(f"[Mirror][{'PASS' if condition else 'FAIL'}] {name}"
          + ("" if condition else f": {detail}"))
    if not condition:
        FAILURES.append(name)


def try_read(package_path):
    """Returns (ok, message, stderr_text) for read_package_entries."""
    stderr = io.StringIO()
    try:
        with contextlib.redirect_stderr(stderr):
            verifier.read_package_entries(str(package_path))
    except ValueError as error:
        return False, str(error), stderr.getvalue()
    except Exception as error:  # noqa: BLE001 - raw tracebacks must fail loudly
        return False, f"UNEXPECTED {type(error).__name__}: {error}", stderr.getvalue()
    return True, "", stderr.getvalue()


def build_package(package_path, files, manifest_records="auto", offset_override=None):
    """Synthetic v1 package writer.

    files: list of (path, stored_bytes, flags, uncompressed_size).
    manifest_records: auto builds a consistent manifest (digests sliced
      from the final layout at the CLAIMED offsets, so overlapping
      entries stay manifest-consistent); or an explicit record list
      (forge fixtures). offset_override maps path -> claimed index
      offset (payload bytes keep their natural layout).
    Returns [(entry_start, path, offset, comp, uncomp, flags)] index map.
    """
    offset_override = offset_override or {}
    payload = bytearray(HEADER.size)
    natural = {}
    for path, blob, _, _ in files:
        natural[path] = len(payload)
        payload.extend(blob)
    claimed = {path: offset_override.get(path, natural[path]) for path, _, _, _ in files}

    if manifest_records == "auto":
        records = []
        for path, blob, flags, uncomp in files:
            if path == MANIFEST_PATH:
                continue
            absolute = claimed[path]
            stored = bytes(payload[absolute:absolute + len(blob)])
            record = {
                "compressed_size": len(blob),
                "flags": flags,
                "path": path,
                "sha256": (hashlib.sha256(stored).hexdigest() if flags == 0
                           else "ab" * 32),
                "size": uncomp,
            }
            if flags == 1:
                record["compressed_sha256"] = hashlib.sha256(stored).hexdigest()
            records.append(record)
        records.sort(key=lambda record: record["path"])
        manifest_doc = {"files": records, "format": 1}
    else:
        manifest_doc = {"files": manifest_records, "format": 1}
    manifest_bytes = (json.dumps(manifest_doc, sort_keys=True,
                                 separators=(",", ":")) + "\n").encode("utf-8")

    manifest_offset = len(payload)
    payload.extend(manifest_bytes)
    index_offset = len(payload)
    index = bytearray()
    index_map = []
    full_spec = ([(path, blob, flags, uncomp) for path, blob, flags, uncomp in files]
                 + [(MANIFEST_PATH, manifest_bytes, 0, len(manifest_bytes))])
    for path, blob, flags, uncomp in full_spec:
        offset = claimed.get(path, manifest_offset if path == MANIFEST_PATH else 0)
        entry_start = index_offset + len(index)
        encoded = path.encode("utf-8")
        index.extend(ENTRY.pack(0, len(encoded), offset, len(blob), uncomp, flags))
        index.extend(encoded)
        index_map.append((entry_start, path, offset, len(blob), uncomp, flags))
    HEADER.pack_into(payload, 0, b"ROWL", 1, len(full_spec), index_offset)
    payload.extend(index)
    pathlib.Path(package_path).write_bytes(bytes(payload))
    return index_map


def probe1(directory):
    """Overlapping payload ranges are rejected (individually valid)."""
    blob_a = b"A" * 100
    blob_b = b"B" * 100
    files = [("a.bin", blob_a, 0, len(blob_a)),
             ("b.bin", blob_b, 0, len(blob_b))]
    healthy = directory / "p1-healthy.rowlpkg"
    build_package(healthy, files)
    ok, message, _ = try_read(healthy)
    check("P1-control-exit0", ok, f"healthy package rejected: {message!r}")

    forged = directory / "p1-overlap.rowlpkg"
    base = directory / "p1-base.rowlpkg"
    index_map = build_package(base, files)
    a_offset = next(offset for _, path, offset, _, _, _ in index_map if path == "a.bin")
    build_package(forged, files, offset_override={"b.bin": a_offset + 50})
    ok, message, _ = try_read(forged)
    check("P1-exit1", not ok, "overlapping payloads accepted (hole open)")
    check("P1-overlapping-tani", (not ok) and ("overlapping payload" in message),
          f"wrong rejection reason: {message!r}")


def probe2(directory):
    """Path-length cap 4096 (reader :298); 16 KiB constant is gone."""
    boundary = directory / "p2-boundary.rowlpkg"
    build_package(boundary, [("x" * 4096, b"edge", 0, 4)])
    ok, message, _ = try_read(boundary)
    check("P2-boundary-exit0", ok, f"4096-byte path rejected: {message!r}")

    forged = directory / "p2-long.rowlpkg"
    build_package(forged, [("y" * 5000, b"long-path-payload", 0, 17)])
    ok, message, _ = try_read(forged)
    check("P2-exit1", not ok, "5000-byte path accepted (hole open)")
    check("P2-path-length-tani", (not ok) and ("invalid path length" in message),
          f"wrong rejection reason: {message!r}")


def probe3(directory):
    """Canonical duplicate: a/./b collapses onto a/b."""
    control = directory / "p3-control.rowlpkg"
    build_package(control, [("a/b", b"one", 0, 3), ("a/c", b"two", 0, 3)])
    ok, message, _ = try_read(control)
    check("P3-control-exit0", ok, f"distinct paths rejected: {message!r}")

    forged = directory / "p3-dup.rowlpkg"
    build_package(forged, [("a/./b", b"one", 0, 3), ("a/b", b"two", 0, 3)])
    ok, message, _ = try_read(forged)
    check("P3-exit1", not ok, "canonical duplicate accepted (hole open)")
    check("P3-duplicate-tani",
          (not ok) and (("duplicate" in message) or ("unsafe" in message)),
          f"wrong rejection reason: {message!r}")


def probe4(directory):
    """Manifest/index cross-check (4a forge FAIL, 4b legacy WARN-open)."""
    stored = b"C" * 200
    comp, uncomp = len(stored), 800  # ratio 4: inside the 1024 gate
    files = [("c.bin", stored, 1, uncomp)]
    truthful = {
        "compressed_size": comp,
        "compressed_sha256": hashlib.sha256(stored).hexdigest(),
        "flags": 1,
        "path": "c.bin",
        "sha256": "ab" * 32,
        "size": uncomp,
    }

    forged_size = directory / "p4-size.rowlpkg"
    build_package(forged_size, files,
                  manifest_records=[{**truthful, "compressed_size": uncomp}])
    ok, message, _ = try_read(forged_size)
    check("P4a-exit1", not ok, "forged compressed_size accepted (hole open)")
    check("P4a-size-tani", (not ok) and ("manifest size mismatch" in message),
          f"wrong rejection reason: {message!r}")

    forged_flags = directory / "p4-flags.rowlpkg"
    build_package(forged_flags, files, manifest_records=[{**truthful, "flags": 0}])
    ok, message, _ = try_read(forged_flags)
    check("P4a-flags-exit1", not ok, "forged flags accepted (hole open)")
    check("P4a-flags-tani", (not ok) and ("manifest flags mismatch" in message),
          f"wrong rejection reason: {message!r}")

    legacy = directory / "p4-legacy.rowlpkg"
    legacy_record = {key: truthful[key] for key in
                     ("flags", "path", "sha256", "size", "compressed_size")}
    build_package(legacy, files, manifest_records=[legacy_record])
    ok, message, stderr_text = try_read(legacy)
    check("P4b-exit0", ok, f"legacy record rejected: {message!r}")
    check("P4b-legacy-warn", ok and ("legacy-unverified" in stderr_text),
          f"legacy WARN missing on stderr: {stderr_text!r}")


PROBES = {"1": probe1, "2": probe2, "3": probe3, "4": probe4}

with tempfile.TemporaryDirectory() as workdir:
    selected = sys.argv[1:] or ["1", "2", "3", "4"]
    for number in selected:
        if number not in PROBES:
            raise SystemExit(f"unknown probe {number!r}, want one of 1 2 3 4")
        PROBES[number](pathlib.Path(workdir))

print(f"[VerifierMirror] {'OK' if not FAILURES else 'FAIL: ' + ','.join(FAILURES)}")
sys.exit(1 if FAILURES else 0)
