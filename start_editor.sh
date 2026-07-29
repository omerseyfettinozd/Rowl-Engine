#!/usr/bin/env bash
# Rowl Engine - Visual Node Editor Başlatma Betiği
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
export DOTNET_CLI_HOME="$PWD/.dotnet"
export NUGET_PACKAGES="$PWD/.nuget"

echo "==================================================="
echo "  Rowl Engine - Görsel Düğüm (Node) Editörü"
echo "==================================================="
echo "[1/1] Editor başlatılıyor (Avalonia UI .NET)..."
dotnet run --project editor/RowlEngine.Editor.csproj "$@"
