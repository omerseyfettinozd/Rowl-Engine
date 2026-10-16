using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace RowlEngine.Editor.Services;

/// <summary>Çakışmada kullanıcı kararı.</summary>
public enum ImportConflictChoice
{
    Overwrite,
    KeepBoth,
    Skip,
}

/// <summary>Çakışma kararı + tümüne uygula bayrağı.</summary>
public sealed record ImportConflictDecision(ImportConflictChoice Choice, bool ApplyToAll = false);

/// <summary>Çakışma bilgisi (hedef göreli yol '/' ayraçlı).</summary>
public sealed record ImportConflict(string SourcePath, string DestinationRelative);

/// <summary>İçe aktarma ilerlemesi.</summary>
public sealed record ImportProgress(int Completed, int Total, string CurrentFile);

/// <summary>
/// Service responsible for managing asset file imports, extension categorization,
/// collision checks, and local asset storage in the project directory.
/// Faz 5 Dilim 5: MP3/FLAC/WebP kaynakları reddedilmez; kabul-dönüştürülür.
/// Faz 8 B1-B3: ilerleme + iptal + çakışma kararı (yıkıcı olmayan varsayılan).
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
    /// Aynı adlı farklı içerik yıkıcı olmayan çözülür (varsayılan KeepBoth).
    /// </summary>
    public static async Task<List<string>> ImportAssetFilesAsync(
        IEnumerable<string> sourcePaths,
        string assetsRoot,
        MediaConverterService.ConverterOptions? converterOptions = null,
        Action<string>? log = null,
        CancellationToken cancellationToken = default,
        IProgress<ImportProgress>? progress = null,
        Func<ImportConflict, CancellationToken, Task<ImportConflictDecision>>? resolveConflict = null)
    {
        var imported = new List<string>();
        if (string.IsNullOrWhiteSpace(assetsRoot) || sourcePaths == null) return imported;

        Directory.CreateDirectory(assetsRoot);

        // Geçerli girdiler (boş olmayan + var olan); total buradan sayılır.
        List<string> valid = sourcePaths
            .Where(p => !string.IsNullOrWhiteSpace(p) && File.Exists(p))
            .ToList();
        int total = valid.Count;
        progress?.Report(new ImportProgress(0, total, string.Empty));

        ImportConflictDecision? sticky = null;

        for (int i = 0; i < valid.Count; i++)
        {
            cancellationToken.ThrowIfCancellationRequested();
            string fullPath = valid[i];

            string fileName = Path.GetFileName(fullPath);
            if (MediaFormatCatalog.RequiresExplicitRejection(fileName))
            {
                log?.Invoke($"Import rejected: {MediaFormatCatalog.RejectionMessage(fileName)}");
                progress?.Report(new ImportProgress(i + 1, total, fileName));
                continue;
            }
            if (MediaConverterService.TryGetConversionTarget(
                    fileName, out string outExt, out string outDir, out string toolName))
            {
                string outputName = Path.ChangeExtension(fileName, outExt);
                string outputFull = MediaConverterService.BuildConvertedOutputFullPath(assetsRoot, outDir, outputName);
                bool existedBefore = File.Exists(outputFull);
                MediaConverterService.ConversionResult conversion;
                try
                {
                    conversion = await MediaConverterService.ConvertFileAsync(
                        fullPath, outputFull, toolName, converterOptions, log, cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException)
                {
                    // Yarım çıktı kalmasın.
                    if (!existedBefore)
                    {
                        try { if (File.Exists(outputFull)) File.Delete(outputFull); } catch (Exception) { }
                    }
                    throw;
                }
                if (conversion.Outcome == MediaConverterService.ConversionOutcome.Failed)
                {
                    log?.Invoke($"Import conversion failed: {conversion.Message}");
                    progress?.Report(new ImportProgress(i + 1, total, fileName));
                    continue;
                }
                log?.Invoke(conversion.Outcome == MediaConverterService.ConversionOutcome.SkippedUpToDate
                    ? $"Import skipped (up to date): {fileName} -> Assets/{outDir}/{outputName}"
                    : $"Imported Asset (converted): {fileName} -> Assets/{outDir}/{outputName}");
                imported.Add($"{outDir}/{outputName}");
                progress?.Report(new ImportProgress(i + 1, total, fileName));
                continue;
            }
            string subDir = DetermineSubdirectory(fileName);
            string targetDir = string.IsNullOrEmpty(subDir)
                ? assetsRoot
                : Path.Combine(assetsRoot, subDir);

            Directory.CreateDirectory(targetDir);
            string destPath = Path.Combine(targetDir, fileName);
            string relativeLog = string.IsNullOrEmpty(subDir) ? fileName : $"{subDir}/{fileName}";

            // Kaynak = hedef ise dokunma (eski davranış).
            if (string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
            {
                log?.Invoke($"Imported Asset: {fileName} -> Assets/{relativeLog}");
                imported.Add(relativeLog);
                progress?.Report(new ImportProgress(i + 1, total, fileName));
                continue;
            }

            if (!File.Exists(destPath))
            {
                CopyFileAtomic(fullPath, destPath, cancellationToken);
                log?.Invoke($"Imported Asset: {fileName} -> Assets/{relativeLog}");
                imported.Add(relativeLog);
                progress?.Report(new ImportProgress(i + 1, total, fileName));
                continue;
            }

            if (FilesHaveSameContent(fullPath, destPath))
            {
                log?.Invoke($"Import skipped (identical): {fileName} -> Assets/{relativeLog}");
                imported.Add(relativeLog);
                progress?.Report(new ImportProgress(i + 1, total, fileName));
                continue;
            }

            // Farklı içerik: karar sor (null ise KeepBoth).
            var conflict = new ImportConflict(fullPath, relativeLog);
            ImportConflictDecision decision;
            if (sticky is not null)
            {
                decision = sticky;
            }
            else if (resolveConflict is null)
            {
                decision = new ImportConflictDecision(ImportConflictChoice.KeepBoth);
            }
            else
            {
                decision = await resolveConflict(conflict, cancellationToken).ConfigureAwait(false);
            }
            if (decision.ApplyToAll)
                sticky = decision;

            switch (decision.Choice)
            {
                case ImportConflictChoice.Overwrite:
                    CopyFileAtomic(fullPath, destPath, cancellationToken);
                    log?.Invoke($"Imported Asset: {fileName} -> Assets/{relativeLog}");
                    imported.Add(relativeLog);
                    break;
                case ImportConflictChoice.KeepBoth:
                    string newName = UniqueName(targetDir, fileName);
                    string newDest = Path.Combine(targetDir, newName);
                    CopyFileAtomic(fullPath, newDest, cancellationToken);
                    string newRelative = string.IsNullOrEmpty(subDir) ? newName : $"{subDir}/{newName}";
                    log?.Invoke($"Imported Asset: {fileName} -> Assets/{newRelative}");
                    imported.Add(newRelative);
                    break;
                default:
                    log?.Invoke($"Import skipped (kept existing): {fileName} -> Assets/{relativeLog}");
                    break;
            }
            progress?.Report(new ImportProgress(i + 1, total, fileName));
        }

        return imported;
    }

    /// <summary>İlk boş adı döndürür: a.png → a (2).png → a (3).png …</summary>
    public static string UniqueName(string directory, string fileName)
    {
        if (string.IsNullOrEmpty(fileName))
            return fileName;
        string ext = Path.GetExtension(fileName);
        string baseName = Path.GetFileNameWithoutExtension(fileName);
        string root = baseName;
        int start = 2;
        // "ad (N)" eki varsa köke in, sıradakinden başla.
        if (baseName.EndsWith(")", StringComparison.Ordinal))
        {
            int open = baseName.LastIndexOf(" (", StringComparison.Ordinal);
            if (open >= 0 && open + 2 < baseName.Length)
            {
                string numPart = baseName.Substring(open + 2, baseName.Length - open - 3);
                if (int.TryParse(numPart, out int n) && n >= 1)
                {
                    root = baseName.Substring(0, open);
                    start = n + 1;
                }
            }
        }
        string candidate = fileName;
        if (!File.Exists(Path.Combine(directory, candidate)))
            return candidate;
        for (int k = start; ; k++)
        {
            candidate = $"{root} ({k}){ext}";
            if (!File.Exists(Path.Combine(directory, candidate)))
                return candidate;
        }
    }

    /// <summary>Önce uzunluk, sonra akışla bayt karşılaştırma.</summary>
    public static bool FilesHaveSameContent(string a, string b)
    {
        try
        {
            var infoA = new FileInfo(a);
            var infoB = new FileInfo(b);
            if (!infoA.Exists || !infoB.Exists)
                return false;
            if (infoA.Length != infoB.Length)
                return false;
            if (infoA.Length == 0)
                return true;
            const int bufSize = 81920;
            using var streamA = File.OpenRead(a);
            using var streamB = File.OpenRead(b);
            var bufA = new byte[bufSize];
            var bufB = new byte[bufSize];
            while (true)
            {
                int readA = streamA.Read(bufA, 0, bufSize);
                int readB = streamB.Read(bufB, 0, bufSize);
                if (readA != readB)
                    return false;
                if (readA == 0)
                    return true;
                for (int i = 0; i < readA; i++)
                {
                    if (bufA[i] != bufB[i])
                        return false;
                }
            }
        }
        catch (Exception)
        {
            return false;
        }
    }

    /// <summary>Atomik kopya: geçici dosyaya yaz, sonra taşı.</summary>
    private static void CopyFileAtomic(string sourceFullPath, string destFullPath, CancellationToken cancellationToken)
    {
        string? dir = Path.GetDirectoryName(destFullPath);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);
        string fileName = Path.GetFileName(destFullPath);
        string tempPath = Path.Combine(dir ?? ".", "." + fileName + ".rowlimport.part");
        try
        {
            File.Copy(sourceFullPath, tempPath, overwrite: true);
            cancellationToken.ThrowIfCancellationRequested();
            File.Move(tempPath, destFullPath, overwrite: true);
        }
        catch
        {
            try { if (File.Exists(tempPath)) File.Delete(tempPath); } catch (Exception) { }
            throw;
        }
    }

    /// <summary>
    /// Copies an external image file into Assets/images/ if not already present,
    /// and returns the local relative filename. WebP sources are accepted via
    /// conversion (PNG + sidecar); genuinely unsupported formats are rejected.
    /// Aynı içerik mevcut adı döndürür, farklı içerik KeepBoth yapar (asla ezmez).
    /// </summary>
    public static string ImportImageFile(string fullPath, string assetsRoot, Action<string>? log = null)
    {
        if (string.IsNullOrWhiteSpace(fullPath) || string.IsNullOrWhiteSpace(assetsRoot)) return string.Empty;
        if (!File.Exists(fullPath)) return string.Empty;
        if (MediaFormatCatalog.RequiresExplicitRejection(fullPath))
        {
            log?.Invoke($"Import rejected: {MediaFormatCatalog.RejectionMessage(Path.GetFileName(fullPath))}");
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
                log?.Invoke($"Import conversion failed: {conversion.Message}");
                return string.Empty;
            }
            log?.Invoke($"Auto-imported image (converted) '{outputName}' into Assets/{outDir}/");
            return outputName;
        }

        string imagesDir = Path.Combine(assetsRoot, "images");
        Directory.CreateDirectory(imagesDir);

        string fileName = Path.GetFileName(fullPath);
        string destPath = Path.Combine(imagesDir, fileName);

        if (string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
        {
            log?.Invoke($"Auto-imported image '{fileName}' into Assets/images/");
            return fileName;
        }
        if (!File.Exists(destPath))
        {
            CopyFileAtomic(fullPath, destPath, CancellationToken.None);
            log?.Invoke($"Auto-imported image '{fileName}' into Assets/images/");
            return fileName;
        }
        if (FilesHaveSameContent(fullPath, destPath))
        {
            log?.Invoke($"Import skipped (identical): {fileName} -> Assets/images/{fileName}");
            return fileName;
        }
        string newName = UniqueName(imagesDir, fileName);
        CopyFileAtomic(fullPath, Path.Combine(imagesDir, newName), CancellationToken.None);
        log?.Invoke($"Imported Asset: {fileName} -> Assets/images/{newName}");
        return newName;
    }

    /// <summary>
    /// Copies an external audio file into Assets/audio/ if not already present,
    /// and returns the local relative filename. MP3/FLAC sources are accepted
    /// via conversion (OGG + sidecar); genuinely unsupported formats are rejected.
    /// Aynı içerik mevcut adı döndürür, farklı içerik KeepBoth yapar (asla ezmez).
    /// </summary>
    public static string ImportAudioFile(string fullPath, string assetsRoot, Action<string>? log = null)
    {
        if (string.IsNullOrWhiteSpace(fullPath) || string.IsNullOrWhiteSpace(assetsRoot)) return string.Empty;
        if (!File.Exists(fullPath)) return string.Empty;
        if (MediaFormatCatalog.RequiresExplicitRejection(fullPath))
        {
            log?.Invoke($"Import rejected: {MediaFormatCatalog.RejectionMessage(Path.GetFileName(fullPath))}");
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
                log?.Invoke($"Import conversion failed: {conversion.Message}");
                return string.Empty;
            }
            log?.Invoke($"Auto-imported audio (converted) '{outputName}' into Assets/{outDir}/");
            return outputName;
        }

        string audioDir = Path.Combine(assetsRoot, "audio");
        Directory.CreateDirectory(audioDir);

        string fileName = Path.GetFileName(fullPath);
        string destPath = Path.Combine(audioDir, fileName);

        if (string.Equals(Path.GetFullPath(fullPath), Path.GetFullPath(destPath), StringComparison.OrdinalIgnoreCase))
        {
            log?.Invoke($"Auto-imported audio '{fileName}' into Assets/audio/");
            return fileName;
        }
        if (!File.Exists(destPath))
        {
            CopyFileAtomic(fullPath, destPath, CancellationToken.None);
            log?.Invoke($"Auto-imported audio '{fileName}' into Assets/audio/");
            return fileName;
        }
        if (FilesHaveSameContent(fullPath, destPath))
        {
            log?.Invoke($"Import skipped (identical): {fileName} -> Assets/audio/{fileName}");
            return fileName;
        }
        string newName = UniqueName(audioDir, fileName);
        CopyFileAtomic(fullPath, Path.Combine(audioDir, newName), CancellationToken.None);
        log?.Invoke($"Imported Asset: {fileName} -> Assets/audio/{newName}");
        return newName;
    }
}
