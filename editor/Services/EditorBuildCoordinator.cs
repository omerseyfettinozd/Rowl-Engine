using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for coordinating project build pipelines, validation gating,
/// standalone distribution exports, and .rowlpkg package generation.
/// </summary>
public static class EditorBuildCoordinator
{
    /// <summary>
    /// Executes the full standalone build pipeline with validation checks and file persistence.
    /// </summary>
    public static PipelineExecutionResult ExecuteBuildPipeline(
        string projectRoot,
        string assetsPath,
        string buildOutDir,
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong? startNodeId,
        Action persistFiles,
        Action<IReadOnlyList<ProjectValidationIssue>> reportIssues,
        Action<string>? log = null)
    {
        persistFiles();
        var result = ProjectBuildService.ExecuteBuildPipeline(
            projectRoot,
            assetsPath,
            buildOutDir,
            nodes,
            connections,
            startNodeId,
            reportIssues,
            log);

        if (!result.Succeeded && !result.Cancelled)
        {
            log?.Invoke($"⚠️ {result.Message}");
        }

        return result;
    }

    /// <summary>
    /// Validates story graph and builds a standalone game distribution into a timestamped subfolder.
    /// </summary>
    public static async Task<StandaloneBuildResult> BuildStandaloneGameAsync(
        string projectRoot,
        string assetsPath,
        string baseOutDir,
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong? startNodeId,
        Action persistFiles,
        Action<IReadOnlyList<ProjectValidationIssue>> reportIssues,
        Action<string>? log = null,
        Action<string>? reportProgress = null,
        CancellationToken cancellationToken = default)
    {
        persistFiles();
        var validation = ProjectValidationService.Validate(nodes, connections, assetsPath, startNodeId);
        reportIssues(validation);

        if (validation.Any(issue => issue.IsError))
        {
            string errorMsg = "⛔ Build cancelled: fix blocking project validation errors first.";
            log?.Invoke(errorMsg);
            return new StandaloneBuildResult(false, false, baseOutDir, errorMsg);
        }

        string buildFolderName = $"RowlBuild_{DateTime.Now:yyyy-MM-dd_HH-mm}";
        string finalBuildDir = Path.Combine(baseOutDir, buildFolderName);

        var progress = new Progress<string>(message =>
        {
            reportProgress?.Invoke(message);
            log?.Invoke(message);
        });

        var result = await ProjectBuildService.BuildStandaloneAsync(
            projectRoot,
            assetsPath,
            finalBuildDir,
            progress,
            cancellationToken).ConfigureAwait(false);

        if (!result.Succeeded)
        {
            log?.Invoke($"{(result.Cancelled ? "ℹ️" : "⚠️")} {result.Message}");
        }

        return result;
    }

    /// <summary>
    /// Packages project assets into a timestamped .rowlpkg archive inside the chosen output directory.
    /// </summary>
    public static async Task<PackageBuildResult> PackageAssetsAsync(
        string assetsPath,
        string outputDirectory,
        Action<string>? log = null,
        CancellationToken cancellationToken = default)
    {
        Directory.CreateDirectory(outputDirectory);
        string pkgFileName = $"game_data_{DateTime.Now:yyyy-MM-dd_HH-mm}.rowlpkg";
        string outPkg = Path.Combine(outputDirectory, pkgFileName);

        var result = await ProjectBuildService.PackageAssetsAsync(assetsPath, outPkg, log, cancellationToken).ConfigureAwait(false);
        if (result.Succeeded)
        {
            log?.Invoke($"📦 [VFS PAKET] .rowlpkg başarıyla oluşturuldu:\n  📁 Konum: {result.PackagePath}\n{result.Output}");
        }
        else
        {
            log?.Invoke($"⚠️ Paket oluşturma başarısız: {result.Message}");
        }

        return result;
    }
}
