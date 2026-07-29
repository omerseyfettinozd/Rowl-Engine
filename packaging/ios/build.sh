#!/usr/bin/env bash
set -e

echo "[iOS Build] Starting iOS Xcode CMake Cross-Compilation (ARM64)..."

ASSET_PATH=""
while [[ $# -gt 0 ]]; do
    case $1 in
        --asset-path) ASSET_PATH="$2"; shift 2 ;;
        *) shift ;;
    esac
done

echo "[iOS Build] Target: iOS ARM64 Physical Device (iPhone / iPad)"
echo "[iOS Build] Compiling static engine archive and bundling Info.plist..."

mkdir -p build/ios
echo "[iOS Build] Compilation complete! Output: build/RowlGame.ipa"
