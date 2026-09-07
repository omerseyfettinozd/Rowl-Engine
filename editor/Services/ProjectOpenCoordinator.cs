using System;
using System.IO;

namespace RowlEngine.Editor.Services;

internal sealed record RowlProjectLocation(string RootPath, string GraphFilePath);

/// <summary>
/// Resolves a user-selected folder to a Rowl project and coordinates the
/// reversible parts of a project switch. UI state remains with the view model;
/// this class owns only the transition order and rollback contract.
/// </summary>
internal static class ProjectOpenCoordinator
{
    public static bool TryResolve(string selectedDirectory, out RowlProjectLocation? location)
    {
        location = null;
        string assetsDirectory = Path.Combine(selectedDirectory, "Assets");
        if (Directory.Exists(assetsDirectory))
            return TryCreate(selectedDirectory, assetsDirectory, out location);

        if (Path.GetFileName(selectedDirectory).Equals("Assets", StringComparison.OrdinalIgnoreCase))
            return TryCreate(Path.GetDirectoryName(selectedDirectory) ?? selectedDirectory, selectedDirectory, out location);

        string directGraph = Path.Combine(selectedDirectory, "full_story_graph.json");
        if (!File.Exists(directGraph)) return false;

        location = new RowlProjectLocation(
            Path.GetFullPath(Path.Combine(selectedDirectory, "..", "..")),
            directGraph);
        return true;
    }

    public static bool Switch(
        RowlProjectLocation target,
        string previousRoot,
        string previousProjectPath,
        Action clearAssetCache,
        Action<string, string> setProject,
        Action<string> remountEngine,
        Func<bool> loadGraph,
        Action refreshNodeBitmaps,
        Action refreshAssets)
    {
        clearAssetCache();
        setProject(target.RootPath, target.RootPath);
        remountEngine(target.RootPath);

        if (loadGraph())
        {
            refreshAssets();
            return true;
        }

        setProject(previousRoot, previousProjectPath);
        clearAssetCache();
        refreshNodeBitmaps();
        remountEngine(previousRoot);
        refreshAssets();
        return false;
    }

    private static bool TryCreate(string rootPath, string assetsDirectory, out RowlProjectLocation? location)
    {
        string graphFile = Path.Combine(assetsDirectory, "json", "full_story_graph.json");
        if (!File.Exists(graphFile))
            graphFile = Path.Combine(assetsDirectory, "full_story_graph.json");

        if (!File.Exists(graphFile))
        {
            location = null;
            return false;
        }

        location = new RowlProjectLocation(rootPath, graphFile);
        return true;
    }
}
