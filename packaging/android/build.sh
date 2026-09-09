#!/usr/bin/env bash
# Rowl Engine - Android NDK Cross-Compilation Script
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "==========================================================="
echo "  📱 Rowl Engine — Android NDK Cross-Compilation Pipeline"
echo "==========================================================="

# 1. Resolve Android NDK
if [ -z "$ANDROID_NDK_HOME" ]; then
    CANDIDATE_NDKS=(
        "${ANDROID_HOME:-}/ndk-bundle"
        "${ANDROID_HOME:-}/ndk/"*
        "${HOME:-}/Android/Sdk/ndk/"*
        "/opt/android-ndk"
    )
    for c in "${CANDIDATE_NDKS[@]}"; do
        if [ -d "$c" ]; then
            export ANDROID_NDK_HOME="$c"
            break
        fi
    done
fi

if [ -z "$ANDROID_NDK_HOME" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "❌ HATA: ANDROID_NDK_HOME bulunamadı veya geçerli bir dizin değil!"
    echo "  Lütfen Android NDK kurun ve ortam değişkenini ayarlayın:"
    echo "    export ANDROID_NDK_HOME=/path/to/android-ndk"
    echo "  Veya Android Studio SDK yöneticisi üzerinden NDK yükleyin."
    exit 2
fi

TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
if [ ! -f "$TOOLCHAIN_FILE" ]; then
    echo "❌ HATA: Android CMake toolchain dosyası bulunamadı: $TOOLCHAIN_FILE"
    exit 2
fi

# 2. Parse arguments
ASSET_PATH=""
ABI="arm64-v8a"
API_LEVEL="24"
CMAKE_PREFIX_PATH_VALUE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --asset-path) ASSET_PATH="${2:?--asset-path requires a path}"; shift 2 ;;
        --abi) ABI="${2:?--abi requires an ABI}"; shift 2 ;;
        --api) API_LEVEL="${2:?--api requires an API level}"; shift 2 ;;
        --cmake-prefix-path) CMAKE_PREFIX_PATH_VALUE="${2:?--cmake-prefix-path requires a path}"; shift 2 ;;
        *) echo "❌ HATA: Bilinmeyen seçenek: $1"; exit 2 ;;
    esac
done

BUILD_DIR="$ROOT_DIR/build/android-$ABI"
mkdir -p "$BUILD_DIR"

echo "ℹ️ NDK: $ANDROID_NDK_HOME"
echo "ℹ️ Target ABI: $ABI"
echo "ℹ️ Min API: $API_LEVEL"
echo "ℹ️ Asset Package: ${ASSET_PATH:-'(None - using Assets/)'}"
echo "ℹ️ Bu adım yalnızca native runtime kütüphanesini üretir; APK/asset paketleme ayrı Android Gradle işidir."

echo "[1/2] CMake Android Toolchain yapılandırılıyor..."
cmake_args=(
    -B "$BUILD_DIR" -S "$ROOT_DIR"
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    -DANDROID_ABI="$ABI"
    -DANDROID_PLATFORM="android-$API_LEVEL"
    -DCMAKE_BUILD_TYPE=Release
    -DROWL_BUILD_PLAYER=OFF
    -DROWL_BUILD_RUNTIME_TESTS=OFF
    -DROWL_ENABLE_GPU_MSDF=OFF
)
if [[ -n "$CMAKE_PREFIX_PATH_VALUE" ]]; then
    cmake_args+=("-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH_VALUE")
fi
cmake "${cmake_args[@]}"

echo "[2/2] libRowlEngineCore.so derleniyor..."
cmake --build "$BUILD_DIR" --target RowlEngineCore --parallel

OUTPUT_SO="$BUILD_DIR/lib/libRowlEngineCore.so"
if [ -f "$OUTPUT_SO" ]; then
    echo "✅ [BAŞARILI] Android paylaşımlı kütüphanesi derlendi: $OUTPUT_SO"
else
    echo "⚠️ UYARI: libRowlEngineCore.so beklenilen konumda üretilemedi."
fi
