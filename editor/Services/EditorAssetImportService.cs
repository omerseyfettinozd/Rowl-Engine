using System;
using System.Collections.Generic;
using System.IO;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for managing asset file imports, extension categorization,
/// collision checks, and local asset storage in the project directory.
/// </summary>
public static class EditorAssetImportService
{
    /// <summary>
    /// Determines the standard asset subdirectory (under Assets/) based on file extension.
    /// Media mapping comes from <see cref="MediaFormatCatalog"/>; rejected formats
    /// (MP3/FLAC/WebP and other unsupported media) have no subdirectory.
    /// </summary>
    public static string DetermineSubdirectory(string fileNameOrExt)
    {
        if (MediaFormatCatalog.TryGetAssetSubdirectory(fileNameOrExt, out string mediaDir))
            return mediaDir;
        if (MediaFormatCatalog.RequiresExplicitRejection(fileNameOrExt))
            return string.Empty;
        string ext = Path.GetExtension(fileNameOrExt).ToLowerInvariant();
        return ext switch
        {
            ".json" or ".lua" => "json",
            ".rowlpkg" => "packages",
            _ => ""
        };
    }

    /// <summary>
    /// Imports multiple asset files into the project's Assets directory.
    /// Returns the list of imported local relative filenames.
    /// </summary>
    public static List<string> ImportAssetFiles(IEnumerable<string> sourcePaths, string assetsRoot, Action<string>? log = null)
    {
        var imported = new List<string>();
        if (string.IsNullOrWhiteSpace(assetsRoot) || sourcePaths == null) return imported;

        Directory.CreateDirectory(assetsRoot);

        foreach (var fullPath in sourcePaths)
        {
            if (string.IsNullOrWhiteSpace(fullPath) || !File.Exists(fullPath)) continue;

            string fileName = Path.GetFileName(fullPath);
            if (MediaFormatCatalog.RequiresExplicitRejection(fileName))
            {
                log?.Invoke($"❌ Import rejected: {MediaFormatCatalog.RejectionMessage(fileName)}");
                continue;
            }
            string subDir = DetermineSubdirectory(fileName);
            string targetDir = string.IsNullOrEmpty(subDir)
                ? assetsRoot
                : Path.Combine(assetsRoot, subDir);

            Directory.CreateDirectory(targetDir);
            string destPath = Path.Combine(targetDir, fileName);

            if (!string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
            {
                File.Copy(fullPath, destPath, overwrite: true);
            }

            string relativeLog = string.IsNullOrEmpty(subDir) ? fileName : $"{subDir}/{fileName}";
            log?.Invoke($"📥 Imported Asset: {fileName} -> Assets/{relativeLog}");
            imported.Add(relativeLog);
        }

        return imported;
    }

    /// <summary>
    /// Copies an external image file into Assets/images/ if not already present,
    /// and returns the local relative filename.
    /// </summary>
    public static string ImportImageFile(string fullPath, string assetsRoot, Action<string>? log = null)
    {
        if (string.IsNullOrWhiteSpace(fullPath) || string.IsNullOrWhiteSpace(assetsRoot)) return string.Empty;
        if (!File.Exists(fullPath)) return string.Empty;
        if (MediaFormatCatalog.RequiresExplicitRejection(fullPath))
        {
            log?.Invoke($"❌ Import rejected: {MediaFormatCatalog.RejectionMessage(Path.GetFileName(fullPath))}");
            return string.Empty;
        }

        string imagesDir = Path.Combine(assetsRoot, "images");
        Directory.CreateDirectory(imagesDir);

        string fileName = Path.GetFileName(fullPath);
        string destPath = Path.Combine(imagesDir, fileName);

        if (!string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
        {
            File.Copy(fullPath, destPath, overwrite: true);
        }

        log?.Invoke($"📥 Auto-imported image '{fileName}' into Assets/images/");
        return fileName;
    }

    /// <summary>
    /// Copies an external audio file into Assets/audio/ if not already present,
    /// and returns the local relative filename.
    /// </summary>
    public static string ImportAudioFile(string fullPath, string assetsRoot, Action<string>? log = null)
    {
        if (string.IsNullOrWhiteSpace(fullPath) || string.IsNullOrWhiteSpace(assetsRoot)) return string.Empty;
        if (!File.Exists(fullPath)) return string.Empty;
        if (MediaFormatCatalog.RequiresExplicitRejection(fullPath))
        {
            log?.Invoke($"❌ Import rejected: {MediaFormatCatalog.RejectionMessage(Path.GetFileName(fullPath))}");
            return string.Empty;
        }

        string audioDir = Path.Combine(assetsRoot, "audio");
        Directory.CreateDirectory(audioDir);

        string fileName = Path.GetFileName(fullPath);
        string destPath = Path.Combine(audioDir, fileName);

        if (!string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
        {
            File.Copy(fullPath, destPath, overwrite: true);
        }

        log?.Invoke($"📥 Auto-imported audio '{fileName}' into Assets/audio/");
        return fileName;
    }
}
