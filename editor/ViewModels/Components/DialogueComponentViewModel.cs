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

        [RelayCommand]
        public void SetSquare()
        {
            Width = 500.0;
            Height = 500.0;
            X = 80.0;
            Y = 540.0;
        }

        [RelayCommand]
        public void SetStandard()
        {
            Width = 1760.0;
            Height = 180.0;
            X = 80.0;
            Y = 860.0;
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
                ["scale"] = Scale
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
        }
    }

    // Backward-compatible aliases
    public class SpeakerComponentViewModel : DialogueComponentViewModel { }
    public class DialogueBoxComponentViewModel : DialogueComponentViewModel { }
}
