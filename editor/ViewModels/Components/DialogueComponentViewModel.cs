using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Unified Dialogue component combining Speaker Name, Dialogue Text,
    /// Box Position, Size (Width/Height) and Scale in a single component.
    /// </summary>
    public partial class DialogueComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Dialogue";
        public override string Icon => "💬";
        public override string TypeKey => "dialogue";

        [ObservableProperty]
        private string _speaker = "Evelyn";

        [ObservableProperty]
        private string _dialogueText = string.Empty;

        // Transform & Geometry
        [ObservableProperty]
        private double _x = 80.0;

        [ObservableProperty]
        private double _y = 860.0;

        [ObservableProperty]
        private double _width = 1760.0;

        [ObservableProperty]
        private double _height = 180.0;

        [ObservableProperty]
        private double _scale = 1.0;

        // Typewriter & Audio Timing
        [ObservableProperty]
        private bool _typewriterEnabled = true;

        [ObservableProperty]
        private int _textSpeed = 30; // Milliseconds per character (10 - 100)

        [ObservableProperty]
        private string _typewriterSound = string.Empty; // Optional click sfx path

        [ObservableProperty]
        private bool _autoAdvance = false;

        [ObservableProperty]
        private double _autoAdvanceDelay = 2.0; // Seconds

        // Typography & Text Styling
        [ObservableProperty]
        private double _fontSize = 24.0;

        [ObservableProperty]
        private double _speakerFontSize = 20.0;

        [ObservableProperty]
        private string _textColor = "#F1F5F9"; // Default clean light slate

        [ObservableProperty]
        private string _speakerColor = "#38BDF8"; // Sky blue accent

        [ObservableProperty]
        private string _textAlignment = "Left"; // Left, Center, Right

        public bool IsAlignLeft
        {
            get => TextAlignment == "Left";
            set { if (value) TextAlignment = "Left"; }
        }

        public bool IsAlignCenter
        {
            get => TextAlignment == "Center";
            set { if (value) TextAlignment = "Center"; }
        }

        public bool IsAlignRight
        {
            get => TextAlignment == "Right";
            set { if (value) TextAlignment = "Right"; }
        }

        partial void OnTextAlignmentChanged(string value)
        {
            OnPropertyChanged(nameof(IsAlignLeft));
            OnPropertyChanged(nameof(IsAlignCenter));
            OnPropertyChanged(nameof(IsAlignRight));
        }

        // Box Visuals & Theme
        [ObservableProperty]
        private double _boxOpacity = 0.88; // 0.0 - 1.0

        [ObservableProperty]
        private string _boxColor = "#0F0F1A"; // Dark cyber glass

        [ObservableProperty]
        private string _borderColor = "#00F0FF"; // Neon cyan

        [ObservableProperty]
        private double _borderThickness = 2.0;

        [ObservableProperty]
        private double _cornerRadius = 8.0;

        [ObservableProperty]
        private string _customBoxTexture = string.Empty; // Optional 9-slice / frame PNG

        // Quick Presets
        [RelayCommand]
        public void SetStandard()
        {
            Width = 1760.0;
            Height = 180.0;
            X = 80.0;
            Y = 860.0;
            Scale = 1.0;
            BoxOpacity = 0.88;
            BoxColor = "#0F0F1A";
            BorderColor = "#00F0FF";
            FontSize = 24.0;
        }

        [RelayCommand]
        public void SetSquare()
        {
            Width = 500.0;
            Height = 500.0;
            X = 80.0;
            Y = 540.0;
            Scale = 1.0;
            BoxOpacity = 0.90;
            BoxColor = "#0F0F1A";
            BorderColor = "#38BDF8";
            FontSize = 22.0;
        }

        [RelayCommand]
        public void SetNvlFullscreen()
        {
            Width = 1800.0;
            Height = 960.0;
            X = 60.0;
            Y = 60.0;
            Scale = 1.0;
            BoxOpacity = 0.92;
            BoxColor = "#0A0A12";
            BorderColor = "#334155";
            FontSize = 26.0;
        }

        [RelayCommand]
        public void SetTopSubtitle()
        {
            Width = 1500.0;
            Height = 120.0;
            X = 210.0;
            Y = 60.0;
            Scale = 1.0;
            BoxOpacity = 0.70;
            BoxColor = "#000000";
            BorderColor = "#F59E0B";
            FontSize = 22.0;
        }

        [RelayCommand]
        public void SetComicBubble()
        {
            Width = 500.0;
            Height = 220.0;
            X = 120.0;
            Y = 500.0;
            Scale = 1.0;
            BoxOpacity = 0.95;
            BoxColor = "#1E1E2E";
            BorderColor = "#EC4899";
            FontSize = 20.0;
        }

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["speaker"] = Speaker,
                ["dialogue"] = DialogueText,
                ["x"] = X,
                ["y"] = Y,
                ["width"] = Width,
                ["height"] = Height,
                ["scale"] = Scale,
                ["typewriter_enabled"] = TypewriterEnabled,
                ["text_speed"] = TextSpeed,
                ["typewriter_sound"] = TypewriterSound,
                ["auto_advance"] = AutoAdvance,
                ["auto_advance_delay"] = AutoAdvanceDelay,
                ["font_size"] = FontSize,
                ["speaker_font_size"] = SpeakerFontSize,
                ["text_color"] = TextColor,
                ["speaker_color"] = SpeakerColor,
                ["text_alignment"] = TextAlignment,
                ["box_opacity"] = BoxOpacity,
                ["box_color"] = BoxColor,
                ["border_color"] = BorderColor,
                ["border_thickness"] = BorderThickness,
                ["corner_radius"] = CornerRadius,
                ["custom_box_texture"] = CustomBoxTexture
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("speaker", out var spk) && spk is string s)
                Speaker = s;

            if (data.TryGetValue("dialogue", out var dlg) && dlg is string d)
                DialogueText = d;

            if (data.TryGetValue("x", out var xv) && xv is double x)
                X = x;
            else if (data.TryGetValue("dialogue_box_x", out var dbx) && dbx is double dbxVal)
                X = dbxVal;

            if (data.TryGetValue("y", out var yv) && yv is double y)
                Y = y;
            else if (data.TryGetValue("dialogue_box_y", out var dby) && dby is double dbyVal)
                Y = dbyVal;

            if (data.TryGetValue("width", out var wv) && wv is double w)
                Width = w;
            else if (data.TryGetValue("dialogue_box_width", out var dbw) && dbw is double dbwVal)
                Width = dbwVal;

            if (data.TryGetValue("height", out var hv) && hv is double h)
                Height = h;
            else if (data.TryGetValue("dialogue_box_height", out var dbh) && dbh is double dbhVal)
                Height = dbhVal;

            if (data.TryGetValue("scale", out var scv) && scv is double sc)
                Scale = sc;

            // Enriched features deserialization with safe type conversion
            if (data.TryGetValue("typewriter_enabled", out var twe) && twe is bool bTwe)
                TypewriterEnabled = bTwe;

            if (data.TryGetValue("text_speed", out var tsp))
            {
                if (tsp is int iTsp) TextSpeed = iTsp;
                else if (tsp is long lTsp) TextSpeed = (int)lTsp;
                else if (tsp is double dTsp) TextSpeed = (int)dTsp;
            }

            if (data.TryGetValue("typewriter_sound", out var tws) && tws is string sTws)
                TypewriterSound = sTws;

            if (data.TryGetValue("auto_advance", out var aadv) && aadv is bool bAadv)
                AutoAdvance = bAadv;

            if (data.TryGetValue("auto_advance_delay", out var aadd) && aadd is double dAadd)
                AutoAdvanceDelay = dAadd;

            if (data.TryGetValue("font_size", out var fsz))
            {
                if (fsz is double dFsz) FontSize = dFsz;
                else if (fsz is int iFsz) FontSize = iFsz;
                else if (fsz is long lFsz) FontSize = lFsz;
            }

            if (data.TryGetValue("speaker_font_size", out var sfs))
            {
                if (sfs is double dSfs) SpeakerFontSize = dSfs;
                else if (sfs is int iSfs) SpeakerFontSize = iSfs;
                else if (sfs is long lSfs) SpeakerFontSize = lSfs;
            }

            if (data.TryGetValue("text_color", out var tc) && tc is string sTc)
                TextColor = sTc;

            if (data.TryGetValue("speaker_color", out var scClr) && scClr is string sSc)
                SpeakerColor = sSc;

            if (data.TryGetValue("text_alignment", out var ta) && ta is string sTa)
                TextAlignment = sTa;

            if (data.TryGetValue("box_opacity", out var bop) && bop is double dBop)
                BoxOpacity = dBop;

            if (data.TryGetValue("box_color", out var bc) && bc is string sBc)
                BoxColor = sBc;

            if (data.TryGetValue("border_color", out var brc) && brc is string sBrc)
                BorderColor = sBrc;

            if (data.TryGetValue("border_thickness", out var bt) && bt is double dBt)
                BorderThickness = dBt;

            if (data.TryGetValue("corner_radius", out var cr) && cr is double dCr)
                CornerRadius = dCr;

            if (data.TryGetValue("custom_box_texture", out var cbt) && cbt is string sCbt)
                CustomBoxTexture = sCbt;
        }
    }

    // Backward-compatible aliases
    public class SpeakerComponentViewModel : DialogueComponentViewModel { }
    public class DialogueBoxComponentViewModel : DialogueComponentViewModel { }
}
