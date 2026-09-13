#!/usr/bin/env python3
"""Pack an asset directory into a deterministic ROWL v1 (.rowlpkg) archive.

Determinism contract: the same source tree always yields byte-identical
output, regardless of filesystem walk order or file mtimes. File entries are
processed in canonical (byte-wise, locale-independent) rel-path order, the
archive carries no timestamps, compression uses fixed settings, and the
embedded manifest is canonical JSON. Packing the same folder twice must
produce identical SHA-256 digests.

Layout (unchanged v1 format, readable by RowlPkgDataSource):
  master header, payload blobs (sorted entry order), index table.
Plus one synthetic entry, `rowl/manifest.json`, listing every packed file
with its uncompressed size and SHA-256. The manifest entry itself is always
stored uncompressed so auditors can parse it with the standard library only.
"""

import hashlib
import json
import os
import struct
import sys

try:
    import zstandard as zstd
    HAS_ZSTD = True
except ImportError:
    HAS_ZSTD = False

MANIFEST_PATH = "rowl/manifest.json"
MANIFEST_FORMAT = 1
SKIP_SUFFIXES = (".rowlpkg", ".tmp", ".gitkeep")


def fnv1a64(data):
    value = 14695981039346656037
    for byte in data:
        value ^= byte
        value = (value * 1099511628211) & 0xffffffffffffffff
    return value


class PackError(Exception):
    """Structured pack failure: carries a machine-readable issue list."""

    def __init__(self, issues):
        super().__init__("; ".join(f"[{code}] {path}: {detail}" for code, path, detail in issues))
        self.issues = issues


def report_issues(issues):
    for code, path, detail in issues:
        print(f"[Packer][ERROR][{code}] {path}: {detail}", file=sys.stderr)
    print(f"[Packer][ERROR] asset validation failed with {len(issues)} issue(s).", file=sys.stderr)


def collect_files(input_dir):
    """Walk the tree and validate early; returns sorted (full_path, rel_path)."""
    issues = []
    file_list = []
    seen = set()
    for root, dirnames, filenames in os.walk(input_dir, followlinks=False):
        # Canonical traversal order (the final sort below is authoritative,
        # but a stable walk keeps logs and issue reports reproducible too).
        dirnames.sort()
        filenames.sort()
        for name in filenames:
            full_path = os.path.join(root, name)
            rel_path = os.path.relpath(full_path, input_dir).replace("\\", "/")
            if rel_path.endswith(SKIP_SUFFIXES):
                continue
            if rel_path in seen:
                issues.append(("duplicate-path", rel_path, "same relative path collected twice"))
                continue
            seen.add(rel_path)
            if rel_path == MANIFEST_PATH:
                issues.append(("reserved-path", rel_path,
                               f"'{MANIFEST_PATH}' is reserved for the embedded package manifest"))
                continue
            if os.path.islink(full_path) and not os.path.exists(full_path):
                issues.append(("dangling-symlink", rel_path, "symbolic link target does not exist"))
                continue
            real_path = os.path.realpath(full_path)
            if os.path.commonpath((input_dir, real_path)) != input_dir:
                issues.append(("symlink-outside-root", rel_path,
                               "symbolic link escapes the asset root"))
                continue
            try:
                size = os.path.getsize(full_path)
            except OSError as error:
                issues.append(("unreadable", rel_path, str(error)))
                continue
            if size == 0:
                issues.append(("zero-byte", rel_path, "empty files are rejected at pack time"))
                continue
            file_list.append((full_path, rel_path))
    # Canonical byte-wise (codepoint) order: locale-independent, mtime-blind.
    file_list.sort(key=lambda item: item[1])
    return file_list, issues


