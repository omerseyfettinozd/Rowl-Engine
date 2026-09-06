@echo off
REM ===================================================
REM   Rowl Engine - Full Build and Run (Windows)
REM ===================================================

echo [1/2] C++ Motoru kontrol ediliyor / derleniyor...
if not exist "build" (
    cmake -B build
)
cmake --build build --config Release
if %ERRORLEVEL% NEQ 0 (
    echo [HATA] C++ motoru derlenemedi! Visual Studio C++ build tools veya MinGW kurulu oldugundan emin olun.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [2/2] Rowl Engine Editor baslatiliyor...
dotnet run --project editor\RowlEngine.Editor.csproj
if %ERRORLEVEL% NEQ 0 (
    pause
)
