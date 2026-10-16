using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Media;

namespace RowlEngine.Editor.Services;

/// <summary>Faz 8 D1: yüksek kontrast overlay (tema üstüne ezilir).</summary>
public static class EditorHighContrast
{
    // Açık mı?
    public static bool Enabled { get; private set; }

    // Ezilen palet anahtarları (hex yalnız burada).
    public static IReadOnlyDictionary<string, Color> Overlay { get; } = new Dictionary<string, Color>
    {
        ["MutedTextColor"] = Color.Parse("#B8B6AE"),
        ["DimTextColor"] = Color.Parse("#9C9A93"),
        ["BorderColorValue"] = Color.Parse("#55545A"),
        ["BorderSubtleColorValue"] = Color.Parse("#77767C"),
        ["GraphDotColorValue"] = Color.Parse("#45454A"),
        ["WireColor"] = Color.Parse("#A5A39A"),
    };

    /// <summary>
    /// Önce temanın paletini geri koyar, sonra açıksa katmanı yazar (özyineleme yok).
    /// Durum değişmiyorsa kaynaklara dokunmaz: her pencere/viewmodel kurulumunda ayar
    /// yüklemesi tüm uygulama kaynaklarını yeniden yazmasın (UI iş parçacığı dışından
    /// kurulan viewmodel'ler de kaynak değişikliği bildirimiyle çökmesin). Kaynak yazımı
    /// her zaman UI iş parçacığında yapılır.
    /// </summary>
    public static void Apply(bool enabled, string themeName)
    {
        if (enabled == Enabled)
            return;
        Enabled = enabled;
        var app = Application.Current;
        if (app is null)
            return;
        if (Avalonia.Threading.Dispatcher.UIThread.CheckAccess())
            WriteResources(app, enabled, themeName);
        else
            Avalonia.Threading.Dispatcher.UIThread.Post(() => WriteResources(app, Enabled, themeName));
    }

    private static void WriteResources(Application app, bool enabled, string themeName)
    {
        ViewModels.SettingsViewModel.ApplyPaletteOnly(themeName);
        if (enabled)
        {
            foreach (var (key, color) in Overlay)
                app.Resources[key] = color;
        }
    }

    // WCAG kontrast oranı (sRGB doğrusallaştırma).
    public static double ContrastRatio(Color a, Color b)
    {
        double l1 = RelativeLuminance(a);
        double l2 = RelativeLuminance(b);
        if (l1 < l2)
            (l1, l2) = (l2, l1);
        return (l1 + 0.05) / (l2 + 0.05);
    }

    private static double RelativeLuminance(Color c)
    {
        double r = Linearize(c.R / 255.0);
        double g = Linearize(c.G / 255.0);
        double bl = Linearize(c.B / 255.0);
        return 0.2126 * r + 0.7152 * g + 0.0722 * bl;
    }

    private static double Linearize(double s) =>
        s <= 0.03928 ? s / 12.92 : Math.Pow((s + 0.055) / 1.055, 2.4);
}
