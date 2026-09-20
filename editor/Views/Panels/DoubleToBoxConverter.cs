using System;
using System.Globalization;
using Avalonia;
using Avalonia.Data.Converters;

namespace RowlEngine.Editor.Views.Panels;

/// <summary>
/// Bileşen VM'lerindeki salt-sayı (double) kenar değerlerini Border'ın
/// istediği yapılara çevirir: hedef Thickness ise tekdüze kalınlık,
/// hedef CornerRadius ise tekdüze köşe yarıçapı üretir. double doğrudan
/// bağlanınca Avalonia dönüştüremez (binding hatası + çerçeve kaybolur),
/// bu yüzden önizleme/kart şablonları bu dönüştürücüden geçer.
/// Null ya da sayısal-olmayan değer hedef tipin varsayılanını verir.
/// Tek yönlüdür.
/// </summary>
public sealed class DoubleToBoxConverter : IValueConverter
{
    public static readonly DoubleToBoxConverter Instance = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (value is string text
            && double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double parsed))
            value = parsed;
        if (value is not double number || double.IsNaN(number))
        {
            if (targetType == typeof(Thickness)) return default(Thickness);
            if (targetType == typeof(CornerRadius)) return default(CornerRadius);
            return AvaloniaProperty.UnsetValue;
        }
        if (targetType == typeof(Thickness)) return new Thickness(number);
        if (targetType == typeof(CornerRadius)) return new CornerRadius(number);
        return AvaloniaProperty.UnsetValue;
    }

    public object ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture) =>
        throw new NotSupportedException();
}
