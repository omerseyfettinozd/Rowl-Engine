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

Sidecar + verify (Faz 6 Dilim 2): every successful pack also writes
`<output>.sha256` in canonical `sha256sum` format
(`<hash><two-spaces><basename>\\n`), and `verify` re-checks the sidecar
digest plus the embedded manifest / per-file hashes:

  python3 tools/package_assets.py <input_dir> <output_rowlpkg>
  python3 tools/package_assets.py verify <package_rowlpkg> [--json]

Exit codes for `verify`: 0 OK, 1 corrupt/missing, 2 usage error.
`--json` prints one line with keys `ok, package, sha256, sidecar
(ok|mismatch|missing|malformed), entries, error`.

Relationship with tools/verify_release_package.py: both live. The `verify`
subcommand here is the *distribution gate* (is this single .rowlpkg file
intact?). verify_release_package.py is the separate *CI gate* for the full
portable release layout (launchers, runtime lib, mods, notices). The deep
manifest/file-hash check is NOT duplicated: `verify` imports
`read_package_entries` from verify_release_package (single source of truth,
lazy import so `pack` never pays for it and no import cycle exists).
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
# #145: reader agreement — engine/src/vfs/rowlpkg_reader.cpp rejects flags==1
# entries whose integer expansion ratio (uncompressed/compressed) exceeds this.
# The packer must never emit what the reader must reject; over-compressible
# blobs fall back to raw storage below. Keep in sync with
# kMaxCompressionExpansionRatio and tools/verify_release_package.py.
MAX_EXPANSION_RATIO = 1024
SKIP_SUFFIXES = (".rowlpkg", ".tmp", ".gitkeep")

# Faz 1 Dilim 1: MVP medya format sözleşmesi. C# tarafındaki tek tablonun
# (editor/Services/MediaFormatCatalog.cs) aynasıdır; iki tarafın kümeleri
# tests/test_media_format_gate.py ile kilitlenir. Yorum satırlarındaki
# CONTRACT etiketleri o testin parse ettiği blokları işaretler.
# CONTRACT(accepted-image)
ACCEPTED_IMAGE_EXTS = frozenset({
    ".png",
    ".jpg",
    ".jpeg",
    ".bmp",
    ".tga",
})
# CONTRACT(accepted-audio)
ACCEPTED_AUDIO_EXTS = frozenset({
    ".wav",
    ".ogg",
})
# CONTRACT(accepted-font)
ACCEPTED_FONT_EXTS = frozenset({
    ".ttf",
    ".otf",
})
# CONTRACT(converter-pending)
# Faz 5 Dilim 5 dönüştürücü hattı (MP3/FLAC -> OGG Vorbis, WebP -> PNG) ile
# kabul-dönüştürülür: kaynaklar paketlenmez, dönüştürülmüş .ogg/.png çıktıları
# normal asset olarak paketlenir; yanındaki .rowlconv.json sidecar varsa
# manifest kaydına converted_from (kaynak yol + hash) yazılır.
CONVERTER_PENDING_EXTS = frozenset({
    ".mp3",
    ".flac",
    ".webp",
})
# CONTRACT(known-unsupported)
# Bilinen ama MVP dışında kalan medya uzantıları (GIF dahil).
KNOWN_UNSUPPORTED_MEDIA_EXTS = frozenset({
    ".gif",
    ".psd",
    ".hdr",
    ".aiff",
    ".aif",
    ".m4a",
    ".wma",
    ".aac",
    ".opus",
    ".woff",
    ".woff2",
    ".eot",
})

ACCEPTED_MEDIA_EXTS = ACCEPTED_IMAGE_EXTS | ACCEPTED_AUDIO_EXTS | ACCEPTED_FONT_EXTS

# Faz 5 Dilim 5: dönüştürücü kaynakları (MP3/FLAC/WebP) doğrudan paketlenmez
# ama reddedilmez de (kabul-dönüştürerek): import hattı bunları .ogg/.png'ye
# çevirir ve dönüştürülmüş çıktılar yukarıdaki kabul kümeleriyle paketlenir
# (.ogg/.png zaten ACCEPTED_* kümelerindedir; ayrı kümeye gerek yok).


