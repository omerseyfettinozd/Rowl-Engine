using System;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Styling;

namespace RowlEngine.Editor.Tools.RowlEye.Targets;

/// <summary>
/// Wraps arbitrary Avalonia controls in an isolated host window for rendering without full desktop shell dependencies.
/// </summary>
public static class RowlEyeIsolationHost
{
    public static Window WrapInWindow(Control content, Size size, string theme = "Dark")
    {
        ArgumentNullException.ThrowIfNull(content);

        var window = new Window
        {
            Content = content,
            Width = size.Width,
            Height = size.Height,
            CanResize = false
        };

        if (string.Equals(theme, "Light", StringComparison.OrdinalIgnoreCase))
        {
            window.RequestedThemeVariant = ThemeVariant.Light;
        }
        else
        {
            window.RequestedThemeVariant = ThemeVariant.Dark;
        }

        return window;
    }
}
