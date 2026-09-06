@echo off
REM ===================================================
REM   Rowl Engine - Visual Node Editor (Windows)
REM ===================================================
echo [1/1] Editor baslatiliyor (Avalonia UI .NET)...
dotnet run --project editor\RowlEngine.Editor.csproj %*
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [HATA] Editor calistirilamadi! Lutfen once motoru derleyin:
    echo   cmake -B build
    echo   cmake --build build --config Release
    pause
)
