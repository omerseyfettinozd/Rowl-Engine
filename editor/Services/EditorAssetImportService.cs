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
    /// </summary>
    public static string DetermineSubdirectory(string fileNameOrExt)
    {
        string ext = Path.GetExtension(fileNameOrExt).ToLowerInvariant();
        return ext switch
        {
            ".png" or ".jpg" or ".jpeg" or ".bmp" or ".webp" or ".tga" => "images",
            ".json" or ".lua" => "json",
            ".wav" or ".ogg" or ".mp3" => "audio",
            ".ttf" or ".otf" => "fonts",
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
}
