using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;

namespace RowlEngine.Editor.Services.Localization;

/// <summary>One parsed CSV data row (header excluded).</summary>
public sealed record ExchangeRow(
    string ContentId,
    string Speaker,
    string SourceText,
    string TranslatedText,
    string AltText);

/// <summary>Outcome of <see cref="TranslationExchangeService.TryImportCsv"/>.</summary>
public sealed record ImportResult(
    bool Accepted,
    string? Error,
    int UpdatedCount,
    int SkippedUnknownCount,
    IReadOnlyList<string> SkippedUnknownIds);

/// <summary>
/// Faz 3 Dilim 4 — CSV / JSON exchange with external translators.
/// Wire format (RFC 4180, CRLF, UTF-8):
/// <c>content_id,speaker,source_text,translated_text,alt_text</c>.
/// Imports are fail-closed and atomic: the catalog is only touched after
/// every row parses and validates; a missing column, an unbalanced quote
/// or a non-UUID key rejects the whole document.
/// </summary>
public static class TranslationExchangeService
{
    public static readonly string[] CsvHeader =
    {
        "content_id", "speaker", "source_text", "translated_text", "alt_text",
    };

    /// <summary>Serializes desk rows for a spreadsheet round trip.</summary>
    public static string ExportCsv(IReadOnlyList<TranslationRow> rows)
    {
        var builder = new StringBuilder();
        builder.Append(string.Join(",", CsvHeader)).Append("\r\n");
        foreach (TranslationRow row in (rows ?? Array.Empty<TranslationRow>())
            .OrderBy(r => r.ContentId, StringComparer.Ordinal))
        {
            builder.Append(Escape(row.ContentId)).Append(',');
            builder.Append(Escape(row.Speaker)).Append(',');
            builder.Append(Escape(row.SourceText)).Append(',');
            builder.Append(Escape(row.TranslatedText)).Append(',');
            builder.Append(Escape(row.AltText)).Append("\r\n");
        }
        return builder.ToString();
    }

    /// <summary>
    /// Parses and applies a translator CSV onto <paramref name="rows"/>
    /// (matched by <c>content_id</c>, in place). Unknown ids are skipped
    /// with a warning count; anything structurally wrong rejects the
    /// entire document and leaves <paramref name="rows"/> untouched.
    /// </summary>
    public static ImportResult TryImportCsv(
        string? csv, IList<TranslationRow> rows)
    {
        var targets = (rows ?? new List<TranslationRow>())
            .ToDictionary(r => r.ContentId, StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(csv))
            return Fail("CSV document is empty; nothing was imported.");
        if (!TryParseCsv(csv, out List<string[]>? records, out string? parseError) || records is null)
            return Fail(parseError ?? "CSV document could not be parsed; nothing was imported.");
        if (records.Count == 0)
            return Fail("CSV document has no header row; nothing was imported.");
        if (!IsExpectedHeader(records[0]))
            return Fail("CSV header must be exactly 'content_id,speaker,source_text,translated_text,alt_text'; nothing was imported.");
        var staged = new List<ExchangeRow>(records.Count - 1);
        for (int i = 1; i < records.Count; i++)
        {
            string[] fields = records[i];
            if (IsBlankRecord(fields))
                continue;
            if (fields.Length != CsvHeader.Length)
                return Fail($"CSV row {i + 1} has {fields.Length} fields instead of {CsvHeader.Length}; nothing was imported.");
            string contentId = ContentIdService.Normalize(fields[0]) ?? string.Empty;
            if (string.IsNullOrEmpty(contentId))
                return Fail($"CSV row {i + 1} has no valid content_id; nothing was imported.");
            staged.Add(new ExchangeRow(contentId, fields[1], fields[2], fields[3], fields[4]));
        }
        int updated = 0;
        var skipped = new List<string>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (ExchangeRow stagedRow in staged)
        {
            if (!targets.TryGetValue(stagedRow.ContentId, out TranslationRow? row))
            {
                if (seen.Add(stagedRow.ContentId))
                    skipped.Add(stagedRow.ContentId);
                continue;
            }
            row.TranslatedText = stagedRow.TranslatedText;
            row.AltText = stagedRow.AltText;
            if (!string.IsNullOrEmpty(stagedRow.Speaker))
                row.Speaker = stagedRow.Speaker;
            row.State = string.IsNullOrEmpty(stagedRow.TranslatedText)
                ? TranslationState.Missing
                : TranslationState.Translated;
            row.LegacyNoHash = false;
            updated++;
        }
        return new ImportResult(true, null, updated, skipped.Count, skipped);
    }

