using System;
using System.IO;

namespace RowlEngine.Editor.Services;

/// <summary>
/// File-system primitives used by project save and build workflows. These are
/// intentionally UI-free so callers can test persistence without Avalonia.
/// </summary>
internal static class ProjectFileSystem
{
    public static bool IsSameOrDescendant(string candidatePath, string rootPath)
    {
        string candidate = Path.GetFullPath(candidatePath)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string root = Path.GetFullPath(rootPath)
            .TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        return string.Equals(candidate, root, StringComparison.OrdinalIgnoreCase) ||
               candidate.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    public static void CopyDirectory(string sourceDirectory, string targetDirectory)
    {
        if (!Directory.Exists(sourceDirectory)) return;
        Directory.CreateDirectory(targetDirectory);

        foreach (string file in Directory.GetFiles(sourceDirectory))
        {
            var info = new FileInfo(file);
            if (info.LinkTarget is not null) continue;
            File.Copy(file, Path.Combine(targetDirectory, info.Name), overwrite: true);
        }

        foreach (string subDirectory in Directory.GetDirectories(sourceDirectory))
        {
            var info = new DirectoryInfo(subDirectory);
            if (info.LinkTarget is not null) continue;
            CopyDirectory(subDirectory, Path.Combine(targetDirectory, info.Name));
        }
    }

    /// <summary>
    /// Walks up from a start directory looking for the canonical project root
    /// (a directory containing both Assets/ and editor/ or CMakeLists.txt).
    /// Prefers the highest match; falls back to any Assets/ ancestor.
    /// </summary>
    public static string ResolveProjectRootFrom(string startDirectory)
    {
        string dir = startDirectory;
        // Walk up to 6 levels looking for the canonical project root.
        // Strategy: prefer the parent that contains BOTH Assets/ AND editor/.
        // editor/ itself may also have an Assets/ stub, so skip up if Assets/
        // appears inside editor/ sub-tree.
        string? best = null;
        // Windows RID/platform output adds an extra x64 directory
        // (Tests/bin/x64/Debug/netX), so six parents stop at editor/.
        // Keep the search bounded while allowing the repository root to
        // be reached from both portable and platform-specific layouts.
        for (int i = 0; i < 12; i++)
        {
            bool hasAssets = Directory.Exists(Path.Combine(dir, "Assets"));
            bool hasEditor = Directory.Exists(Path.Combine(dir, "editor")) ||
                             File.Exists(Path.Combine(dir, "CMakeLists.txt"));
            // Prefer the directory that has BOTH Assets and editor/ or CMakeLists.txt
            if (hasAssets && hasEditor)
            {
                best = dir;
                // Keep going up — parent may also qualify (repo root is the highest match)
            }

            var parent = Directory.GetParent(dir);
            if (parent == null) break;
            dir = parent.FullName;
        }
        // Fallback: any dir with Assets/ found along the way
        if (best == null)
        {
            dir = startDirectory;
            for (int i = 0; i < 12; i++)
            {
                if (Directory.Exists(Path.Combine(dir, "Assets")))
                    return dir;
                var parent = Directory.GetParent(dir);
                if (parent == null) break;
                dir = parent.FullName;
            }
        }
        return best ?? startDirectory;
    }

    /// <summary>
    /// Resolves the default standalone build output directory by walking up
    /// from the executing assembly location to the repository root.
    /// </summary>
    public static string GetDefaultBuildDirectory()
    {
        string baseDir = AppDomain.CurrentDomain.BaseDirectory;
        string rootDir = Path.GetFullPath(Path.Combine(baseDir, "..", "..", "..", ".."));
        return Path.Combine(rootDir, "Builds", "Standalone_PC");
    }

    public static void WriteAllTextAtomically(string filePath, string content)
    {
        string directory = Path.GetDirectoryName(filePath)
            ?? throw new InvalidOperationException($"Dosya dizini çözümlenemedi: {filePath}");
        Directory.CreateDirectory(directory);

        string temporaryPath = Path.Combine(directory, $".{Path.GetFileName(filePath)}.{Guid.NewGuid():N}.tmp");
        try
        {
            File.WriteAllText(temporaryPath, content);
            File.Move(temporaryPath, filePath, overwrite: true);
        }
        finally
        {
            if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
        }
    }
}
