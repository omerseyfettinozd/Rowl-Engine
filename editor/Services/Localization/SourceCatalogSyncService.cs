using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services.Localization;

/// <summary>
/// Kaynak-dil sözleşmesi: story graph'taki diyalog metni kaynak-dil metnidir,
/// çalışma zamanı ise <c>Assets/locales/&lt;locale&gt;.json</c> kataloğunu
/// düğüm metninin ÖNÜNDE çözer (aktif → varsayılan → özgün). Yazarın editörde
/// yazdığı satır, varsayılan-dil kataloğundaki eski satırın gölgesinde kalmasın
/// diye her graph kaydından sonra:
/// <list type="bullet">
/// <item>varsayılan-dil kataloğunda bu <c>content_id</c> varsa konuşmacı/metin
/// graph'a eşitlenir (alt_text korunur, <c>source_hash</c> damgalanır);</item>
/// <item>diğer dillerde kaynağı değişen satırın <c>source_hash</c>'i yoksa ESKİ
/// kaynağın hash'iyle damgalanır; Çeviri Masası satırı "Değişmiş" gösterir.
/// Çeviri metni silinmez, o dilde çeviri görünmeye devam eder.</item>
/// </list>
/// Tüm I/O fail-closed: hata kaydı asla bozmaz, katalog bozuksa dokunulmaz.
/// Katalog JsonNode ile yerinde düzenlenir (çoğul tabloları vb. kaybetmez).
/// </summary>
public static class SourceCatalogSyncService
{
    private static readonly JsonSerializerOptions WriteOptions = new()
    {
        WriteIndented = true,
        Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    /// <summary>Kayıttan ÖNCE diskteki graph'ın diyalog envanteri (yoksa boş).</summary>
    public static IReadOnlyList<SourceDialogue> CaptureBefore(string assetsJsonPath)
    {
        string? root = ProjectRootFromAssetsJson(assetsJsonPath);
        if (root is null) return Array.Empty<SourceDialogue>();
        try { return TranslationInventoryService.BuildInventory(root, out _); }
        catch (Exception) { return Array.Empty<SourceDialogue>(); }
    }

    /// <summary>
    /// Kayıttan SONRA çağrılır. Dönüş: yeniden yazılan katalog sayısı.
    /// </summary>
    public static int SyncAfterSave(
        string assetsJsonPath, IReadOnlyList<SourceDialogue> before, Action<string>? log = null)
    {
        string? root = ProjectRootFromAssetsJson(assetsJsonPath);
        if (root is null) return 0;
        try
        {
            string localesDir = TranslationStatusService.GetLocalesDirectory(root);
            if (!Directory.Exists(localesDir)) return 0;
            var after = TranslationInventoryService.BuildInventory(root, out string? error);
            if (error is not null || after.Count == 0) return 0;

            string manifestPath = Path.Combine(root, "project.rowlproj");
            var manifest = LocalizationService.ParseManifestLocales(
                File.Exists(manifestPath) ? File.ReadAllText(manifestPath) : "{}");
            var oldById = before.ToDictionary(d => d.ContentId, StringComparer.Ordinal);

            int written = 0;
            foreach (string locale in manifest.SupportedLocales)
            {
                string path = Path.Combine(localesDir, locale + ".json");
                if (!File.Exists(path)) continue;
                bool isDefault = string.Equals(locale, manifest.DefaultLocale, StringComparison.Ordinal);
                int touched = isDefault
                    ? SyncDefaultCatalog(path, after)
                    : ArmStaleDetection(path, after, oldById);
                if (touched > 0)
                {
                    written++;
                    log?.Invoke(isDefault
                        ? $"[Localization] '{locale}' kaynak kataloğu graph'la eşitlendi ({touched} satır)."
                        : $"[Localization] '{locale}' çevirisinde {touched} satırın kaynağı değişti (Çeviri Masası'nda 'Değişmiş').");
                }
            }
            return written;
        }
        catch (Exception exception) when (exception is IOException or JsonException or UnauthorizedAccessException)
        {
            log?.Invoke($"[Localization] Kaynak katalog eşitlenemedi: {exception.Message}");
            return 0;
        }
    }

    private static int SyncDefaultCatalog(string path, IReadOnlyList<SourceDialogue> after)
    {
        JsonObject? entries = LoadEntries(path, out JsonObject? rootNode);
        if (entries is null || rootNode is null) return 0;
        int touched = 0;
        foreach (SourceDialogue source in after)
        {
            if (entries[source.ContentId] is not JsonObject entry) continue;
            // Çoğul tablosu / dize olmayan satırlara dokunma.
            if (entry["text"] is not JsonValue textNode || !textNode.TryGetValue(out string? text)) continue;
            string speaker = entry["speaker"] is JsonValue sv && sv.TryGetValue(out string? s) ? s ?? string.Empty : string.Empty;
            if (string.Equals(text, source.SourceText, StringComparison.Ordinal) &&
                string.Equals(speaker, source.Speaker, StringComparison.Ordinal))
                continue;
            entry["speaker"] = source.Speaker;
            entry["text"] = source.SourceText;
            entry[TranslationStatusService.SourceHashKey] =
                TranslationInventoryService.ComputeSourceHash(source.Speaker, source.SourceText);
            touched++;
        }
        if (touched > 0) Write(path, rootNode);
        return touched;
    }

    private static int ArmStaleDetection(
        string path, IReadOnlyList<SourceDialogue> after,
        IReadOnlyDictionary<string, SourceDialogue> oldById)
    {
        JsonObject? entries = LoadEntries(path, out JsonObject? rootNode);
        if (entries is null || rootNode is null) return 0;
        int touched = 0;
        foreach (SourceDialogue source in after)
        {
            if (!oldById.TryGetValue(source.ContentId, out SourceDialogue? old)) continue;
            if (string.Equals(old.SourceText, source.SourceText, StringComparison.Ordinal) &&
                string.Equals(old.Speaker, source.Speaker, StringComparison.Ordinal))
                continue;
            if (entries[source.ContentId] is not JsonObject entry) continue;
            if (entry.ContainsKey(TranslationStatusService.SourceHashKey)) continue; // zaten izleniyor
            entry[TranslationStatusService.SourceHashKey] =
                TranslationInventoryService.ComputeSourceHash(old.Speaker, old.SourceText);
            touched++;
        }
        if (touched > 0) Write(path, rootNode);
        return touched;
    }

    private static JsonObject? LoadEntries(string path, out JsonObject? rootNode)
    {
        rootNode = null;
        try
        {
            rootNode = JsonNode.Parse(File.ReadAllText(path)) as JsonObject;
            return rootNode?["entries"] as JsonObject;
        }
        catch (JsonException)
        {
            return null; // bozuk katalog: dokunma
        }
    }

    private static void Write(string path, JsonObject rootNode) =>
        ProjectFileSystem.WriteAllTextAtomically(path, rootNode.ToJsonString(WriteOptions) + "\n");

    /// <summary><c>&lt;root&gt;/Assets/json</c> → <c>&lt;root&gt;</c>; başka düzen için null.</summary>
    internal static string? ProjectRootFromAssetsJson(string assetsJsonPath)
    {
        if (string.IsNullOrWhiteSpace(assetsJsonPath)) return null;
        string? assets = Path.GetDirectoryName(Path.GetFullPath(assetsJsonPath));
        return assets is null ? null : Path.GetDirectoryName(assets);
    }
}
