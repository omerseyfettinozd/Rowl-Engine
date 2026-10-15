using System;
using System.Globalization;
using System.IO;
using RowlEngine.Editor.Tools.RowlEye.Targets;

namespace RowlEngine.Editor.Tools.RowlEye.Goldens;

/// <summary>
/// Manages baseline golden snapshots for regression testing.
/// </summary>
public sealed class RowlEyeGoldenManager
{
    public string GoldensDirectory { get; }

    public RowlEyeGoldenManager(string? goldensDirectory = null)
    {
        GoldensDirectory = !string.IsNullOrWhiteSpace(goldensDirectory)
            ? Path.GetFullPath(goldensDirectory)
            : Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "__goldens__");

        Directory.CreateDirectory(GoldensDirectory);
    }

    public string GetGoldenPath(RowlEyeTarget target, string theme, double scale)
    {
        string scaleStr = scale.ToString("0.##", CultureInfo.InvariantCulture);
        return Path.Combine(GoldensDirectory, $"{target}_{theme}@{scaleStr}x.png");
    }

    public bool HasGolden(RowlEyeTarget target, string theme, double scale)
    {
        return File.Exists(GetGoldenPath(target, theme, scale));
    }

    public void UpdateGolden(
        RowlEyeTarget target,
        string theme,
        double scale,
        string sourcePngPath,
        string? sourceManifestPath = null,
        string? sourceVmPath = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sourcePngPath);
        if (!File.Exists(sourcePngPath))
            throw new FileNotFoundException("Source snapshot PNG not found", sourcePngPath);

        string destPng = GetGoldenPath(target, theme, scale);
        string? dir = Path.GetDirectoryName(destPng);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);

        // Backup existing golden if present
        if (File.Exists(destPng))
        {
            string bakPath = destPng + ".bak";
            File.Copy(destPng, bakPath, overwrite: true);
        }

        File.Copy(sourcePngPath, destPng, overwrite: true);

        string baseName = $"{target}_{theme}";
        if (!string.IsNullOrEmpty(sourceManifestPath) && File.Exists(sourceManifestPath))
        {
            string destManifest = Path.Combine(GoldensDirectory, $"{baseName}.manifest.json");
            File.Copy(sourceManifestPath, destManifest, overwrite: true);
        }

        if (!string.IsNullOrEmpty(sourceVmPath) && File.Exists(sourceVmPath))
        {
            string destVm = Path.Combine(GoldensDirectory, $"{baseName}.viewmodel.json");
            File.Copy(sourceVmPath, destVm, overwrite: true);
        }
    }
}
