using System.Text.RegularExpressions;
using Avalonia.Media;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Tests
{
    /// <summary>
    /// Faz 1 kilidi (docs/EDITOR_UI_PORT_PLAN.md): varsayılan palet ÜÇ yerde
    /// yaşıyor — ThemeStyles.axaml (açılış), SettingsViewModel paletleri (tema
    /// seçilince app.Resources'ı EZER) ve ThemeFallbackColors (kaynak
    /// bulunamazsa). Biri değişip diğeri unutulursa tema seçimi ya da yedek yol
    /// eski renkleri sessizce geri getirir; bu test o kaymayı kırmızı yapar.
    /// </summary>
    public sealed class EditorThemePaletteDriftTests
    {
        private const string DefaultTheme = "Kemik Beyazı (Karanlık)";
        private const string OledTheme = "Saf Siyah (OLED)";

        private static Dictionary<string, Color> ReadThemeStyles()
        {
            string root = ProjectFileSystem.ResolveProjectRootFrom(System.AppContext.BaseDirectory);
            string xaml = File.ReadAllText(Path.Combine(root, "editor", "Styles", "ThemeStyles.axaml"));
            var colors = new Dictionary<string, Color>();
            foreach (Match m in Regex.Matches(xaml, "<Color x:Key=\"(\\w+)\">(#[0-9A-Fa-f]{6,8})</Color>"))
                colors.Add(m.Groups[1].Value, Color.Parse(m.Groups[2].Value));
            Assert.True(colors.Count > 20, $"ThemeStyles.axaml'da yalnız {colors.Count} renk okundu; ayrıştırma bozuk.");
            return colors;
        }

        [Fact]
        public void DefaultPalette_MatchesThemeStylesExactly()
        {
            var styles = ReadThemeStyles();
            var palette = SettingsViewModel.GetThemePalette(DefaultTheme);

            Assert.Equal(styles.Keys.OrderBy(k => k), palette.Keys.OrderBy(k => k));
            var drift = styles.Where(kv => palette[kv.Key] != kv.Value)
                .Select(kv => $"{kv.Key}: axaml {kv.Value} != palet {palette[kv.Key]}")
                .ToList();
            Assert.True(drift.Count == 0, "Varsayılan palet kaydı:\n" + string.Join("\n", drift));
        }

        [Fact]
        public void OledPalette_DefinesEveryKeyOfTheDefault()
        {
            var defaults = SettingsViewModel.GetThemePalette(DefaultTheme);
            var oled = SettingsViewModel.GetThemePalette(OledTheme);
            Assert.Equal(defaults.Keys.OrderBy(k => k), oled.Keys.OrderBy(k => k));
            // OLED, varsayılanın bir kademe koyusu olmalı: yüzeyler daha açık olamaz.
            foreach (string key in new[] { "AppBackgroundColor", "SurfaceBackgroundColor", "PanelBackgroundColor", "InputBackgroundColor" })
            {
                Color d = defaults[key], o = oled[key];
                Assert.True(o.R + o.G + o.B <= d.R + d.G + d.B, $"OLED {key} ({o}) varsayılandan ({d}) açık.");
            }
        }

        [Fact]
        public void CanvasBackground_IsPrototypeSurface()
        {
            // Faz 8 A1 kararı (Chaple, 2026-10-16): tuval zemini prototip yüzeyi, saf siyah değil;
            // nokta ızgara prototip .node-area tonu. OLED teması saf siyahını korur.
            var defaults = SettingsViewModel.GetThemePalette(DefaultTheme);
            Assert.Equal(Color.Parse("#131315"), defaults["CanvasBackgroundColor"]);
            Assert.Equal(defaults["SurfaceBackgroundColor"], defaults["CanvasBackgroundColor"]);
            Assert.Equal(Color.Parse("#303034"), defaults["GraphDotColorValue"]);
            Assert.Equal(Color.Parse("#000000"), SettingsViewModel.GetThemePalette(OledTheme)["CanvasBackgroundColor"]);
        }

        [Fact]
        public void FallbackColors_MatchDefaultPalette()
        {
            var palette = SettingsViewModel.GetThemePalette(DefaultTheme);
            Assert.Equal(palette["SurfaceBackgroundColor"], ThemeFallbackColors.Surface);
            Assert.Equal(palette["BorderColorValue"], ThemeFallbackColors.Border);
            Assert.Equal(palette["PrimaryTextColor"], ThemeFallbackColors.Text);
            Assert.Equal(palette["MutedTextColor"], ThemeFallbackColors.Muted);
            Assert.Equal(palette["DimTextColor"], ThemeFallbackColors.Dim);
            Assert.Equal(palette["SuccessColor"], ThemeFallbackColors.Success);
            Assert.Equal(palette["WarningColor"], ThemeFallbackColors.Warning);
            Assert.Equal(palette["ErrorColor"], ThemeFallbackColors.Error);
            Assert.Equal(palette["InfoColor"], ThemeFallbackColors.Info);
        }
    }
}
