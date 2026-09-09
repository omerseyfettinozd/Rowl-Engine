#!/usr/bin/env bash
set -euo pipefail

# Reproducible baseline for VFS, scene JSON, offscreen frame time, and texture
# cache telemetry. Compare only runs made with the same build type and device.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/.." && pwd)"
build_dir="${ROWL_BENCHMARK_BUILD_DIR:-$project_root/build-benchmarks}"
gpu_msdf="${ROWL_BENCHMARK_GPU_MSDF:-OFF}"

cmake -S "$project_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DROWL_ENABLE_GPU_MSDF="$gpu_msdf"
cmake --build "$build_dir" --parallel 4

# The test executable includes the benchmark output and uses the dummy audio
# driver so measurements do not require a desktop sound server.
SDL_AUDIODRIVER=dummy "$build_dir/bin/rowl_tests"
