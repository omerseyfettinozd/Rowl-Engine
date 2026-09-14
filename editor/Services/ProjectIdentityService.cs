using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 2 Dilim 1 — stable per-project identity (<c>project_uuid</c>).
/// The uuid namespaces deterministic content_id migration
/// (<see cref="ContentIdService.MigrateContentId"/>) so the same legacy
/// project always migrates to the same content ids on every machine.
/// Stored in <c>project.rowlproj</c> as <c>project_uuid</c> (canonical UUID).
/// Projects predating the field get one generated exactly once and written
/// back atomically; every other manifest field is preserved byte-for-byte.
/// </summary>
public static class ProjectIdentityService
{
    /// <summary>Manifest key carrying the stable project uuid.</summary>
    public const string ManifestKey = "project_uuid";

    /// <summary>Outcome of <see cref="EnsureProjectUuid"/>.</summary>
    public sealed record IdentityResult(
        string ProjectUuid,
        bool Created,
        string? Error);

    /// <summary>
    /// Returns the project's uuid, generating and persisting one when the
    /// manifest lacks a valid entry. Never throws: failures are reported via
    /// <see cref="IdentityResult.Error"/> with a fallback transient uuid that
    /// is NOT persisted, so a half-written manifest can never poison identity.
    /// </summary>
    public static IdentityResult EnsureProjectUuid(string manifestPath)
    {
        string? stored = TryReadStoredUuid(manifestPath, out string? readError);
        if (stored is not null)
            return new IdentityResult(stored, Created: false, Error: null);
        if (readError is not null)
            return new IdentityResult(Guid.NewGuid().ToString("D").ToLowerInvariant(), Created: false, Error: readError);

        string fresh = Guid.NewGuid().ToString("D").ToLowerInvariant();
        string? writeError = TryPersistUuid(manifestPath, fresh);
        if (writeError is not null)
            return new IdentityResult(fresh, Created: false, Error: writeError);
        return new IdentityResult(fresh, Created: true, Error: null);
    }

    /// <summary>
    /// Reads the stored uuid without mutating anything.
    /// Returns null when absent or malformed (<paramref name="error"/> set only
    /// when the file itself could not be read as JSON).
    /// </summary>
    public static string? TryReadStoredUuid(string manifestPath, out string? error)
    {
        error = null;
        string raw;
        try
        {
            raw = File.ReadAllText(manifestPath);
        }
        catch (Exception readFailure) when (readFailure is IOException or UnauthorizedAccessException)
        {
            error = $"Project manifest '{manifestPath}' could not be read ({readFailure.GetType().Name}: {readFailure.Message}).";
            return null;
        }
        JsonDocument document;
        try
        {
            document = JsonDocument.Parse(raw);
        }
        catch (JsonException parseFailure)
        {
            error = $"Project manifest '{manifestPath}' is not valid JSON ({parseFailure.Message}).";
            return null;
        }
        using (document)
        {
            if (document.RootElement.ValueKind != JsonValueKind.Object)
            {
                error = $"Project manifest '{manifestPath}' does not contain a JSON object.";
                return null;
            }
            if (document.RootElement.TryGetProperty(ManifestKey, out var uuidElement) &&
                uuidElement.ValueKind == JsonValueKind.String)
            {
                string? candidate = uuidElement.GetString();
                if (!string.IsNullOrWhiteSpace(candidate) && Guid.TryParse(candidate, out var parsed))
                    return parsed.ToString("D").ToLowerInvariant();
            }
            return null;
        }
    }

    private static string? TryPersistUuid(string manifestPath, string uuid)
    {
        try
        {
            JsonObject manifest;
            if (File.Exists(manifestPath))
            {
                try
                {
                    manifest = JsonNode.Parse(File.ReadAllText(manifestPath)) as JsonObject ?? new JsonObject();
                }
                catch (JsonException parseFailure)
                {
                    return $"Project manifest '{manifestPath}' is not valid JSON ({parseFailure.Message}); identity was not persisted.";
                }
            }
            else
            {
                manifest = new JsonObject();
            }
            manifest[ManifestKey] = uuid;
            ProjectFileSystem.WriteAllTextAtomically(
                manifestPath,
                manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            return null;
        }
        catch (Exception writeFailure) when (writeFailure is IOException or UnauthorizedAccessException)
        {
            return $"Project identity could not be persisted to '{manifestPath}' ({writeFailure.GetType().Name}: {writeFailure.Message}).";
        }
    }
}
