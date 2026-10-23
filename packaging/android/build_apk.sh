#!/usr/bin/env bash
# Rowl Engine - Android APK (deneme). Gradle kullanmaz: aapt2 + javac + d8 +
# zipalign + apksigner (Android SDK build-tools) ile imzalı bir debug APK üretir.
#
#   packaging/android/build_apk.sh --project "<oyun projesi klasörü>" [--out <apk>]
#
# Önce: packaging/android/build_deps.sh (SDL3/zstd/Lua/Ogg/Vorbis) ve bu betik
# motoru (libRowlEngineCore.so) + oyuncuyu (libmain.so) arm64 için derler.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

PROJECT=""
OUT_APK=""
ABI="arm64-v8a"
MIN_SDK=24
TARGET_SDK=35
while [[ $# -gt 0 ]]; do
    case $1 in
        --project) PROJECT="${2:?--project bir klasör ister}"; shift 2 ;;
        --out) OUT_APK="${2:?--out bir dosya ister}"; shift 2 ;;
        *) echo "HATA: Bilinmeyen seçenek: $1" >&2; exit 2 ;;
    esac
done
[ -f "$PROJECT/project.rowlproj" ] || { echo "HATA: --project bir Rowl projesi olmalı (project.rowlproj yok)" >&2; exit 2; }

SDK="${ANDROID_HOME:-$HOME/Android/Sdk}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)}"
BT="$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)"
PLATFORM_JAR="$(ls -d "$SDK"/platforms/android-* | sort -V | tail -1)/android.jar"
for tool in aapt2 d8 zipalign apksigner; do
    [ -x "$BT/$tool" ] || { echo "HATA: $BT/$tool yok" >&2; exit 2; }
done

WORK="$ROOT_DIR/build/android-apk"
OUT_APK="${OUT_APK:-$WORK/RowlGame-$ABI.apk}"
DEPS_PREFIX="$ROOT_DIR/build/android-deps/$ABI/prefix"
[ -f "$DEPS_PREFIX/lib/libSDL3.so" ] || bash "$SCRIPT_DIR/build_deps.sh"

# pkg-config bayrakları boşluklu yolları bölüyor ("Rowl Engine"): öneki
# boşluksuz bir bağlantı üzerinden ver.
LINK_PREFIX="$(mktemp -d)/prefix"
ln -s "$DEPS_PREFIX" "$LINK_PREFIX"

echo "[1/5] Motor + oyuncu ($ABI)"
NATIVE="$ROOT_DIR/build/android-$ABI"
PKG_CONFIG_LIBDIR="$LINK_PREFIX/lib/pkgconfig" cmake -S "$ROOT_DIR" -B "$NATIVE" -G Ninja \
    "-DPKG_CONFIG_ARGN=--define-prefix;--static" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" -DANDROID_PLATFORM="android-$MIN_SDK" -DCMAKE_BUILD_TYPE=Release \
    -DROWL_BUILD_PLAYER=ON -DROWL_BUILD_RUNTIME_TESTS=OFF -DROWL_ENABLE_GPU_MSDF=OFF \
    -DROWL_ENABLE_TEXT_SHAPING=OFF \
    -DCMAKE_PREFIX_PATH="$LINK_PREFIX" -DCMAKE_FIND_ROOT_PATH="$LINK_PREFIX" >/dev/null
mkdir -p "$WORK"
cmake --build "$NATIVE" --target rowl_player --parallel >"$WORK/native-build.log" 2>&1 \
    || { grep -E "error|FAILED" "$WORK/native-build.log" | head -20 >&2; echo "HATA: native derleme (ayrıntı: $WORK/native-build.log)" >&2; exit 1; }

echo "[2/5] Oyun paketi"
rm -rf "$WORK/stage" "$WORK/classes" "$WORK/dex"
GAME="$WORK/stage/assets/game"
mkdir -p "$GAME/Assets/packages" "$WORK/stage/lib/$ABI" "$WORK/classes" "$WORK/dex"
GAME_ID="$(python3 -c 'import json,sys;print(json.load(open(sys.argv[1])).get("project_uuid",""))' "$PROJECT/project.rowlproj")"
[ -n "$GAME_ID" ] || { echo "HATA: project.rowlproj içinde project_uuid yok" >&2; exit 2; }
python3 "$ROOT_DIR/tools/package_assets.py" "$PROJECT/Assets" "$GAME/Assets/packages/game.rowlpkg" \
    --game-id "$GAME_ID" >/dev/null
cp "$PROJECT/project.rowlproj" "$GAME/"
for so in "$DEPS_PREFIX/lib/libSDL3.so" "$NATIVE/lib/libRowlEngineCore.so" "$NATIVE/lib/libmain.so"; do
    [ -f "$so" ] || so="$NATIVE/bin/$(basename "$so")"
    cp "$so" "$WORK/stage/lib/$ABI/"
done
"$NDK"/toolchains/llvm/prebuilt/*/bin/llvm-strip --strip-unneeded "$WORK/stage/lib/$ABI/"*.so

echo "[3/5] Java (SDLActivity + RowlActivity)"
SDL_JAVA="$(ls -d "$ROOT_DIR"/build/android-deps/"$ABI"/src/SDL3-*/android-project/app/src/main/java | head -1)"
# javac @dosya boşlukta böler: her yolu tırnakla yaz.
find "$SDL_JAVA" "$SCRIPT_DIR/java" -name '*.java' -printf '"%p"\n' > "$WORK/sources.txt"
javac -nowarn -encoding UTF-8 --release 17 -cp "$PLATFORM_JAR" -d "$WORK/classes" @"$WORK/sources.txt"
find "$WORK/classes" -name '*.class' -print0 \
    | xargs -0 "$BT/d8" --release --min-api "$MIN_SDK" --lib "$PLATFORM_JAR" --output "$WORK/dex"

echo "[4/5] APK"
"$BT/aapt2" link -o "$WORK/unsigned.apk" -I "$PLATFORM_JAR" \
    --manifest "$SCRIPT_DIR/AndroidManifest.xml" \
    --min-sdk-version "$MIN_SDK" --target-sdk-version "$TARGET_SDK" \
    -A "$WORK/stage/assets"
cp "$WORK/dex/classes.dex" "$WORK/stage/"
(cd "$WORK/stage" && zip -q -r "$WORK/unsigned.apk" classes.dex lib)
"$BT/zipalign" -f -P 16 4 "$WORK/unsigned.apk" "$WORK/aligned.apk"

echo "[5/5] İmza (debug)"
KEYSTORE="$WORK/debug.keystore"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -storepass android -keypass android \
        -alias rowldebug -keyalg RSA -keysize 2048 -validity 10000 \
        -dname "CN=Rowl Engine Debug" >/dev/null 2>&1
fi
mkdir -p "$(dirname "$OUT_APK")"
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-pass pass:android --key-pass pass:android \
    --out "$OUT_APK" "$WORK/aligned.apk"
rm -f "$WORK/unsigned.apk" "$WORK/aligned.apk" "$OUT_APK.idsig"
echo "Hazır: $OUT_APK ($(du -h "$OUT_APK" | cut -f1))"
