using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

public sealed record StandaloneBuildResult(bool Succeeded, bool Cancelled, string OutputDirectory, string Message);
public sealed record PackageBuildResult(bool Succeeded, bool Cancelled, string PackagePath, string Message, string Output);
public sealed record PipelineExecutionResult(bool Succeeded, bool Cancelled, string OutputDirectory, string Message, IReadOnlyList<ProjectValidationIssue> Issues);

/// <summary>Creates complete standalone packages; a partial package is never published.</summary>
public static class ProjectBuildService
{
    public static string ResolveRepoRoot(string hintPath)
    {
        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        // Walk up from the app directory first: fixed depths break under
        // RID/platform-specific output layouts (e.g. bin/x64/Debug/net10.0).
        string? dir = baseDir;
        for (int i = 0; i < 10 && dir is not null; i++)
        {
            if (File.Exists(Path.Combine(dir, "tools", "package_assets.py")))
                return dir;
            dir = Path.GetDirectoryName(dir);
        }
        string[] candidates = {
            hintPath,
            Path.GetFullPath(Path.Combine(hintPath, "..")),
            Path.GetFullPath(Path.Combine(hintPath, "..", "..")),
            baseDir,
            Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", "..")),
            Path.GetFullPath(Path.Combine(baseDir, "..", "..", ".."))
        };
        foreach (var candidate in candidates)
        {
            if (File.Exists(Path.Combine(candidate, "tools", "package_assets.py")))
                return candidate;
        }
        return candidates[4];
    }

    public static StandaloneBuildResult BuildStandalone(string projectRoot, string assetsPath, string buildOutDir, Action<string>? log = null)
        => BuildStandaloneAsync(projectRoot, assetsPath, buildOutDir, null, CancellationToken.None, log).GetAwaiter().GetResult();

    public static Task<StandaloneBuildResult> BuildStandaloneAsync(string projectRoot, string assetsPath, string buildOutDir,
        IProgress<string>? progress, CancellationToken cancellationToken, Action<string>? log = null)
        // Build translates cancellation to a result and removes its staging
        // directory. Do not let Task.Run short-circuit a pre-cancelled token.
        => Task.Run(() => Build(projectRoot, assetsPath, buildOutDir, progress, cancellationToken, log));

    public static PipelineExecutionResult ExecuteBuildPipeline(
        string projectRoot,
        string assetsPath,
        string buildOutDir,
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong? startNodeId,
        Action<IReadOnlyList<ProjectValidationIssue>>? onValidationIssues = null,
        Action<string>? log = null)
    {
        var validation = ProjectValidationService.Validate(nodes, connections, assetsPath, startNodeId);
        onValidationIssues?.Invoke(validation);
        foreach (var issue in validation)
            log?.Invoke($"{(issue.IsError ? "❌" : "⚠️")} [BUILD CHECK] {issue.Message}");

        if (validation.Any(issue => issue.IsError))
        {
            string errorMsg = "Build cancelled: fix blocking project validation errors first.";
            log?.Invoke($"⛔ {errorMsg}");
            return new(false, false, buildOutDir, errorMsg, validation);
        }

        var result = BuildStandalone(projectRoot, assetsPath, buildOutDir, log);
        return new(result.Succeeded, result.Cancelled, result.OutputDirectory, result.Message, validation);
    }

    public static PackageBuildResult PackageAssets(string assetsPath, string outputPackagePath, Action<string>? log = null)
        => Package(assetsPath, outputPackagePath, CancellationToken.None, log);

    public static Task<PackageBuildResult> PackageAssetsAsync(
        string assetsPath,
        string outputPackagePath,
        Action<string>? log = null,
        CancellationToken cancellationToken = default)
        => Task.Run(() => Package(assetsPath, outputPackagePath, cancellationToken, log));

