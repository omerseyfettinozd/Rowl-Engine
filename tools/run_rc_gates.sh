#!/usr/bin/env bash
# run_rc_gates.sh — Release-candidate kapı koşucusu (Linux x86_64; C05).
#
# Temiz bir ağaçta native / yönetilen / paket kapılarını, ASan+UBSan ve TSan
# kapılarını sırayla koşar; her kapının kendi logu vardır. Sonuç
# <out>/report.md + report.json. Genel sonuç yalnız ZORUNLU her kapı PASS ise
# PASS; SKIPPED / ENV / eksik kapı PASS sayılmaz.
#
# Kullanım:
#   tools/run_rc_gates.sh [--quick] [--allow-dirty] [--out DIR] [--help]
#
#   --quick        sanitizer/TSan kapılarını SKIPPED(quick) bırakır; en iyi
#                  "PASS (partial)" ve çıkış kodu 2 (RC kanıtı DEĞİL).
#   --allow-dirty  kirli ağaçta koşar; rapor "dirty" işaretlenir ve en iyi
#                  "PASS (dirty)" / çıkış kodu 2 olur.
#   --out DIR      çıktı dizini (varsayılan build/rc-gates/<commit>).
#
# Çıkış: 0 PASS, 1 FAIL/INCOMPLETE, 2 PASS (partial|dirty).
# Ortam: ROWL_RC_JOBS (paralel derleme, varsayılan 8).
set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo_root"

QUICK=0
ALLOW_DIRTY=0
OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --quick) QUICK=1 ;;
        --allow-dirty) ALLOW_DIRTY=1 ;;
        --out) shift; OUT=${1:?--out bir dizin ister} ;;
        --help|-h) sed -n '2,19p' "$0"; exit 0 ;;
        *) echo "bilinmeyen argüman: $1" >&2; exit 64 ;;
    esac
    shift
done

REPORT_PY="$repo_root/tools/rc_gate_report.py"
JOBS=${ROWL_RC_JOBS:-8}

# ---- Kimlik ----------------------------------------------------------------
commit=$(git rev-parse HEAD)
short=$(git rev-parse --short=10 HEAD)
dirty_files=$(git status --porcelain --untracked-files=normal || true)
dirty=0
if [ -n "$dirty_files" ]; then
    dirty=1
    if [ "$ALLOW_DIRTY" -ne 1 ]; then
        echo "[rc-gates] REDDEDİLDİ: ağaç kirli (RC kapısı temiz kaynak commit ister)." >&2
        echo "$dirty_files" | head -20 >&2
        echo "[rc-gates] bilerek koşmak için --allow-dirty (rapor işaretlenir)." >&2
        exit 1
    fi