    /// <summary>
    /// Exports the current rows as a locale catalog JSON document
    /// (same shape the desk saves to disk).
    /// </summary>
    public static string ExportCatalogJson(
        string locale, IReadOnlyList<TranslationRow> rows)
    {
        string normalized = LocalizationService.NormalizeLocale(locale)
            ?? LocalizationService.FallbackDefaultLocale;
        var entries = new Dictionary<string, TranslationStatusService.CatalogEntry>(StringComparer.Ordinal);
        foreach (TranslationRow row in rows ?? Array.Empty<TranslationRow>())
        {
            entries[row.ContentId] = new TranslationStatusService.CatalogEntry(
                row.Speaker, row.TranslatedText, row.AltText,
                TranslationInventoryService.ComputeSourceHash(row.SourceSpeaker, row.SourceText));
        }
        return TranslationStatusService.SerializeCatalog(normalized, entries);
    }

    // ── CSV codec (RFC 4180) ──────────────────────────────────────────

    internal static string Escape(string? value)
    {
        string text = value ?? string.Empty;
        if (text.IndexOfAny(new[] { '"', ',', '\r', '\n' }) < 0)
            return text;
        return "\"" + text.Replace("\"", "\"\"") + "\"";
    }

    internal static bool TryParseCsv(
        string csv, out List<string[]>? records, out string? error)
    {
        records = new List<string[]>();
        error = null;
        var fields = new List<string>();
        var current = new StringBuilder();
        bool inQuotes = false;
        for (int i = 0; i < csv.Length; i++)
        {
            char c = csv[i];
            if (inQuotes)
            {
                if (c == '"')
                {
                    if (i + 1 < csv.Length && csv[i + 1] == '"')
                    {
                        current.Append('"');
                        i++;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    current.Append(c);
                }
            }
            else if (c == '"')
            {
                if (current.Length == 0)
                {
                    inQuotes = true;
                }
                else
                {
                    error = "CSV has a stray quote inside an unquoted field; nothing was imported.";
                    records = null;
                    return false;
                }
            }
            else if (c == ',')
            {
                fields.Add(current.ToString());
                current.Clear();
            }
            else if (c == '\r' || c == '\n')
            {
                if (c == '\r' && i + 1 < csv.Length && csv[i + 1] == '\n')
                    i++;
                fields.Add(current.ToString());
                current.Clear();
                // A lone empty line carries no data; anything else is a record
                // (a one-field record is rejected later by the arity check).
                if (fields.Count == 1 && fields[0].Length == 0)
                    fields.Clear();
                else
                {
                    records.Add(fields.ToArray());
                    fields.Clear();
                }
            }
            else
            {
                current.Append(c);
            }
        }
        if (inQuotes)
        {
            error = "CSV ends inside a quoted field; nothing was imported.";
            records = null;
            return false;
        }
        if (current.Length > 0 || fields.Count > 0)
        {
            fields.Add(current.ToString());
            records.Add(fields.ToArray());
        }
        return true;
    }

    private static bool IsExpectedHeader(string[] header)
    {
        if (header.Length != CsvHeader.Length)
            return false;
        for (int i = 0; i < header.Length; i++)
        {
            if (!string.Equals(header[i].Trim(), CsvHeader[i], StringComparison.OrdinalIgnoreCase))
                return false;
        }
        return true;
    }

    private static bool IsBlankRecord(string[] fields) =>
        fields.Length == 0 || fields.All(f => f.Length == 0);

    private static ImportResult Fail(string error) =>
        new(false, error, 0, 0, Array.Empty<string>());
}
