#!/usr/bin/env python3
"""D18a — VFS butunluk derin-kontrol + sidecar provenance kilitleri (KI-11).

Dort prob, tek dosya (python-degisikligi -> python-prob; yeni native ikili yok):

  R1 (KI-11 yanlis-yesil): flags=1 (zstd) payload tek-bayt flip + sidecar
      yeniden hesaplanir -> verify exit 1, tani satirinda "compressed".
      (Pre-fix delik: exit 0 "OK" — determinizm butunluk degildir.)
      zstd YOKSA sentetik flags=1 paketi ayni sahneyi kosar (R1-synth) —
      SKIP YOK, kilit her ortamda calisir; kaskad-cemberi on-kosul
      kirmizisinda verify sahnesini atlar.
  R2 (kontrol): flags=0 (raw) payload tek-bayt flip + taze sidecar ->
      once/sonra exit 1, "checksum mismatch" (eski davranis korunur).
  S1 (sidecar stale-output): cikti dosyasi sidecar yazimindan SONRA degisir
      (output_sha256 bayat) -> kaydin `converted_from`'i DUSER + stderr
      uyarisi, pack exit 0 (fail-soft: sidecar provenance ipucudur,
      butunluk kaniti degil; pack'i fail-closed yapmak mesru akislari kirar).
      (Pre-fix delik: manifestte `converted_from` VAR — sorgusuz guven.)
  S2 (kontrol): fresh sidecar -> `converted_from` once/sonra VAR.
  S3 (malformed sidecar): `output_sha256` bozuk deger tasir -> kayit
      DUSER + stderr uyarisi, pack exit 0 (fail-open deliginin kilidi).
  R3 (bozuk kayit): manifestte index-disi path -> exit 1 "unknown path",
      ham traceback YOK (FAIL/JSON sozlesmesi korunur).

Tum problar calisir, her prob PASS/FAIL yazdirir; herhangi biri duserde
exit 1. Kirmizi cerceve: pre-fix kosumda R1 FAIL (exit 0 gozlemi) + S1 FAIL
(converted_from VAR gozlemi), R2/S2 PASS beklenir.
"""

import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
PACKAGER = ROOT / "tools" / "package_assets.py"

HEADER = struct.Struct("<4sHIQ")
ENTRY = struct.Struct("<QIQQQI")
MANIFEST_PATH = "rowl/manifest.json"

FAILURES = []

try:
    import zstandard  # noqa: F401
    HAS_ZSTD = True
except ImportError:
    HAS_ZSTD = False


def check(name, condition, detail):
    print(f"[D18a][{'PASS' if condition else 'FAIL'}] {name}"
          + ("" if condition else f": {detail}"))
    if not condition:
        FAILURES.append(name)


def run_pack(src_dir, package_path):
    return subprocess.run([sys.executable, str(PACKAGER),
                           str(src_dir), str(package_path)],
                          capture_output=True, text=True, check=False)


def run_verify(package_path):
    return subprocess.run([sys.executable, str(PACKAGER), "verify",
                           str(package_path)],
                          capture_output=True, text=True, check=False)


def parse_index(package_path):
    """Test-tarafi minimal v1 index okuyucu: path -> (offset, csize, usize, flags)."""
    raw = pathlib.Path(package_path).read_bytes()
    magic, version, count, index_offset = HEADER.unpack_from(raw, 0)
    assert magic == b"ROWL" and version == 1, "not a ROWL v1 archive"
    entries = {}
    cursor = index_offset
    for _ in range(count):
        _, path_len, offset, csize, usize, flags = ENTRY.unpack_from(raw, cursor)
        cursor += ENTRY.size
        path = raw[cursor:cursor + path_len].decode("utf-8")
        cursor += path_len
        entries[path] = (offset, csize, usize, flags)
    return entries


def read_manifest(package_path):
    raw = pathlib.Path(package_path).read_bytes()
    entries = parse_index(package_path)
    offset, csize, usize, flags = entries[MANIFEST_PATH]
    assert flags == 0 and csize == usize, "manifest must be stored uncompressed"
    return json.loads(raw[offset:offset + csize].decode("utf-8"))