def check_media_format(rel_path):
    """Returns an issue tuple when rel_path violates the MVP media contract, else None."""
    _, ext = os.path.splitext(rel_path)
    ext = ext.lower()
    if not ext or ext in ACCEPTED_MEDIA_EXTS:
        return None
    if ext in CONVERTER_PENDING_EXTS:
        # Kabul-dönüştürerek: ham kaynak görülürse reddetmek yerine uyarısız
        # geçilir (dönüştürülmüş çıktı zaten kabul kümelerindedir). Ham
        # kaynağın pakete girmesi import hattının sorumluluğundadır.
        return None
    if ext in KNOWN_UNSUPPORTED_MEDIA_EXTS:
        return ("unsupported-media-format", rel_path,
                f"'{ext}' is not an accepted media format. "
                "Accepted: PNG/JPEG/BMP/TGA, WAV/OGG, TTF/OTF.")
    return None


def read_sidecar_converted_from(full_path):
    """Adjacent <file>.rowlconv.json sidecar -> converted_from record, else None.

    D18a (KI-11 kardes kilit): sidecar bir `output_sha256` tasiyorsa cikti
    dosyasi yeniden hash'lenir; uyusmazsa sidecar bayattir (cikti sidecar
    yazimindan sonra degismistir) -> kayit DUSER + stderr uyarisi, pack
    normal devam eder (fail-soft: sidecar provenance ipucudur, butunluk
    kaniti degil). `output_sha256`'siz eski sidecar'lar aynen guvenilir
    (anahtarsiz gecis, additive format).
    """
    sidecar_path = full_path + ".rowlconv.json"
    try:
        if not os.path.isfile(sidecar_path):
            return None
        with open(sidecar_path, "r", encoding="utf-8") as f:
            sidecar = json.load(f)
    except (OSError, ValueError):
        return None
    if not isinstance(sidecar, dict):
        return None
    source_sha = sidecar.get("source_sha256")
    if not source_sha:
        return None
    expected_output = sidecar.get("output_sha256")
    if isinstance(expected_output, str) and len(expected_output) == 64:
        try:
            with open(full_path, "rb") as f:
                actual_output = hashlib.sha256(f.read()).hexdigest()
        except OSError:
            return None
        if actual_output != expected_output:
            print(f"[Packer][WARN][stale-sidecar] {full_path}: output_sha256 "
                  f"mismatch (sidecar bayat, cikti sonradan degismis) — "
                  f"converted_from dusuruldu.", file=sys.stderr)
            return None
    settings = sidecar.get("settings") if isinstance(sidecar.get("settings"), dict) else {}
    source_path = (sidecar.get("source_path") or settings.get("source_path")
                   or settings.get("source"))
    converter = " ".join(part for part in
                         (sidecar.get("converter_name"), sidecar.get("converter_version"))
                         if part).strip()
    record = {"source_sha256": source_sha}
    if source_path:
        record["path"] = source_path
    if converter:
        record["converter"] = converter
    return record


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
            format_issue = check_media_format(rel_path)
            if format_issue is not None:
                issues.append(format_issue)
                continue
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
            # #145 pack-time decision: the runtime reader fail-closes on
            # flags==1 entries with uncompressed//compressed > 1024 (integer
            # division, mirrored exactly). Highly redundant blobs (e.g. long
            # zero runs) trip that gate, so store them raw instead of emitting
            # a package the reader must reject.
            if len(compressed_data) == 0 or uncompressed_size // len(compressed_data) > MAX_EXPANSION_RATIO:
                compressed_data = uncompressed_data
                flags = 0  # Raw (over-compressible for the reader's ratio gate)
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
            # D18a (KI-11): flags=1 kayda sikistirilmis-bayt hash anahtari.
            # Deterministik + ortam-bagimsiz; okumada opsiyonel (eski
            # paketler anahtarsiz gecmeye devam eder). Format eklentisi
            # additive'dir, okuyucu/ABI etkilenmez.
            **({"compressed_sha256": hashlib.sha256(compressed_data).hexdigest()}
               if flags == 1 else {}),
            **({"converted_from": converted_from} if (converted_from := read_sidecar_converted_from(full_path)) else {}),
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
    write_sha256_sidecar(output_pkg, package_sha256)


def write_sha256_sidecar(output_pkg, package_sha256):
    """Write `<output>.sha256` in canonical sha256sum-compatible format.

    Exactly `<hash><two-spaces><basename>\\n`; the basename (not the full
    path) keeps the file relocatable and `sha256sum -c` compatible.
    Atomic commit, mirroring the package write above.
    """
    sidecar_path = output_pkg + ".sha256"
    content = f"{package_sha256}  {os.path.basename(output_pkg)}\n"
    temporary_sidecar = f"{sidecar_path}.tmp-{os.getpid()}"
    try:
        with open(temporary_sidecar, "w", encoding="utf-8", newline="\n") as out_f:
            out_f.write(content)
            out_f.flush()
            os.fsync(out_f.fileno())
        os.replace(temporary_sidecar, sidecar_path)
    finally:
        if os.path.exists(temporary_sidecar):
            os.unlink(temporary_sidecar)
    print(f"[Packer] SHA256 sidecar: {sidecar_path}")


