#!/usr/bin/env bash
# Rowl Engine - Embedded C++ Library Builder & Launcher
echo "==================================================="
echo "  Rowl Engine - Embedded C++ Library (libRowlEngineCore.so)"
echo "==================================================="
echo "[1/2] C++ Motor Kütüphanesi derleniyor..."
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

echo "[2/2] Editör (Avalonia UI + Embedded C++ Engine) başlatılıyor..."
dotnet run --project editor/RowlEngine.Editor.csproj "$@"
