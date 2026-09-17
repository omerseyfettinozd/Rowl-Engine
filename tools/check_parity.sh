#!/usr/bin/env bash
# check_parity.sh — yerel araç zincirinin CI kaydıyla paritesini doğrular.
# docs/CI_DEPENDENCIES.md'deki "sabit pin" ailesine karşı kontrol eder.
# Başarı: exit 0 + sürüm dökümü. Sapma: exit 1 + hangi araç.
set -u

fail=0
report() { echo "[parity] $1"; }
fail_line() { echo "[parity] SAPMA: $1"; fail=1; }

need_tool() {
    if ! command -v "$1" >/dev/null 2>&1; then
        fail_line "'$1' bulunamadi (PATH'te yok)"
    else
        report "'$1': $(command -v "$1")"
    fi
}

need_tool cmake
need_tool ninja
need_tool pkg-config
need_tool python3
need_tool dotnet

# .NET major sürüm ailesi CI'da 10.0.x'e pin'li.
if command -v dotnet >/dev/null 2>&1; then
    dotnet_version=$(dotnet --version 2>/dev/null || echo "bilinmiyor")
    report "dotnet sürümü: $dotnet_version"
    case "$dotnet_version" in
        10.*) ;;
        *) fail_line "dotnet major beklenen 10, bulunan: $dotnet_version" ;;
    esac
fi

if command -v cmake >/dev/null 2>&1; then
    report "cmake sürümü: $(cmake --version 2>/dev/null | head -n 1)"
fi

if [ "$fail" -ne 0 ]; then
    echo "[parity] SONUÇ: parite YOK — docs/CI_DEPENDENCIES.md ile karşılaştırın."
    exit 1
fi
echo "[parity] SONUÇ: parite TAMAM."
