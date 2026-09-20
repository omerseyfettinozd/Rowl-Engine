using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace RowlEngine.Editor.Services.Localization;

/// <summary>Translation state of one inventory line against one catalog.</summary>
public enum TranslationState
{
    Translated,
    Missing,
    Changed,
}

/// <summary>Desk row filter.</summary>
public enum TranslationFilter
{
    All,
    MissingOnly,
    ChangedOnly,
}

/// <summary>One editable desk row: source snapshot plus catalog values.</summary>
public sealed class TranslationRow
{
    public string ContentId { get; set; } = string.Empty;
    /// <summary>Translated speaker name (editable, catalog-owned).</summary>
    public string Speaker { get; set; } = string.Empty;
    /// <summary>Source speaker snapshot (inventory-owned, never edited).</summary>
    public string SourceSpeaker { get; set; } = string.Empty;
    public string SourceText { get; set; } = string.Empty;
    public string TranslatedText { get; set; } = string.Empty;
    public string AltText { get; set; } = string.Empty;
    public TranslationState State { get; set; } = TranslationState.Missing;
    public ulong NodeId { get; set; }
    public string NodeTitle { get; set; } = string.Empty;

    /// <summary>
    /// True when the catalog entry predates <c>source_hash</c> tracking.
    /// Such rows count as Translated while they carry text; the next desk
    /// save stamps the hash and arms Changed detection.
    /// </summary>
    public bool LegacyNoHash { get; set; }
}

/// <summary>
/// Faz 3 Dilim 4 — matches the story-graph inventory against
/// <c>Assets/locales/&lt;locale&gt;.json</c> catalogs and owns atomic
/// catalog saves. All I/O is fail-closed (errors out, never throws);
/// filtering and search are pure and fully unit-testable.
/// </summary>
public static class TranslationStatusService
{
    /// <summary>Optional per-entry fingerprint of the translated source.</summary>
    public const string SourceHashKey = "source_hash";

    /// <summary>
    /// Builds desk rows for <paramref name="targetLocale"/>. A missing or
    /// unreadable catalog yields all-Missing rows (not an error when the
    /// file simply does not exist yet); corrupt JSON is reported through
    /// <paramref name="error"/> with all-Missing rows.
    /// </summary>
    public static List<TranslationRow> BuildRows(
        IReadOnlyList<SourceDialogue> inventory,
        string projectRoot,
        string targetLocale,
        out string? error)
    {
        error = null;
        var catalog = LoadCatalogEntries(projectRoot, targetLocale, out string? loadError);
        if (loadError is not null)
            error = loadError;
        var rows = new List<TranslationRow>(inventory.Count);
        foreach (SourceDialogue source in inventory)
        {
            var row = new TranslationRow
            {
                ContentId = source.ContentId,
                Speaker = source.Speaker,
                SourceSpeaker = source.Speaker,
                SourceText = source.SourceText,
                NodeId = source.NodeId,
                NodeTitle = source.NodeTitle,
            };
            if (catalog.TryGetValue(source.ContentId, out CatalogEntry? entry) &&
                entry is not null)
            {
                row.TranslatedText = entry.Text;
                row.AltText = entry.AltText;
                if (!string.IsNullOrEmpty(entry.Speaker))
                    row.Speaker = entry.Speaker;
                string currentHash = TranslationInventoryService.ComputeSourceHash(
                    row.SourceSpeaker, row.SourceText);
                if (string.IsNullOrEmpty(entry.Text))
                {
                    row.State = TranslationState.Missing;
                }
                else if (entry.SourceHash is null)
                {
                    row.State = TranslationState.Translated;
                    row.LegacyNoHash = true;
                }
                else if (!string.Equals(entry.SourceHash, currentHash, StringComparison.Ordinal))
                {
                    row.State = TranslationState.Changed;
                }
                else
                {
                    row.State = TranslationState.Translated;
                }
            }
            else
            {
                row.State = TranslationState.Missing;
            }
            rows.Add(row);
        }
        rows.Sort((a, b) => string.Compare(a.ContentId, b.ContentId, StringComparison.Ordinal));
        return rows;
    }

    /// <summary>
    /// Pure filter + search over rows. The query matches speaker, source
    /// text, translated text and content id, case-insensitively.
    /// </summary>
    public static IReadOnlyList<TranslationRow> ApplyFilter(
        IReadOnlyList<TranslationRow> rows, TranslationFilter filter, string? query)
    {
        string needle = (query ?? string.Empty).Trim();
        var result = new List<TranslationRow>(rows.Count);
        foreach (TranslationRow row in rows)
        {
            if (filter == TranslationFilter.MissingOnly && row.State != TranslationState.Missing)
                continue;
            if (filter == TranslationFilter.ChangedOnly && row.State != TranslationState.Changed)
                continue;
            if (needle.Length > 0 &&
                row.ContentId.IndexOf(needle, StringComparison.OrdinalIgnoreCase) < 0 &&
                row.Speaker.IndexOf(needle, StringComparison.OrdinalIgnoreCase) < 0 &&
                row.SourceText.IndexOf(needle, StringComparison.OrdinalIgnoreCase) < 0 &&
                row.TranslatedText.IndexOf(needle, StringComparison.OrdinalIgnoreCase) < 0)
                continue;
            result.Add(row);
        }
        return result;
    }

    /// <summary>
    /// Counts rows per state for the desk status bar.
    /// </summary>
    public static (int translated, int missing, int changed) CountStates(
        IReadOnlyList<TranslationRow> rows)
    {
        int translated = 0, missing = 0, changed = 0;
        foreach (TranslationRow row in rows)
        {
            switch (row.State)
            {
                case TranslationState.Translated: translated++; break;
                case TranslationState.Missing: missing++; break;
                case TranslationState.Changed: changed++; break;
            }
        }
        return (translated, missing, changed);
    }

