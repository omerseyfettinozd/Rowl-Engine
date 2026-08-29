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
        private string _selectedTheme = "Rowl Cyber Dark";

        public List<string> AvailableThemes { get; } = new()
        {
            "Rowl Cyber Dark",
            "Midnight OLED",
            "Unreal Slate",
            "Nordic Emerald"
        };

        // ── Build & Dışa Aktarım ────────────────────────────────────────────
        [ObservableProperty]
        private string _defaultBuildTarget = "Linux";

        [ObservableProperty]
        private string _defaultExportPath = "";

        public List<string> BuildTargetOptions { get; } = new()
        {
            "Windows", "Linux", "macOS", "Android", "iOS", "PackageOnly"
        };

        // ── Genel Editör Ayarları ───────────────────────────────────────────
        [ObservableProperty]
        private bool _autoSaveEnabled = true;

        [ObservableProperty]
        private int _autoSaveIntervalSeconds = 60;

        public List<int> AutoSaveIntervals { get; } = new() { 15, 30, 60, 120, 300 };

        [ObservableProperty]
        private bool _showFpsOverlay = false;

        [ObservableProperty]
        private bool _gridSnapping = false;

        [ObservableProperty]
        private string _cableStyle = "Bezier";

        public List<string> CableStyleOptions { get; } = new() { "Bezier", "Düz Çizgi" };

        [ObservableProperty]
        private bool _showNodeMinimap = false;

        [ObservableProperty]
        private string _editorLanguage = "Türkçe";

        public List<string> LanguageOptions { get; } = new() { "Türkçe", "English" };

        [ObservableProperty]
        private int _selectedTabIndex = 0;

        // ── Tema Uygulama ───────────────────────────────────────────────────
        private static readonly Dictionary<string, Dictionary<string, Color>> ThemePalettes = new()
        {
            ["Rowl Cyber Dark"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#121218"),
                ["SurfaceBackgroundColor"] = Color.Parse("#1E1E2A"),
                ["PanelBackgroundColor"] = Color.Parse("#181822"),
                ["InputBackgroundColor"] = Color.Parse("#0F172A"),
                ["CanvasBackgroundColor"] = Color.Parse("#0B0F19"),
                ["BorderColorValue"] = Color.Parse("#2D2D3F"),
                ["BorderSubtleColorValue"] = Color.Parse("#334155"),
                ["PrimaryTextColor"] = Color.Parse("#F8FAFC"),
                ["SecondaryTextColor"] = Color.Parse("#CBD5E1"),
                ["MutedTextColor"] = Color.Parse("#94A3B8"),
                ["DimTextColor"] = Color.Parse("#64748B"),
                ["AccentColor"] = Color.Parse("#38BDF8"),
                ["AccentHoverColor"] = Color.Parse("#7DD3FC"),
                ["AccentButtonBgColor"] = Color.Parse("#2563EB"),
                ["AccentButtonHoverColor"] = Color.Parse("#3B82F6"),
                ["ToolbarButtonBgColor"] = Color.Parse("#1E293B"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#334155"),
                ["StatusBarBgColor"] = Color.Parse("#0F172A"),
                ["NodeCardBgColor"] = Color.Parse("#181825"),
                ["NodeHeaderBgColor"] = Color.Parse("#1E1E2E"),
            },
            ["Midnight OLED"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#000000"),
                ["SurfaceBackgroundColor"] = Color.Parse("#0A0A12"),
                ["PanelBackgroundColor"] = Color.Parse("#050510"),
                ["InputBackgroundColor"] = Color.Parse("#0A0A18"),
                ["CanvasBackgroundColor"] = Color.Parse("#000000"),
                ["BorderColorValue"] = Color.Parse("#1A1A2E"),
                ["BorderSubtleColorValue"] = Color.Parse("#2A2A3E"),
                ["PrimaryTextColor"] = Color.Parse("#F0F0F8"),
                ["SecondaryTextColor"] = Color.Parse("#B8B8D0"),
                ["MutedTextColor"] = Color.Parse("#7878A0"),
                ["DimTextColor"] = Color.Parse("#505078"),
                ["AccentColor"] = Color.Parse("#A78BFA"),
                ["AccentHoverColor"] = Color.Parse("#C4B5FD"),
                ["AccentButtonBgColor"] = Color.Parse("#7C3AED"),
                ["AccentButtonHoverColor"] = Color.Parse("#8B5CF6"),
                ["ToolbarButtonBgColor"] = Color.Parse("#0F0F1E"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#1A1A2E"),
                ["StatusBarBgColor"] = Color.Parse("#050510"),
                ["NodeCardBgColor"] = Color.Parse("#0A0A14"),
                ["NodeHeaderBgColor"] = Color.Parse("#0F0F1A"),
            },
            ["Unreal Slate"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#1A1A1A"),
                ["SurfaceBackgroundColor"] = Color.Parse("#2A2A2A"),
                ["PanelBackgroundColor"] = Color.Parse("#222222"),
                ["InputBackgroundColor"] = Color.Parse("#1A1A1A"),
                ["CanvasBackgroundColor"] = Color.Parse("#141414"),
                ["BorderColorValue"] = Color.Parse("#3A3A3A"),
                ["BorderSubtleColorValue"] = Color.Parse("#4A4A4A"),
                ["PrimaryTextColor"] = Color.Parse("#E8E8E8"),
                ["SecondaryTextColor"] = Color.Parse("#C0C0C0"),
                ["MutedTextColor"] = Color.Parse("#909090"),
                ["DimTextColor"] = Color.Parse("#707070"),
                ["AccentColor"] = Color.Parse("#F59E0B"),
                ["AccentHoverColor"] = Color.Parse("#FBBF24"),
                ["AccentButtonBgColor"] = Color.Parse("#D97706"),
                ["AccentButtonHoverColor"] = Color.Parse("#F59E0B"),
                ["ToolbarButtonBgColor"] = Color.Parse("#2A2A2A"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#3A3A3A"),
                ["StatusBarBgColor"] = Color.Parse("#1A1A1A"),
                ["NodeCardBgColor"] = Color.Parse("#222222"),
                ["NodeHeaderBgColor"] = Color.Parse("#2A2A2A"),
            },
            ["Nordic Emerald"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#0F1A14"),
                ["SurfaceBackgroundColor"] = Color.Parse("#162420"),
                ["PanelBackgroundColor"] = Color.Parse("#121E1A"),
                ["InputBackgroundColor"] = Color.Parse("#0A1610"),
                ["CanvasBackgroundColor"] = Color.Parse("#081210"),
                ["BorderColorValue"] = Color.Parse("#1E3A30"),
                ["BorderSubtleColorValue"] = Color.Parse("#2A4A3E"),
                ["PrimaryTextColor"] = Color.Parse("#E8F5E9"),
                ["SecondaryTextColor"] = Color.Parse("#A5D6A7"),
                ["MutedTextColor"] = Color.Parse("#6B9E78"),
                ["DimTextColor"] = Color.Parse("#4A7A58"),
                ["AccentColor"] = Color.Parse("#34D399"),
                ["AccentHoverColor"] = Color.Parse("#6EE7B7"),
                ["AccentButtonBgColor"] = Color.Parse("#059669"),
                ["AccentButtonHoverColor"] = Color.Parse("#10B981"),
                ["ToolbarButtonBgColor"] = Color.Parse("#162420"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#1E3A30"),
                ["StatusBarBgColor"] = Color.Parse("#0A1610"),
                ["NodeCardBgColor"] = Color.Parse("#121E1A"),
                ["NodeHeaderBgColor"] = Color.Parse("#162420"),
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
            SelectedTheme = "Rowl Cyber Dark";
            AutoSaveEnabled = true;
            AutoSaveIntervalSeconds = 60;
            ShowFpsOverlay = false;
            GridSnapping = false;
            CableStyle = "Bezier";
            ShowNodeMinimap = false;
            EditorLanguage = "Türkçe";
            DefaultBuildTarget = "Linux";
            DefaultExportPath = "";
            ApplyTheme();
        }
    }
}
