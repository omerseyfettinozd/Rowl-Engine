#!/bin/bash
# Rowl Engine - Unified Launcher
# Starts C++ Engine in IPC mode, then launches C# Avalonia Editor
# Usage: ./run_editor.sh

set -e

PROJECT_ROOT="/home/chaple/Belgeler/Rowl Engine"
ENGINE_BIN="$PROJECT_ROOT/build/bin/rowl_engine"
EDITOR_DIR="$PROJECT_ROOT/editor"
PIPE_ID="rowl_engine_ipc"
SOCKET_PATH="/tmp/${PIPE_ID}.sock"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

log() { echo -e "${BLUE}[Launcher]${NC} $1"; }
success() { echo -e "${GREEN}[Launcher]${NC} $1"; }
warn() { echo -e "${YELLOW}[Launcher]${NC} $1"; }
error() { echo -e "${RED}[Launcher]${NC} $1"; }

# Cleanup on exit
cleanup() {
    log "Shutting down..."
    if [ ! -z "$ENGINE_PID" ] && kill -0 "$ENGINE_PID" 2>/dev/null; then
        log "Stopping Engine (PID: $ENGINE_PID)..."
        kill -TERM "$ENGINE_PID" 2>/dev/null
        wait "$ENGINE_PID" 2>/dev/null || true
    fi
    # Remove stale socket
    rm -f "$SOCKET_PATH"
    success "Cleanup complete."
}
trap cleanup EXIT INT TERM

# Check engine binary exists
if [ ! -f "$ENGINE_BIN" ]; then
    error "Engine binary not found at $ENGINE_BIN"
    error "Run: cd '$PROJECT_ROOT' && cmake --build build"
    exit 1
fi

# Check editor project exists
if [ ! -f "$EDITOR_DIR/RowlEngine.Editor.csproj" ]; then
    error "Editor project not found at $EDITOR_DIR"
    exit 1
fi

# Remove stale socket from previous run
if [ -S "$SOCKET_PATH" ]; then
    warn "Removing stale socket: $SOCKET_PATH"
    rm -f "$SOCKET_PATH"
fi

# Launch Editor (foreground - blocks until closed)
log "Launching Rowl Engine Editor..."
cd "$EDITOR_DIR"
exec dotnet run --project RowlEngine.Editor.csproj