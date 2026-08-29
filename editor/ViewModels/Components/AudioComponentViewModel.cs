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

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["dsp_filter"] = DspFilter
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("dsp_filter", out var f) && f is string filter)
                DspFilter = filter;
        }
    }
}
