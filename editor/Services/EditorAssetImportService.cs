using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for managing asset file imports, extension categorization,
/// collision checks, and local asset storage in the project directory.
/// Faz 5 Dilim 5: MP3/FLAC/WebP kaynakları reddedilmez; kabul-dönüştürülür —
/// kaynak asla üzerine yazılmaz, çıktı <c>Assets/{audio,images}/</c> altına
/// <c>.ogg/.png</c> + <c>.rowlconv.json</c> sidecar olarak üretilir
/// (<see cref="MediaConverterService"/>). Gerçek destek-dışı formatlar
/// (GIF vb.) hâlâ reddedilir.
/// </summary>
public static class EditorAssetImportService
{
    /// <summary>
    /// Determines the standard asset subdirectory (under Assets/) based on file extension.
    /// Media mapping comes from <see cref="MediaFormatCatalog"/>; genuinely
    /// unsupported formats (GIF etc.) have no subdirectory. Converter sources
    /// (MP3/FLAC/WebP) live under SourceAssets/ and likewise map to none here —
    /// their converted outputs resolve via <see cref="MediaConverterService"/>.
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
    /// Returns the list of imported local relative filenames. Converter
    /// sources are converted (never rejected); see
    /// <see cref="ImportAssetFilesAsync"/> for the async core.
    /// </summary>
    public static List<string> ImportAssetFiles(IEnumerable<string> sourcePaths, string assetsRoot, Action<string>? log = null)
        => ImportAssetFilesAsync(sourcePaths, assetsRoot, null, log).GetAwaiter().GetResult();

    /// <summary>
    /// Async import core: accepted files are copied, converter sources are
    /// converted in place (source only read, never overwritten), genuinely
    /// unsupported files are rejected with an explicit log.
    /// </summary>
    public static async Task<List<string>> ImportAssetFilesAsync(
        IEnumerable<string> sourcePaths,
        string assetsRoot,
        MediaConverterService.ConverterOptions? converterOptions = null,
        Action<string>? log = null,
        CancellationToken cancellationToken = default)
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
            if (MediaConverterService.TryGetConversionTarget(
                    fileName, out string outExt, out string outDir, out string toolName))
            {
                string outputName = Path.ChangeExtension(fileName, outExt);
                // Tek-dosya import: yalın dosya adı → doğası gereği flat
                // (dışarıdan bırakılan dosyanın ağacı yoktur; kural ortaktır,
                // bkz. MediaConverterService.BuildConvertedOutputFullPath).
                string outputFull = MediaConverterService.BuildConvertedOutputFullPath(assetsRoot, outDir, outputName);
                var conversion = await MediaConverterService.ConvertFileAsync(
                    fullPath, outputFull, toolName, converterOptions, log, cancellationToken).ConfigureAwait(false);
                if (conversion.Outcome == MediaConverterService.ConversionOutcome.Failed)
                {
                    log?.Invoke($"❌ Import conversion failed: {conversion.Message}");
                    continue;
                }
                log?.Invoke(conversion.Outcome == MediaConverterService.ConversionOutcome.SkippedUpToDate
                    ? $"⏭️ Import skipped (up to date): {fileName} -> Assets/{outDir}/{outputName}"
                    : $"📥 Imported Asset (converted): {fileName} -> Assets/{outDir}/{outputName}");
                imported.Add($"{outDir}/{outputName}");
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
    /// and returns the local relative filename. WebP sources are accepted via
    /// conversion (PNG + sidecar); genuinely unsupported formats are rejected.
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
        if (MediaConverterService.TryGetConversionTarget(
                fullPath, out string outExt, out string outDir, out string toolName))
        {
            string outputName = Path.ChangeExtension(Path.GetFileName(fullPath), outExt);
            var conversion = MediaConverterService.ConvertFileAsync(
                fullPath, MediaConverterService.BuildConvertedOutputFullPath(assetsRoot, outDir, outputName), toolName, null, log)
                .GetAwaiter().GetResult();
            if (conversion.Outcome == MediaConverterService.ConversionOutcome.Failed)
            {
                log?.Invoke($"❌ Import conversion failed: {conversion.Message}");
                return string.Empty;
            }
            log?.Invoke($"📥 Auto-imported image (converted) '{outputName}' into Assets/{outDir}/");
            return outputName;
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
    /// and returns the local relative filename. MP3/FLAC sources are accepted
    /// via conversion (OGG + sidecar); genuinely unsupported formats are rejected.
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
        if (MediaConverterService.TryGetConversionTarget(
                fullPath, out string outExt, out string outDir, out string toolName))
        {
            string outputName = Path.ChangeExtension(Path.GetFileName(fullPath), outExt);
            var conversion = MediaConverterService.ConvertFileAsync(
                fullPath, MediaConverterService.BuildConvertedOutputFullPath(assetsRoot, outDir, outputName), toolName, null, log)
                .GetAwaiter().GetResult();
            if (conversion.Outcome == MediaConverterService.ConversionOutcome.Failed)
            {
                log?.Invoke($"❌ Import conversion failed: {conversion.Message}");
                return string.Empty;
            }
            log?.Invoke($"📥 Auto-imported audio (converted) '{outputName}' into Assets/{outDir}/");
            return outputName;
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
