#!/usr/bin/env python3
"""Expansion-ratio agreement gate for the .rowlpkg pipeline (#145).

The runtime reader (engine/src/vfs/rowlpkg_reader.cpp) fail-closes on
flags==1 entries whose integer expansion ratio (uncompressed/compressed)
exceeds 1024. Two pipeline ends must agree with that gate:

1. Pack-time decision (tools/package_assets.py): over-compressible blobs
   (e.g. long zero runs, which zstd shrinks past 1024x) are stored RAW
   instead of emitting a package the reader must reject.
2. Verifier mirror (tools/verify_release_package.py): read_package_entries
   rejects whatever the reader would fail-close on (size caps, flags
   coherence, the ratio gate), so a gated release never ships a package
   that loads short. The packer `verify` subcommand inherits this through
   its shared import (single source of truth).
"""

import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

try:
    import zstandard  # noqa: F401  (presence matters: the pack path needs it)
    HAS_ZSTD = True
except ImportError:
    # W8-c fix-turu: ctest yorumlayicisinda zstd yoksa (Ders-6) yalniz
    # pack-karari SKIP olur; verifier-ayna kontrolleri zstd'siz sentetik
    # yoldan kosar (RAW paketle + flags'i ele yukselt, ratio=1 gecerli
    # taban). Tamamen SKIP yok — kilit her host'ta en az aynayi tutar.
    HAS_ZSTD = False

ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"

sys.path.insert(0, str(ROOT / "tools"))
import verify_release_package as verifier


HEADER = struct.Struct("<4sHIQ")
ENTRY = struct.Struct("<QIQQQI")
MAX_EXPANSION_RATIO = 1024


def pack(source, output):
    return subprocess.run([sys.executable, str(PACKAGER), str(source), str(output)],
                          capture_output=True, text=True, check=False)


def verify_package(package):
    return subprocess.run([sys.executable, str(PACKAGER), "verify", str(package)],
                          capture_output=True, text=True, check=False)


def read_index(package_path):
    """Returns (header_bytes, [(entry_start, path_hash, path_len, offset, comp, uncomp, flags, path)])."""
    data = pathlib.Path(package_path).read_bytes()
    _, _, count, index_offset = HEADER.unpack(data[:HEADER.size])
    entries = []
    cursor = index_offset
    for _ in range(count):
        start = cursor
        path_hash, path_len, offset, comp, uncomp, flags = ENTRY.unpack(
            data[cursor:cursor + ENTRY.size])
        cursor += ENTRY.size
        path = data[cursor:cursor + path_len].decode("utf-8")
        cursor += path_len
        entries.append((start, path_hash, path_len, offset, comp, uncomp, flags, path))
    return data, entries


def promote_to_compressed(package_bytes, entry_start):
    """RAW (flags=0, comp==uncomp) girdiyi ele flags=1'e yukseltir.

    ENTRY duzeni "<QIQQQI": flags alani kayit basindan +36 bayttadir.
    Verifier'da ratio=1 ile gecer; manifest'te compressed_sha256 yoksa
    legacy-WARN ile gecer (D18a bulgu-2/11 karari). zstd'siz host'ta ayna
    kontrolleri icin gecerli flags==1 tabani uretir.
    """
    raw = bytearray(package_bytes)
    struct.pack_into("<I", raw, entry_start + 36, 1)
    return bytes(raw)


def promote_manifest_flags(package_bytes, manifest_offset, manifest_size,
                            target_path):
    """Gomulu manifestteki hedef kaydin "flags":0 degerini 1'e cevirir.

    W8-f2 capraz-kontrolu (manifest flags/index flags tutarliligi) yalniz
    index-yukseltmeyi fail-closed reddeder ("embedded manifest flags
    mismatch") — sentetik taban iki yuzu birlikte yukseltmelidir. Yama
    tek bayttir (0->1, ayni uzunluk): manifest yuk boyu, tum offset'ler ve
    index aynen korunur. compressed_size sayisal olarak gecerli kalir
    (RAW tabanda comp==uncomp); compressed_sha256 eksikligi legacy-WARN
    ile gecer (bilincli, D18a karari). Kapsam kilidi: hedef "path"ten
    geriye dogru ilk "flags":0 alinir, arada '}' varsa (baska kayit)
    SystemExit — yanlis kayit yamalanmaz.
    """
    raw = bytearray(package_bytes)
    payload = raw[manifest_offset:manifest_offset + manifest_size]
    marker = f'"path":"{target_path}"'.encode("utf-8")
    at = payload.find(marker)
    if at < 0:
        raise SystemExit(f"manifestte kayit yok: {target_path}")
    flag = b'"flags":0'
    found = payload.rfind(flag, 0, at)
    if found < 0 or b"}" in payload[found:at]:
        raise SystemExit(
            f"manifest yama kapsam-disi: {target_path} kaydinda tekil "
            f'"flags":0 bulunamadi')
    raw[manifest_offset + found + len(flag) - 1] = ord("1")
    return bytes(raw)


