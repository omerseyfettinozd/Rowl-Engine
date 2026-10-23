using System;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services;

public sealed record SaveAsResult(bool Succeeded, string TargetDirectory, string Message);

/// <summary>
/// Coordinates the "Save As" (Farklı Kaydet) workflow:
/// validates the target directory, copies the source assets, updates project manifest,
/// and ensures atomic persistence without modifying the currently open project.
/// </summary>
public static class ProjectSaveAsCoordinator
{
    public static SaveAsResult SaveProjectCopy(
        string sourceProjectRoot,
        string targetDirectory,
        int nodeCount,
        ulong startNodeId,
        Action saveSourceProject,
        Action? afterAssetsCopied = null)
    {
        if (string.IsNullOrWhiteSpace(targetDirectory))
            return new(false, targetDirectory, "Hedef klasör belirtilmedi.");

        if (ProjectFileSystem.IsSameOrDescendant(targetDirectory, sourceProjectRoot))
            throw new InvalidOperationException("Farklı Kaydet hedefi açık projenin kendisi veya alt klasörü olamaz.");

        // Mevcut dolu klasör asla ezilmez; boş klasör kabul edilir.
        if (Directory.Exists(targetDirectory) &&
            Directory.EnumerateFileSystemEntries(targetDirectory).Any())
            return new(false, targetDirectory, "Hedef klasör zaten var ve boş değil; üzerine yazılmadı: " + targetDirectory);
        if (File.Exists(targetDirectory))
            return new(false, targetDirectory, "Hedef yol bir dosya; klasör oluşturulamadı: " + targetDirectory);

        // 1. Ensure current in-memory edits are flushed to source project files
        saveSourceProject();

        // Kopya kardeş bir hazırlık klasörüne yapılır; ancak tam başarıda hedefe taşınır.
        string fullTarget = Path.GetFullPath(targetDirectory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string stagingDirectory = fullTarget + "." + Guid.NewGuid().ToString("N") + ".partial";
        try
        {
            string? parent = Path.GetDirectoryName(fullTarget);
            if (!string.IsNullOrEmpty(parent)) Directory.CreateDirectory(parent);
            Directory.CreateDirectory(stagingDirectory);
            WriteCopy(sourceProjectRoot, stagingDirectory, nodeCount, startNodeId, afterAssetsCopied);

            // Windows'ta Directory.Move boş hedefin üstüne çıkmaz; boş klasörü önce kaldır.
            if (Directory.Exists(fullTarget)) Directory.Delete(fullTarget);
            Directory.Move(stagingDirectory, fullTarget);
        }
        catch
        {
            try { if (Directory.Exists(stagingDirectory)) Directory.Delete(stagingDirectory, recursive: true); }
            catch (Exception) { /* temizlik en iyi çaba; asıl hata yukarı gider */ }
            throw;
        }
        return new(true, targetDirectory, "Proje başarıyla kopyalandı.");
    }

    private static void WriteCopy(string sourceProjectRoot, string targetDirectory, int nodeCount, ulong startNodeId, Action? afterAssetsCopied)
    {
        // 2. Copy Assets directory
        string sourceAssets = Path.Combine(sourceProjectRoot, "Assets");
        string targetAssets = Path.Combine(targetDirectory, "Assets");
        ProjectFileSystem.CopyDirectory(sourceAssets, targetAssets);
        afterAssetsCopied?.Invoke();

        // 3. Write project metadata manifest
        string sourceManifestPath = Path.Combine(sourceProjectRoot, "project.rowlproj");
        string targetManifestPath = Path.Combine(targetDirectory, "project.rowlproj");
        JsonObject manifest;
        try
        {
            if (File.Exists(sourceManifestPath))
                manifest = JsonNode.Parse(File.ReadAllText(sourceManifestPath)) as JsonObject ?? new();
            else
                manifest = new JsonObject();
        }
        catch
        {
            manifest = new JsonObject();
        }

        manifest["name"] ??= "Rowl Engine Project";
        manifest["version"] ??= "1.0.0";
        manifest["engineVersion"] ??= "1.0.0";
        manifest["savedAt"] = DateTime.UtcNow.ToString("o");
        manifest["nodeCount"] = nodeCount;
        manifest["startNodeId"] = startNodeId;
        manifest["virtualResolution"] ??= JsonSerializer.SerializeToNode(new { width = 1920, height = 1080 });

        ProjectFileSystem.WriteAllTextAtomically(targetManifestPath, manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
    }
}
