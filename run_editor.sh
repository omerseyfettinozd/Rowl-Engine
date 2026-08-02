#!/bin/bash
# Rowl Engine — Single-Process Embedded Launcher
# Builds libRowlEngineCore.so and runs the C# Avalonia Editor with embedded engine.
# Usage: ./run_editor.sh

set -e

PROJECT_ROOT="/home/chaple/Belgeler/Rowl Engine"
BUILD_DIR="$PROJECT_ROOT/build"
NATIVE_LIB="$BUILD_DIR/lib/libRowlEngineCore.so"
EDITOR_DIR="$PROJECT_ROOT/editor"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

log() { echo -e "${BLUE}[Launcher]${NC} $1"; }
success() { echo -e "${GREEN}[Launcher]${NC} $1"; }
error() { echo -e "${RED}[Launcher]${NC} $1"; }

log "Checking C++ Engine Shared Library..."

if [ ! -f "$NATIVE_LIB" ]; then
    log "Building C++ Engine library (libRowlEngineCore.so)..."
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j$(nproc)
fi

success "C++ Engine Library Ready: $NATIVE_LIB"

log "Launching Rowl Engine Editor (Embedded Mode)..."
cd "$EDITOR_DIR"
exec dotnet run --project RowlEngine.Editor.csproj