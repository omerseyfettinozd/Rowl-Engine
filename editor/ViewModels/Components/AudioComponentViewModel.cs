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

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["dsp_filter"] = DspFilter,
                ["bgm_track"]  = BgmTrack,
                ["sfx_track"]  = SfxTrack,
                ["volume"]     = Volume
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
        }
    }
}
