#!/usr/bin/env bash
# Rowl Engine - Live Sync Mode (Engine + Editor Hot-Reload)
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export DOTNET_CLI_HOME="$PWD/.dotnet"
export NUGET_PACKAGES="$PWD/.nuget"

echo "==========================================================="
echo "  Rowl Engine - Canlı Senkronizasyon (Engine + Editor)"
echo "==========================================================="
echo "[!] C++ Oyun Motoru IPC Modunda arkaplanda başlatılıyor..."
./build/bin/rowl_engine --ipc-mode --pipe-id rowl_engine_ipc &
ENGINE_PID=$!

# Terminate background engine when this script finishes or editor exits
trap "echo '[!] Editör kapandı. Arkaplandaki motor (PID: $ENGINE_PID) kapatılıyor...'; kill -9 $ENGINE_PID 2>/dev/null" EXIT INT TERM

# Give IPC socket a moment to initialize
sleep 1.0

echo "[!] Görsel Node Editörü açılıyor (Hot-Reload aktif)..."
dotnet run --project editor/RowlEngine.Editor.csproj "$@"
