using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace RowlEngine.Editor.ViewModels
{
    public partial class SettingsViewModel : ObservableObject
    {
        // ── Tema ────────────────────────────────────────────────────────────
        [ObservableProperty]
        private string _selectedTheme = "Kemik Beyazı (Karanlık)";

        public List<string> AvailableThemes { get; } = new()
        {
            "Kemik Beyazı (Karanlık)",
            "Saf Siyah (OLED)"
        };

        // ── Genel Editör Ayarları ───────────────────────────────────────────
        [ObservableProperty]
        private bool _autoSaveEnabled = true;

        [ObservableProperty]
        private int _autoSaveIntervalSeconds = 60;

        // Faz 6: varlık ızgarası simge boyutu (kaydırıcı AssetBrowserViewModel'dedir;
        // burası makine profiline giden/gelen kalıcılık köprüsüdür).
        [ObservableProperty]
        private double _assetGridItemSize = 72;

        public List<int> AutoSaveIntervals { get; } = new() { 15, 30, 60, 120, 300 };

        [ObservableProperty]
        private int _selectedTabIndex = 0;

        // ── Proje runtime ayarları ─────────────────────────────────────────
        [ObservableProperty] private int _projectSaveSlotCount = 10;
        [ObservableProperty] private string _projectDefaultBgmTransition = "instant";
        [ObservableProperty] private float _projectDefaultBgmTransitionDurationSeconds = 1;
        public List<string> ProjectBgmTransitionOptions { get; } = new() { "instant", "fade", "crossfade" };

        // ── Oyuncu tercihleri ───────────────────────────────────────────────
        // Bunlar proje dosyasına değil, cihazdaki PlayerSettingsProfile'a yazılır.
        [ObservableProperty] private float _masterVolume = 1;
        [ObservableProperty] private float _bgmVolume = 1;
        [ObservableProperty] private float _voiceVolume = 1;
        [ObservableProperty] private float _sfxVolume = 1;
        [ObservableProperty] private float _textSpeedMultiplier = 1;
        [ObservableProperty] private float _autoAdvanceDelay = 2;

        // ── Tema Uygulama ───────────────────────────────────────────────────
        private static readonly Dictionary<string, Dictionary<string, Color>> ThemePalettes = new()
        {
            ["Kemik Beyazı (Karanlık)"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#0A0A0B"),
                ["SurfaceBackgroundColor"] = Color.Parse("#131315"),
                ["PanelBackgroundColor"] = Color.Parse("#17171A"),
                ["InputBackgroundColor"] = Color.Parse("#0E0E10"),
                ["CanvasBackgroundColor"] = Color.Parse("#000000"),
                ["BorderColorValue"] = Color.Parse("#26262B"),
                ["BorderSubtleColorValue"] = Color.Parse("#34343B"),
                ["PrimaryTextColor"] = Color.Parse("#F2EFE6"),
                ["SecondaryTextColor"] = Color.Parse("#D8D4CC"),
                ["MutedTextColor"] = Color.Parse("#A8A49C"),
                ["DimTextColor"] = Color.Parse("#6E6C66"),
                ["AccentColor"] = Color.Parse("#F2EFE6"),
                ["AccentHoverColor"] = Color.Parse("#FFFFFF"),
                ["AccentButtonBgColor"] = Color.Parse("#2A2A2E"),
                ["AccentButtonHoverColor"] = Color.Parse("#3A3A40"),
                ["ToolbarButtonBgColor"] = Color.Parse("#222226"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#333338"),
                ["DangerButtonBgColor"] = Color.Parse("#6E3235"),
                ["PlayButtonGreenColor"] = Color.Parse("#4A5D4C"),
                ["PlayButtonRedColor"] = Color.Parse("#6E3235"),
                ["StatusBarBgColor"] = Color.Parse("#0A0A0B"),
                ["NodeCardBgColor"] = Color.Parse("#17171A"),
                ["NodeHeaderBgColor"] = Color.Parse("#222226"),
                ["WireColor"] = Color.Parse("#8E8E96"),
                ["WireDragColor"] = Color.Parse("#F2EFE6"),
                ["SuccessColor"] = Color.Parse("#7DA56D"),
                ["WarningColor"] = Color.Parse("#DCA85A"),
                ["ErrorColor"] = Color.Parse("#D96868"),
                ["InfoColor"] = Color.Parse("#9C7FD1"),
            },
            ["Saf Siyah (OLED)"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#000000"),
                ["SurfaceBackgroundColor"] = Color.Parse("#0A0A0C"),
                ["PanelBackgroundColor"] = Color.Parse("#101013"),
                ["InputBackgroundColor"] = Color.Parse("#000000"),
                ["CanvasBackgroundColor"] = Color.Parse("#000000"),
                ["BorderColorValue"] = Color.Parse("#1C1C20"),
                ["BorderSubtleColorValue"] = Color.Parse("#2A2A2F"),
                ["PrimaryTextColor"] = Color.Parse("#F2EFE6"),
                ["SecondaryTextColor"] = Color.Parse("#D8D4CC"),
                ["MutedTextColor"] = Color.Parse("#A8A49C"),
                ["DimTextColor"] = Color.Parse("#6E6C66"),
                ["AccentColor"] = Color.Parse("#F2EFE6"),
                ["AccentHoverColor"] = Color.Parse("#FFFFFF"),
                ["AccentButtonBgColor"] = Color.Parse("#161618"),
                ["AccentButtonHoverColor"] = Color.Parse("#232327"),
                ["ToolbarButtonBgColor"] = Color.Parse("#101012"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#1A1A1E"),
                ["DangerButtonBgColor"] = Color.Parse("#6E3235"),
                ["PlayButtonGreenColor"] = Color.Parse("#4A5D4C"),
                ["PlayButtonRedColor"] = Color.Parse("#6E3235"),
                ["StatusBarBgColor"] = Color.Parse("#000000"),
                ["NodeCardBgColor"] = Color.Parse("#000000"),
                ["NodeHeaderBgColor"] = Color.Parse("#101012"),
                ["WireColor"] = Color.Parse("#8E8E96"),
                ["WireDragColor"] = Color.Parse("#F2EFE6"),
                ["SuccessColor"] = Color.Parse("#7DA56D"),
                ["WarningColor"] = Color.Parse("#DCA85A"),
                ["ErrorColor"] = Color.Parse("#D96868"),
                ["InfoColor"] = Color.Parse("#9C7FD1"),
            },
        };

        [RelayCommand]
        public void ApplyTheme()
        {
            ApplyTheme(SelectedTheme);
        }

        public static void ApplyTheme(string themeName)
        {
            if (!ThemePalettes.TryGetValue(themeName, out var palette)) return;

            var app = Application.Current;
            if (app == null) return;

            foreach (var (key, color) in palette)
            {
                app.Resources[key] = color;
            }
        }

        [RelayCommand]
        public void ResetDefaults()
        {
            SelectedTheme = "Kemik Beyazı (Karanlık)";
            AutoSaveEnabled = true;
            AutoSaveIntervalSeconds = 60;
            MasterVolume = BgmVolume = VoiceVolume = SfxVolume = 1;
            TextSpeedMultiplier = 1;
            AutoAdvanceDelay = 2;
            ApplyTheme();
        }
    }
}
