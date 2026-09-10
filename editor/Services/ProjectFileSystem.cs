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