fi
[ -n "$OUT" ] || OUT="build/rc-gates/$short$([ "$dirty" -eq 1 ] && echo -dirty || true)"
case "$OUT" in /*) ;; *) OUT="$repo_root/$OUT" ;; esac
rm -rf "$OUT"
mkdir -p "$OUT/logs" "$OUT/work"
# Yalıtılmış TMPDIR/XDG depo AĞACININ DIŞINDA olmalı: ağaç içinde olursa
# "git sınırı yok" varsayan editör testleri depo .git'ini bulup yanlış kırmızı verir.
SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/rowl-rc-gates.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
mkdir -p "$SCRATCH/tmp" "$SCRATCH/xdg"
RESULTS="$OUT/results.jsonl"
: > "$RESULTS"

# Güç profili: tests/test_native_performance_benchmarks.cpp benchmarkPowerProfile
# ile aynı mantık (env geçersiz kılar; yoksa cpufreq governor[/EPP]).
power_profile() {
    if [ -n "${ROWL_BENCHMARK_POWER_PROFILE:-}" ]; then
        printf '%s' "$ROWL_BENCHMARK_POWER_PROFILE"; return
    fi
    local cpu=/sys/devices/system/cpu/cpu0/cpufreq gov="" epp=""
    [ -r "$cpu/scaling_governor" ] && gov=$(head -n1 "$cpu/scaling_governor" 2>/dev/null || true)
    [ -r "$cpu/energy_performance_preference" ] && epp=$(head -n1 "$cpu/energy_performance_preference" 2>/dev/null || true)
    if [ -n "$gov" ]; then
        [ -n "$epp" ] && printf '%s/%s' "$gov" "$epp" || printf '%s' "$gov"
        return
    fi
    if command -v powerprofilesctl >/dev/null 2>&1; then
        powerprofilesctl get 2>/dev/null && return
    fi
    printf 'unknown'
}
POWER=$(power_profile)
# power-profiles-daemon cpufreq'den bağımsız söyler (governor powersave/
# balance_performance görünürken profil power-saver olabilir): ikisine de bak.
PPD=""
command -v powerprofilesctl >/dev/null 2>&1 && PPD=$(powerprofilesctl get 2>/dev/null || true)
POWER_SAVER=0
python3 "$REPORT_PY" power-saver "$POWER" && POWER_SAVER=1 || true
[ -n "$PPD" ] && python3 "$REPORT_PY" power-saver "$PPD" && POWER_SAVER=1 || true

first_line() { "$@" 2>&1 | head -n1 || true; }
CXX_BIN=${CXX:-c++}
python3 - "$OUT/identity.json" <<PY
import json, sys
json.dump({
    "commit": "$commit",
    "dirty": bool($dirty),
    "started_utc": "$(date -u +%Y-%m-%dT%H:%M:%SZ)",
    "host": "$(uname -srm)",
    "compiler": "$(first_line "$CXX_BIN" --version | tr '"' "'")",
    "cmake": "$(first_line cmake --version)",
    "dotnet": "$(first_line dotnet --version | tr '"' "'")",
    "python": "$(first_line python3 --version)",
    "power_profile": "$POWER",
    "power_profiles_daemon": "$PPD",
    "power_saver": bool($POWER_SAVER),
}, open(sys.argv[1], "w"), indent=2)
PY
echo "[rc-gates] commit=$short dirty=$dirty power=$POWER ppd=${PPD:-n/a} quick=$QUICK out=$OUT"

# ---- Kapı koşucu -----------------------------------------------------------
# gate NAME DESC -- komut...   (log: logs/NAME.log; komut çıkışı = sonuç)
# Dönüş: 0 PASS, 1 FAIL. Sonuç results.jsonl'e yazılır; betik FAIL'de durmaz.
record() { python3 "$REPORT_PY" record --file "$RESULTS" "$@"; }
skip() { # NAME STATUS REASON
    record --name "$1" --status "$2" --reason "$3"
    echo "[rc-gates] $1: $2 ($3)"
}
gate() {
    local name=$1; shift
    local log="logs/$name.log" start end rc=0 hint=""
    start=$(date +%s)
    echo "[rc-gates] >> $name"
    # `( ... ) || rc=$?` bağlamında set -e devre dışı kalır; bu yüzden kapı
    # fonksiyonları ayrı bir `bash -e` içinde (tüm fonksiyonlar aktarılarak)
    # koşar ve ilk başarısız komutta durur.
    bash -e -c "set -o pipefail; $(declare -f); $*" >"$OUT/$log" 2>&1 || rc=$?
    end=$(date +%s)
    if [ "$rc" -eq 0 ]; then
        record --name "$name" --status PASS --duration $((end - start)) --log "$log"
        echo "[rc-gates] $name: PASS ($((end - start)) sn)"
        return 0
    fi
    # Bilinen ortam sinyali: RC Soak bütçesi güç tasarrufunda kaçar. GİZLENMEZ,
    # FAIL kalır; yalnız ipucu eklenir (RC performans profilinde kapılanmalı).
    if [ "$POWER_SAVER" -eq 1 ] && grep -q "RC Soak" "$OUT/$log" 2>/dev/null; then
        hint="power profile is power-saver (RC Soak N-stress 300 sn bütçesi; performans profilinde yeniden koş)"
    fi
    record --name "$name" --status FAIL --reason "rc=$rc" --duration $((end - start)) \
        --log "$log" ${hint:+--hint "$hint"}
    echo "[rc-gates] $name: FAIL rc=$rc${hint:+ — env hint: $hint}"
    return 1
}

# Her kapı komutu bir fonksiyon (alt kabukta, set -e ile koşar).
export TMPDIR="$SCRATCH/tmp" XDG_DATA_HOME="$SCRATCH/xdg"
export SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy
XVFB=""
command -v xvfb-run >/dev/null 2>&1 && XVFB="xvfb-run -a"

g_diff_check() { git diff --check; git diff --cached --check; }
g_portability() { bash tools/check_portability.sh; }
g_toolchain_parity() { bash tools/check_parity.sh; }
g_launcher_parity() { python3 tools/check_release_launcher_parity.py; }

DEBUG_BUILD=build   # editör csproj'ları build/lib/ altını bekler (CI ile aynı)
g_native_build() {
    cmake -S . -B "$DEBUG_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug
    cmake --build "$DEBUG_BUILD" --parallel "$JOBS"
    # Bayat .o kontrolü: derleme sonrası yapılacak iş kalmamalı.
    ninja -C "$DEBUG_BUILD" -n | grep -q "no work to do"
}
g_native_ctest() {
    # CI `linux` işi ile aynı politika: mutlak tavanlar enforced, 30 FPS report.
    ROWL_PERF_FLOOR=enforced ROWL_CAMERA_FPS_FLOOR=report \
        ctest --test-dir "$DEBUG_BUILD" --output-on-failure --timeout 1800 --no-tests=error
}
g_abi() { python3 tools/check_abi_additive.py "$DEBUG_BUILD/lib/libRowlEngineCore.so" tools/abi_baseline.txt; }
g_xunit() {
    # Native kütüphane kopyalandığı log'da kanıtlanmalı (yoksa motor testleri çıplak koşar).
    $XVFB dotnet test editor/Tests/RowlEngine.Editor.Tests.csproj --configuration Debug \
        --artifacts-path build/dotnet-rc --logger "console;verbosity=normal" | tee "$OUT/work/xunit.out"
    grep -q "Native Engine Library .* copied" "$OUT/work/xunit.out"
}
g_rowleye() {
    $XVFB dotnet test editor/Tests/RowlEngine.Editor.Tests.csproj --configuration Debug \
        --artifacts-path build/dotnet-rc --filter "FullyQualifiedName~RowlEye" \
        --logger "console;verbosity=normal" | tee "$OUT/work/rowleye.out"
    # Süpürme sessizce boş filtreyle yeşil olmasın.
    grep -Eq "(Geçti|Passed): *[1-9]" "$OUT/work/rowleye.out"
}

# Sanitizer: CI nightly bayrakları; mutlak tavanlar report (sanitizer altında
# 939/1387 sn ölçüldü), oran kapısı ve bellek/UB/yarış kilitleri enforced.
g_asan() {
    local f="-fsanitize=address,undefined -fno-omit-frame-pointer"
    cmake -S . -B build-rc-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="$f" -DCMAKE_CXX_FLAGS="$f" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
    cmake --build build-rc-asan --target rowl_tests --parallel "$JOBS"
    ROWL_PERF_FLOOR=report ROWL_CAMERA_FPS_FLOOR=report \
        ASAN_OPTIONS=halt_on_error=1:print_stacktrace=1:detect_leaks=0 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        ctest --test-dir build-rc-asan -R "rowl_native_tests|rowl_native_soak_tests|rowl_benchmark_tool_tests" \
        --output-on-failure --timeout 1800 --no-tests=error
}
g_tsan() {
    local f="-fsanitize=thread -fno-omit-frame-pointer"
    cmake -S . -B build-rc-tsan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_C_FLAGS="$f" -DCMAKE_CXX_FLAGS="$f" \
        -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" \
        -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=thread"
    cmake --build build-rc-tsan --target rowl_tests --parallel "$JOBS"
    ROWL_PERF_FLOOR=report ROWL_CAMERA_FPS_FLOOR=report \
        TSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        ctest --test-dir build-rc-tsan -R "rowl_native_tests|rowl_native_soak_tests|rowl_benchmark_tool_tests" \
        --output-on-failure --timeout 1800 --no-tests=error
}
tsan_available() {
    local probe="$OUT/work/tsan_probe"
    printf 'int main(){return 0;}\n' > "$probe.cpp"
    "$CXX_BIN" -fsanitize=thread "$probe.cpp" -o "$probe" >/dev/null 2>&1 && "$probe" >/dev/null 2>&1
}

# Paket kapısı: second_signal -> game.rowlpkg -> portable-zip -> çıkar ->
# doğrula -> paketli oynatıcı smoke (SDL dummy), çıkarılmış düzende.
g_package() {
    local work="$OUT/work/package"
    rm -rf "$work"; mkdir -p "$work/extracted"
    python3 tools/package_assets.py samples/second_signal/Assets "$work/game.rowlpkg" \
        --game-id 5ec0d519-9a1e-4c2b-8f3d-2a6b7c8d9e01 --game-name "Second Signal"
    # Golden Project dondurulmuş (project.rowlproj sağlaması kilitli; uuid
    # taşımaz) → kapı sabit bir test kimliği gömer; doğrulayıcı loose
    # manifestte uuid yoksa eşitlik aramaz.
    python3 tools/export_game.py portable-zip --output "$work/rowl-portable.zip" \
        --player "$DEBUG_BUILD/bin/rowl_player" \
        --runtime "$DEBUG_BUILD/lib/libRowlEngineCore.so" --package "$work/game.rowlpkg"
    python3 -I -m zipfile -e "$work/rowl-portable.zip" "$work/extracted"
    # zipfile izinleri korumaz; kodlanmış 0755 bitlerini geri ver.
    chmod 755 "$work/extracted/RowlGame" "$work/extracted/run_game.sh"
    (cd "$work/extracted" && sha256sum -c SHA256SUMS)
    python3 tools/package_assets.py verify "$work/extracted/Assets/packages/game.rowlpkg"
    # Çıkarılmış düzen kaynak project.rowlproj'u taşımıyorsa (portable-zip
    # yalnız canonical çekirdeği paketler) paketli smoke için sample'dan ekle.
    [ -f "$work/extracted/project.rowlproj" ] || cp samples/second_signal/project.rowlproj "$work/extracted/"
    python3 tools/verify_release_package.py "$work/extracted"
    (
        cd "$work/extracted"
        LD_LIBRARY_PATH=. $XVFB timeout 1800s ./RowlGame --project . --package-smoke-test
    ) | tee "$work/smoke.out"
    grep -q "Package smoke frame rendered" "$work/smoke.out"
}

# ---- Koşu ------------------------------------------------------------------
export OUT DEBUG_BUILD JOBS XVFB CXX_BIN
gate static-diff-check g_diff_check || true
gate static-portability g_portability || true
gate static-toolchain-parity g_toolchain_parity || true
gate static-launcher-parity g_launcher_parity || true

if gate native-build-debug g_native_build; then
    gate native-ctest-debug g_native_ctest || true
    gate abi-additive g_abi || true
    gate editor-xunit g_xunit || true
    gate rowleye-tests g_rowleye || true
    gate package-gate g_package || true
else
    for n in native-ctest-debug abi-additive editor-xunit rowleye-tests package-gate; do
        skip "$n" SKIPPED "dependency: native-build-debug FAIL"
    done
fi

if [ "$QUICK" -eq 1 ]; then
    skip sanitizer-asan-ubsan SKIPPED "quick: --quick sanitizer kapısını koşmaz"
    skip sanitizer-tsan SKIPPED "quick: --quick TSan kapısını koşmaz"
else
    gate sanitizer-asan-ubsan g_asan || true
    if tsan_available; then
        gate sanitizer-tsan g_tsan || true
    else
        skip sanitizer-tsan SKIPPED "TSan bu ortamda derlenemiyor/çalışmıyor (libtsan yok veya ASLR/vm.mmap_rnd_bits uyumsuz)"
    fi
fi

rc=0
args=(finalize --file "$RESULTS" --identity "$OUT/identity.json" --out "$OUT")
[ "$QUICK" -eq 1 ] && args+=(--quick)
[ "$ALLOW_DIRTY" -eq 1 ] && args+=(--allow-dirty)
python3 "$REPORT_PY" "${args[@]}" || rc=$?
echo "[rc-gates] rapor: $OUT/report.md"
exit "$rc"
