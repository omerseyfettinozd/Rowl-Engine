#!/usr/bin/env bash
# check_portability.sh — taşınabilirlik tripwire'ı (T0).
# İki kural; ikisi de bugün yeşil, ikisi de bilerek-boz ile kanıtlı:
#  1. `__has_feature` tek-satırlık `#if`/`#elif` içinde kullanılamaz
#     (Clang-only ifade; eski GCC/MSVC "missing binary operator" ile
#     patlar). İç-içe guard zorunlu:
#       #if defined(__has_feature)
#       #if __has_feature(x)
#  2. tests/ içindeki POSIX-only include'lar guardsız dosyada duramaz
#     (`unistd.h`, `sys/*`, `pthread.h`, `dlfcn.h`, `execinfo.h`):
#     dosya en az bir platform `#if`i içermeli (kaba tripwire, ispat değil).
set -u

fail=0
repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root" || exit 1

# Kural 1: guardsız `__has_feature()` çağrısı. Üçüncü-parti `build/`
# çıktıları taranmaz. Her çağrının üstündeki 4 satırdan birinde
# `defined(__has_feature)` dış-guardı olmalı (tek-satırlık `#if`
# eski GCC/MSVC'de "missing binary operator" ile patlar).
call_sites=$(grep -rn -E '__has_feature\s*\(' engine tests tools --include='*.cpp' --include='*.hpp' --include='*.h' 2>/dev/null | grep -v '/build/' || true)
rule1_bad=""
while IFS= read -r hit; do
    [ -z "$hit" ] && continue
    file=${hit%%:*}
    rest=${hit#*:}
    line=${rest%%:*}
    if [ "$line" -gt 4 ]; then
        context=$(sed -n "$((line - 4)),$((line - 1))p" "$file")
    else
        context=$(sed -n "1,$((line - 1))p" "$file")
    fi
    if ! printf '%s' "$context" | grep -q 'defined(__has_feature)'; then
        rule1_bad="$rule1_bad $file:$line"
    fi
done <<< "$call_sites"
if [ -n "$rule1_bad" ]; then
    echo "[portability] KURAL-1 İHLAL: guardsız __has_feature çağrısı:$rule1_bad"
    fail=1
else
    echo "[portability] Kural-1 temiz (guardsız __has_feature çağrısı yok)."
fi

# Kural 2: tests/ içinde guardsız POSIX include.
posix_hits=$(grep -rln -E '^\s*#\s*include\s*<(unistd\.h|sys/|pthread\.h|dlfcn\.h|execinfo\.h)' tests --include='*.cpp' --include='*.hpp' || true)
if [ -n "$posix_hits" ]; then
    bad=""
    while IFS= read -r file; do
        if ! grep -q -E '^\s*#\s*(if|ifdef|ifndef).*(_WIN32|__linux__|__unix__|__APPLE__|__MACH__)' "$file"; then
            bad="$bad $file"
        fi
    done <<< "$posix_hits"
    if [ -n "$bad" ]; then
        echo "[portability] KURAL-2 İHLAL: guardsız POSIX include:$bad"
        fail=1
    else
        echo "[portability] Kural-2 temiz (POSIX include'lar guardlı dosyalarda)."
    fi
else
    echo "[portability] Kural-2 temiz (POSIX include yok)."
fi

if [ "$fail" -ne 0 ]; then
    echo "[portability] SONUÇ: KIRMIZI."
    exit 1
fi
echo "[portability] SONUÇ: YEŞİL."
