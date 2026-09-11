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

        [ObservableProperty]
        private bool _isPreviewingBgm = false;

        [ObservableProperty]
        private bool _isPreviewingSfx = false;

        [ObservableProperty]
        private float _bgmPeakL = 0.0f;

        [ObservableProperty]
        private float _bgmPeakR = 0.0f;

        [ObservableProperty]
        private float _bgmRmsL = 0.0f;

        [ObservableProperty]
        private float _bgmRmsR = 0.0f;

        [ObservableProperty]
        private float _sfxPeakL = 0.0f;

        [ObservableProperty]
        private float _sfxPeakR = 0.0f;

        [ObservableProperty]
        private float _sfxRmsL = 0.0f;

        [ObservableProperty]
        private float _sfxRmsR = 0.0f;

        public static Action<string, int, int>? GlobalPreviewAudioAction { get; set; }
        public static Action? GlobalStopAudioAction { get; set; }

        public int GetFilterIndex() => DspFilter switch
        {
            "CaveReverb" => 1,
            "Telephone" => 2,
            "UnderwaterLowPass" => 3,
            _ => 0
        };

        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void ToggleBgmPreview()
        {
            if (IsPreviewingBgm)
            {
                StopAllPreview();
            }
            else
            {
                if (string.IsNullOrWhiteSpace(BgmTrack)) return;
                StopAllPreview();
                IsPreviewingBgm = true;
                GlobalPreviewAudioAction?.Invoke(BgmTrack, 0, GetFilterIndex());
            }
        }

        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void ToggleSfxPreview()
        {
            if (IsPreviewingSfx)
            {
                StopAllPreview();
            }
            else
            {
                if (string.IsNullOrWhiteSpace(SfxTrack)) return;
                StopAllPreview();
                IsPreviewingSfx = true;
                GlobalPreviewAudioAction?.Invoke(SfxTrack, 2, GetFilterIndex());
            }
        }

        [CommunityToolkit.Mvvm.Input.RelayCommand]
        public void StopAllPreview()
        {
            IsPreviewingBgm = false;
            IsPreviewingSfx = false;
            GlobalStopAudioAction?.Invoke();
            ResetTelemetry();
        }

        public void ResetTelemetry()
        {
            BgmPeakL = 0.0f;
            BgmPeakR = 0.0f;
            BgmRmsL = 0.0f;
            BgmRmsR = 0.0f;
            SfxPeakL = 0.0f;
            SfxPeakR = 0.0f;
            SfxRmsL = 0.0f;
            SfxRmsR = 0.0f;
        }

        public void UpdateAudioTelemetry(float peakL, float peakR, float rmsL, float rmsR, bool isSfx = false)
        {
            if (isSfx)
            {
                SfxPeakL = peakL;
                SfxPeakR = peakR;
                SfxRmsL = rmsL;
                SfxRmsR = rmsR;
            }
            else
            {
                BgmPeakL = peakL;
                BgmPeakR = peakR;
                BgmRmsL = rmsL;
                BgmRmsR = rmsR;
            }
        }

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
