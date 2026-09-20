using System;
using System.Collections.Generic;
using System.Globalization;
using Avalonia.Data.Converters;

namespace RowlEngine.Editor.Views.Panels;

/// <summary>
/// Unity kromu (H4): çok satırlı konsol metnini satır listesine çevirir.
/// Null-güvenlidir; AppendLog'un ayracından kalan sondaki boş satırı atar,
/// içteki boş satırlar log bütünlüğü için korunur. Tek yönlüdür.
/// </summary>
public sealed class LogLinesConverter : IValueConverter
{
    public static readonly LogLinesConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not string text || text.Length == 0)
            return Array.Empty<string>();
        string[] raw = text.Split('\n');
        var lines = new List<string>(raw.Length);
        foreach (string part in raw)
            lines.Add(part.EndsWith('\r') ? part[..^1] : part);
        while (lines.Count > 0 && lines[^1].Length == 0)
            lines.RemoveAt(lines.Count - 1);
        return lines.ToArray();
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
