#!/usr/bin/env bash
set -euo pipefail

# Reproducible baselines for engine startup, VFS, scene JSON, offscreen frame
# time, and texture cache telemetry — plus the frozen Golden Project fixture
# series (startup, project load, first/steady frame, memory). Compare only
# runs made with the same build type and device.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/.." && pwd)"
build_dir="${ROWL_BENCHMARK_BUILD_DIR:-$project_root/build-benchmarks}"
gpu_msdf="${ROWL_BENCHMARK_GPU_MSDF:-OFF}"
output_json="${ROWL_BENCHMARK_OUTPUT:-$project_root/benchmark-results/native-baseline.json}"
golden_output_json="${ROWL_GOLDEN_BENCHMARK_OUTPUT:-$project_root/benchmark-results/golden-baseline.json}"
build_id="${ROWL_BENCHMARK_BUILD_ID:-$(git -C "$project_root" rev-parse --short HEAD)}"
nlohmann_source_dir="${ROWL_BENCHMARK_NLOHMANN_SOURCE_DIR:-$project_root/build/_deps/nlohmann_json-src}"

cmake_args=(
    -S "$project_root" -B "$build_dir"
    -DCMAKE_BUILD_TYPE=Release
    -DROWL_ENABLE_GPU_MSDF="$gpu_msdf"
)
# Reuse the already pinned source when the normal development build has it.
# Fresh CI remains free to use FetchContent or its toolchain package.
if [[ -d "$nlohmann_source_dir" ]]; then
    cmake_args+=("-DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=$nlohmann_source_dir")
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --parallel 4

# The test executable includes the benchmark output and uses the dummy audio
# driver so measurements do not require a desktop sound server.
ROWL_BENCHMARK_BUILD_ID="$build_id" \
ROWL_BENCHMARK_BUILD_TYPE=Release \
ROWL_BENCHMARK_FIXTURE=native-default-v1 \
ROWL_BENCHMARK_MACHINE="$(uname -m)" \
SDL_AUDIODRIVER=dummy "$build_dir/bin/rowl_tests" --benchmark-json "$output_json" \
    --golden-benchmark-json "$golden_output_json"
echo "Wrote baseline: $output_json"
echo "Wrote golden baseline: $golden_output_json"
