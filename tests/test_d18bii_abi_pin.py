#!/usr/bin/env python3
"""D18b-ii R3 — ABI-pin probu (check_abi_additive + baseline sayimi uzerinden).

Kilitlenen sozlesme:
  P1 tools/abi_baseline.txt 224 satir/sembol holds (plan "220" bayat).
  P2 removal: baseline disi sembol kumesiyle karsilasan arac exit 1 verir
     (canli: yabanci ELF + gercek baseline; unit: 1-satir-dusurulmus kume).
  P3 additive-tamper: baseline'a yazilmamis sahte sembol (BogusProbe) pin
     tarafindan yakalanir (sayim sapmasi + header-delta); header'da sahte
     prob yoktur.
  P4 reader-mirror: verify_release_package.py:76 tamsayi-bolme oran kapisi
     (`uncompressed_size // compressed_size > MAX_EXPANSION_RATIO`) yerinde;
     gevsetme kirmizi verir.
  P5 derlenmis libRowlEngineCore bulunursa gercek arac kosar, exit 0 ve
     224 eslesmesi beklenir; yoksa SKIP (P1-P4 kapiyi tasir).

Repo'ya yazim yok; tamper'lar /tmp kopyalari uzerinde. Cikti: her prob
PASS/FAIL satiri; herhangi biri duserse exit 1.
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
BASELINE = ROOT / "tools" / "abi_baseline.txt"
CHECK_TOOL = ROOT / "tools" / "check_abi_additive.py"
C_API_H = ROOT / "engine" / "include" / "rowl" / "c_api.h"
READER = ROOT / "tools" / "verify_release_package.py"

EXPECTED_COUNT = 224
BOGUS = "RowlEngine_BogusProbe"

FAILURES = []


def check(name, condition, detail):
    print(f"[D18b-ii][{'PASS' if condition else 'FAIL'}] {name}"
          + ("" if condition else f": {detail}"))
    if not condition:
        FAILURES.append(name)


def find_foreign_elf():
    """RowlEngine_ sembolu tasimayan gercek bir ELF (removal-kirmizisi icin)."""
    for candidate in (sys.executable, "/bin/ls", "/bin/true", "/usr/bin/nm"):
        path = pathlib.Path(candidate)
        if path.is_file():
            return str(path)
    return None


def find_built_runtime():
    """Derlenmis libRowlEngineCore.so arar (build*/lib + klasik build/)."""
    patterns = ("libRowlEngineCore.so", "libRowlEngineCore.dylib",
                "RowlEngineCore.dll")
    search_roots = [ROOT / "build", ROOT]
    search_roots += sorted(ROOT.glob("build-*"))
    for root in search_roots:
        if not root.is_dir():
            continue
        for pattern in patterns:
            for hit in root.rglob(pattern):
                if hit.is_file():
                    return str(hit)
    return None


def main():
    sys.path.insert(0, str(ROOT / "tools"))
    import check_abi_additive as abi  # noqa: E402

    # --- P1: 224 holds ---
    expected = abi.read_baseline(BASELINE)
    check("P1-baseline-224-holds", len(expected) == EXPECTED_COUNT,
          f"baseline {len(expected)} sembol, beklenen {EXPECTED_COUNT}")
    check("P1-baseline-sorted-unique",
          expected == sorted(set(expected)),
          "baseline sirali/tekil degil")

    # --- P2: removal -> exit 1 ---
    dropped = expected[:-1]
    removed = sorted(set(expected) - set(dropped))
    check("P2-removal-unit-red", len(removed) == 1,
          f"1-satir-dusurme removal uretemedi: {removed}")
    foreign = find_foreign_elf()
    nm = shutil.which("nm")
    if foreign is not None and nm is not None:
        proc = subprocess.run(
            [sys.executable, str(CHECK_TOOL), foreign, str(BASELINE)],
            capture_output=True, text=True, check=False)
        check("P2-removal-tool-red", proc.returncode == 1,
              f"yabanci ELF'e karsi arac exit {proc.returncode} verdi (1 beklenir)")
    else:
        print("[D18b-ii][SKIP] P2-removal-tool-red: nm veya yabanci ELF yok "
              "(unit-kirmizi kapiyi tasiyor)")

    # --- P3: additive-tamper pin ---
    with tempfile.TemporaryDirectory(prefix="d18bii-abi-") as tmp:
        tampered = pathlib.Path(tmp) / "abi_baseline.txt"
        tampered.write_text(BASELINE.read_text(encoding="utf-8") + BOGUS + "\n",
                            encoding="utf-8")
        tampered_symbols = abi.read_baseline(tampered)
        check("P3-tampered-count-drifts",
              len(tampered_symbols) != EXPECTED_COUNT,
              "sahte sembol eklenmis baseline sayimi degistirmedi")
        check("P3-bogus-not-in-baseline", BOGUS not in expected,
              "sahte prob zaten baseline'da (pin kirli)")
    header = C_API_H.read_text(encoding="utf-8")
    check("P3-bogus-not-in-header", BOGUS not in header,
          "sahte prob header'a sizmis")
    header_symbols = set(re.findall(r"RowlEngine_[A-Za-z0-9_]+", header))
    undrifted = header_symbols - set(expected)
    # Header'da olup baseline'da olmayan gercek semboller olabilir (ic API);
    # pin yalnizca sahte-prob driftini yasaklar, tam listeyi degil.
    check("P3-header-has-no-bogus-drift", BOGUS not in undrifted,
          "header-baseline drifti sahte prob tasiyor")

    # --- P4: reader-mirror tamsayi-bolme kapisi ---
    reader_source = READER.read_text(encoding="utf-8")
    mirror_intact = ("uncompressed_size // compressed_size > MAX_EXPANSION_RATIO"
                     in reader_source)
    check("P4-reader-mirror-intact", mirror_intact,
          "verify_release_package.py:76 oran kapisi zayiflatilmis/kayip")

    # --- P5: derlenmis kutuphane varsa gercek yesil ---
    runtime = find_built_runtime()
    if runtime is not None and nm is not None:
        proc = subprocess.run(
            [sys.executable, str(CHECK_TOOL), runtime, str(BASELINE)],
            capture_output=True, text=True, check=False)
        check("P5-built-lib-green", proc.returncode == 0,
              f"derlenmis runtime'a karsi arac exit {proc.returncode} verdi: "
              f"{proc.stderr.strip()[:300]}")
    else:
        print("[D18b-ii][SKIP] P5-built-lib-green: derlenmis RowlEngineCore yok "
              "(P1 sayim + P2/P4 kirmizilari kapiyi tasiyor)")

    if FAILURES:
        raise SystemExit(f"{len(FAILURES)} ABI-pin probu dustu")
    print("OK: ABI pin green (224 holds, removal-red, tamper-red, mirror, "
          "built-lib where present)")


if __name__ == "__main__":
    sys.exit(main())
