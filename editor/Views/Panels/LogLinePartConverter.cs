using System;
using System.Globalization;
using Avalonia.Data.Converters;

namespace RowlEngine.Editor.Views.Panels;

/// <summary>
/// Konsol satırını zaman/etiketi ve mesaj parçalarına ayırır.
/// Null-güvenlidir; tek yönlüdür.
/// </summary>
public sealed class LogLinePartConverter : IValueConverter
{
    public static readonly LogLinePartConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        string line = value as string ?? string.Empty;
        string time = string.Empty;
        string message = line;

        int close = line.IndexOf(']');
        if (line.StartsWith('[') && close >= 2 && close - 1 <= 16)
        {
            time = line[1..close];
            message = line[(close + 1)..];
            if (message.StartsWith(' '))
                message = message[1..];
        }

        bool wantTime = string.Equals(parameter as string, "time", StringComparison.Ordinal);
        return wantTime ? time : message;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
