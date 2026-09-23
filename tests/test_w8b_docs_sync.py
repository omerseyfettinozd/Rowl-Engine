#!/usr/bin/env python3
"""W8-b — localization docs-sync probu (d18bi deseni: kod capasi + belge kilidi).

Kod gercegi (salt-okunur capalar, pre/post YESIL beklenir):
  W1 pseudo-locale + translation desk + CSV/JSON exchange sevk edildi
     (b382219: PseudoLocaleGenerator.cs, TranslationExchangeService.cs,
     LocalizationDeskCoordinator.cs, LOCALIZATION_EDITOR_CONTRACT.md).
  W2 native cogul-tablo kabul eder: parseEntryField zorunlu "other"
     (localization_manager.cpp, T5 test_locale_cluster.cpp:340-400).
  W3 isWellFormedTag (localization_manager.cpp:55-71: dil 2-3 alfa, en
     fazla 4 alt-etiket, toplam 32) + kapaklar yalniz native bootstrap
     (c_api_i18n.cpp: 16 MiB katalog / 1 MiB manifest); managed
     ValidateCatalog kapaksiz.
  W4 SetLocale CatalogMissing -> FILE_NOT_FOUND (c_api_i18n.cpp:147-157,
     T3 test_locale_cluster.cpp:235-281).
  W5 MainWindowViewModel.OpenLocalizationDesk mevcut (Dilim 4, :2473-2475).

Dokuman iddialari (pre-fix KIRMIZI, post-fix YESIL beklenir):
  W1 S8 sevk-gercegini yansitmali (editor kontrati referansi).
  W2 S3 native cogul-istisnasi + S5 managed-string sapmasi.
  W3 S6 etiket kurali + native-bootstrap kapak kapsami + managed kapaksiz.
  W4 S4 FILE_NOT_FOUND satiri + normalize yonlendirmesi (S2/S5).
  W5 S5 untouched iddiasi tarihsel cumleye donmeli + OpenLocalizationDesk.
  W6 S2 uc madde taraf kapsami (ParseManifestLocales adi gecer).
  W7 S3 mount cumlesi native-bootstrap kapsami.
  W8 BASELINE Localization hucresi native-kapsam + managed-sapma notu.

KILIT: bu prob S2'ye "primary subtag" dizesi eklemeyi yasaklar (d18bi
R2 S2'de o dizeyi gorurse kirmiziya doner); S3'te "1|2" kalmalidir.

Cikti: her prob PASS/FAIL satiri; herhangi biri duserse exit 1.
"""

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CPP = ROOT / "engine" / "src" / "i18n" / "localization_manager.cpp"
CAPI = ROOT / "engine" / "src" / "c_api_i18n.cpp"
CLUSTER = ROOT / "tests" / "test_locale_cluster.cpp"
VM = ROOT / "editor" / "ViewModels" / "MainWindowViewModel.cs"
BASELINE = ROOT / "docs" / "PRODUCTIZATION_BASELINE.md"
CONTRACT = ROOT / "docs" / "LOCALIZATION_CONTRACT.md"
ED_CONTRACT = ROOT / "docs" / "LOCALIZATION_EDITOR_CONTRACT.md"
ED = ROOT / "editor" / "Services" / "Localization"

FAILURES = []


