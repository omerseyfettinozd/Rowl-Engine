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
ROWL_EDITOR_BENCHMARK_JSON="$output_json" \
SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" \
dotnet test "$project_root/editor/Tests/RowlEngine.Editor.Tests.csproj" \
    --configuration "${ROWL_EDITOR_BENCHMARK_BUILD_TYPE:-Debug}" \
    --logger "console;verbosity=normal"

echo "Wrote editor baseline: $output_json"
