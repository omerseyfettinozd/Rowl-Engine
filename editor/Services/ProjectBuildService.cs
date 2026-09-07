using System;
using System.IO;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Owns the standalone release export pipeline.
/// Packages story assets, copies the native rowl_player binary and shared library,
/// and produces cross-platform launcher scripts.
/// </summary>
public static class ProjectBuildService
{
    public static void BuildStandalone(
        string projectRoot,
        string assetsPath,
        string buildOutDir,
        Action<string>? log = null)
    {
        void Log(string message) => log?.Invoke(message);

        Log("\n=======================================================");
        Log("🚀 ROWL ENGINE STANDALONE BUILD PIPELINE BAŞLATILDI");
        Log($"📦 Hedef Çıktı Dizini: {buildOutDir}");
        Log("=======================================================");

        Directory.CreateDirectory(buildOutDir);

        // Step 1: Copy Assets folder
        Log("[BUILD 1/4] 🖼️ Varlıklar (Assets) ve görseller paketleniyor...");
        string outAssets = Path.Combine(buildOutDir, "Assets");
        if (Directory.Exists(assetsPath))
        {
            ProjectFileSystem.CopyDirectory(assetsPath, outAssets);
        }

        // Step 2: Copy native binaries (rowl_player & libRowlEngineCore)
        Log("[BUILD 2/4] ⚙️ Yerel oyun motoru ikilileri (rowl_player & RowlEngineCore) kopyalanıyor...");
        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string rootDir = Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", ".."));

        string[] playerCandidates = {
            Path.Combine(rootDir, "build", "bin", "rowl_player"),
            Path.Combine(rootDir, "build", "bin", "rowl_player.exe"),
            Path.Combine(baseDir, "rowl_player"),
            Path.Combine(baseDir, "rowl_player.exe"),
            Path.Combine(rootDir, "build", "bin", "rowl_engine"),
            Path.Combine(rootDir, "build", "bin", "rowl_engine.exe")
        };

        string[] libCandidates = {
            Path.Combine(rootDir, "build", "lib", "libRowlEngineCore.so"),
            Path.Combine(rootDir, "build", "bin", "RowlEngineCore.dll"),
            Path.Combine(rootDir, "build", "lib", "libRowlEngineCore.dylib"),
            Path.Combine(baseDir, "libRowlEngineCore.so"),
            Path.Combine(baseDir, "RowlEngineCore.dll"),
            Path.Combine(baseDir, "libRowlEngineCore.dylib")
        };

        string? foundPlayer = null;
        foreach (var candidate in playerCandidates)
        {
            if (File.Exists(candidate))
            {
                foundPlayer = candidate;
                break;
            }
        }

        string? foundLib = null;
        foreach (var candidate in libCandidates)
        {
            if (File.Exists(candidate))
            {
                foundLib = candidate;
                break;
            }
        }

        string destExeName = OperatingSystem.IsWindows() ? "RowlGame.exe" : "RowlGame";
        string destPlayerExe = Path.Combine(buildOutDir, destExeName);

        if (foundPlayer != null)
        {
            File.Copy(foundPlayer, destPlayerExe, overwrite: true);
            // Also copy as rowl_player for direct CLI invocation
            string destAltName = OperatingSystem.IsWindows() ? "rowl_player.exe" : "rowl_player";
            File.Copy(foundPlayer, Path.Combine(buildOutDir, destAltName), overwrite: true);

            try
            {
                if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
                {
                    var mode = File.GetUnixFileMode(destPlayerExe);
                    File.SetUnixFileMode(destPlayerExe, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
                    string altPath = Path.Combine(buildOutDir, destAltName);
                    File.SetUnixFileMode(altPath, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
                }
            }
            catch { }
            Log($"  ✅ Standalone runtime kopyalandı: {Path.GetFileName(foundPlayer)} -> {destExeName}");
        }
        else
        {
            Log("  ⚠️ UYARI: rowl_player çalıştırıcısı bulunamadı! Lütfen önce CMake ile projeyi derleyin (cmake --build build).");
        }

        if (foundLib != null)
        {
            string destLibName = Path.GetFileName(foundLib);
            string destLib = Path.Combine(buildOutDir, destLibName);
            File.Copy(foundLib, destLib, overwrite: true);
            Log($"  ✅ Motor paylaşımlı kütüphanesi kopyalandı: {destLibName}");
        }
        else
        {
            Log("  ⚠️ UYARI: RowlEngineCore kütüphanesi bulunamadı!");
        }

        // Step 3: Create launcher scripts (run_game.sh and run_game.bat)
        Log("[BUILD 3/4] 📜 Otomatik Başlatıcılar (run_game.sh & run_game.bat) oluşturuluyor...");
        string shPath = Path.Combine(buildOutDir, "run_game.sh");
        string shContent = "#!/bin/bash\n" +
                           "SCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\n" +
                           "export LD_LIBRARY_PATH=\"$SCRIPT_DIR:$LD_LIBRARY_PATH\"\n" +
                           "cd \"$SCRIPT_DIR\"\n" +
                           "if [ -f \"$SCRIPT_DIR/RowlGame\" ]; then\n" +
                           "    exec \"$SCRIPT_DIR/RowlGame\" \"$@\"\n" +
                           "elif [ -f \"$SCRIPT_DIR/rowl_player\" ]; then\n" +
                           "    exec \"$SCRIPT_DIR/rowl_player\" \"$@\"\n" +
                           "else\n" +
                           "    echo \"Error: Game executable not found in $SCRIPT_DIR\"\n" +
                           "    exit 1\n" +
                           "fi\n";
        File.WriteAllText(shPath, shContent);

        try
        {
            if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
            {
                var mode = File.GetUnixFileMode(shPath);
                File.SetUnixFileMode(shPath, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
            }
        }
        catch { }

        string batPath = Path.Combine(buildOutDir, "run_game.bat");
        string batContent = "@echo off\r\n" +
                            "setlocal\r\n" +
                            "set SCRIPT_DIR=%~dp0\r\n" +
                            "cd /d \"%SCRIPT_DIR%\"\r\n" +
                            "if exist \"%SCRIPT_DIR%RowlGame.exe\" (\r\n" +
                            "    \"%SCRIPT_DIR%RowlGame.exe\" %*\r\n" +
                            ") else if exist \"%SCRIPT_DIR%rowl_player.exe\" (\r\n" +
                            "    \"%SCRIPT_DIR%rowl_player.exe\" %*\r\n" +
                            ") else (\r\n" +
                            "    echo [Error] Game executable not found in %SCRIPT_DIR%!\r\n" +
                            "    pause\r\n" +
                            ")\r\n";
        File.WriteAllText(batPath, batContent);

        // Step 4: Create README instructions
        Log("[BUILD 4/4] 📄 Dağıtım ve çalıştırma kılavuzu (README.txt) ekleniyor...");
        string readmePath = Path.Combine(buildOutDir, "README.txt");
        string readmeContent = "=======================================================\n" +
                               "🎮 ROWL ENGINE — STANDALONE GAME RELEASE\n" +
                               "=======================================================\n\n" +
                               "Oyunu Başlatmak İçin:\n" +
                               "  Linux / macOS : ./run_game.sh veya ./RowlGame\n" +
                               "  Windows       : run_game.bat veya RowlGame.exe\n\n" +
                               "Gereksinimler:\n" +
                               "  - SDL3 kütüphanesi (sistem genelinde veya kütüphane yolunda)\n" +
                               "  - Tüm görsel ve hikaye verileri Assets/ klasöründen yüklenir.\n\n" +
                               "Paket Oluşturulma Tarihi: " + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "\n";
        File.WriteAllText(readmePath, readmeContent);

        Log("\n=======================================================");
        Log("🎉 [BUILD BAŞARILI] Oyun bağımsız dağıtım paketi oluşturuldu!");
        Log($"📁 Konum: {buildOutDir}");
        Log($"▶️ Çalıştırmak için: {shPath}");
        Log("=======================================================\n");
    }
}
