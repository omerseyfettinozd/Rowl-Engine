#!/usr/bin/env python3
"""D18b-ii R2 — packaging-guard probu (export_game + package_assets verify uzerinden).

Kilitlenen sozlesme:
  P1 export_android APK/AAB uretmez; "APK produced"/"AAB produced" gibi
     overstatement iceren her satir fail-closed olmalidir ("not"/"no" sarti).
  P2 export_ios IPA uretmez; "IPA produced" overstatement'i ayni sekilde yasaktir.
  P3 taze pack + sidecar -> verify_package exit 0 (yesil yol).
  P4 yuk bozulmasi (flags=1'de compressed_sha256, yoksa ham sha256) + sidecar
     duzeltmesi -> verify_package exit 1 (gomulu manifest hash'i yakalar).
  P5 sidecar silinmesi -> verify_package exit 1 (missing-sidecar fail-closed).

Fixture'lar /tmp altinda kurulur; repo'ya yazim yok. Cikti: her prob
PASS/FAIL satiri; herhangi biri duserse exit 1.
"""

import hashlib
import os
import pathlib
import struct
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPORT_GAME = ROOT / "tools" / "export_game.py"
TOOLS_DIR = ROOT / "tools"

HEADER = struct.Struct("<4sHIQ")
ENTRY = struct.Struct("<QIQQQI")

FAILURES = []


def check(name, condition, detail):
    print(f"[D18b-ii][{'PASS' if condition else 'FAIL'}] {name}"
          + ("" if condition else f": {detail}"))
    if not condition:
        FAILURES.append(name)


def overstatement_lines(source, keywords):
    """'uretim' iddia eden ama fail-closed ('not'/'no') tasimayan satirlar."""
    bad = []
    for number, line in enumerate(source.splitlines(), start=1):
        lowered = line.lower()
        if any(k.lower() in lowered for k in keywords) and "produc" in lowered:
            if "not" not in lowered and "no " not in lowered and "non-" not in lowered:
                bad.append((number, line.strip()))
    return bad


def parse_entries(package_path):
    with open(package_path, "rb") as f:
        blob = f.read()
    magic, version, count, index_offset = HEADER.unpack_from(blob, 0)
    assert magic == b"ROWL" and version == 1
    pos = index_offset
    entries = []
    for _ in range(count):
        _, path_len, offset, csize, usize, flags = ENTRY.unpack_from(blob, pos)
        pos += ENTRY.size
        path = blob[pos:pos + path_len].decode("utf-8")
        pos += path_len
        entries.append({"path": path, "offset": offset, "csize": csize,
                        "usize": usize, "flags": flags})
    return blob, entries


def main():
    # Repo kaynagina yazim yok, sadece import.
    sys.path.insert(0, str(TOOLS_DIR))
    import package_assets  # noqa: E402

    # --- P1/P2: overstatement guard (statik, fail-closed mesaj kilidi) ---
    export_source = EXPORT_GAME.read_text(encoding="utf-8")
    bad_android = overstatement_lines(export_source, ("APK", "AAB"))
    check("P1-android-no-overstatement", not bad_android,
          f"overstatement satirlari: {bad_android}")
    check("P1-android-fail-closed-msg",
          "APK/AAB was not produced" in export_source
          or "APK/AAB uretmez" in export_source,
          "export_android fail-closed mesaji bulunamadi")
    bad_ios = overstatement_lines(export_source, ("IPA",))
    check("P2-ios-no-overstatement", not bad_ios,
          f"overstatement satirlari: {bad_ios}")
    check("P2-ios-fail-closed-msg",
          "IPA was not produced" in export_source
          or "IPA uretilmez" in export_source,
          "export_ios fail-closed mesaji bulunamadi")

    # --- P3: yesil yol (taze pack + sidecar -> exit 0) ---
    with tempfile.TemporaryDirectory(prefix="d18bii-pkg-") as tmp:
        src = pathlib.Path(tmp) / "assets"
        (src / "json").mkdir(parents=True)
        (src / "data").mkdir(parents=True)
        (src / "json" / "story.json").write_text(
            '{"format": 1, "chapters": ["rowl"]}\n', encoding="utf-8")
        # Orta-sikistirilabilir govde: zstd ile flags=1 uretir, reader oran
        # kapisini (>1024) asla tetiklemez. Tamamen redundant govde raw'a
        # duserdi (pack-time guard), o yuzden cumle-karisik govde kullanilir.
        sentences = ("The quick brown fox jumps over the lazy dog. ",
                     "Rowl engine asset pipeline determinism probe. ",
                     "Packaging guard verifies the embedded manifest hash. ")
        blob_text = "".join(sentences[i % 3] + f"[{i}] " for i in range(4000))
        (src / "data" / "lorem.txt").write_text(blob_text, encoding="utf-8")
        pkg = pathlib.Path(tmp) / "game.rowlpkg"
        try:
            package_assets.pack_directory(str(src), str(pkg))
        except Exception as error:  # noqa: BLE001
            check("P3-fresh-pack-verifies", False, f"pack_directory patladi: {error}")
            raise SystemExit(1)
        check("P3-sidecar-written",
              pathlib.Path(str(pkg) + ".sha256").is_file(),
              "pack sidecar dosyasi uretilmedi")
        check("P3-fresh-pack-verifies",
              package_assets.verify_package(str(pkg)) == 0,
              "taze paket verify exit 0 vermedi")

        # --- P4: yuk-bozma (sidecar duzeltilmisken gomulu hash yakalamali) ---
        raw, entries = parse_entries(str(pkg))
        compressed = [e for e in entries if e["flags"] == 1
                      and e["path"] != "rowl/manifest.json"]
        if compressed:
            victim = compressed[0]
            kind = "compressed_sha256"
        else:
            cands = [e for e in entries if e["path"] != "rowl/manifest.json"]
            victim = cands[0]
            kind = "sha256-raw-fallback"
        print(f"[D18b-ii][INFO] P4 victim: {victim['path']} "
              f"(flags={victim['flags']}, {kind})")
        tampered = bytearray(raw)
        tampered[victim["offset"]] ^= 0x01  # 1 bit flip
        with open(pkg, "wb") as f:
            f.write(bytes(tampered))
        digest = hashlib.sha256(bytes(tampered)).hexdigest()
        with open(str(pkg) + ".sha256", "w", encoding="utf-8", newline="\n") as f:
            f.write(f"{digest}  {pkg.name}\n")
        check("P4-payload-tamper-fails",
              package_assets.verify_package(str(pkg)) == 1,
              f"{kind} bozulmasi yakalanamadi (exit 0 dondu)")

    # --- P5: missing-sidecar fail-closed (ayri taze paket) ---
    with tempfile.TemporaryDirectory(prefix="d18bii-noside-") as tmp:
        src = pathlib.Path(tmp) / "assets"
        src.mkdir(parents=True)
        (src / "story.json").write_text('{"format": 1}\n', encoding="utf-8")
        pkg = pathlib.Path(tmp) / "game.rowlpkg"
        package_assets.pack_directory(str(src), str(pkg))
        os.unlink(str(pkg) + ".sha256")
        check("P5-missing-sidecar-fails",
              package_assets.verify_package(str(pkg)) == 1,
              "sidecar'siz paket verify'dan gecti (fail-closed ihlali)")

    if FAILURES:
        raise SystemExit(f"{len(FAILURES)} packaging-guard probu dustu")
    print("OK: packaging guard green (no-overstatement, fresh-verify, "
          "tamper-red, missing-sidecar-red)")


if __name__ == "__main__":
    sys.exit(main())
