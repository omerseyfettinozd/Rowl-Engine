#!/usr/bin/env python3
"""Butunluk kapisi: THIRD_PARTY_LICENSES.md envanter dogrulugu (Faz 6 Dilim 9).

Kontroller:
  (a) envanterdeki her dosya-kaniti mevcuttur (dosya:satir mevcut olmali),
  (b) her gomulu kutuphanenin lisans basligi dosyada durur (anahtar cumle),
  (c) FetchContent surumu ile tablodaki surum eslesir,
  (d) NUGET-LISANS-BAK isaretli satirlar ATLANIR ama SAYILIR (sessiz gecme yok).

Uretim kodu degistirilmez; yalnizca okunur.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
INVENTORY = ROOT / "THIRD_PARTY_LICENSES.md"
CMAKELISTS = ROOT / "engine" / "CMakeLists.txt"
CSPROJ = ROOT / "editor" / "RowlEngine.Editor.csproj"

failures: list[str] = []
skipped_nuget = 0


def check(condition: bool, message: str) -> None:
    if not condition:
        failures.append(message)


# (a) dosya-kanitlari: (envanter adi, repo-ici dosya, beklenen minimum satir)
FILE_EVIDENCE = [
    ("stb_image", "engine/include/thirdparty/stb_image.h", 7952),
    ("stb_truetype", "engine/include/thirdparty/stb_truetype.h", 5043),
    ("nlohmann-cmake", "engine/CMakeLists.txt", 59),
    ("rowl_sha256", "tools/rowl_sha256.h", 1),
    ("csproj", "editor/RowlEngine.Editor.csproj", 1),
    ("ffmpeg-sozlesme", "docs/MEDIA_CONVERTERS_CONTRACT.md", 25),
    ("ffmpeg-kod", "editor/Services/MediaConverterService.cs", 25),
]

for name, rel, min_line in FILE_EVIDENCE:
    path = ROOT / rel
    check(path.is_file(), f"(a) kanit dosyasi yok: {rel} ({name})")
    if path.is_file():
        line_count = sum(1 for _ in path.open(encoding="utf-8", errors="replace"))
        check(
            line_count >= min_line,
            f"(a) kanit dosyasi cok kisa ({line_count} satir): {rel} ({name})",
        )

# (b) lisans basligi anahtar cumleleri: (dosya, beklenen cumle, ilk-satir, son-satir)
# Cumle yalnizca verilen satir bandinda aranir (dosyanin rastgele bir
# yerinde gecmesi yetmez).
LICENSE_PHRASES = [
    ("engine/include/thirdparty/stb_image.h", "stb_image - v2.30", 1, 5),
    ("engine/include/thirdparty/stb_image.h", "ALTERNATIVE A - MIT License", 7950, 7960),
    ("engine/include/thirdparty/stb_image.h", "ALTERNATIVE B - Public Domain", 7968, 7975),
    ("engine/include/thirdparty/stb_truetype.h", "stb_truetype.h - v1.26", 1, 5),
    ("engine/include/thirdparty/stb_truetype.h", "ALTERNATIVE A - MIT License", 5040, 5050),
    ("engine/include/thirdparty/stb_truetype.h", "released into the public domain", 5060, 5065),
    ("tools/rowl_sha256.h", "public-domain SHA-256", 1, 10),
    ("editor/Services/MediaConverterService.cs", "link YOK", 28, 35),
    ("docs/MEDIA_CONVERTERS_CONTRACT.md", "external-process call only", 27, 34),
]

for rel, phrase, lo, hi in LICENSE_PHRASES:
    path = ROOT / rel
    if path.is_file():
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        band = "\n".join(lines[lo - 1 : hi])
        check(phrase in band, f"(b) lisans cumlesi {lo}-{hi} bandinda yok: '{phrase}' <- {rel}")
    else:
        check(False, f"(b) dosya yok, cumle aranamadi: {rel}")

# (c) FetchContent surumu <-> tablo surumu eslesmesi
cmake_text = CMAKELISTS.read_text(encoding="utf-8", errors="replace")
match = re.search(r"GIT_TAG\s+v(\d+\.\d+\.\d+)", cmake_text)
check(match is not None, "(c) engine/CMakeLists.txt icinde GIT_TAG bulunamadi")
inventory_text = INVENTORY.read_text(encoding="utf-8", errors="replace")
table_rows = [ln for ln in inventory_text.splitlines() if ln.startswith("| ") and "---" not in ln and "Kütüphane" not in ln]
if match:
    cmake_version = match.group(1)
    nlohmann_rows = [ln for ln in table_rows if "nlohmann" in ln]
    check(len(nlohmann_rows) == 1, "(c) envanterde nlohmann satiri yok/tekil degil")
    if len(nlohmann_rows) == 1:
        cells = [c.strip() for c in nlohmann_rows[0].split("|")]
        table_version = cells[2] if len(cells) > 2 else ""
        check(
            table_version == f"v{cmake_version}",
            f"(c) surum uyusmazligi: CMake GIT_TAG v{cmake_version} != tablo '{table_version}'",
        )

# nlohmann FetchContent blogu (temiz klonda da mevcuttur; build-artifaktina
# bakilmaz — surum ve URL dogrudan kaynak agacindan dogrulanir).
# Not: dosyada iki Declare vardir (cevrimdisi SOURCE_DIR + ag GIT_TAG);
# ag blogu (GIT_REPOSITORY iceren) aranir.
fetch_blocks = re.findall(
    r"FetchContent_Declare\(\s*nlohmann_json(.*?)\)", cmake_text, re.S
)
check(len(fetch_blocks) > 0, "(c) nlohmann FetchContent blogu bulunamadi")
net_blocks = [b for b in fetch_blocks if "GIT_REPOSITORY" in b]
check(len(net_blocks) > 0, "(c) FetchContent ag blogu (GIT_REPOSITORY) yok")
if net_blocks:
    block = net_blocks[0]
    check("GIT_TAG" in block, "(c) FetchContent blogunda GIT_TAG yok")
    check(
        "https://github.com/nlohmann/json.git" in block,
        "(c) FetchContent blogunda kaynak URL yok",
    )

# csproj NuGet paketleri envanterde geciyor mu
csproj_text = CSPROJ.read_text(encoding="utf-8", errors="replace")
nuget_packages = re.findall(r'PackageReference Include="([^"]+)" Version="([^"]+)"', csproj_text)
check(len(nuget_packages) > 0, "(a) csproj icinde hic PackageReference yok")
for pkg_name, pkg_version in nuget_packages:
    pkg_lines = [ln for ln in inventory_text.splitlines() if pkg_name in ln]
    if any("NUGET-LISANS-BAK" in ln for ln in pkg_lines):
        skipped_nuget += 1
        continue
    check(
        pkg_name in inventory_text,
        f"(a) NuGet paketi envanterde yok: {pkg_name}",
    )
    check(
        pkg_version in inventory_text,
        f"(c) NuGet surumu envanterde yok: {pkg_name} {pkg_version}",
    )

# (d) NUGET-LISANS-BAK isaretleri: atlanir ama SAYILIR
# (Yalnizca tablo satirlari sayilir; aciklama metnindeki gecisler degil.)
bak_marks = sum(1 for ln in table_rows if "NUGET-LISANS-BAK" in ln)
for line in table_rows:
    if "NUGET-LISANS-BAK" in line:
        skipped_nuget += 1
print(f"[third-party] NUGET-LISANS-BAK isaretli satir (atlandi, sayildi): {bak_marks} tabloda, {skipped_nuget} toplam atlama")

print(f"[third-party] envanter veri satiri: {len(table_rows)}")
check(len(table_rows) >= 20, f"(a) envanter satir sayisi cok dusuk: {len(table_rows)}")

if failures:
    print(f"[third-party] BASARISIZ ({len(failures)}):")
    for failure in failures:
        print(f"  - {failure}")
    sys.exit(1)
print("[third-party] YESIL: tum lisans kanitlari dogrulandi")
