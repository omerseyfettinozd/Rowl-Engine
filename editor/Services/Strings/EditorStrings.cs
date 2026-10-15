using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Text.Json;
using Avalonia.Media;
using RowlEngine.Editor.Services.Localization;

namespace RowlEngine.Editor.Services.Strings;

/// <summary>
/// Editör arayüzü metinleri (EDITOR_UI_PORT_PLAN Faz 2b, K5-K8). Oyun
/// içeriği çevirisinden (LocalizationService) ayrıdır: bu sınıf yalnızca
/// editörün kendi kromunu çevirir.
/// <list type="bullet">
/// <item>Kataloglar çıktı klasöründeki <c>locales/editor.&lt;dil&gt;.json</c>
/// dosyalarıdır (kaynak: <c>editor/Assets/locales</c>). Yeni dil yeniden
/// derlemeden eklenebilir; Avalonia çalışmadan da okunur (testler).</item>
/// <item>Geri dönüş: etkin dil → <c>en</c> → <c>tr</c> → anahtarın kendisi.
/// Eksik anahtar sessiz değildir: dil başına bir kez uyarı loglanır.</item>
/// <item><c>qps-ploc</c> sahte dili <c>en</c>'den anında üretilir (taşma ve
/// koda gömülü metin taraması için).</item>
/// <item>Dil değişince <c>Item</c> (indeksleyici) bildirimi tüm bağlı metinleri canlı
/// yeniler (K7); sağdan-sola dillerde <see cref="FlowDirection"/> döner (K6).</item>
/// </list>
/// </summary>
public sealed class EditorStrings : INotifyPropertyChanged
{
    public const string SourceLanguage = "tr";
    public const string PseudoLanguage = "qps-ploc";
    internal const string IndexerPropertyName = "Item";
    private const string FallbackLanguage = "en";
    private static readonly HashSet<string> RightToLeftLanguages =
        new(StringComparer.OrdinalIgnoreCase) { "ar", "fa", "he", "ur" };

    /// <summary>Kataloğu derlemede bulunan diller (sahte dil hariç).</summary>
    public static IReadOnlyList<string> AvailableLanguages { get; } = DiscoverLanguages();

    public static EditorStrings Instance { get; } = new();

    /// <summary>Testler kendi örneklerinde dil değiştirir; paylaşılan örneğe dokunmaz.</summary>
    internal EditorStrings() { }

    private readonly ConcurrentDictionary<string, IReadOnlyDictionary<string, string>> _catalogs = new(StringComparer.OrdinalIgnoreCase);
    private readonly ConcurrentDictionary<string, byte> _reportedMissing = new(StringComparer.Ordinal);
    private string _language = SourceLanguage;

    public event PropertyChangedEventHandler? PropertyChanged;

    public string Language => _language;

    public FlowDirection FlowDirection => IsRightToLeft(_language)
        ? FlowDirection.RightToLeft
        : FlowDirection.LeftToRight;

    /// <summary>XAML indeks bağlaması: <c>{l:T Anahtar}</c> bunu kullanır.</summary>
    public string this[string key] => Get(key);

    public static string T(string key) => Instance.Get(key);

    public static bool IsRightToLeft(string language)
    {
        string primary = language.Split('-', 2)[0];
        return RightToLeftLanguages.Contains(primary);
    }

    /// <summary>Dili değiştirir; bilinmeyen dil kaynak dile düşer. Değişiklik yoksa bildirim yok.</summary>
    public void SetLanguage(string? language)
    {
        string normalized = Normalize(language);
        if (string.Equals(normalized, _language, StringComparison.Ordinal))
            return;
        _language = normalized;
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(Language)));
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(FlowDirection)));
        // Avalonia indeksleyici bağlamaları "Item" adını dinler (CommonPropertyNames.IndexerName);
        // WPF'teki "Item[]" burada hiçbir şeyi yenilemez.
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(IndexerPropertyName));
    }

    public string Get(string key)
    {
        if (string.IsNullOrEmpty(key))
            return string.Empty;

        if (string.Equals(_language, PseudoLanguage, StringComparison.OrdinalIgnoreCase))
            return PseudoLocaleGenerator.Generate(Lookup(FallbackLanguage, key) ?? Lookup(SourceLanguage, key) ?? key);

        string? value = Lookup(_language, key);
        if (value is not null)
            return value;

        ReportMissing(_language, key);
        return Lookup(FallbackLanguage, key) ?? Lookup(SourceLanguage, key) ?? key;
    }

    /// <summary>Bir dilin anahtar kümesi (kapı testleri: tr ile en eşit olmalı).</summary>
    public IReadOnlyCollection<string> KeysOf(string language) =>
        (IReadOnlyCollection<string>)LoadCatalog(language).Keys;

    private string? Lookup(string language, string key) =>
        LoadCatalog(language).TryGetValue(key, out string? value) ? value : null;

    private IReadOnlyDictionary<string, string> LoadCatalog(string language) =>
        _catalogs.GetOrAdd(language, ReadCatalog);

    private static IReadOnlyDictionary<string, string> ReadCatalog(string language)
    {
        var entries = new Dictionary<string, string>(StringComparer.Ordinal);
        string path = CatalogPath(language);
        if (!File.Exists(path))
            return entries;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(path));
            if (doc.RootElement.TryGetProperty("entries", out JsonElement map) && map.ValueKind == JsonValueKind.Object)
            {
                foreach (JsonProperty entry in map.EnumerateObject())
                {
                    if (entry.Value.ValueKind == JsonValueKind.Object &&
                        entry.Value.TryGetProperty("text", out JsonElement text) &&
                        text.ValueKind == JsonValueKind.String)
                    {
                        entries[entry.Name] = text.GetString() ?? string.Empty;
                    }
                }
            }
        }
        catch (Exception error) when (error is JsonException or IOException or UnauthorizedAccessException)
        {
            Debug.WriteLine($"[EditorStrings] editor.{language}.json okunamadı: {error.Message}");
        }
        return entries;
    }

    private void ReportMissing(string language, string key)
    {
        if (_reportedMissing.TryAdd(language + "\0" + key, 0))
            Debug.WriteLine($"[EditorStrings] '{language}' dilinde eksik anahtar: {key}");
    }

    private static string Normalize(string? language)
    {
        if (string.IsNullOrWhiteSpace(language))
            return SourceLanguage;
        string trimmed = language.Trim();
        if (string.Equals(trimmed, PseudoLanguage, StringComparison.OrdinalIgnoreCase))
            return PseudoLanguage;
        foreach (string available in AvailableLanguages)
        {
            if (string.Equals(available, trimmed, StringComparison.OrdinalIgnoreCase))
                return available;
        }
        return SourceLanguage;
    }

    private const string CatalogPrefix = "editor.";

    private static string CatalogDirectory => Path.Combine(AppContext.BaseDirectory, "locales");

    private static string CatalogPath(string language) =>
        Path.Combine(CatalogDirectory, CatalogPrefix + language + ".json");

    private static IReadOnlyList<string> DiscoverLanguages()
    {
        var languages = new List<string>();
        if (Directory.Exists(CatalogDirectory))
        {
            foreach (string file in Directory.GetFiles(CatalogDirectory, CatalogPrefix + "*.json"))
            {
                string name = Path.GetFileNameWithoutExtension(file);
                languages.Add(name[CatalogPrefix.Length..]);
            }
        }
        languages.Sort(StringComparer.Ordinal);
        return languages;
    }
}
