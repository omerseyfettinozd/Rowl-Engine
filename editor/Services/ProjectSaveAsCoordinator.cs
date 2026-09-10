using System;
using System.IO;
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
        Action saveSourceProject)
    {
        if (string.IsNullOrWhiteSpace(targetDirectory))
            return new(false, targetDirectory, "Hedef klasör belirtilmedi.");

        if (ProjectFileSystem.IsSameOrDescendant(targetDirectory, sourceProjectRoot))
            throw new InvalidOperationException("Farklı Kaydet hedefi açık projenin kendisi veya alt klasörü olamaz.");

        Directory.CreateDirectory(targetDirectory);

        // 1. Ensure current in-memory edits are flushed to source project files
        saveSourceProject();

        // 2. Copy Assets directory
        string sourceAssets = Path.Combine(sourceProjectRoot, "Assets");
        string targetAssets = Path.Combine(targetDirectory, "Assets");
        ProjectFileSystem.CopyDirectory(sourceAssets, targetAssets);

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
        return new(true, targetDirectory, "Proje başarıyla kopyalandı.");
    }
}