    /// <summary>
    /// Saves <paramref name="rows"/> into the target catalog atomically
    /// (temp file + move). Every inventory row is stamped with the current
    /// <c>source_hash</c>; catalog entries outside the inventory (orphans)
    /// are preserved untouched. Returns null on success, else an error.
    /// </summary>
    public static string? SaveCatalog(
        string projectRoot,
        string targetLocale,
        IReadOnlyList<TranslationRow> rows)
    {
        string? locale = LocalizationService.NormalizeLocale(targetLocale);
        if (locale is null)
            return $"Target locale '{targetLocale}' is not a valid locale code.";
        if (string.IsNullOrWhiteSpace(projectRoot))
            return "Project root is not set.";
        try
        {
            string localesDir = GetLocalesDirectory(projectRoot);
            Directory.CreateDirectory(localesDir);
            var merged = LoadCatalogEntries(projectRoot, locale, out _);
            foreach (TranslationRow row in rows)
            {
                string hash = TranslationInventoryService.ComputeSourceHash(
                    row.SourceSpeaker, row.SourceText);
                merged[row.ContentId] = new CatalogEntry(
                    row.Speaker, row.TranslatedText, row.AltText, hash);
            }
            string json = SerializeCatalog(locale, merged);
            string? strictError = LocalizationService.ValidateCatalog(json, locale, out _);
            if (strictError is not null)
                return $"Generated catalog failed validation ({strictError}). Nothing was written.";
            ProjectFileSystem.WriteAllTextAtomically(
                Path.Combine(localesDir, locale + ".json"), json);
            return null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return $"Catalog could not be saved ({exception.Message}). Nothing was written.";
        }
    }

    // Internals

    internal sealed record CatalogEntry(
        string Speaker, string Text, string AltText, string? SourceHash);

    internal static string GetLocalesDirectory(string projectRoot) =>
        Path.Combine(projectRoot, "Assets", "locales");

    internal static Dictionary<string, CatalogEntry> LoadCatalogEntries(
        string projectRoot, string locale, out string? error)
    {
        error = null;
        var entries = new Dictionary<string, CatalogEntry>(StringComparer.Ordinal);
        string? normalized = LocalizationService.NormalizeLocale(locale);
        if (normalized is null || string.IsNullOrWhiteSpace(projectRoot))
            return entries;
        // Prefer the literal file stem ("qps-ploc.json"), then the
        // normalized code ("qps.json"). Raw stems with separators or
        // parent segments are never trusted (fail closed).
        string rawStem = (locale ?? string.Empty).Trim().ToLowerInvariant();
        string? filePath = null;
        if (rawStem.Length > 0 && rawStem.Length <= 64 &&
            rawStem.IndexOfAny(new[] { '/', '\\', Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar }) < 0 &&
            !rawStem.Contains("..", StringComparison.Ordinal))
        {
            string rawPath = Path.Combine(GetLocalesDirectory(projectRoot), rawStem + ".json");
            if (File.Exists(rawPath))
                filePath = rawPath;
        }
        filePath ??= Path.Combine(GetLocalesDirectory(projectRoot), normalized + ".json");
        if (!File.Exists(filePath))
            return entries;
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(filePath));
            if (document.RootElement.ValueKind != JsonValueKind.Object ||
                !document.RootElement.TryGetProperty("entries", out JsonElement root) ||
                root.ValueKind != JsonValueKind.Object)
            {
                error = $"Locale catalog '{normalized}.json' has no entries object; treating it as empty.";
                return entries;
            }
            foreach (JsonProperty row in root.EnumerateObject())
            {
                if (string.IsNullOrEmpty(row.Name) || row.Value.ValueKind != JsonValueKind.Object)
                    continue;
                entries[row.Name] = new CatalogEntry(
                    ReadString(row.Value, "speaker"),
                    ReadString(row.Value, "text"),
                    ReadString(row.Value, "alt_text"),
                    ReadOptionalString(row.Value, SourceHashKey));
            }
            return entries;
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        {
            error = $"Locale catalog '{normalized}.json' could not be parsed ({exception.Message}); treating it as empty.";
            return entries;
        }
    }

    internal static string SerializeCatalog(
        string locale, IReadOnlyDictionary<string, CatalogEntry> entries)
    {
        var ordered = entries.Keys.OrderBy(key => key, StringComparer.Ordinal).ToList();
        using var stream = new MemoryStream();
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
        {
            writer.WriteStartObject();
            writer.WriteNumber("schema_version", 1);
            writer.WriteString("locale", locale);
            writer.WriteStartObject("entries");
            foreach (string key in ordered)
            {
                CatalogEntry entry = entries[key];
                writer.WriteStartObject(key);
                writer.WriteString("speaker", entry.Speaker);
                writer.WriteString("text", entry.Text);
                writer.WriteString("alt_text", entry.AltText);
                if (entry.SourceHash is not null)
                    writer.WriteString(SourceHashKey, entry.SourceHash);
                writer.WriteEndObject();
            }
            writer.WriteEndObject();
            writer.WriteEndObject();
        }
        return System.Text.Encoding.UTF8.GetString(stream.ToArray()) + "\n";
    }

    private static string ReadString(JsonElement element, string field) =>
        element.TryGetProperty(field, out JsonElement value) &&
        value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? string.Empty : string.Empty;



    private static string? ReadOptionalString(JsonElement element, string field) =>
        element.TryGetProperty(field, out JsonElement value) &&
        value.ValueKind == JsonValueKind.String
            ? value.GetString() : null;

}
