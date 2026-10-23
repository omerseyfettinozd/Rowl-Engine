#!/usr/bin/env bash
# Rowl Engine - Android bağımlılıkları (SDL3, zstd, Lua) arm64 için derlenir.
# Kaynaklar build/android-deps/dl altına indirilir; kurulum öneki
# build/android-deps/<abi>/prefix olur (build.sh --cmake-prefix-path ile verilir).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

ABI="${ABI:-arm64-v8a}"
API_LEVEL="${API_LEVEL:-24}"
SDL_VERSION="3.4.18"
ZSTD_VERSION="1.5.7"
LUA_VERSION="5.4.7"
OGG_VERSION="1.3.5"
VORBIS_VERSION="1.3.7"

if [ -z "${ANDROID_NDK_HOME:-}" ]; then
    ANDROID_NDK_HOME="$(ls -d "${ANDROID_HOME:-$HOME/Android/Sdk}"/ndk/* 2>/dev/null | sort -V | tail -1)"
fi
[ -d "${ANDROID_NDK_HOME:-}" ] || { echo "HATA: ANDROID_NDK_HOME bulunamadı" >&2; exit 2; }
TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

DEPS="$ROOT_DIR/build/android-deps"
DL="$DEPS/dl"
WORK="$DEPS/$ABI"
PREFIX="$WORK/prefix"
mkdir -p "$DL" "$WORK/src" "$PREFIX"

fetch() {  # url
    local file="$DL/$(basename "$1")"
    [ -f "$file" ] || curl -fsSL -o "$file" "$1"
    tar -xzf "$file" -C "$WORK/src"
}

common=(
    -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DANDROID_ABI="$ABI"
    -DANDROID_PLATFORM="android-$API_LEVEL" -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$PREFIX" -G Ninja
)

echo "[1/5] SDL3 $SDL_VERSION"
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
cmake -S "$WORK/src/SDL3-$SDL_VERSION" -B "$WORK/sdl" "${common[@]}" \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >/dev/null
cmake --build "$WORK/sdl" --parallel >/dev/null
cmake --install "$WORK/sdl" >/dev/null

echo "[2/5] zstd $ZSTD_VERSION"
fetch "https://github.com/facebook/zstd/releases/download/v$ZSTD_VERSION/zstd-$ZSTD_VERSION.tar.gz"
cmake -S "$WORK/src/zstd-$ZSTD_VERSION/build/cmake" -B "$WORK/zstd" "${common[@]}" \
    -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF -DZSTD_BUILD_SHARED=OFF \
    -DZSTD_BUILD_STATIC=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
cmake --build "$WORK/zstd" --parallel >/dev/null
cmake --install "$WORK/zstd" >/dev/null

echo "[3/5] Lua $LUA_VERSION"
fetch "https://www.lua.org/ftp/lua-$LUA_VERSION.tar.gz"
LUA_SRC="$WORK/src/lua-$LUA_VERSION/src"
cat > "$WORK/src/lua-$LUA_VERSION/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(lua C)
file(GLOB LUA_SOURCES src/*.c)
list(REMOVE_ITEM LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/lua.c ${CMAKE_CURRENT_SOURCE_DIR}/src/luac.c)
add_library(lua STATIC ${LUA_SOURCES})
set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
install(TARGETS lua ARCHIVE DESTINATION lib)
install(FILES src/lua.h src/luaconf.h src/lualib.h src/lauxlib.h src/lua.hpp DESTINATION include)
CMAKE
cmake -S "$WORK/src/lua-$LUA_VERSION" -B "$WORK/lua" "${common[@]}" >/dev/null
cmake --build "$WORK/lua" --parallel >/dev/null
cmake --install "$WORK/lua" >/dev/null

echo "[4/5] libogg $OGG_VERSION"
fetch "https://downloads.xiph.org/releases/ogg/libogg-$OGG_VERSION.tar.gz"
cmake -S "$WORK/src/libogg-$OGG_VERSION" -B "$WORK/ogg" "${common[@]}" \
    -DBUILD_SHARED_LIBS=OFF -DINSTALL_DOCS=OFF -DBUILD_TESTING=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 >/dev/null
cmake --build "$WORK/ogg" --parallel >/dev/null
cmake --install "$WORK/ogg" >/dev/null

echo "[5/5] libvorbis $VORBIS_VERSION"
fetch "https://downloads.xiph.org/releases/vorbis/libvorbis-$VORBIS_VERSION.tar.gz"
cmake -S "$WORK/src/libvorbis-$VORBIS_VERSION" -B "$WORK/vorbis" "${common[@]}" \
    -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_FIND_ROOT_PATH="$PREFIX" >/dev/null
cmake --build "$WORK/vorbis" --parallel >/dev/null
cmake --install "$WORK/vorbis" >/dev/null

echo "Hazır: $PREFIX"
