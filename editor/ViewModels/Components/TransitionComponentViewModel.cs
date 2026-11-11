using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for cinematic scene transitions (crossfade, fade to color, wipe).
    /// </summary>
    public partial class TransitionComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayNameKey => "Component.Transition.DisplayName";
        protected override string DisplayNameTrFallback => "Sahne Geçişi";
        protected override string DisplayNameEnFallback => "Scene Transition";
        public override string Icon => "Geçiş";
        public override string TypeKey => "transition";

        [ObservableProperty]
        private string _kind = "crossfade";

        [ObservableProperty]
        private double _duration = 1.0;

        [ObservableProperty]
        private string _colorHex = "#000000";

        // Screen Flash FX
        [ObservableProperty]
        private bool _flashEnabled = false;

        [ObservableProperty]
        private string _flashColorHex = "#FFFFFF";

        [ObservableProperty]
        private double _flashDuration = 0.5;

        [ObservableProperty]
        private double _flashIntensity = 1.0;

        // Screen Tint FX
        [ObservableProperty]
        private bool _tintEnabled = false;

        [ObservableProperty]
        private string _tintColorHex = "#0A183D";

        [ObservableProperty]
        private double _tintOpacity = 0.35;

        // Vignette FX
        [ObservableProperty]
        private bool _vignetteEnabled = false;

        [ObservableProperty]
        private double _vignetteIntensity = 0.6;

        [ObservableProperty]
        private double _vignetteRadius = 0.75;

        [ObservableProperty]
        private string _vignetteColorHex = "#000000";

        /// <summary>
        /// Kind names the picker offers: the native engine's list, or the
        /// six historic kinds when the library cannot be asked (D06-2).
        /// </summary>
        public static IReadOnlyList<string> AvailableKinds => TransitionKindCatalog.Kinds;

        /// <summary>Picker entries (kind + editor-string display name).</summary>
        public static IReadOnlyList<TransitionKindOption> KindOptions => TransitionKindCatalog.Options;

        /// <summary>
        /// Picker selection. Reads resolve native aliases ("fade" shows as
        /// Crossfade); the stored <see cref="Kind"/> string is only replaced
        /// when the author picks an entry.
        /// </summary>
        public TransitionKindOption? SelectedKindOption
        {
            get => TransitionKindCatalog.FindOption(Kind);
            set
            {
                if (value is not null)
                    Kind = value.Kind;
            }
        }

        partial void OnKindChanged(string value) => OnPropertyChanged(nameof(SelectedKindOption));

        public override Dictionary<string, object> Serialize()
        {
            var dict = new Dictionary<string, object>
            {
                ["kind"] = Kind,
                ["duration"] = Duration,
                ["color"] = ColorHex
            };

            if (FlashEnabled)
            {
                dict["flash_enabled"] = true;
                dict["flash_color"] = FlashColorHex;
                dict["flash_duration"] = FlashDuration;
                dict["flash_intensity"] = FlashIntensity;
            }

            if (TintEnabled)
            {
                dict["tint_enabled"] = true;
                dict["tint_color"] = TintColorHex;
                dict["tint_opacity"] = TintOpacity;
            }

            if (VignetteEnabled)
            {
                dict["vignette_enabled"] = true;
                dict["vignette_intensity"] = VignetteIntensity;
                dict["vignette_radius"] = VignetteRadius;
                dict["vignette_color"] = VignetteColorHex;
            }

            return dict;
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("kind", out var kv) && kv is string ks) Kind = ks;
            if (data.TryGetValue("duration", out var dv)) Duration = Math.Clamp(Convert.ToDouble(dv), 0.01, 60.0);
            if (data.TryGetValue("color", out var cv) && cv is string cs) ColorHex = cs;

            if (data.TryGetValue("flash_enabled", out var fev)) FlashEnabled = Convert.ToBoolean(fev);
            if (data.TryGetValue("flash_color", out var fcv) && fcv is string fcs) FlashColorHex = fcs;
            if (data.TryGetValue("flash_duration", out var fdv)) FlashDuration = Math.Clamp(Convert.ToDouble(fdv), 0.01, 60.0);
            if (data.TryGetValue("flash_intensity", out var fiv)) FlashIntensity = Math.Clamp(Convert.ToDouble(fiv), 0.0, 1.0);

            if (data.TryGetValue("tint_enabled", out var tev)) TintEnabled = Convert.ToBoolean(tev);
            if (data.TryGetValue("tint_color", out var tcv) && tcv is string tcs) TintColorHex = tcs;
            if (data.TryGetValue("tint_opacity", out var tov)) TintOpacity = Math.Clamp(Convert.ToDouble(tov), 0.0, 1.0);

            if (data.TryGetValue("vignette_enabled", out var vev)) VignetteEnabled = Convert.ToBoolean(vev);
            if (data.TryGetValue("vignette_intensity", out var viv)) VignetteIntensity = Math.Clamp(Convert.ToDouble(viv), 0.0, 1.0);
            if (data.TryGetValue("vignette_radius", out var vrv)) VignetteRadius = Math.Clamp(Convert.ToDouble(vrv), 0.0, 1.0);
            if (data.TryGetValue("vignette_color", out var vcv) && vcv is string vcs) VignetteColorHex = vcs;
        }
    }
}
