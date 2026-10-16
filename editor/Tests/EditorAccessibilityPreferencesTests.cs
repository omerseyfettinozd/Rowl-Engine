using System;
using System.IO;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using Xunit;

namespace RowlEngine.Editor.Tests;

// Erişilebilirlik tercihleri: kalıcılık + snap + kontrast (uygulamasız).
[Collection("StaticRootSequential")]
public sealed class EditorAccessibilityPreferencesTests
{
    private const string DefaultTheme = "Kemik Beyazı (Karanlık)";
    private const string OledTheme = "Saf Siyah (OLED)";

    private static string TempPath() =>
        Path.Combine(Path.GetTempPath(), "rowl-acc-" + Guid.NewGuid().ToString("N") + ".json");

    [Fact]
    public void Profile_RoundTrip_PersistsUiScaleAndHighContrast()
    {
        string path = TempPath();
        try
        {
            // Kaydet/yükle gidiş-dönüş.
            new EditorSettingsProfile { UiScale = 1.3, HighContrast = true }.Save(path);
            var loaded = EditorSettingsProfile.Load(path);
            Assert.Equal(1.3, loaded.UiScale);
            Assert.True(loaded.HighContrast);
        }
        finally { try { File.Delete(path); } catch { } }
    }

    [Fact]
    public void Profile_OldJson_MissingFields_GetDefaults()
    {
        string path = TempPath();
        try
        {
            // Eski JSON (alanlar yok) → varsayılan.
            File.WriteAllText(path, "{\"AutoSaveEnabled\":false}");
            var loaded = EditorSettingsProfile.Load(path);
            Assert.Equal(1.0, loaded.UiScale);
            Assert.False(loaded.HighContrast);
        }
        finally { try { File.Delete(path); } catch { } }
    }

    [Theory]
    [InlineData(1.2, 1.15)]
    [InlineData(5.0, 1.3)]
    [InlineData(1.0, 1.0)]
    [InlineData(1.3, 1.3)]
    [InlineData(1.15, 1.15)]
    public void Profile_Sanitized_SnapsUiScale(double input, double expected)
    {
        var p = new EditorSettingsProfile { UiScale = input };
        p.Sanitized();
        Assert.Equal(expected, p.UiScale);
    }

    [Fact]
    public void Profile_Sanitized_NaN_GoesToDefault()
    {
        var p = new EditorSettingsProfile { UiScale = double.NaN };
        p.Sanitized();
        Assert.Equal(1.0, p.UiScale);
    }

    [Fact]
    public void UiScale_Apply_RaisesChangedOnlyWhenChanged()
    {
        EditorUiScale.Apply(1.0);
        int count = 0;
        double last = 0;
        void Handler(double v) { count++; last = v; }
        EditorUiScale.Changed += Handler;
        try
        {
            EditorUiScale.Apply(1.0);
            Assert.Equal(0, count);
            EditorUiScale.Apply(1.3);
            Assert.Equal(1, count);
            Assert.Equal(1.3, last);
            Assert.Equal(1.3, EditorUiScale.Current);
            // 1.29 → 1.3 oturur, değişim yok.
            EditorUiScale.Apply(1.29);
            Assert.Equal(1, count);
            Assert.Equal(1.15, EditorUiScale.Snap(1.2));
        }
        finally
        {
            EditorUiScale.Changed -= Handler;
            EditorUiScale.Apply(1.0);
        }
    }

    [Fact]
    public void Overlay_Keys_Exist_In_Default_Palette()
    {
        var defaults = SettingsViewModel.GetThemePalette(DefaultTheme);
        foreach (string key in EditorHighContrast.Overlay.Keys)
            Assert.Contains(key, defaults.Keys);
    }

    [Fact]
    public void HighContrast_Ratios_MeetThresholds_OnDefault()
    {
        var defaults = SettingsViewModel.GetThemePalette(DefaultTheme);
        var overlay = EditorHighContrast.Overlay;
        var panel = defaults["PanelBackgroundColor"];
        // Eşikler: soluk metin ≥7, sönük ≥4.5, kenar ≥2 ve eskisinden iyi.
        Assert.True(EditorHighContrast.ContrastRatio(overlay["MutedTextColor"], panel) >= 7.0, "Muted düşük.");
        Assert.True(EditorHighContrast.ContrastRatio(overlay["DimTextColor"], panel) >= 4.5, "Dim düşük.");
        double overBorder = EditorHighContrast.ContrastRatio(overlay["BorderColorValue"], panel);
        double defBorder = EditorHighContrast.ContrastRatio(defaults["BorderColorValue"], panel);
        Assert.True(overBorder >= 2.0, "Border düşük.");
        Assert.True(overBorder > defBorder, "Overlay eskiden iyi olmalı.");
    }

    [Fact]
    public void HighContrast_Ratios_MeetThresholds_OnOled()
    {
        var oled = SettingsViewModel.GetThemePalette(OledTheme);
        var overlay = EditorHighContrast.Overlay;
        var panel = oled["PanelBackgroundColor"];
        Assert.True(EditorHighContrast.ContrastRatio(overlay["MutedTextColor"], panel) >= 7.0, "OLED Muted düşük.");
        Assert.True(EditorHighContrast.ContrastRatio(overlay["DimTextColor"], panel) >= 4.5, "OLED Dim düşük.");
        double overBorder = EditorHighContrast.ContrastRatio(overlay["BorderColorValue"], panel);
        double defBorder = EditorHighContrast.ContrastRatio(oled["BorderColorValue"], panel);
        Assert.True(overBorder >= 2.0, "OLED Border düşük.");
        Assert.True(overBorder > defBorder, "OLED overlay eskiden iyi olmalı.");
    }
}
