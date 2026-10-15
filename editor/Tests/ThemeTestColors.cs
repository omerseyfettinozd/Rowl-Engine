using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// Testler renkleri hex olarak gömmek yerine varsayılan paletten okur; palet
/// değişince (EDITOR_UI_PORT_PLAN fazları) bu testler kendiliğinden izler.
/// Paletin kendisi EditorThemePaletteDriftTests ile kilitlidir.
/// </summary>
internal static class ThemeTestColors
{
    public static string Hex(string colorKey)
    {
        var c = SettingsViewModel.GetThemePalette("Kemik Beyazı (Karanlık)")[colorKey];
        return $"#{c.R:X2}{c.G:X2}{c.B:X2}";
    }
}
