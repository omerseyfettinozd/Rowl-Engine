#!/usr/bin/env bash
# Rowl Engine - C++ Standalone Motor Başlatma Betiği
echo "==================================================="
echo "  Rowl Engine - C++ Standalone Runtime (Oyun Motoru)"
echo "==================================================="
if [ ! -f "build/bin/rowl_engine" ]; then
    echo "[!] Motor derlenmemiş! Derleme başlatılıyor..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
fi
echo "[1/1] C++ Oyun Motoru Çalıştırılıyor..."
./build/bin/rowl_engine "$@"
