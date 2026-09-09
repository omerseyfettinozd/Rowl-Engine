using System;
using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for audio DSP filter settings.
    /// </summary>
    public partial class AudioComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Audio & DSP";
        public override string Icon => "🔊";
        public override string TypeKey => "audio";

        [ObservableProperty]
        private string _dspFilter = "Normal";

        [ObservableProperty]
        private string _bgmTrack = "";

        [ObservableProperty]
        private string _sfxTrack = "";

        [ObservableProperty]
        private float _volume = 1.0f;

        /// <summary>project_default inherits the manifest value; other values override it for this node.</summary>
        [ObservableProperty]
        private string _bgmTransition = "project_default";

        /// <summary>Zero means inherit the manifest duration for the selected transition.</summary>
        [ObservableProperty]
        private float _bgmTransitionDurationSeconds = 0.0f;

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["dsp_filter"] = DspFilter,
                ["bgm_track"]  = BgmTrack,
                ["sfx_track"]  = SfxTrack,
                ["volume"]     = Volume,
                ["bgm_transition"] = BgmTransition,
                ["bgm_transition_duration_seconds"] = BgmTransitionDurationSeconds
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("dsp_filter", out var f) && f is string filter)
                DspFilter = filter;
            if (data.TryGetValue("bgm_track", out var bgm) && bgm is string b)
                BgmTrack = b;
            if (data.TryGetValue("sfx_track", out var sfx) && sfx is string s)
                SfxTrack = s;
            if (data.TryGetValue("volume", out var v) && v != null)
            {
                if (v is double vd) Volume = (float)vd;
                else if (v is float vf) Volume = vf;
                else if (v is int vi) Volume = (float)vi;
                else if (float.TryParse(v.ToString(), out var parsed)) Volume = parsed;
            }
            if (data.TryGetValue("bgm_transition", out var transition) && transition is string kind &&
                kind is "project_default" or "instant" or "fade" or "crossfade")
                BgmTransition = kind;
            if (data.TryGetValue("bgm_transition_duration_seconds", out var duration) && duration != null)
            {
                if (duration is double doubleDuration) BgmTransitionDurationSeconds = (float)doubleDuration;
                else if (duration is float floatDuration) BgmTransitionDurationSeconds = floatDuration;
                else if (duration is int intDuration) BgmTransitionDurationSeconds = intDuration;
                else if (float.TryParse(duration.ToString(), out var parsedDuration)) BgmTransitionDurationSeconds = parsedDuration;
            }
            BgmTransitionDurationSeconds = float.IsFinite(BgmTransitionDurationSeconds)
                ? Math.Clamp(BgmTransitionDurationSeconds, 0, 60) : 0;
        }
    }
}