def r1_verify_stage(package_path, rel_path, tag):
    """Ortak R1 sahnesi: flip + taze sidecar -> exit 1 + 'compressed' tani."""
    flip_payload_byte(package_path, rel_path, refresh_sidecar=True)
    result = run_verify(package_path)
    combined = result.stdout + result.stderr
    check(tag + "-exit1", result.returncode == 1,
          f"KI-11 deligi: flip'li flags=1 yuk exit {result.returncode}: {combined!r}")
    check(tag + "-compressed-tani", "compressed" in combined,
          f"tani satirinda 'compressed' yok: {combined!r}")


def build_synth_package(package_path, blobs, manifest_doc):
    """Sentetik v1 paketi: blobs=[(path, payload, flags)], manifest_doc dict.
    R1-synth (flags=1 verify-mantigi) + R3 (bozuk-kayit) ortak kurucusu."""
    manifest_bytes = (json.dumps(manifest_doc, sort_keys=True,
                                 separators=(",", ":")) + "\n").encode("utf-8")
    entries_spec = [(path, blob, flags, len(blob)) for path, blob, flags in blobs]
    entries_spec.append((MANIFEST_PATH, manifest_bytes, 0, len(manifest_bytes)))
    raw = bytearray(HEADER.size)
    offsets = []
    for _, blob, _, _ in entries_spec:
        offsets.append(len(raw))
        raw.extend(blob)
    index_offset = len(raw)
    for (path, blob, flags, usize), offset in zip(entries_spec, offsets):
        encoded = path.encode("utf-8")
        raw.extend(ENTRY.pack(0, len(encoded), offset, len(blob), usize, flags))
        raw.extend(encoded)
    HEADER.pack_into(raw, 0, b"ROWL", 1, len(entries_spec), index_offset)
    pathlib.Path(package_path).write_bytes(bytes(raw))


def build_synth_flags1(package_path, rel_path, payload):
    """zstd'siz R1 yuku: flags=1 iddiali sentetik paket. Verify bayt
    seviyesinde hash karsilastirir (decompress YOK) — bu kilit verify
    mantigini ortam-bagimsiz test eder; packer/zstd uretimi ayri
    (zstd'li kosuda packer dali calisir)."""
    manifest_doc = {"files": [
        {"compressed_sha256": hashlib.sha256(payload).hexdigest(),
         "path": rel_path,
         "sha256": "ff" * 32,
         "size": len(payload)},
    ], "format": 1}
    build_synth_package(package_path, [(rel_path, payload, 1)], manifest_doc)


def flip_payload_byte(package_path, rel_path, refresh_sidecar=True):
    """Tek bayt cevir (bozma); istenirse sidecar taze hesaplanir."""
    entries = parse_index(package_path)
    offset, csize, _, _ = entries[rel_path]
    raw = bytearray(pathlib.Path(package_path).read_bytes())
    raw[offset] ^= 1
    pathlib.Path(package_path).write_bytes(bytes(raw))
    if refresh_sidecar:
        digest = hashlib.sha256(bytes(raw)).hexdigest()
        pathlib.Path(str(package_path) + ".sha256").write_text(
            f"{digest}  {pathlib.Path(package_path).name}\n", encoding="utf-8")
    return entries[rel_path]


