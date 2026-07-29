# 🛠️ CROSS-PLATFORM BUILD MATRIX

> **Objective:** Provide a single source of truth for how to compile, package, and deploy Rowl Engine from source on every supported platform. All commands here are copy-paste ready.

---

## 1. SUPPORTED PLATFORMS & COMPILERS

| Platform | OS | Compiler | Architecture | Output |
| :--- | :--- | :--- | :--- | :--- |
| **Windows Desktop** | Windows 10/11 | MSVC 2022 (v143+) | x64 | `.exe` + optional `.dll` plugins |
| **Linux Desktop** | CachyOS, Ubuntu, Fedora | GCC 13+ or Clang 17+ | x64 | ELF binary |
| **macOS Desktop** | macOS 14+ | Apple Clang 15+ | ARM64 (M-series) | Mach-O app bundle |
| **Android Mobile** | Android 7+ (API 21+) | NDK Clang (via CMake) | ARM64-v8a | `.apk` / `.aab` |
| **iOS Mobile** | iOS 15+ | Xcode 15+ Clang | ARM64 (iPhone/iPad) | `.ipa` |

---

## 2. PREREQUISITES INSTALLATION

### A. Windows (MSVC 2022)
1. Install **Visual Studio 2022** with "Desktop development with C++" workload.
2. Install **CMake** (>= 3.25): `winget install Kitware.CMake`
3. Install **Git**: `winget install Git.Git`
4. Open "x64 Native Tools Command Prompt for VS 2022" and proceed to Build Steps.

### B. Linux (CachyOS / Arch-based)
```bash
# Core build dependencies
sudo pacman -S --needed base-devel cmake ninja git clang sdl3 zstd spdlog nlohmann-json flatbuffers

# Optional: LSP / code completion (CLion / VS Code)
sudo pacman -S --needed clangd
```

### C. macOS
1. Install **Xcode 15+** from App Store.
2. Install CMake: `brew install cmake`
3. Install dependencies: `brew install sdl3 zstd spdlog nlohmann-json flatbuffers`

### D. Android (NDK Cross-Compilation)
1. Download **Android NDK r27+** from `https://developer.android.com/ndk/downloads`
2. Set environment variable:
   ```bash
   export ANDROID_NDK_HOME=$HOME/Android/Sdk/ndk/27.0.11718014
   ```
3. Install **Java JDK 17+** and **Gradle**.

### E. iOS (Xcode)
1. Requires physical Mac with **Xcode 15+**.
2. CMake supports iOS via custom toolchain (see Phase 5 spec).

---

## 3. BUILD STEPS (COMMON)

### A. Generic CMake Configure (All Desktop Platforms)
```bash
git clone https://github.com/rowl-engine/rowl.git
cd rowl/engine

mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

### B. Windows Specific (MSVC + Ninja)
```powershell
cd engine
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -A x64
ninja
```

### C. Linux Specific (CachyOS optimized)
```bash
cd engine
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja
```

### D. Android Cross-Compilation (ARM64-v8a)
```bash
cd engine
mkdir build/android && cd build/android
cmake ../.. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release \
  -GNinja
ninja
```

### E. iOS Cross-Compilation (ARM64)
```bash
cd engine
mkdir build/ios && cd build/ios
cmake ../.. \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DARCHS=arm64 \
  -DCMAKE_BUILD_TYPE=Release \
  -GNinja
ninja
```

---

## 4. EDITOR BUILD (C# AVALONIA)

### Windows / Linux / macOS
1. Install **.NET 8 SDK**: `dotnet --list-sdks` should show `8.0.xxx`.
2. Navigate to `editor/` directory.
3. Restore and build:
   ```bash
   cd editor
   dotnet restore
   dotnet build --configuration Release
   ```
4. Run the editor:
   ```bash
   dotnet run --project RowlEngine.Editor.csproj
   ```

---

## 5. PACKAGING & DISTRIBUTION

### A. Desktop Packaging
```bash
# From repository root
python tools/export_game.py pc \
  --asset-path data/game_data.rowlpkg \
  --output build/RowlGame_Windows_x64.zip
```

### B. Android Packaging (APK)
```bash
cd packaging/android
bash build.sh --asset-path ../../data/game_data.rowlpkg --signing-key release.jks
# Output: build/outputs/apk/game-release.apk
```

### C. iOS Packaging (IPA)
```bash
cd packaging/ios
bash build.sh --asset-path ../../data/game_data.rowlpkg --signing-cert "iPhone Distribution: Your Name"
# Output: build/RowlGame.ipa
```

---

## 6. CONTINUOUS INTEGRATION (GITHUB ACTIONS TEMPLATE)

```yaml
name: Build & Test Matrix

on: [push, pull_request]

jobs:
  build:
    strategy:
      fail-fast: false
      matrix:
        os: [ubuntu-latest, windows-latest, macos-latest]
        build_type: [Release]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - name: Configure CMake
        run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.build_type }}
      - name: Build
        run: cmake --build build --parallel 4
      - name: Test
        run: ctest --test-dir build --output-on-failure
```

---

## 7. TROUBLESHOOTING QUICK REFERENCE

| Issue | Fix |
| :--- | :--- |
| `SDL3 not found` on Linux | Install `libsdl3-dev` via pacman / apt. |
| `zstd` linking error on Arch | Ensure `zstd` package is installed, CMake finds it. |
| `flatc` not found | Install `flatbuffers` package or build from source. |
| Android NDK path error | Verify `$ANDROID_NDK_HOME` points to extracted NDK root. |
| iOS build fails for simulator | Use `-DPLATFORM=SIMULATOR64` CMake flag for x86_64 simulator. |