def _load_release_verifier():
    """Import read_package_entries from verify_release_package (single source).

    Lazy + sys.path-tolerant: works both as `python3 tools/package_assets.py`
    (script dir already on sys.path) and via `import package_assets` from the
    repo root (test_media_format_gate does the latter). verify_release_package
    has no CLI side effects on import (guarded by `__main__`) and never
    imports package_assets, so no cycle is possible.
    """
    try:
        from verify_release_package import read_package_entries
        return read_package_entries
    except ImportError:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        if script_dir not in sys.path:
            sys.path.insert(0, script_dir)
        from verify_release_package import read_package_entries
        return read_package_entries


def verify_package(package_path, as_json=False):
    """Verify one .rowlpkg file: sidecar digest + embedded manifest/file hashes.

    Missing-sidecar decision (fail-closed distribution gate): verification
    CONTINUES into the embedded manifest check for diagnostics, but the
    result is still FAIL with exit 1 — an artifact that left the signed
    pipeline without its checksum must not pass as OK.

    Returns a process exit code: 0 OK, 1 corrupt/missing.
    Prints exactly one human-readable line (stdout), or one JSON object line
    with --json.
    """
    package_abs = os.path.abspath(package_path)
    name = os.path.basename(package_abs)
    sidecar_path = package_abs + ".sha256"

    def emit(ok, sidecar_state, actual, entries, error):
        if as_json:
            print(json.dumps({"ok": ok, "package": name, "sha256": actual,
                              "sidecar": sidecar_state, "entries": entries,
                              "error": error}, sort_keys=True))
        elif ok:
            print(f"[Packer][verify] OK: {name} ({entries} entries, sha256 {actual[:16]}...)")
        else:
            print(f"[Packer][verify] FAIL: {name}: {error}")

    if not os.path.isfile(package_abs):
        emit(False, "missing", None, None, "package file does not exist")
        return 1
    try:
        with open(package_abs, "rb") as f:
            actual = hashlib.sha256(f.read()).hexdigest()
    except OSError as error:
        emit(False, "missing", None, None, f"package is unreadable: {error}")
        return 1

    if os.path.isfile(sidecar_path):
        try:
            with open(sidecar_path, "r", encoding="utf-8") as f:
                content = f.read()
        except (OSError, ValueError) as error:
            emit(False, "malformed", actual, None,
                 f".sha256 sidecar is unreadable: {error}")
            return 1
        parts = content.split()
        if (len(parts) != 2 or len(parts[0]) != 64 or parts[1] != name):
            emit(False, "malformed", actual, None,
                 ".sha256 sidecar is malformed (want '<hash>  <basename>')")
            return 1
        try:
            bytes.fromhex(parts[0])
        except ValueError:
            emit(False, "malformed", actual, None,
                 ".sha256 sidecar is malformed (want '<hash>  <basename>')")
            return 1
        if parts[0] != actual:
            emit(False, "mismatch", actual, None,
                 "package digest does not match .sha256 sidecar")
            return 1
        sidecar_state = "ok"
        missing_sidecar = False
    else:
        sidecar_state = "missing"
        missing_sidecar = True

    try:
        read_package_entries = _load_release_verifier()
        entries = read_package_entries(package_abs)
    except (OSError, ValueError) as error:
        emit(False, sidecar_state, actual, None,
             f"embedded manifest/file-hash check failed: {error}")
        return 1

    count = len(entries)
    if missing_sidecar:
        emit(False, sidecar_state, actual, count,
             ".sha256 sidecar is missing (package itself is internally consistent)")
        return 1
    emit(True, sidecar_state, actual, count, None)
    return 0


def _print_usage():
    print("Usage:")
    print("  python3 tools/package_assets.py <input_dir> <output_rowlpkg>")
    print("  python3 tools/package_assets.py verify <package_rowlpkg> [--json]")


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "verify":
        rest = sys.argv[2:]
        as_json = "--json" in rest
        positional = [arg for arg in rest if arg != "--json"]
        if len(positional) != 1:
            _print_usage()
            sys.exit(2)
        sys.exit(verify_package(positional[0], as_json=as_json))

    if len(sys.argv) < 3:
        _print_usage()
        sys.exit(1)

    input_dir = sys.argv[1]
    output_pkg = sys.argv[2]
    try:
        pack_directory(input_dir, output_pkg)
    except PackError as error:
        report_issues(error.issues)
        sys.exit(2)
