#!/usr/bin/env python3
"""D18b-i — i18n docs-sync probu (python-degisikligi -> python-prob).

Kod gercegi (salt-okunur capalar, pre/post YESIL beklenir):
  C1 serma 1|2: engine/src/i18n/localization_manager.cpp loadCatalog
     `schema_version != 1 && ... != 2` -> yalnizca 1|2 kabul.
  C2 zincir-bilincli eslesme: matchSupported fallbackChain(code) uzerinde
     yurur (localization_manager.cpp:286-298).
  C3 etiket-butunlugu: trimLower kucultur (`_`->`-`), region/script
     VALIDATED, never stripped (isWellFormedTag ustu yorum); "tr-TR" -> "tr-tr"
     ayri etiket olarak yasar, "tr"ye kirpilmaz (test_locale_cluster T2).

Dokuman iddialari (pre-fix KIRMIZI, post-fix YESIL beklenir):
  R1 PRODUCTIZATION_BASELINE.md Localization satiri `Missing` diyor; kod
     T1-T7 (test_locale_cluster.cpp) + test_localization.cpp kilitleriyle
     Ready/Partial olmali -> pre-fix FAIL.
  R2 LOCALIZATION_CONTRACT.md S2 `primary subtag`e kirpma iddia ediyor
     (`"tr-TR"` -> `"tr"`); kod zincir-bilincli cozer, kirpmaz -> pre-fix FAIL.
  R3 Kontrakt S3 yalniz `schema_version: 1` diyor; kod 1|2 kabul (#94, T4)
     -> pre-fix FAIL.

Cikti: her prob PASS/FAIL satiri; herhangi biri duserse exit 1.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CPP = ROOT / "engine" / "src" / "i18n" / "localization_manager.cpp"
BASELINE = ROOT / "docs" / "PRODUCTIZATION_BASELINE.md"
CONTRACT = ROOT / "docs" / "LOCALIZATION_CONTRACT.md"

FAILURES = []


def check(name, condition, detail):
    print(f"[D18b-i][{'PASS' if condition else 'FAIL'}] {name}"
          + ("" if condition else f": {detail}"))
    if not condition:
        FAILURES.append(name)


def section(text, begin_marker, end_marker):
    begin = text.find(begin_marker)
    if begin < 0:
        return ""
    end = text.find(end_marker, begin + len(begin_marker))
    return text[begin:end if end >= 0 else len(text)]


cpp = CPP.read_text(encoding="utf-8")
baseline = BASELINE.read_text(encoding="utf-8")
contract = CONTRACT.read_text(encoding="utf-8")

# --- C1: kod sema 1|2 kabul ediyor ---
check("C1-schema-1-ve-2",
      "schema_version" in cpp
      and "get<int>() != 1" in cpp and "get<int>() != 2" in cpp,
      "loadCatalog 1|2 kabul capasi bulunamadi")

# --- C2: matchSupported zincir-bilincli ---
match_region = cpp[cpp.find("matchSupported"):cpp.find("matchSupported") + 600] \
    if "matchSupported" in cpp else ""
check("C2-zincir-bilincli-match",
      "fallbackChain(code)" in match_region,
      "matchSupported fallbackChain(code) yurumuyor")

# --- C3: kirpma yok, dogrulama var ---
check("C3-kirpma-yok",
      "never stripped" in cpp and "fallbackChain" in cpp,
      "etiket-butunluk capasi (never stripped + fallbackChain) yok")

# --- R1: BASELINE Localization satiri Missing olmamali ---
loc_lines = [line for line in baseline.splitlines()
             if line.startswith("| Localization")]
check("R1-baseline-satir-var", len(loc_lines) == 1,
      f"Localization satiri bulunamadi/coklandi: {len(loc_lines)}")
if loc_lines:
    row = loc_lines[0]
    check("R1-baseline-missing-degil", "| Missing |" not in row,
          f"BASELINE hala Localization=Missing diyor: {row!r}")
    check("R1-baseline-ready-partial",
          re.search(r"\|\s*(Ready|Partial)\s*\|", row) is not None,
          f"Localization Ready/Partial degil: {row!r}")
    check("R1-baseline-kanit",
          ("locale_cluster" in row or "test_localization" in row
           or "T1" in row),
          f"satir gercek kapiya (T1-T7/cluster kilitleri) baglanmiyor: {row!r}")

# --- R2: S2 kirpma iddiasi tasimamali, zinciri anlatmali ---
sec2 = section(contract, "## 2.", "## 3.")
check("R2-S2-bolumu-var", bool(sec2), "S2 bolumu bulunamadi")
if sec2:
    check("R2-kirpma-iddiasi-yok", "primary subtag" not in sec2,
          'S2 hala kirpma iddia ediyor (primary subtag: "tr-TR" -> "tr")')
    check("R2-zincir-anlatiliyor",
          ("chain" in sec2 and "matchSupported" in sec2),
          "S2 zincir-bilincli cozumu (chain + matchSupported) anlatmiyor")

# --- R3: S3 sema 1|2 demeli ---
sec3 = section(contract, "## 3.", "## 4.")
check("R3-S3-bolumu-var", bool(sec3), "S3 bolumu bulunamadi")
if sec3:
    check("R3-sema-1-ve-2",
          ("1|2" in sec3 or "1 or 2" in sec3 or "1 and 2" in sec3),
          "S3 sema 1|2 kabulunu belgelemiyor (yalniz 1 diyor)")

print(f"[D18b-i] {'OK' if not FAILURES else 'FAIL: ' + ','.join(FAILURES)}")
sys.exit(1 if FAILURES else 0)
