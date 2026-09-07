#!/usr/bin/env bash
# Rowl Engine - Development Launcher (Embedded In-Process Engine)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export DOTNET_CLI_HOME="$SCRIPT_DIR/.dotnet"
export NUGET_PACKAGES="$SCRIPT_DIR/.nuget"

echo "==========================================================="
echo "  🎮 Rowl Engine — Development Launcher"
echo "  Architecture: In-Process Shared Library (P/Invoke)"
echo "==========================================================="

# Ensure native engine shared library is built
LIB_PATH="$SCRIPT_DIR/build/lib/libRowlEngineCore.so"
if [ ! -f "$LIB_PATH" ]; then
    echo "[!] libRowlEngineCore.so bulunamadı. Derleniyor..."
    cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --target RowlEngineCore rowl_player
fi

export LD_LIBRARY_PATH="$SCRIPT_DIR/build/lib:$LD_LIBRARY_PATH"

echo "[!] Avalonia Node Editörü başlatılıyor..."
dotnet run --project editor/RowlEngine.Editor.csproj "$@"