with tempfile.TemporaryDirectory() as directory:
    root = pathlib.Path(directory)

    # --- R1: flags=1 payload flip + taze sidecar ---
    # D18a-followup (bulgu 7): SKIP kaldirildi — kilit her ortamda CALISIR.
    # zstd VARSA packer uretimi (tam entegrasyon); YOKSA sentetik flags=1
    # paketi (verify bayt-mantigi kilidi; kardes test hard-fail emsali).
    if HAS_ZSTD:
        r1_src = root / "r1-src"
        r1_src.mkdir()
        # Sıkıştırılabilir ama oran-kapısına takılmayan metin (oran ~10-50x,
        # fallback esigi 1024): flags=1 garanti.
        (r1_src / "story.txt").write_text(
            "".join(f"rowl-d18a-probe line {i:06d} with some padding words here\n"
                    for i in range(4000)), encoding="utf-8")
        r1_pkg = root / "r1.rowlpkg"
        packed = run_pack(r1_src, r1_pkg)
        check("R1-pack", packed.returncode == 0, f"pack failed: {packed.stderr!r}")
        r1_flags = parse_index(r1_pkg)["story.txt"][3]
        check("R1-flags1", r1_flags == 1, f"story.txt flags={r1_flags}, want 1")
        # Kaskad-cemberi (bulgu 10): on-kosul kirmiziysa verify sahnesi
        # atlanir — 4x ayni hata yerine tek FAIL.
        if "R1-pack" in FAILURES or "R1-flags1" in FAILURES:
            print("[D18a][SKIP] R1-verify (on-kosul kirmizi, kaskad atlandi)")
        else:
            r1_verify_stage(r1_pkg, "story.txt", "R1")
    else:
        r1_pkg = root / "r1-synth.rowlpkg"
        build_synth_flags1(r1_pkg, "story.txt", b"synth-flags1-payload" * 64)
        r1_flags = parse_index(r1_pkg)["story.txt"][3]
        check("R1-synth-flags1", r1_flags == 1, f"synth flags={r1_flags}, want 1")
        if "R1-synth-flags1" in FAILURES:
            print("[D18a][SKIP] R1-synth-verify (on-kosul kirmizi, kaskad atlandi)")
        else:
            r1_verify_stage(r1_pkg, "story.txt", "R1-synth")

    # --- R2 (kontrol): flags=0 payload flip + taze sidecar ---
    r2_src = root / "r2-src"
    r2_src.mkdir()
    # Uzun sifir kosusu -> oran kapisi (>1024) raw'a dusurur: flags=0 garanti.
    (r2_src / "flat.png").write_bytes(b"\x00" * 262144)
    r2_pkg = root / "r2.rowlpkg"
    packed = run_pack(r2_src, r2_pkg)
    check("R2-pack", packed.returncode == 0, f"pack failed: {packed.stderr!r}")
    r2_flags = parse_index(r2_pkg)["flat.png"][3]
    check("R2-flags0", r2_flags == 0, f"flat.png flags={r2_flags}, want 0")
    flip_payload_byte(r2_pkg, "flat.png", refresh_sidecar=True)
    result = run_verify(r2_pkg)
    combined = result.stdout + result.stderr
    check("R2-exit1", result.returncode == 1,
          f"raw flip yakalanmadi, exit {result.returncode}: {combined!r}")
    check("R2-checksum-tani", "checksum mismatch" in combined,
          f"tani satirinda 'checksum mismatch' yok: {combined!r}")

    # --- S1: stale output sidecar -> converted_from DUSER, pack exit 0 ---
    s1_src = root / "s1-src"
    (s1_src / "audio").mkdir(parents=True)
    s1_out = s1_src / "audio" / "s1.ogg"
    s1_out.write_bytes(b"fake-ogg-bytes-version-one!!")
    s1_sidecar = pathlib.Path(str(s1_out) + ".rowlconv.json")
    s1_sidecar.write_text(json.dumps({
        "source_sha256": "11" * 32,
        "converter_name": "rowl_oggenc",
        "converter_version": "1.0.0",
        "settings": {"quality_q": 4},
        "output_sha256": hashlib.sha256(s1_out.read_bytes()).hexdigest(),
    }), encoding="utf-8")
    # Sidecar yazimindan SONRA cikti degisir -> output_sha256 bayat.
    s1_out.write_bytes(b"fake-ogg-bytes-version-TWO!!")
    s1_pkg = root / "s1.rowlpkg"
    packed = run_pack(s1_src, s1_pkg)
    s1_manifest = read_manifest(s1_pkg)
    s1_record = next(e for e in s1_manifest["files"] if e["path"] == "audio/s1.ogg")
    check("S1-pack-ok", packed.returncode == 0,
          f"fail-soft ihlali: pack exit {packed.returncode}: {packed.stderr!r}")
    check("S1-converted-duser", "converted_from" not in s1_record,
          f"bayat sidecar guvenildi: {s1_record.get('converted_from')!r}")
    check("S1-stale-uyari", "stale-sidecar" in packed.stderr,
          f"stderr uyarisi yok: {packed.stderr!r}")

    # --- S2 (kontrol): fresh sidecar -> converted_from VAR ---
    s2_src = root / "s2-src"
    (s2_src / "audio").mkdir(parents=True)
    s2_out = s2_src / "audio" / "s2.ogg"
    s2_out.write_bytes(b"fake-ogg-bytes-fresh!!")
    pathlib.Path(str(s2_out) + ".rowlconv.json").write_text(json.dumps({
        "source_sha256": "22" * 32,
        "converter_name": "rowl_oggenc",
        "converter_version": "1.0.0",
        "settings": {"quality_q": 4},
        "output_sha256": hashlib.sha256(s2_out.read_bytes()).hexdigest(),
        "source_path": "sfx/theme.mp3",
    }), encoding="utf-8")
    s2_pkg = root / "s2.rowlpkg"
    packed = run_pack(s2_src, s2_pkg)
    s2_manifest = read_manifest(s2_pkg)
    s2_record = next(e for e in s2_manifest["files"] if e["path"] == "audio/s2.ogg")
    check("S2-pack-ok", packed.returncode == 0, f"pack failed: {packed.stderr!r}")
    check("S2-converted-var",
          s2_record.get("converted_from", {}).get("source_sha256") == "22" * 32,
          f"fresh converted_from kayip/bozuk: {s2_record.get('converted_from')!r}")

    # --- S3 (bulgu 3 kilidi): malformed output_sha256 -> DUSER + uyari ---
    # Pre-fix delik: gecersiz deger fail-open guvenilir (converted_from VAR).
    s3_src = root / "s3-src"
    (s3_src / "audio").mkdir(parents=True)
    s3_out = s3_src / "audio" / "s3.ogg"
    s3_out.write_bytes(b"fake-ogg-bytes-malformed-sidecar!!")
    pathlib.Path(str(s3_out) + ".rowlconv.json").write_text(json.dumps({
        "source_sha256": "33" * 32,
        "converter_name": "rowl_oggenc",
        "converter_version": "1.0.0",
        "settings": {"quality_q": 4},
        "output_sha256": "BOZUK-UZUNLUK",
    }), encoding="utf-8")
    s3_pkg = root / "s3.rowlpkg"
    packed = run_pack(s3_src, s3_pkg)
    s3_manifest = read_manifest(s3_pkg)
    s3_record = next(e for e in s3_manifest["files"] if e["path"] == "audio/s3.ogg")
    check("S3-pack-ok", packed.returncode == 0,
          f"fail-soft ihlali: pack exit {packed.returncode}: {packed.stderr!r}")
    check("S3-converted-duser", "converted_from" not in s3_record,
          f"malformed sidecar guvenildi: {s3_record.get('converted_from')!r}")
    check("S3-stale-uyari", "stale-sidecar" in packed.stderr,
          f"stderr uyarisi yok: {packed.stderr!r}")

    # --- R3 (bulgu 6 kilidi): manifestte non-object kayit -> temiz fail ---
    # Pre-fix delik: ham AttributeError traceback (FAIL/JSON sozlesmesi
    # bozulur). Non-dict kayit kume-kontrollerinden ONCE guard'lanir.
    r3_pkg = root / "r3.rowlpkg"
    build_synth_package(r3_pkg, [("evil.bin", b"evil", 0)],
                        {"files": ["BOZUK-KAYIT"], "format": 1})
    result = run_verify(r3_pkg)
    combined = result.stdout + result.stderr
    check("R3-exit1", result.returncode == 1,
          f"bozuk kayit yakalanmadi, exit {result.returncode}: {combined!r}")
    check("R3-tani", "non-object record" in combined,
          f"tani satirinda 'non-object record' yok: {combined!r}")
    check("R3-temiz", "Traceback" not in combined,
          f"ham traceback sizdi: {combined!r}")

print(f"[D18a] {'OK' if not FAILURES else 'FAIL: ' + ','.join(FAILURES)}")
sys.exit(1 if FAILURES else 0)
