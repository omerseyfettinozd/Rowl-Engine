#!/usr/bin/env bash
set -euo pipefail

# Reproducible editor-interaction baseline. Keep comparisons limited to reports
# made with the same fixture, build type, and host identity.
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "$script_dir/.." && pwd)"
output_json="${ROWL_EDITOR_BENCHMARK_OUTPUT:-$project_root/benchmark-results/editor-baseline.json}"
build_id="${ROWL_EDITOR_BENCHMARK_BUILD_ID:-$(git -C "$project_root" rev-parse --short HEAD)}"

ROWL_EDITOR_BENCHMARK_BUILD_ID="$build_id" \
ROWL_EDITOR_BENCHMARK_BUILD_TYPE="${ROWL_EDITOR_BENCHMARK_BUILD_TYPE:-Debug}" \
ROWL_EDITOR_BENCHMARK_MACHINE="${ROWL_EDITOR_BENCHMARK_MACHINE:-$(uname -m)}" \
dotnet run --project "$project_root/editor/RowlEngine.Editor.csproj" -- \
    --headless-test --editor-benchmark-json "$output_json"

echo "Wrote editor baseline: $output_json"