    private static PackageBuildResult Package(
        string assetsPath,
        string outputPackagePath,
        CancellationToken cancellationToken,
        Action<string>? log)
    {
        void Report(string msg) => log?.Invoke(msg);

        if (string.IsNullOrWhiteSpace(assetsPath) || !Directory.Exists(assetsPath))
            return new(false, false, outputPackagePath, "Assets directory does not exist.", string.Empty);

        string fullOutput = Path.GetFullPath(outputPackagePath);
        string parentDir = Path.GetDirectoryName(fullOutput)
            ?? throw new InvalidOperationException("Package output directory cannot be resolved.");
        Directory.CreateDirectory(parentDir);

        string stagingPackage = Path.Combine(parentDir, $".{Path.GetFileName(fullOutput)}.{Guid.NewGuid():N}.tmp");
        try
        {
            cancellationToken.ThrowIfCancellationRequested();

            string repoRoot = ResolveRepoRoot(assetsPath);
            string tool = Path.Combine(repoRoot, "tools", "package_assets.py");
            if (!File.Exists(tool))
                return new(false, false, fullOutput, $"Canonical package tool missing: {tool}", string.Empty);

            var psi = new ProcessStartInfo(OperatingSystem.IsWindows() ? "python" : "python3")
            {
                WorkingDirectory = repoRoot,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            };
            psi.ArgumentList.Add(tool);
            psi.ArgumentList.Add(Path.GetFullPath(assetsPath));
            psi.ArgumentList.Add(stagingPackage);

            using var process = new Process { StartInfo = psi };
            using var registration = cancellationToken.Register(() =>
            {
                try { if (!process.HasExited) process.Kill(true); } catch { }
            });

            if (!process.Start())
                return new(false, false, fullOutput, "Failed to start python packaging process.", string.Empty);

            string stdout = process.StandardOutput.ReadToEnd();
            string stderr = process.StandardError.ReadToEnd();
            process.WaitForExit();
            cancellationToken.ThrowIfCancellationRequested();

            if (process.ExitCode != 0 || !File.Exists(stagingPackage))
            {
                return new(false, false, fullOutput,
                    $"Package creation failed (exit code {process.ExitCode}): {stderr.Trim()}", stdout);
            }

            File.Move(stagingPackage, fullOutput, overwrite: true);
            Report($"📦 [VFS PAKET] .rowlpkg başarıyla oluşturuldu: {fullOutput}");
            return new(true, false, fullOutput, "Package created successfully.", stdout);
        }
        catch (OperationCanceledException)
        {
            return new(false, true, fullOutput, "Package creation cancelled.", string.Empty);
        }
        catch (Exception ex)
        {
            return new(false, false, fullOutput, $"Package creation error: {ex.Message}", string.Empty);
        }
        finally
        {
            if (File.Exists(stagingPackage))
            {
                try { File.Delete(stagingPackage); } catch { }
            }
        }
    }

