using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

public enum BuildDiagnosticSeverity { Info, Warning, Error }
public enum BuildDiagnosticCode
{
    InvalidInput,
    ValidationFailed,
    MissingDependency,
    ProcessStartFailed,
    ToolFailed,
    Cancelled,
    IoFailure,
    CleanupFailed
}

public sealed record BuildDiagnostic(
    BuildDiagnosticCode Code,
    BuildDiagnosticSeverity Severity,
    string Operation,
    string Message,
    string Target,
    int? ExitCode = null,
    string Detail = "");

public sealed record StandaloneBuildResult(bool Succeeded, bool Cancelled, string OutputDirectory, string Message)
{
    public BuildDiagnostic? Diagnostic { get; init; }
}

public sealed record PackageBuildResult(bool Succeeded, bool Cancelled, string PackagePath, string Message, string Output)
{
    public BuildDiagnostic? Diagnostic { get; init; }
}

public sealed record PipelineExecutionResult(bool Succeeded, bool Cancelled, string OutputDirectory, string Message, IReadOnlyList<ProjectValidationIssue> Issues)
{
    public BuildDiagnostic? Diagnostic { get; init; }
}

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
        IProgress<string>? progress, CancellationToken cancellationToken, Action<string>? log = null,
        Action<BuildDiagnostic>? reportDiagnostic = null)
        // Build translates cancellation to a result and removes its staging
        // directory. Do not let Task.Run short-circuit a pre-cancelled token.
        => Task.Run(() => Build(projectRoot, assetsPath, buildOutDir, progress, cancellationToken, log, reportDiagnostic));

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
            log?.Invoke($"[BUILD CHECK] {issue.Message}");

        if (validation.Any(issue => issue.IsError))
        {
            string errorMsg = "Build cancelled: fix blocking project validation errors first.";
            log?.Invoke(errorMsg);
            return new(false, false, buildOutDir, errorMsg, validation)
            {
                Diagnostic = new BuildDiagnostic(
                    BuildDiagnosticCode.ValidationFailed,
                    BuildDiagnosticSeverity.Error,
                    "build_validation",
                    errorMsg,
                    buildOutDir)
            };
        }

        var result = BuildStandalone(projectRoot, assetsPath, buildOutDir, log);
        return new(result.Succeeded, result.Cancelled, result.OutputDirectory, result.Message, validation)
        {
            Diagnostic = result.Diagnostic
        };
    }

    public static PackageBuildResult PackageAssets(string assetsPath, string outputPackagePath, Action<string>? log = null)
        => PackageAssetsAsync(assetsPath, outputPackagePath, log).GetAwaiter().GetResult();

    public static Task<PackageBuildResult> PackageAssetsAsync(
        string assetsPath,
        string outputPackagePath,
        Action<string>? log = null,
        CancellationToken cancellationToken = default,
        Action<BuildDiagnostic>? reportDiagnostic = null)
        => PackageAsync(assetsPath, outputPackagePath, cancellationToken, log, reportDiagnostic);

    private static async Task<PackageBuildResult> PackageAsync(
        string assetsPath,
        string outputPackagePath,
        CancellationToken cancellationToken,
        Action<string>? log,
        Action<BuildDiagnostic>? reportDiagnostic)
    {
        void Report(string msg) => log?.Invoke(msg);
        string fullOutput = outputPackagePath;
        string stagingPackage = string.Empty;
        PackageBuildResult Fail(
            BuildDiagnosticCode code,
            BuildDiagnosticSeverity severity,
            string message,
            string target,
            int? exitCode = null,
            string detail = "",
            string output = "")
        {
            var diagnostic = new BuildDiagnostic(
                code, severity, "package_assets", message, target, exitCode, detail);
            reportDiagnostic?.Invoke(diagnostic);
            return new(false, code == BuildDiagnosticCode.Cancelled, fullOutput, message, output)
            {
                Diagnostic = diagnostic
            };
        }

        if (string.IsNullOrWhiteSpace(assetsPath) || !Directory.Exists(assetsPath))
            return Fail(BuildDiagnosticCode.InvalidInput, BuildDiagnosticSeverity.Error,
                "Assets directory does not exist.", assetsPath);

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            fullOutput = Path.GetFullPath(outputPackagePath);
            string parentDir = Path.GetDirectoryName(fullOutput)
                ?? throw new InvalidOperationException("Package output directory cannot be resolved.");
            Directory.CreateDirectory(parentDir);
            stagingPackage = Path.Combine(parentDir, $".{Path.GetFileName(fullOutput)}.{Guid.NewGuid():N}.tmp");

            string repoRoot = ResolveRepoRoot(assetsPath);
            string tool = Path.Combine(repoRoot, "tools", "package_assets.py");
            if (!File.Exists(tool))
                return Fail(BuildDiagnosticCode.MissingDependency, BuildDiagnosticSeverity.Error,
                    $"Canonical package tool missing: {tool}", tool);

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

            ExternalToolResult processResult = await ExternalToolRunner.RunAsync(
                psi,
                line => Report($"[{(line.Stream == ExternalToolStream.StandardOutput ? "stdout" : "stderr")}] {line.Text}"),
                cancellationToken).ConfigureAwait(false);

            if (processResult.ExitCode != 0 || !File.Exists(stagingPackage))
            {
                string message = $"Package creation failed (exit code {processResult.ExitCode}).";
                return Fail(BuildDiagnosticCode.ToolFailed, BuildDiagnosticSeverity.Error,
                    message, tool, processResult.ExitCode, processResult.StandardError.Trim(),
                    processResult.StandardOutput);
            }

            File.Move(stagingPackage, fullOutput, overwrite: true);
            Report($"Paket [VFS PAKET] .rowlpkg başarıyla oluşturuldu: {fullOutput}");
            return new(true, false, fullOutput, "Package created successfully.", processResult.StandardOutput);
        }
        catch (OperationCanceledException)
        {
            return Fail(BuildDiagnosticCode.Cancelled, BuildDiagnosticSeverity.Info,
                "Package creation cancelled.", fullOutput);
        }
        catch (Exception ex)
        {
            BuildDiagnosticCode code = ex is System.ComponentModel.Win32Exception
                ? BuildDiagnosticCode.ProcessStartFailed
                : BuildDiagnosticCode.IoFailure;
            return Fail(code, BuildDiagnosticSeverity.Error,
                $"Package creation error: {ex.Message}", fullOutput, detail: ex.ToString());
        }
        finally
        {
            if (!string.IsNullOrEmpty(stagingPackage) && File.Exists(stagingPackage))
            {
                try { File.Delete(stagingPackage); }
                catch (Exception cleanupError)
                {
                    reportDiagnostic?.Invoke(new BuildDiagnostic(
                        BuildDiagnosticCode.CleanupFailed,
                        BuildDiagnosticSeverity.Warning,
                        "package_cleanup",
                        cleanupError.Message,
                        stagingPackage,
                        Detail: cleanupError.ToString()));
                }
            }
        }
    }

    private static StandaloneBuildResult Build(string projectRoot, string assetsPath, string buildOutDir,
        IProgress<string>? progress, CancellationToken token, Action<string>? log,
        Action<BuildDiagnostic>? reportDiagnostic)
    {
        void Report(string message) { log?.Invoke(message); progress?.Report(message); }
        string root = Path.GetFullPath(projectRoot);
        string output = Path.GetFullPath(buildOutDir);
        StandaloneBuildResult Fail(
            BuildDiagnosticCode code,
            BuildDiagnosticSeverity severity,
            string operation,
            string message,
            string target,
            int? exitCode = null,
            string detail = "")
        {
            var diagnostic = new BuildDiagnostic(code, severity, operation, message, target, exitCode, detail);
            reportDiagnostic?.Invoke(diagnostic);
            return new(false, code == BuildDiagnosticCode.Cancelled, output, message)
            {
                Diagnostic = diagnostic
            };
        }

        if (!Directory.Exists(assetsPath))
            return Fail(BuildDiagnosticCode.InvalidInput, BuildDiagnosticSeverity.Error,
                "build_standalone", "Assets directory is missing.", assetsPath);
        if (Directory.Exists(output) || File.Exists(output))
            return Fail(BuildDiagnosticCode.InvalidInput, BuildDiagnosticSeverity.Error,
                "build_standalone", "The build output directory already exists.", output);

        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string repoRoot = ResolveRepoRoot(root);
        string[] playerCandidates = { Path.Combine(root, "build", "bin", "rowl_player"), Path.Combine(root, "build", "bin", "rowl_player.exe"), Path.Combine(repoRoot, "build", "bin", "rowl_player"), Path.Combine(repoRoot, "build", "bin", "rowl_player.exe"), Path.Combine(baseDir, "rowl_player"), Path.Combine(baseDir, "rowl_player.exe") };
        string[] libraryCandidates = { Path.Combine(root, "build", "lib", "libRowlEngineCore.so"), Path.Combine(root, "build", "bin", "RowlEngineCore.dll"), Path.Combine(root, "build", "lib", "libRowlEngineCore.dylib"), Path.Combine(repoRoot, "build", "lib", "libRowlEngineCore.so"), Path.Combine(repoRoot, "build", "bin", "RowlEngineCore.dll"), Path.Combine(repoRoot, "build", "lib", "libRowlEngineCore.dylib"), Path.Combine(baseDir, "libRowlEngineCore.so"), Path.Combine(baseDir, "RowlEngineCore.dll"), Path.Combine(baseDir, "libRowlEngineCore.dylib") };
        string? player = playerCandidates.FirstOrDefault(File.Exists);
        string? library = libraryCandidates.FirstOrDefault(File.Exists);
        if (player is null || library is null)
            return Fail(BuildDiagnosticCode.MissingDependency, BuildDiagnosticSeverity.Error,
                "build_standalone",
                "rowl_player or RowlEngineCore is missing; build the native runtime before export. " +
                $"Searched projectRoot='{root}', repoRoot='{repoRoot}', appBase='{baseDir}'.",
                output);

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
                throw new MissingDependencyException("Third-party license inventory is missing: " + noticesSource);
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
            Report($"Build complete: {output}");
            return new(true, false, output, "Build complete.");
        }
        catch (OperationCanceledException)
        {
            return Fail(BuildDiagnosticCode.Cancelled, BuildDiagnosticSeverity.Info,
                "build_standalone", "Build cancelled.", output);
        }
        catch (Exception ex)
        {
            BuildDiagnosticCode code = ex switch
            {
                ToolFailedException => BuildDiagnosticCode.ToolFailed,
                MissingDependencyException => BuildDiagnosticCode.MissingDependency,
                System.ComponentModel.Win32Exception => BuildDiagnosticCode.ProcessStartFailed,
                _ => BuildDiagnosticCode.IoFailure
            };
            int? exitCode = ex is ToolFailedException toolFailure ? toolFailure.ExitCode : null;
            string detail = ex is ToolFailedException failed ? failed.Detail : ex.ToString();
            return Fail(code, BuildDiagnosticSeverity.Error,
                "build_standalone", ex.Message, output, exitCode, detail);
        }
        finally
        {
            if (Directory.Exists(staging))
            {
                try { Directory.Delete(staging, true); }
                catch (Exception cleanupError)
                {
                    reportDiagnostic?.Invoke(new BuildDiagnostic(
                        BuildDiagnosticCode.CleanupFailed,
                        BuildDiagnosticSeverity.Warning,
                        "build_cleanup",
                        cleanupError.Message,
                        staging,
                        Detail: cleanupError.ToString()));
                }
            }
        }
    }

    private static void RunCanonicalPackageTool(string repoRoot, string assetsPath, string packagePath,
        CancellationToken token, Action<string> report)
    {
        string tool = Path.Combine(repoRoot, "tools", "package_assets.py");
        if (!File.Exists(tool)) throw new MissingDependencyException("Canonical package tool is missing: " + tool);
        RunPythonTool(OperatingSystem.IsWindows() ? "python" : "python3", tool,
            new[] { assetsPath, packagePath }, repoRoot, token, report);
    }

    private static void RunReleaseVerifier(string repoRoot, string releaseRoot,
        CancellationToken token, Action<string> report)
    {
        string tool = Path.Combine(repoRoot, "tools", "verify_release_package.py");
        if (!File.Exists(tool)) throw new MissingDependencyException("Release verifier is missing: " + tool);
        RunPythonTool(OperatingSystem.IsWindows() ? "python" : "python3", tool,
            new[] { releaseRoot }, repoRoot, token, report);
    }

    private static void RunPythonTool(string executable, string script, IReadOnlyList<string> arguments, string workingDirectory,
        CancellationToken token, Action<string> report)
    {
        var startInfo = new ProcessStartInfo(executable)
        {
            WorkingDirectory = workingDirectory
        };
        startInfo.ArgumentList.Add(script);
        foreach (string argument in arguments)
            startInfo.ArgumentList.Add(argument);

        ExternalToolResult result = ExternalToolRunner.RunAsync(
            startInfo,
            line => report($"[{(line.Stream == ExternalToolStream.StandardOutput ? "stdout" : "stderr")}] {line.Text}"),
            token).GetAwaiter().GetResult();
        if (result.ExitCode != 0)
            throw new ToolFailedException(
                $"{Path.GetFileName(script)} failed with exit code {result.ExitCode}.",
                result.ExitCode,
                result.StandardError.Trim());
    }

    private sealed class ToolFailedException(string message, int exitCode, string detail) : Exception(message)
    {
        public int ExitCode { get; } = exitCode;
        public string Detail { get; } = detail;
    }

    private sealed class MissingDependencyException(string message) : Exception(message)
    {
    }
}
