#!/usr/bin/env bash
# Rowl Engine - Android NDK Cross-Compilation Script
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "==========================================================="
echo "  📱 Rowl Engine — Android NDK Cross-Compilation Pipeline"
echo "==========================================================="

# 1. Resolve Android NDK
if [ -z "$ANDROID_NDK_HOME" ]; then
    CANDIDATE_NDKS=(
        "$ANDROID_HOME/ndk-bundle"
        "$ANDROID_HOME/ndk/"*
        "$HOME/Android/Sdk/ndk/"*
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

while [[ $# -gt 0 ]]; do
    case $1 in
        --asset-path) ASSET_PATH="$2"; shift 2 ;;
        --abi) ABI="$2"; shift 2 ;;
        --api) API_LEVEL="$2"; shift 2 ;;
        *) shift ;;
    esac
done

BUILD_DIR="$ROOT_DIR/build/android-$ABI"
mkdir -p "$BUILD_DIR"

echo "ℹ️ NDK: $ANDROID_NDK_HOME"
echo "ℹ️ Target ABI: $ABI"
echo "ℹ️ Min API: $API_LEVEL"
echo "ℹ️ Asset Package: ${ASSET_PATH:-'(None - using Assets/)'}"

echo "[1/2] CMake Android Toolchain yapılandırılıyor..."
cmake -B "$BUILD_DIR" -S "$ROOT_DIR/engine" \
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API_LEVEL" \
    -DCMAKE_BUILD_TYPE=Release

echo "[2/2] libRowlEngineCore.so derleniyor..."
cmake --build "$BUILD_DIR" --target RowlEngineCore --parallel

OUTPUT_SO="$BUILD_DIR/lib/libRowlEngineCore.so"
if [ -f "$OUTPUT_SO" ]; then
    echo "✅ [BAŞARILI] Android paylaşımlı kütüphanesi derlendi: $OUTPUT_SO"
else
    echo "⚠️ UYARI: libRowlEngineCore.so beklenilen konumda üretilemedi."
fi

