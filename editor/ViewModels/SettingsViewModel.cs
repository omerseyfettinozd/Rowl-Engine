using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Media;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using RowlEngine.Editor.Services.Strings;

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

        // Faz 2b (K5/K7): editör arayüzü dili — makine profiline kalıcı, canlı uygulanır.
        public sealed record EditorLanguageOption(string Code, string Label);

        public List<EditorLanguageOption> EditorLanguageOptions { get; } = BuildLanguageOptions();

        [ObservableProperty]
        private string _editorLanguage = EditorStrings.SourceLanguage;

        public EditorLanguageOption? SelectedEditorLanguageOption
        {
            get => EditorLanguageOptions.Find(o => o.Code == EditorLanguage);
            set { if (value is not null) EditorLanguage = value.Code; }
        }

        partial void OnEditorLanguageChanged(string value)
        {
            EditorStrings.Instance.SetLanguage(value);
            OnPropertyChanged(nameof(SelectedEditorLanguageOption));
        }

        private static List<EditorLanguageOption> BuildLanguageOptions()
        {
            var options = new List<EditorLanguageOption>();
            foreach (string code in EditorStrings.AvailableLanguages)
                options.Add(new EditorLanguageOption(code, EditorStrings.T("Language." + code)));
            options.Add(new EditorLanguageOption(EditorStrings.PseudoLanguage, EditorStrings.T("Language." + EditorStrings.PseudoLanguage)));
            return options;
        }

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
        /// <summary>Kayma testi için salt-okunur erişim (EditorThemePaletteDriftTests).</summary>
        internal static IReadOnlyDictionary<string, Color> GetThemePalette(string themeName) =>
            ThemePalettes[themeName];

        private static readonly Dictionary<string, Dictionary<string, Color>> ThemePalettes = new()
        {
            ["Kemik Beyazı (Karanlık)"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#0A0A0B"),
                ["SurfaceBackgroundColor"] = Color.Parse("#131315"),
                ["PanelBackgroundColor"] = Color.Parse("#19191C"),
                ["InputBackgroundColor"] = Color.Parse("#0F0F12"),
                ["CanvasBackgroundColor"] = Color.Parse("#000000"),
                ["BorderColorValue"] = Color.Parse("#29292D"),
                ["BorderSubtleColorValue"] = Color.Parse("#4A4A52"),
                ["PrimaryTextColor"] = Color.Parse("#F2EFE6"),
                ["SecondaryTextColor"] = Color.Parse("#DAD7CF"),
                ["MutedTextColor"] = Color.Parse("#85858B"),
                ["DimTextColor"] = Color.Parse("#66666D"),
                ["AccentColor"] = Color.Parse("#F2EFE6"),
                ["AccentHoverColor"] = Color.Parse("#FFFFFF"),
                ["AccentButtonBgColor"] = Color.Parse("#19191D"),
                ["AccentButtonHoverColor"] = Color.Parse("#26262B"),
                ["ToolbarButtonBgColor"] = Color.Parse("#111113"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#1E1E22"),
                ["DangerButtonBgColor"] = Color.Parse("#3E2B26"),
                ["PlayButtonGreenColor"] = Color.Parse("#111113"),
                ["PlayButtonRedColor"] = Color.Parse("#26262B"),
                ["StatusBarBgColor"] = Color.Parse("#0A0A0B"),
                ["NodeCardBgColor"] = Color.Parse("#19191D"),
                ["NodeHeaderBgColor"] = Color.Parse("#202023"),
                ["WireColor"] = Color.Parse("#74736C"),
                ["WireDragColor"] = Color.Parse("#F2EFE6"),
                ["SuccessColor"] = Color.Parse("#9DB58F"),
                ["WarningColor"] = Color.Parse("#D9B47A"),
                ["ErrorColor"] = Color.Parse("#E3A296"),
                ["InfoColor"] = Color.Parse("#A9A3C9"),
                ["SuccessSurfaceColor"] = Color.Parse("#2C332C"),
                ["WarningSurfaceColor"] = Color.Parse("#3A3124"),
                ["ErrorSurfaceColor"] = Color.Parse("#3E2B26"),
                ["InfoSurfaceColor"] = Color.Parse("#2B2A36"),
            },
            ["Saf Siyah (OLED)"] = new()
            {
                ["AppBackgroundColor"] = Color.Parse("#000000"),
                ["SurfaceBackgroundColor"] = Color.Parse("#0B0B0D"),
                ["PanelBackgroundColor"] = Color.Parse("#111113"),
                ["InputBackgroundColor"] = Color.Parse("#070709"),
                ["CanvasBackgroundColor"] = Color.Parse("#000000"),
                ["BorderColorValue"] = Color.Parse("#222226"),
                ["BorderSubtleColorValue"] = Color.Parse("#4A4A52"),
                ["PrimaryTextColor"] = Color.Parse("#F2EFE6"),
                ["SecondaryTextColor"] = Color.Parse("#DAD7CF"),
                ["MutedTextColor"] = Color.Parse("#85858B"),
                ["DimTextColor"] = Color.Parse("#66666D"),
                ["AccentColor"] = Color.Parse("#F2EFE6"),
                ["AccentHoverColor"] = Color.Parse("#FFFFFF"),
                ["AccentButtonBgColor"] = Color.Parse("#111114"),
                ["AccentButtonHoverColor"] = Color.Parse("#1E1E22"),
                ["ToolbarButtonBgColor"] = Color.Parse("#0B0B0D"),
                ["ToolbarButtonHoverColor"] = Color.Parse("#1A1A1E"),
                ["DangerButtonBgColor"] = Color.Parse("#3E2B26"),
                ["PlayButtonGreenColor"] = Color.Parse("#0B0B0D"),
                ["PlayButtonRedColor"] = Color.Parse("#1E1E22"),
                ["StatusBarBgColor"] = Color.Parse("#000000"),
                ["NodeCardBgColor"] = Color.Parse("#111114"),
                ["NodeHeaderBgColor"] = Color.Parse("#18181B"),
                ["WireColor"] = Color.Parse("#74736C"),
                ["WireDragColor"] = Color.Parse("#F2EFE6"),
                ["SuccessColor"] = Color.Parse("#9DB58F"),
                ["WarningColor"] = Color.Parse("#D9B47A"),
                ["ErrorColor"] = Color.Parse("#E3A296"),
                ["InfoColor"] = Color.Parse("#A9A3C9"),
                ["SuccessSurfaceColor"] = Color.Parse("#2C332C"),
                ["WarningSurfaceColor"] = Color.Parse("#3A3124"),
                ["ErrorSurfaceColor"] = Color.Parse("#3E2B26"),
                ["InfoSurfaceColor"] = Color.Parse("#2B2A36"),
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
            EditorLanguage = EditorStrings.SourceLanguage;
            MasterVolume = BgmVolume = VoiceVolume = SfxVolume = 1;
            TextSpeedMultiplier = 1;
            AutoAdvanceDelay = 2;
            ApplyTheme();
        }
    }
}
