using System;
using System.Collections.Generic;
using System.Text;

namespace RowlEngine.Editor.Services.Localization;

/// <summary>
/// Faz 3 Dilim 4 — deterministic pseudo-localization for overflow and
/// hardcoded-string testing. Every Latin letter becomes a wider accented
/// lookalike and the line is wrapped so unexpanded UI clips visibly:
/// <c>"Play Game" → "[!!! Ƥľȧẏ Ɠȧṁē !!!]"</c>.
/// Rich-text tag spans (<c>&lt;...&gt;</c>) pass through verbatim so pseudo
/// catalogs never break the markup parser under test.
/// </summary>
public static class PseudoLocaleGenerator
{
    /// <summary>Display tag for generated test catalogs.</summary>
    public const string PseudoLocaleCode = "qps-ploc";

    /// <summary>Catalog file name for generated test catalogs.</summary>
    public const string PseudoFileName = "qps-ploc.json";

    public const string Prefix = "[!!! ";
    public const string Suffix = " !!!]";

    private static readonly Dictionary<char, char> AccentMap = new()
    {
        ['A'] = 'Ȁ', ['B'] = 'Ɓ', ['C'] = 'Ƈ', ['D'] = 'Ɖ', ['E'] = 'Ē',
        ['F'] = 'Ƒ', ['G'] = 'Ɠ', ['H'] = 'Ĥ', ['I'] = 'Ī', ['J'] = 'Ĵ',
        ['K'] = 'Ƙ', ['L'] = 'Ľ', ['M'] = 'Ṁ', ['N'] = 'Ň', ['O'] = 'Ō',
        ['P'] = 'Ƥ', ['Q'] = 'Ǫ', ['R'] = 'Ř', ['S'] = 'Ŝ', ['T'] = 'Ŧ',
        ['U'] = 'Ū', ['V'] = 'Ṽ', ['W'] = 'Ŵ', ['X'] = 'Ẋ', ['Y'] = 'Ŷ',
        ['Z'] = 'Ẑ',
        ['a'] = 'ȧ', ['b'] = 'ƀ', ['c'] = 'ƈ', ['d'] = 'ɗ', ['e'] = 'ē',
        ['f'] = 'ƒ', ['g'] = 'ǥ', ['h'] = 'ĥ', ['i'] = 'ī', ['j'] = 'ĵ',
        ['k'] = 'ƙ', ['l'] = 'ľ', ['m'] = 'ṁ', ['n'] = 'ň', ['o'] = 'ō',
        ['p'] = 'ƥ', ['q'] = 'ʠ', ['r'] = 'ř', ['s'] = 'ŝ', ['t'] = 'ŧ',
        ['u'] = 'ū', ['v'] = 'ṽ', ['w'] = 'ŵ', ['x'] = 'ẋ', ['y'] = 'ẏ',
        ['z'] = 'ẑ',
    };

    /// <summary>
    /// Pseudo-localizes one string. Null becomes empty; tag spans survive
    /// untouched; everything else maps deterministically (no randomness,
    /// so golden tests stay byte-stable).
    /// </summary>
    public static string Generate(string? text)
    {
        if (string.IsNullOrEmpty(text))
            return Prefix + Suffix;
        var builder = new StringBuilder(text.Length + Prefix.Length + Suffix.Length);
        builder.Append(Prefix);
        for (int i = 0; i < text.Length; i++)
        {
            char c = text[i];
            if (c == '<')
            {
                int end = text.IndexOf('>', i);
                if (end > i)
                {
                    builder.Append(text, i, end - i + 1);
                    i = end;
                    continue;
                }
            }
            builder.Append(AccentMap.TryGetValue(c, out char mapped) ? mapped : c);
        }
        builder.Append(Suffix);
        return builder.ToString();
    }

    /// <summary>
    /// Builds a complete test catalog JSON for <paramref name="inventory"/>.
    /// Every entry carries the current <c>source_hash</c>, so the desk
    /// reports the pseudo catalog as Translated until the source changes.
    /// </summary>
    public static string GenerateCatalog(
        IReadOnlyList<SourceDialogue> inventory, string locale = PseudoLocaleCode)
    {
        string? normalized = LocalizationService.NormalizeLocale(locale) ?? "qps";
        var entries = new Dictionary<string, TranslationStatusService.CatalogEntry>(StringComparer.Ordinal);
        foreach (SourceDialogue source in inventory)
        {
            entries[source.ContentId] = new TranslationStatusService.CatalogEntry(
                Generate(source.Speaker),
                Generate(source.SourceText),
                string.Empty,
                TranslationInventoryService.ComputeSourceHash(source.Speaker, source.SourceText));
        }
        return TranslationStatusService.SerializeCatalog(normalized, entries);
    }
}