def pack_directory(input_dir, output_pkg):
    input_dir = os.path.realpath(input_dir)
    output_pkg = os.path.abspath(output_pkg)
    print(f"[Packer] Compressing assets from '{input_dir}' into '{output_pkg}'...")

    if not os.path.isdir(input_dir):
        raise PackError([("missing-input-dir", input_dir, "asset source directory does not exist")])

    file_list, issues = collect_files(input_dir)
    if issues:
        raise PackError(issues)

    entries = []
    payload_bytes = bytearray()
    manifest_records = []

    header_size = 4 + 2 + 4 + 8  # 18 bytes
    current_offset = header_size

    cctx = zstd.ZstdCompressor(level=3) if HAS_ZSTD else None

    for full_path, rel_path in file_list:
        try:
            with open(full_path, "rb") as f:
                uncompressed_data = f.read()
        except OSError as error:
            raise PackError([("unreadable", rel_path, str(error))])
        if len(uncompressed_data) == 0:
            raise PackError([("zero-byte", rel_path, "file became empty while packing")])

        uncompressed_size = len(uncompressed_data)
        digest = hashlib.sha256(uncompressed_data).hexdigest()

        if HAS_ZSTD and uncompressed_size > 0:
            compressed_data = cctx.compress(uncompressed_data)
            flags = 1  # Zstd
        else:
            compressed_data = uncompressed_data
            flags = 0  # Raw

        compressed_size = len(compressed_data)
        path_bytes = rel_path.encode("utf-8")
        path_hash = fnv1a64(path_bytes)

        entries.append({
            "path_hash": path_hash,
            "rel_path": rel_path,
            "path_bytes": path_bytes,
            "offset": current_offset,
            "compressed_size": compressed_size,
            "uncompressed_size": uncompressed_size,
            "flags": flags,
            "data": compressed_data,
        })
        manifest_records.append({
            "path": rel_path,
            "size": uncompressed_size,
            "sha256": digest,
            "compressed_size": compressed_size,
            "flags": flags,
        })

        payload_bytes.extend(compressed_data)
        current_offset += compressed_size

    # Embedded manifest: canonical JSON (sorted keys, compact separators, LF),
    # always stored uncompressed so it stays readable without third-party libs.
    manifest_doc = {"format": MANIFEST_FORMAT, "files": manifest_records}
    manifest_bytes = (json.dumps(manifest_doc, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
    manifest_path_bytes = MANIFEST_PATH.encode("utf-8")
    entries.append({
        "path_hash": fnv1a64(manifest_path_bytes),
        "rel_path": MANIFEST_PATH,
        "path_bytes": manifest_path_bytes,
        "offset": current_offset,
        "compressed_size": len(manifest_bytes),
        "uncompressed_size": len(manifest_bytes),
        "flags": 0,
        "data": bytes(manifest_bytes),
    })
    payload_bytes.extend(manifest_bytes)
    current_offset += len(manifest_bytes)

    index_offset = current_offset
    index_bytes = bytearray()

    # Build index table
    for entry in entries:
        path_len = len(entry["path_bytes"])
        # struct fmt: uint64 pathHash, uint32 pathLength, uint64 offset, uint64 compressedSize, uint64 uncompressedSize, uint32 flags
        entry_header = struct.pack("<QIQQQI",
            entry["path_hash"],
            path_len,
            entry["offset"],
            entry["compressed_size"],
            entry["uncompressed_size"],
            entry["flags"]
        )
        index_bytes.extend(entry_header)
        index_bytes.extend(entry["path_bytes"])

    # Build master header: "ROWL", version=1 (uint16), fileCount (uint32), indexOffset (uint64)
    file_count = len(entries)
    master_header = struct.pack("<4sHIQ", b"ROWL", 1, file_count, index_offset)

    # Atomic commit: readers never observe a half-written package.
    output_dir = os.path.dirname(output_pkg) or "."
    os.makedirs(output_dir, exist_ok=True)
    temporary_pkg = f"{output_pkg}.tmp-{os.getpid()}"
    try:
        with open(temporary_pkg, "wb") as out_f:
            out_f.write(master_header)
            out_f.write(payload_bytes)
            out_f.write(index_bytes)
            out_f.flush()
            os.fsync(out_f.fileno())
        os.replace(temporary_pkg, output_pkg)
    finally:
        if os.path.exists(temporary_pkg):
            os.unlink(temporary_pkg)

    with open(output_pkg, "rb") as finished:
        package_sha256 = hashlib.sha256(finished.read()).hexdigest()
    print(f"[Packer] Package creation successful! Total files: {file_count}, Output size: {os.path.getsize(output_pkg)} bytes")
    print(f"[Packer] SHA256: {package_sha256}")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 tools/package_assets.py <input_dir> <output_rowlpkg>")
        sys.exit(1)

    input_dir = sys.argv[1]
    output_pkg = sys.argv[2]
    try:
        pack_directory(input_dir, output_pkg)
    except PackError as error:
        report_issues(error.issues)
        sys.exit(2)
