using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace RowlEngine.Editor.Services.Localization;

/// <summary>
/// Faz 3 Dilim 4 — one dialogue line found in the persisted story graph.
/// <c>SourceHash</c> fingerprints <c>speaker + "\0" + sourceText</c> so the
/// desk can tell a changed source line from a translated one.
/// </summary>
public sealed record SourceDialogue(
    string ContentId,
    string Speaker,
    string SourceText,
    ulong NodeId,
    string NodeTitle);

/// <summary>
/// Scans <c>full_story_graph.json</c> (canonical <c>Assets/json</c> location
/// first, legacy <c>Assets</c> copy as read fallback) and extracts every
/// dialogue component as a stable <c>content_id</c> inventory. Pure file
/// parsing: no ViewModels, no native handles, fully unit-testable.
/// </summary>
public static class TranslationInventoryService
{
    /// <summary>Dialogue component discriminator on disk.</summary>
    public const string DialogueTypeKey = "dialogue";

    /// <summary>
    /// Builds the inventory for a project root. Returns an empty list (with
    /// <paramref name="error"/> set) when the graph is missing or unreadable
    /// instead of throwing — the desk surfaces the error, fail closed.
    /// </summary>
    public static IReadOnlyList<SourceDialogue> BuildInventory(
        string projectRoot, out string? error)
    {
        error = null;
        if (string.IsNullOrWhiteSpace(projectRoot))
        {
            error = "Project root is not set.";
            return Array.Empty<SourceDialogue>();
        }
        string primary = Path.Combine(projectRoot, "Assets", "json", "full_story_graph.json");
        string legacy = Path.Combine(projectRoot, "Assets", "full_story_graph.json");
        string? filePath = File.Exists(primary) ? primary
            : File.Exists(legacy) ? legacy : null;
        if (filePath is null)
        {
            error = "Story graph file was not found (Assets/json/full_story_graph.json).";
            return Array.Empty<SourceDialogue>();
        }
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(filePath));
            return ExtractFromDocument(document, out error);
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        {
            error = $"Story graph could not be parsed ({exception.Message}).";
            return Array.Empty<SourceDialogue>();
        }
    }

    /// <summary>
    /// Extracts the inventory from an already parsed story graph document.
    /// Tolerates unknown node/object/component shapes; only
    /// <c>type == "dialogue"</c> components with a valid <c>content_id</c>
    /// are collected. Later duplicates of one id win (last write wins).
    /// </summary>
    public static IReadOnlyList<SourceDialogue> ExtractFromDocument(
        JsonDocument document, out string? error)
    {
        error = null;
        var byId = new Dictionary<string, SourceDialogue>(StringComparer.Ordinal);
        if (document.RootElement.ValueKind != JsonValueKind.Object ||
            !document.RootElement.TryGetProperty("nodes", out JsonElement nodes) ||
            nodes.ValueKind != JsonValueKind.Array)
        {
            error = "Story graph does not contain a nodes array.";
            return Array.Empty<SourceDialogue>();
        }
        foreach (JsonElement node in nodes.EnumerateArray())
        {
            if (node.ValueKind != JsonValueKind.Object)
                continue;
            ulong nodeId = node.TryGetProperty("id", out JsonElement idElement) &&
                idElement.ValueKind == JsonValueKind.Number &&
                idElement.TryGetUInt64(out ulong parsedId)
                    ? parsedId : 0;
            string nodeTitle = node.TryGetProperty("title", out JsonElement titleElement) &&
                titleElement.ValueKind == JsonValueKind.String
                    ? titleElement.GetString() ?? string.Empty : string.Empty;
            if (!node.TryGetProperty("objects", out JsonElement objects) ||
                objects.ValueKind != JsonValueKind.Array)
                continue;
            foreach (JsonElement frameObject in objects.EnumerateArray())
            {
                if (frameObject.ValueKind != JsonValueKind.Object ||
                    !frameObject.TryGetProperty("components", out JsonElement components) ||
                    components.ValueKind != JsonValueKind.Array)
                    continue;
                foreach (JsonElement component in components.EnumerateArray())
                {
                    SourceDialogue? dialogue = TryReadDialogue(component, nodeId, nodeTitle);
                    if (dialogue is not null)
                        byId[dialogue.ContentId] = dialogue;
                }
            }
        }
        return new List<SourceDialogue>(byId.Values);
    }

    private static SourceDialogue? TryReadDialogue(JsonElement component, ulong nodeId, string nodeTitle)
    {
        if (component.ValueKind != JsonValueKind.Object ||
            !component.TryGetProperty("type", out JsonElement type) ||
            type.ValueKind != JsonValueKind.String ||
            !string.Equals(type.GetString(), DialogueTypeKey, StringComparison.Ordinal) ||
            !component.TryGetProperty("data", out JsonElement data) ||
            data.ValueKind != JsonValueKind.Object)
            return null;
        if (!data.TryGetProperty(ContentIdService.StorageKey, out JsonElement idElement) ||
            idElement.ValueKind != JsonValueKind.String ||
            ContentIdService.Normalize(idElement.GetString()) is not string contentId)
            return null;
        string speaker = data.TryGetProperty("speaker", out JsonElement speakerElement) &&
            speakerElement.ValueKind == JsonValueKind.String
                ? speakerElement.GetString() ?? string.Empty : string.Empty;
        string text = data.TryGetProperty("dialogue", out JsonElement textElement) &&
            textElement.ValueKind == JsonValueKind.String
                ? textElement.GetString() ?? string.Empty : string.Empty;
        return new SourceDialogue(contentId, speaker, text, nodeId, nodeTitle);
    }

    /// <summary>
    /// Stable SHA-256 fingerprint (lowercase hex) of
    /// <c>speaker + "\0" + sourceText</c>. Stored beside translations as
    /// <c>source_hash</c> so later source edits surface as Changed.
    /// </summary>
    public static string ComputeSourceHash(string speaker, string sourceText)
    {
        byte[] bytes = Encoding.UTF8.GetBytes((speaker ?? string.Empty) + "\0" + (sourceText ?? string.Empty));
        byte[] hash = SHA256.HashData(bytes);
        var builder = new StringBuilder(hash.Length * 2);
        foreach (byte b in hash)
            builder.Append(b.ToString("x2"));
        return builder.ToString();
    }
}