def check(name, condition, detail):
    print(f"[W8b][{'PASS' if condition else 'FAIL'}] {name}"
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
capi = CAPI.read_text(encoding="utf-8")
cluster = CLUSTER.read_text(encoding="utf-8")
vm = VM.read_text(encoding="utf-8")
baseline = BASELINE.read_text(encoding="utf-8")
contract = CONTRACT.read_text(encoding="utf-8")

sec2 = section(contract, "## 2.", "## 3.")
sec3 = section(contract, "## 3.", "## 4.")
sec4 = section(contract, "## 4.", "## 5.")
sec5 = section(contract, "## 5.", "## 6.")
sec6 = section(contract, "## 6.", "## 7.")
sec8 = section(contract, "## 8.", "END-OF-FILE-NEVER-MATCHES-zzz")

# --- W1: sevk capasi + S8 sevk-gercegi ---
check("W1-code-desk-sevk",
      ED_CONTRACT.is_file()
      and (ED / "PseudoLocaleGenerator.cs").is_file()
      and (ED / "TranslationExchangeService.cs").is_file()
      and (ED / "LocalizationDeskCoordinator.cs").is_file(),
      "b382219 sevk dosyalari (desk/pseudo/exchange/editor-kontrat) eksik")
check("W1-S8-editor-kontrat-ref", "LOCALIZATION_EDITOR_CONTRACT" in sec8,
      "S8 hala desk/pseudo/exchange'i sevk-edilmemis gibi listeliyor")
check("W1-S8-sevk-cumlesi",
      "ipped" in sec8,
      "S8'de Dilim 4 sevk cumlesi yok")

# --- W2: cogul capasi + S3/S5 ---
check("W2-code-plural-anchor",
      "parseEntryField" in cpp and '"other"' in cpp
      and "pluralCategory" in cluster,
      "native cogul-tablo capasi (parseEntryField+other+T5) yok")
check("W2-S3-plural-istisna", "plural" in sec3,
      "S3 native cogul-tablo istisnasini belgelemiyor")
check("W2-S3-managed-sapma-ref",
      ("§5" in sec3 or "see §5" in sec3 or "managed" in sec3),
      "S3 managed-string sapmasina (S5) baglanmiyor")

# --- W3: etiket+kapak capasi + S6 ---
check("W3-code-tag-anchor", "isWellFormedTag" in cpp,
      "isWellFormedTag capasi yok")
check("W3-code-caps-anchor",
      "16 * 1024 * 1024" in capi and "1024 * 1024" in capi,
      "native bootstrap kapak capasi (16 MiB/1 MiB) yok")
check("W3-S6-tag-rule", "isWellFormedTag" in sec6,
      "S6 etiket kuralini (isWellFormedTag) belgelemiyor")
check("W3-S6-native-bootstrap", "bootstrap" in sec6,
      "S6 kapaklarin native-bootstrap kapsamina baglanmiyor")
check("W3-S6-managed-capless",
      ("no size cap" in sec6 or "kapaksız" in sec6 or "capless" in sec6),
      "S6 managed ValidateCatalog kapaksizligini belgelemiyor")
check("W3-S6-yanlis-ozet-gitti",
      "1–32 ASCII alphanumerics" not in sec6,
      "S6 hala yanlis ozeti tasiyor (1-32 ASCII alphanumerics)")

# --- W4: FILE_NOT_FOUND capasi + S4 ---
check("W4-code-file-not-found",
      "FILE_NOT_FOUND" in capi and "CatalogMissing" in capi
      and "FILE_NOT_FOUND" in cluster,
      "SetLocale FILE_NOT_FOUND capasi (capi+T3) yok")
check("W4-S4-file-not-found", "FILE_NOT_FOUND" in sec4,
      "S4 SetLocale FILE_NOT_FOUND satirini belgelemiyor")
check("W4-S4-normalize-yonlendirme",
      ("§2" in sec4 and "§5" in sec4),
      "S4 Tags-normalize satiri S2/S5'e yonlendirmiyor")

# --- W5: desk capasi + S5 tarihsel cumle ---
check("W5-code-desk", "OpenLocalizationDesk" in vm,
      "MainWindowViewModel.OpenLocalizationDesk yok")
check("W5-S5-untouched-gitti", "are untouched" not in sec5,
      "S5 hala MainWindowViewModel untouched iddia ediyor")
check("W5-S5-desk-doc", "OpenLocalizationDesk" in sec5,
      "S5 OpenLocalizationDesk delegasyonunu belgelemiyor")

# --- W6: S2 taraf kapsami ---
check("W6-S2-managed-adi", "ParseManifestLocales" in sec2,
      "S2 uc maddeye taraf kapsami tasimiyor (managed adi yok)")

# --- W7: S3 mount native kapsami ---
check("W7-S3-mount-bootstrap", "bootstrap" in sec3,
      "S3 mount cumlesi native-bootstrap kapsamini tasimiyor")

# --- W8: BASELINE hucresi ---
loc_lines = [line for line in baseline.splitlines()
             if line.startswith("| Localization")]
check("W8-baseline-satir-var", len(loc_lines) == 1,
      f"Localization satiri bulunamadi/coklandi: {len(loc_lines)}")
if loc_lines:
    row = loc_lines[0]
    check("W8-native-kapsam", "native" in row,
          "hucre native-kapsam tasimiyor")
    check("W8-managed-sapma",
          ("managed" in row and "§5" in row),
          "hucre managed-sapma notu (S5) tasimiyor")

print(f"[W8b] {'OK' if not FAILURES else 'FAIL: ' + ','.join(FAILURES)}")
sys.exit(1 if FAILURES else 0)
