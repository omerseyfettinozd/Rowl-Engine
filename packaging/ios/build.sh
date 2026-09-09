#!/usr/bin/env bash
set -euo pipefail

# Builds only the reusable iOS arm64 runtime library. An IPA requires an app
# host, signing identity, provisioning profile, and asset bundle, so it must
# never be claimed as an output of this native-library step.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/../.." && pwd)"
build_dir="$project_root/build/ios-arm64"
cmake_prefix_path=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) build_dir="${2:?--build-dir requires a path}"; shift 2 ;;
        --cmake-prefix-path) cmake_prefix_path="${2:?--cmake-prefix-path requires a path}"; shift 2 ;;
        *) echo "error: unknown option: $1" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "error: iOS builds require macOS with Xcode; no IPA or library was produced." >&2
    exit 2
fi
if ! command -v xcodebuild >/dev/null 2>&1 || ! command -v xcrun >/dev/null 2>&1; then
    echo "error: Xcode command-line tools are required for an iOS build." >&2
    exit 2
fi

sdk_path="$(xcrun --sdk iphoneos --show-sdk-path)"
cmake_args=(
    -S "$project_root" -B "$build_dir" -G Xcode
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_OSX_SYSROOT="$sdk_path"
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
    -DROWL_BUILD_PLAYER=OFF
    -DROWL_BUILD_RUNTIME_TESTS=OFF
    -DROWL_ENABLE_GPU_MSDF=OFF
)
if [[ -n "$cmake_prefix_path" ]]; then
    cmake_args+=("-DCMAKE_PREFIX_PATH=$cmake_prefix_path")
fi

echo "[iOS] Configuring arm64 runtime library..."
cmake "${cmake_args[@]}"
echo "[iOS] Building RowlEngineCore..."
cmake --build "$build_dir" --config Release --target RowlEngineCore --parallel 4
echo "[iOS] Native runtime library build completed in: $build_dir"
echo "[iOS] Next required step: link the library into a signed app host, bundle assets, then run a physical-device smoke test."
