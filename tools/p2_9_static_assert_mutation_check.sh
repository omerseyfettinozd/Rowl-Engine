#!/usr/bin/env bash
# P2-9 DENETIMI — BULGU 1 & 2 mutasyon kaniti.
#
# Denetim, eski static_assert'in TOTOLOJI oldugunu gosterdi:
#     static_assert(kTransientSiteCount == (int)SaveDurabilityTransientSite::Count)
# sol zaten sagdan turuyor, derleyici katlayip siliyor. Count 6->5 mutasyonu
# DERLENIP kapiyi YESIL birakti. Projede -Werror YOK (CI'da da yok), yani
# tek gercek koruma static_assert'tir.
#
# Bu betik o mutasyonlari tek tek uygulayip DERLEME ZAMANINDA yakalandigini
# kanitlar. -Werror YOK; bir mutasyon yesil gecerse betik exit 1 verir.
#
# Kullanim: tools/p2_9_static_assert_mutation_check.sh
# Ortam: ROWL_BUILD (compile_commands.json olan bir build dizini).
set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROWL_BUILD:-/home/chaple/rowl-build/p2-9-rel}"
HPP="$REPO_ROOT/engine/include/rowl/state/save_durability.hpp"
CPP="$REPO_ROOT/engine/src/state/save_durability.cpp"

WORK="$(mktemp -d)"
trap 'cp "$WORK/HPP.pristine" "$HPP" 2>/dev/null; cp "$WORK/CPP.pristine" "$CPP" 2>/dev/null; rm -rf "$WORK"' EXIT

cp "$HPP" "$WORK/HPP.pristine"
cp "$CPP" "$WORK/CPP.pristine"

restore() {
    cp "$WORK/HPP.pristine" "$HPP"
    cp "$WORK/CPP.pristine" "$CPP"
}

# Derleme komutunu compile_commands.json'dan al, yolun KENDI worktree'mize
# cevir. -Werror YOK: yakalama yalnizca static_assert sayesinde olmalidir.
COMPILE_CMD="$(python3 - "$BUILD_DIR/compile_commands.json" "$REPO_ROOT" <<'PY'
import json, shlex, sys
cc_path, repo = sys.argv[1], sys.argv[2]
for e in json.load(open(cc_path)):
    if e["file"].endswith("src/state/save_durability.cpp"):
        parts = shlex.split(e["command"])
        out = []
        skip = False
        for i, p in enumerate(parts):
            if skip:
                skip = False
                continue
            if p == "-o":
                skip = True
                continue
            if p == "-c":
                continue
            out.append(p.replace(repo + "/engine/", repo + "/engine/"))
        # Kaynak yolunu ve include yollarini bizim worktree'mize cevir.
        fixed = []
        for p in out:
            p = p.replace("/home/chaple/rowl-fix/p2-9-probe-counter/", repo + "/")
            fixed.append(p)
        print(shlex.join(fixed + ["-fsyntax-only"]))
        break
PY
)"

if [ -z "$COMPILE_CMD" ]; then
    echo "HATA: compile_commands.json icinde save_durability.cpp bulunamadi ($BUILD_DIR)"
    exit 2
fi

pass=0; fail=0
# try_mutation <etiket> <dosya> [sed ifadesi ...]
# Her ifade ayri bir sed komutudur; birden fazla satir degistiren
# mutasyonlar icin kullanilir.
try_mutation() {
    local label="$1" file="$2"; shift 2
    local out rc
    local expr
    for expr in "$@"; do
        sed -i "$expr" "$file"
    done
    out="$(eval "$COMPILE_CMD" 2>&1)"
    rc=$?
    restore
    if printf '%s' "$out" | grep -q "static assertion failed"; then
        echo "  RED  (static_assert)  $label"
        pass=$((pass+1)); return 0
    fi
    if [ "$rc" -eq 0 ]; then
        echo "  YESIL (derlendi!)      $label   <<< KAPI YALAN SOYLUYOR"
        fail=$((fail+1)); return 1
    fi
    echo "  RED  (derleme hatasi)  $label"
    pass=$((pass+1)); return 0
}

echo "P2-9 BULGU 1 & 2 MUTASYON KANITI"
echo "  kaynak : $REPO_ROOT"
echo "  bayrak : -Wall -Wextra -Wpedantic  (-Werror YOK, projede de yok)"
echo

echo "M0  kontrol: mutasyonsuz temel derleme"
if eval "$COMPILE_CMD" >/dev/null 2>&1; then
    echo "  YESIL (derlendi)  temel durum saglam"
else
    echo "  HATA  temel derleme zaten kirik; mutasyonlarin anlami yok"
    exit 2
fi
echo

echo "BULGU 1 — enum zinciri mutasyonlari"
# M1: Count artik turetilmis oldugu icin elle kisilamaz; mutasyonun KAYNAGI
# yoktur. Bu yuzden M1, turetilmeyi BOGMAK yerine "Count'i baska bir ifadeye
# bagla" olarak denenir (insanin yapabilecegi tek sey).
try_mutation "M1 Count turetilmesi bozuldu, elle 5 yazildi" "$HPP" \
    's/^\( *\)Count,.*/\1Count = 5,/'
try_mutation "M2 FingerprintMeasure 5->6, son etiket Count ile eslesmez" "$HPP" \
    's/FingerprintMeasure = 5,/FingerprintMeasure = 6,/'
try_mutation "M3 Probe 0->1, enum sifirdan baslamaz" "$HPP" \
    's/^\( *\)Probe = 0,/\1Probe = 1,/'
try_mutation "M4 yeni site eklendi, kapi listesi guncellenmedi" "$HPP" \
    's/^\( *\)FingerprintMeasure = 5,/\1FingerprintMeasure = 5,\n    YeniSite = 5,\n    Last = 6,/'
try_mutation "M5 dizi uzunlugu elle [4] yapildi" "$CPP" \
    's/g_injectTransientFailures\[kTransientSiteCount\]/g_injectTransientFailures[4]/'
echo

echo "BULGU 2 — kapi kapsami mutasyonu"
# 7. site eklenir; Count turetilmeye devam eder ama liste guncellenmez.
try_mutation "M6 7. site eklendi, kapi kapsam listesi guncellenmedi" "$HPP" \
    's/^\( *\)FingerprintMeasure = 5,/\1FingerprintMeasure = 5,\n    YeniSite = 6,\n    Last = 7,/' \
    's/^\( *\)Count,.*/\1Count = Last + 1,/'

echo
echo "------------------------------------------------------------"
echo "yakalandi: $pass / $((pass+fail))"
if [ "$fail" -ne 0 ]; then
    echo "SONUC: KIRMIZI — $fail mutasyon DERLENIP GECTI"
    exit 1
fi
echo "SONUC: YESIL — tum mutasyonlar derleme zamaninda yakalandi (-Werror gerekmedi)"
exit 0