    private static StandaloneBuildResult Build(string projectRoot, string assetsPath, string buildOutDir,
        IProgress<string>? progress, CancellationToken token, Action<string>? log)
    {
        void Report(string message) { log?.Invoke(message); progress?.Report(message); }
        string root = Path.GetFullPath(projectRoot);
        string output = Path.GetFullPath(buildOutDir);
        if (!Directory.Exists(assetsPath)) return new(false, false, output, "Assets directory is missing.");
        if (Directory.Exists(output) || File.Exists(output)) return new(false, false, output, "The build output directory already exists.");

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string repoRoot = ResolveRepoRoot(root);
        string[] playerCandidates = { Path.Combine(root, "build", "bin", "rowl_player"), Path.Combine(root, "build", "bin", "rowl_player.exe"), Path.Combine(repoRoot, "build", "bin", "rowl_player"), Path.Combine(repoRoot, "build", "bin", "rowl_player.exe"), Path.Combine(baseDir, "rowl_player"), Path.Combine(baseDir, "rowl_player.exe") };
        string[] libraryCandidates = { Path.Combine(root, "build", "lib", "libRowlEngineCore.so"), Path.Combine(root, "build", "bin", "RowlEngineCore.dll"), Path.Combine(root, "build", "lib", "libRowlEngineCore.dylib"), Path.Combine(repoRoot, "build", "lib", "libRowlEngineCore.so"), Path.Combine(repoRoot, "build", "bin", "RowlEngineCore.dll"), Path.Combine(repoRoot, "build", "lib", "libRowlEngineCore.dylib"), Path.Combine(baseDir, "libRowlEngineCore.so"), Path.Combine(baseDir, "RowlEngineCore.dll"), Path.Combine(baseDir, "libRowlEngineCore.dylib") };
        string? player = playerCandidates.FirstOrDefault(File.Exists);
        string? library = libraryCandidates.FirstOrDefault(File.Exists);
        if (player is null || library is null)
            return new(false, false, output,
                "rowl_player or RowlEngineCore is missing; build the native runtime before export. " +
                $"Searched projectRoot='{root}', repoRoot='{repoRoot}', appBase='{baseDir}'.");

        string parent = Path.GetDirectoryName(output) ?? throw new InvalidOperationException("Build parent cannot be resolved.");
        Directory.CreateDirectory(parent);
        string staging = Path.Combine(parent, $".{Path.GetFileName(output)}.{Guid.NewGuid():N}.building");
        try
        {
            token.ThrowIfCancellationRequested(); Directory.CreateDirectory(staging);
            Report("[BUILD 1/4] Assets are being packaged...");
            string packagePath = Path.Combine(staging, "Assets", "packages", "game.rowlpkg");
            RunCanonicalPackageTool(repoRoot, assetsPath, packagePath, token, Report);
            string sourceManifest = Path.Combine(root, "project.rowlproj");
            if (File.Exists(sourceManifest)) File.Copy(sourceManifest, Path.Combine(staging, "project.rowlproj"));
            string noticesSource = Path.Combine(repoRoot, "packaging", "THIRD_PARTY_NOTICES.md");
            if (!File.Exists(noticesSource))
                throw new InvalidOperationException("Third-party license inventory is missing: " + noticesSource);
            File.Copy(noticesSource, Path.Combine(staging, "THIRD_PARTY_NOTICES.md"));
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
            Directory.CreateDirectory(Path.Combine(staging, "mods"));
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(staging, "mods", "README.md"),
                "# Rowl Engine mods\n\nPlace an asset here using its package-relative path to override it. For example, `images/hero.png` overrides `images/hero.png` in `Assets/packages/game.rowlpkg`.\n");
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(staging, "README.txt"),
                "ROWL ENGINE — STANDALONE GAME RELEASE\n\nRun ./run_game.sh on Linux/macOS or run_game.bat on Windows.\n\nAssets/packages/game.rowlpkg is the canonical game content. Put an asset in mods/ with the same relative path to override it.\n");
            RunReleaseVerifier(repoRoot, staging, token, Report);
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

    private static void RunCanonicalPackageTool(string repoRoot, string assetsPath, string packagePath,
        CancellationToken token, Action<string> report)
    {
        string tool = Path.Combine(repoRoot, "tools", "package_assets.py");
        if (!File.Exists(tool)) throw new InvalidOperationException("Canonical package tool is missing: " + tool);
        RunPythonTool(OperatingSystem.IsWindows() ? "python" : "python3", tool,
            $"\"{assetsPath}\" \"{packagePath}\"", repoRoot, token, report);
    }

    private static void RunReleaseVerifier(string repoRoot, string releaseRoot,
        CancellationToken token, Action<string> report)
    {
        string tool = Path.Combine(repoRoot, "tools", "verify_release_package.py");
        if (!File.Exists(tool)) throw new InvalidOperationException("Release verifier is missing: " + tool);
        RunPythonTool(OperatingSystem.IsWindows() ? "python" : "python3", tool,
            $"\"{releaseRoot}\"", repoRoot, token, report);
    }

    private static void RunPythonTool(string executable, string script, string arguments, string workingDirectory,
        CancellationToken token, Action<string> report)
    {
        using var process = new Process
        {
            StartInfo = new ProcessStartInfo(executable, $"\"{script}\" {arguments}")
            {
                WorkingDirectory = workingDirectory,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
                CreateNoWindow = true
            }
        };
        using var registration = token.Register(() => { try { if (!process.HasExited) process.Kill(true); } catch { } });
        if (!process.Start()) throw new InvalidOperationException("Could not start " + Path.GetFileName(script));
        string stdout = process.StandardOutput.ReadToEnd();
        string stderr = process.StandardError.ReadToEnd();
        process.WaitForExit();
        token.ThrowIfCancellationRequested();
        if (!string.IsNullOrWhiteSpace(stdout)) report(stdout.Trim());
        if (process.ExitCode != 0)
            throw new InvalidOperationException($"{Path.GetFileName(script)} failed: {stderr.Trim()}");
    }
}
