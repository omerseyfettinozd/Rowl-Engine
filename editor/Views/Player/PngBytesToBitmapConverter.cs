using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using Avalonia.Data.Converters;
using Avalonia.Media.Imaging;

namespace RowlEngine.Editor.Views.Player;

/// <summary>
/// Decodes raw thumbnail PNG bytes (see PlayerSaveSlotEntry) into a
/// Bitmap on the UI thread. Null or malformed input yields null so a bad
/// slot never breaks the picker.
/// </summary>
public sealed class PngBytesToBitmapConverter : IValueConverter
{
    public static readonly PngBytesToBitmapConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is not byte[] bytes || bytes.Length == 0)
            return null;
        try
        {
            using var stream = new MemoryStream(bytes, writable: false);
            return new Bitmap(stream);
        }
        catch (Exception failure)
        {
            Debug.WriteLine($"Player thumbnail Bitmap decode failed: {failure.Message}");
            return null;
        }
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
