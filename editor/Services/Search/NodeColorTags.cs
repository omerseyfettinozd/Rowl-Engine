using System;
using System.Collections.Generic;
using Avalonia.Media;

namespace RowlEngine.Editor.Services.Search;

/// <summary>
/// Faz 4 Dilim 2 — canonical node color-tag palette. Tags are stored as
/// lowercase names (<c>metadata.color_tag</c>); this type maps them to hex
/// for card strips, minimap dots and the filter bar. Unknown non-empty
/// values are preserved in JSON but render as slate fallback — never throw.
/// </summary>
public static class NodeColorTags
{
    public const int MaxTagChars = 64;
    public const int MaxTagsPerNode = 32;

    public const string None = "";

    private static readonly KeyValuePair<string, string>[] Palette =
    {
        new("red", "#EF4444"),
        new("orange", "#F59E0B"),
        new("yellow", "#EAB308"),
        new("green", "#22C55E"),
        new("blue", "#3B82F6"),
        new("purple", "#A855F7"),
        new("pink", "#EC4899"),
        new("gray", "#6B7280"),
    };

    public const string FallbackHex = "#64748B";

    private static readonly Dictionary<string, IBrush> BrushCache = new(StringComparer.Ordinal);
    private static readonly Dictionary<string, IBrush> DimBrushCache = new(StringComparer.Ordinal);

    /// <summary>Palette names in display order (without the empty none).</summary>
    public static IReadOnlyList<string> PaletteNames
    {
        get
        {
            var names = new List<string>(Palette.Length);
            foreach (var entry in Palette)
                names.Add(entry.Key);
            return names;
        }
    }

    /// <summary>Trims + lowercases; null/whitespace becomes <see cref="None"/>.</summary>
    public static string Normalize(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return None;
        string normalized = value.Trim().ToLowerInvariant();
        return normalized.Length > MaxTagChars
            ? normalized.Substring(0, MaxTagChars)
            : normalized;
    }

    public static bool IsKnown(string? tag) =>
        !string.IsNullOrEmpty(tag) && TryGetHex(tag, out _);

    public static bool TryGetHex(string? tag, out string hex)
    {
        hex = FallbackHex;
        if (string.IsNullOrEmpty(tag))
            return false;
        foreach (var entry in Palette)
        {
            if (string.Equals(entry.Key, tag, StringComparison.OrdinalIgnoreCase))
            {
                hex = entry.Value;
                return true;
            }
        }
        return false;
    }

    public static string ToHex(string? tag) =>
        TryGetHex(tag, out string hex) ? hex : FallbackHex;

    /// <summary>Cached brush for card strips and minimap dots (dim variant for filtered-out nodes).</summary>
    public static IBrush GetBrush(string? tag, bool dimmed = false)
    {
        string key = (Normalize(tag) is string n && n.Length > 0 ? n : "none") + (dimmed ? ":dim" : string.Empty);
        var cache = dimmed ? DimBrushCache : BrushCache;
        lock (cache)
        {
            if (cache.TryGetValue(key, out IBrush? brush))
                return brush;
            string hex = string.IsNullOrEmpty(Normalize(tag)) ? "#38BDF8" : ToHex(tag);
            byte alpha = dimmed ? (byte)48 : (byte)255;
            Color color = Color.Parse(hex);
            brush = new SolidColorBrush(Color.FromArgb(alpha, color.R, color.G, color.B));
            cache[key] = brush;
            return brush;
        }
    }

    /// <summary>Assignment dropdown options: none + palette (stable order).</summary>
    public static IReadOnlyList<ColorTagOption> AssignmentOptions
    {
        get
        {
            var options = new List<ColorTagOption>(Palette.Length + 1)
            {
                new(None, "(Yok)"),
            };
            foreach (var entry in Palette)
                options.Add(new ColorTagOption(entry.Key, Capitalize(entry.Key)));
            return options;
        }
    }

    private static string Capitalize(string name) =>
        name.Length == 0 ? name : char.ToUpperInvariant(name[0]) + name.Substring(1);

    /// <summary>Normalizes one free-form list tag; null when it should be dropped.</summary>
    public static string? NormalizeListTag(string? value)
    {
        if (string.IsNullOrWhiteSpace(value))
            return null;
        string normalized = value.Trim();
        if (normalized.Length == 0 || normalized.Length > MaxTagChars)
            return null;
        return normalized;
    }
}

/// <summary>One assignment-dropdown row (value persisted, label shown).</summary>
public sealed class ColorTagOption
{
    public ColorTagOption(string value, string label)
    {
        Value = value;
        Label = label;
    }

    public string Value { get; }

    public string Label { get; }

    public IBrush Brush => string.IsNullOrEmpty(Value)
        ? Brushes.Transparent
        : NodeColorTags.GetBrush(Value);
}
