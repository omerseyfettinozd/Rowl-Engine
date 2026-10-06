using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading;

namespace RowlEngine.Editor.Services;

public sealed record SaveAsResult(bool Succeeded, string TargetDirectory, string Message)
{
    public bool Cancelled { get; init; }
}

/// <summary>
/// Copies the authoring project to a sibling staging directory, then publishes
/// it with a directory rename. Existing destinations are never merged or replaced.
/// </summary>
public static class ProjectSaveAsCoordinator
{
    public static SaveAsResult SaveProjectCopy(
        string sourceProjectRoot,
        string targetDirectory,
        int nodeCount,
        ulong startNodeId,
        Func<bool> saveSourceProject,
        CancellationToken cancellationToken = default,
        Action<string>? reportCopiedFile = null)
    {
        if (string.IsNullOrWhiteSpace(targetDirectory))
            return new(false, targetDirectory, "Hedef klasör belirtilmedi.");

        string source = Path.TrimEndingDirectorySeparator(Path.GetFullPath(sourceProjectRoot));
        string target = Path.TrimEndingDirectorySeparator(Path.GetFullPath(targetDirectory));
        if (ProjectFileSystem.IsSameOrDescendant(target, source) ||
            ProjectFileSystem.IsSameOrDescendant(source, target))
            throw new InvalidOperationException("Farklı Kaydet hedefi açık projenin kendisi, üst veya alt klasörü olamaz.");

        string? staging = null;
        var result = new SaveAsResult(false, targetDirectory, "Proje kopyalanamadı.");
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            RequireUnlinkedAncestors(target);
            if (Path.Exists(target))
                throw new IOException("Hedef zaten mevcut; farklı kaydetme için yeni bir klasör seçin.");
            RequireUnlinked(source);
            if (!Directory.Exists(Path.Combine(source, "Assets")))
                throw new DirectoryNotFoundException("Kaynak projenin Assets klasörü bulunamadı.");

            if (!saveSourceProject())
                return new(false, targetDirectory, "Kaynak proje kaydedilemedi; farklı kaydetme durduruldu.");
            cancellationToken.ThrowIfCancellationRequested();

            string parent = Path.GetDirectoryName(target)
                ?? throw new IOException("Hedefin üst klasörü çözümlenemedi.");
            Directory.CreateDirectory(parent);
            staging = Path.Combine(parent, $".{Path.GetFileName(target)}.{Guid.NewGuid():N}.saving");
            Directory.CreateDirectory(staging);
            CopyTree(Path.Combine(source, "Assets"), Path.Combine(staging, "Assets"), cancellationToken, reportCopiedFile);
            string raw = Path.Combine(source, "SourceAssets");
            if (Path.Exists(raw))
                CopyTree(raw, Path.Combine(staging, "SourceAssets"), cancellationToken, reportCopiedFile);

            // Project-local authoring data lives in Assets/ and SourceAssets/.
            // Preserve sample documentation/acceptance metadata without copying
            // repository code, build products, Git state or user runtime saves.
            foreach (string name in new[] { "README.md", "README.txt", "golden_project.json" })
            {
                string file = Path.Combine(source, name);
                if (Path.Exists(file)) CopyFile(file, Path.Combine(staging, name), cancellationToken, reportCopiedFile);
            }

            string manifestPath = Path.Combine(source, "project.rowlproj");
            JsonObject manifest = new();
            if (Path.Exists(manifestPath))
            {
                RequireUnlinked(manifestPath);
                manifest = JsonNode.Parse(File.ReadAllText(manifestPath)) as JsonObject
                    ?? throw new InvalidDataException("Proje manifesti JSON nesnesi olmalı.");
            }
            manifest["name"] ??= "Rowl Engine Project";
            manifest["version"] ??= "1.0.0";
            manifest["engineVersion"] ??= "1.0.0";
            manifest["savedAt"] = DateTime.UtcNow.ToString("o");
            manifest["nodeCount"] = nodeCount;
            manifest["startNodeId"] = startNodeId;
            manifest["virtualResolution"] ??= JsonSerializer.SerializeToNode(new { width = 1920, height = 1080 });
            ProjectFileSystem.WriteAllTextAtomically(Path.Combine(staging, "project.rowlproj"),
                manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            if (!File.Exists(Path.Combine(staging, "Assets", "json", "full_story_graph.json")))
                throw new InvalidDataException("Kopyada kanonik hikâye grafiği bulunamadı.");

            cancellationToken.ThrowIfCancellationRequested();
            // A target created concurrently also makes Move fail; it is never
            // deleted or overwritten. Cancellation after this commit is success.
            Directory.Move(staging, target);
            staging = null;
            result = new(true, targetDirectory, "Proje başarıyla kopyalandı.");
        }
        catch (OperationCanceledException)
        {
            result = new(false, targetDirectory, "Farklı kaydetme iptal edildi.") { Cancelled = true };
        }
        catch (Exception error)
        {
            result = new(false, targetDirectory, $"Farklı kaydetme başarısız: {error.Message}");
        }
        finally
        {
            if (staging != null && Directory.Exists(staging))
            {
                try { Directory.Delete(staging, recursive: true); }
                catch (Exception error)
                {
                    result = result with
                    {
                        Message = $"{result.Message} Geçici kopya temizlenemedi: {staging} ({error.Message})"
                    };
                }
            }
        }
        return result;
    }

    private static void RequireUnlinked(string path)
    {
        if ((File.GetAttributes(path) & FileAttributes.ReparsePoint) != 0)
            throw new IOException($"Bağlantı içeren proje yolu kopyalanamaz: {path}");
    }

    private static void RequireUnlinkedAncestors(string path)
    {
        for (string? current = path; current != null; current = Path.GetDirectoryName(current))
            if (Path.Exists(current)) RequireUnlinked(current);
    }

    private static void CopyTree(string source, string target, CancellationToken token, Action<string>? reportCopiedFile)
    {
        token.ThrowIfCancellationRequested();
        RequireUnlinked(source);
        Directory.CreateDirectory(target);
        foreach (string file in Directory.EnumerateFiles(source))
            CopyFile(file, Path.Combine(target, Path.GetFileName(file)), token, reportCopiedFile);
        foreach (string directory in Directory.EnumerateDirectories(source))
            CopyTree(directory, Path.Combine(target, Path.GetFileName(directory)), token, reportCopiedFile);
    }

    private static void CopyFile(string source, string target, CancellationToken token, Action<string>? reportCopiedFile)
    {
        token.ThrowIfCancellationRequested();
        RequireUnlinked(source);
        using (var input = File.OpenRead(source))
        using (var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None))
        {
            byte[] buffer = new byte[81920];
            int count;
            while ((count = input.Read(buffer, 0, buffer.Length)) > 0)
            {
                token.ThrowIfCancellationRequested();
                output.Write(buffer, 0, count);
            }
        }
        reportCopiedFile?.Invoke(source);
    }
}
