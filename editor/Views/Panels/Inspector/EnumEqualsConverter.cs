using System;
using System.Globalization;
using Avalonia.Data;
using Avalonia.Data.Converters;

namespace RowlEngine.Editor.Views.Panels.Inspector;

public sealed class EnumEqualsConverter : IValueConverter
{
    public static EnumEqualsConverter Instance { get; } = new();

    public object? Convert(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        return value?.ToString() == parameter as string;
    }

    public object? ConvertBack(object? value, Type targetType, object? parameter, CultureInfo culture)
    {
        if (Equals(true, value) && parameter is string name)
        {
            return Enum.Parse(targetType, name);
        }

        return BindingOperations.DoNothing;
    }
}
