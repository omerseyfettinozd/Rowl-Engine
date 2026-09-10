using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Services;

public sealed record StandaloneBuildResult(bool Succeeded, bool Cancelled, string OutputDirectory, string Message);

/// <summary>Creates complete standalone packages; a partial package is never published.</summary>
public static class ProjectBuildService
{
    public static StandaloneBuildResult BuildStandalone(string projectRoot, string assetsPath, string buildOutDir, Action<string>? log = null)
        => BuildStandaloneAsync(projectRoot, assetsPath, buildOutDir, null, CancellationToken.None, log).GetAwaiter().GetResult();

    public static Task<StandaloneBuildResult> BuildStandaloneAsync(string projectRoot, string assetsPath, string buildOutDir,
        IProgress<string>? progress, CancellationToken cancellationToken, Action<string>? log = null)
        => Task.Run(() => Build(projectRoot, assetsPath, buildOutDir, progress, cancellationToken, log), cancellationToken);

    private static StandaloneBuildResult Build(string projectRoot, string assetsPath, string buildOutDir,
        IProgress<string>? progress, CancellationToken token, Action<string>? log)
    {
        void Report(string message) { log?.Invoke(message); progress?.Report(message); }
        string root = Path.GetFullPath(projectRoot);
        string output = Path.GetFullPath(buildOutDir);
        if (!Directory.Exists(assetsPath)) return new(false, false, output, "Assets directory is missing.");
        if (Directory.Exists(output) || File.Exists(output)) return new(false, false, output, "The build output directory already exists.");

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string repoRoot = Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", ".."));
        string[] playerCandidates = { Path.Combine(root, "build", "bin", "rowl_player"), Path.Combine(root, "build", "bin", "rowl_player.exe"), Path.Combine(repoRoot, "build", "bin", "rowl_player"), Path.Combine(repoRoot, "build", "bin", "rowl_player.exe"), Path.Combine(baseDir, "rowl_player"), Path.Combine(baseDir, "rowl_player.exe") };
        string[] libraryCandidates = { Path.Combine(root, "build", "lib", "libRowlEngineCore.so"), Path.Combine(root, "build", "bin", "RowlEngineCore.dll"), Path.Combine(root, "build", "lib", "libRowlEngineCore.dylib"), Path.Combine(repoRoot, "build", "lib", "libRowlEngineCore.so"), Path.Combine(repoRoot, "build", "bin", "RowlEngineCore.dll"), Path.Combine(repoRoot, "build", "lib", "libRowlEngineCore.dylib"), Path.Combine(baseDir, "libRowlEngineCore.so"), Path.Combine(baseDir, "RowlEngineCore.dll"), Path.Combine(baseDir, "libRowlEngineCore.dylib") };
        string? player = playerCandidates.FirstOrDefault(File.Exists);
        string? library = libraryCandidates.FirstOrDefault(File.Exists);
        if (player is null || library is null)
            return new(false, false, output, "rowl_player or RowlEngineCore is missing; build the native runtime before export.");

        string parent = Path.GetDirectoryName(output) ?? throw new InvalidOperationException("Build parent cannot be resolved.");
        Directory.CreateDirectory(parent);
        string staging = Path.Combine(parent, $".{Path.GetFileName(output)}.{Guid.NewGuid():N}.building");
        try
        {
            token.ThrowIfCancellationRequested(); Directory.CreateDirectory(staging);
            Report("[BUILD 1/4] Assets are being packaged...");
            CopyDirectoryCancellable(assetsPath, Path.Combine(staging, "Assets"), token);
            string sourceManifest = Path.Combine(root, "project.rowlproj");
            if (File.Exists(sourceManifest)) File.Copy(sourceManifest, Path.Combine(staging, "project.rowlproj"));
            token.ThrowIfCancellationRequested();

            Report("[BUILD 2/4] Native runtime is being copied...");
            string exe = OperatingSystem.IsWindows() ? "RowlGame.exe" : "RowlGame";
            string targetPlayer = Path.Combine(staging, exe);
            File.Copy(player, targetPlayer);
            File.Copy(library, Path.Combine(staging, Path.GetFileName(library)));
            if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS())
            {
                var mode = File.GetUnixFileMode(targetPlayer);
                File.SetUnixFileMode(targetPlayer, mode | UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute);
            }
            token.ThrowIfCancellationRequested();

            Report("[BUILD 3/4] Launchers are being created...");
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(staging, "run_game.sh"), "#!/bin/bash\nSCRIPT_DIR=\"$(cd \"$(dirname \"${BASH_SOURCE[0]}\")\" && pwd)\"\nexport LD_LIBRARY_PATH=\"$SCRIPT_DIR:$LD_LIBRARY_PATH\"\nexec \"$SCRIPT_DIR/RowlGame\" \"$@\"\n");
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(staging, "run_game.bat"), "@echo off\r\ncd /d \"%~dp0\"\r\nRowlGame.exe %*\r\n");
            if (OperatingSystem.IsLinux() || OperatingSystem.IsMacOS()) File.SetUnixFileMode(Path.Combine(staging, "run_game.sh"), UnixFileMode.UserRead | UnixFileMode.UserWrite | UnixFileMode.UserExecute | UnixFileMode.GroupRead | UnixFileMode.GroupExecute | UnixFileMode.OtherRead | UnixFileMode.OtherExecute);
            token.ThrowIfCancellationRequested();

            Report("[BUILD 4/4] Release manifest is being verified...");
            if (!File.Exists(Path.Combine(staging, "Assets", "full_story_graph.json")) && !File.Exists(Path.Combine(staging, "Assets", "json", "full_story_graph.json")))
                throw new InvalidOperationException("The release package has no story graph.");
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(staging, "README.txt"),
                "ROWL ENGINE — STANDALONE GAME RELEASE\n\nRun ./run_game.sh on Linux/macOS or run_game.bat on Windows.\n");
            Directory.Move(staging, output);
            Report($"✅ Build complete: {output}");
            return new(true, false, output, "Build complete.");
        }
        catch (OperationCanceledException)
        {
            return new(false, true, output, "Build cancelled.");
        }
        catch (Exception ex)
        {
            return new(false, false, output, ex.Message);
        }
        finally
        {
            if (Directory.Exists(staging)) { try { Directory.Delete(staging, true); } catch { } }
        }
    }

    private static void CopyDirectoryCancellable(string source, string target, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        Directory.CreateDirectory(target);
        foreach (var file in Directory.GetFiles(source))
        {
            token.ThrowIfCancellationRequested();
            if (new FileInfo(file).LinkTarget is null)
                File.Copy(file, Path.Combine(target, Path.GetFileName(file)), true);
        }
        foreach (var child in Directory.GetDirectories(source))
        {
            token.ThrowIfCancellationRequested();
            if (new DirectoryInfo(child).LinkTarget is null)
                CopyDirectoryCancellable(child, Path.Combine(target, Path.GetFileName(child)), token);
        }
    }
}