def write_sidecar(package_path):
    """Taze pakete gecerli .sha256 sidecar yazar (dogrulanabilir taban)."""
    data = pathlib.Path(package_path).read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    pathlib.Path(str(package_path) + ".sha256").write_text(
        f"{digest}  {pathlib.Path(package_path).name}\n", encoding="utf-8")


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)

    # 1. Pack-time decision: a 256 KiB zero run compresses ~8500x — past the
    # reader's 1024 gate — so the packer must store it raw (flags==0).
    # (zstd'siz host'ta bu adim SKIP — packer zaten hepsini RAW yazar.)
    redundant = root / "redundant"
    redundant.mkdir()
    (redundant / "zeros.bin").write_bytes(b"\x00" * (256 * 1024))
    (redundant / "normal.txt").write_text("hello rowl\n" * 64, encoding="utf-8")
    package = root / "ratio.rowlpkg"
    result = pack(redundant, package)
    if result.returncode != 0:
        raise SystemExit(f"pack failed on redundant input: {result.stderr.strip()}")
    _, entries = read_index(package)
    if HAS_ZSTD:
        zeros = next(e for e in entries if e[7] == "zeros.bin")
        if zeros[6] != 0 or zeros[4] != zeros[5]:
            raise SystemExit(
                f"packer emitted a reader-rejected entry: zeros.bin flags={zeros[6]} "
                f"compressed={zeros[4]} uncompressed={zeros[5]}")
        print("[RatioGate] over-compressible blob stored raw "
              f"(flags=0, {zeros[5]} bytes).")
    else:
        print("[RatioGate][SKIP] pack-time RAW decision needs zstd on this host; "
              "mirror checks below run on the zstd-free promoted base.")

    # The healthy package passes both gates.
    if verify_package(package).returncode != 0:
        raise SystemExit("packer verify rejected the ratio-safe package")
    verifier.read_package_entries(str(package))

    # 2. Verifier mirror: inflate one flags==1 entry's declared uncompressed
    # size 2048x — the reader would fail-close, so the verifier must too.
    normal = next(e for e in entries if e[7] == "normal.txt")
    if normal[6] != 1:
        if not HAS_ZSTD:
            # zstd'siz sentetik taban: RAW girdiyi ele flags=1'e yukselt
            # (ratio=1, verifier gecer; legacy-WARN uyarisi normaldir).
            # Index + manifest birlikte yukseltilir (W8-f2 capraz-kontrolu
            # tek-yuz yukseltmeyi fail-closed reddeder).
            promoted = root / "promoted.rowlpkg"
            promoted.write_bytes(
                promote_to_compressed(pathlib.Path(package).read_bytes(), normal[0]))
            data, entries = read_index(promoted)
            manifest = next(e for e in entries if e[7] == "rowl/manifest.json")
            promoted.write_bytes(
                promote_manifest_flags(data, manifest[3], manifest[4],
                                       "normal.txt"))
            write_sidecar(promoted)
            if verify_package(promoted).returncode != 0:
                raise SystemExit("packer verify rejected the promoted zstd-free base")
            verifier.read_package_entries(str(promoted))
            package = promoted
            _, entries = read_index(package)
            normal = next(e for e in entries if e[7] == "normal.txt")
            print("[RatioGate] zstd-free promoted base holds "
                  "(flags==1, ratio=1, sidecar OK).")
        else:
            raise SystemExit("fixture assumption collapsed: normal.txt is not zstd-compressed")
    forged = root / "forged.rowlpkg"
    raw = bytearray(pathlib.Path(package).read_bytes())
    # ENTRY layout "<QIQQQI": uncompressedSize starts 28 bytes into the record.
    struct.pack_into("<Q", raw, normal[0] + 28, normal[4] * 2048)
    forged.write_bytes(bytes(raw))
    try:
        verifier.read_package_entries(str(forged))
    except ValueError as error:
        if "ratio" not in str(error):
            raise SystemExit(f"verifier rejected the forged package for the wrong reason: {error}")
    else:
        raise SystemExit("verifier accepted a package breaching the reader ratio gate")
    if verify_package(forged).returncode == 0:
        raise SystemExit("packer verify accepted a package breaching the reader ratio gate")
    print("[RatioGate] forged 2048x entry rejected by verifier and packer verify.")

    # 3. Flags coherence mirror: an unknown flags value is reader-rejected.
    badflags = root / "badflags.rowlpkg"
    raw = bytearray(pathlib.Path(package).read_bytes())
    struct.pack_into("<I", raw, normal[0] + 36, 2)
    badflags.write_bytes(bytes(raw))
    try:
        verifier.read_package_entries(str(badflags))
    except ValueError as error:
        if "compression metadata" not in str(error):
            raise SystemExit(f"verifier rejected bad flags for the wrong reason: {error}")
    else:
        raise SystemExit("verifier accepted a package with an unknown flags value")
    print("[RatioGate] unknown flags value rejected.")

print("[PackageRatioGate] pack-time decision and verifier mirror hold.")
