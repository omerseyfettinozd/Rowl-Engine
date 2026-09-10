using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for cinematic scene transitions (crossfade, fade to color, wipe).
    /// </summary>
    public partial class TransitionComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Scene Transition";
        public override string Icon => "🎬";
        public override string TypeKey => "transition";

        [ObservableProperty]
        private string _kind = "crossfade";

        [ObservableProperty]
        private double _duration = 1.0;

        [ObservableProperty]
        private string _colorHex = "#000000";

        public static IReadOnlyList<string> AvailableKinds { get; } = new[]
        {
            "crossfade",
            "fade_black",
            "fade_white",
            "fade_color",
            "wipe_left",
            "wipe_right"
        };

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["kind"] = Kind,
                ["duration"] = Duration,
                ["color"] = ColorHex
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("kind", out var kv) && kv is string ks) Kind = ks;
            if (data.TryGetValue("duration", out var dv)) Duration = Math.Clamp(Convert.ToDouble(dv), 0.01, 60.0);
            if (data.TryGetValue("color", out var cv) && cv is string cs) ColorHex = cs;
        }
    }
}
