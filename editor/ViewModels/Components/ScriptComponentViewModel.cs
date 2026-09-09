using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>Runs a project-local Lua script when its story node becomes active.</summary>
    public partial class ScriptComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Lua Script";
        public override string Icon => "📜";
        public override string TypeKey => "script";

        [ObservableProperty]
        private string _scriptPath = "";

        [ObservableProperty]
        private string _inlineCode = "";

        // Runtime-only editor feedback. These fields are never serialized into
        // story JSON: they describe the latest preview execution, not authoring
        // data.
        [ObservableProperty]
        private string _runtimeState = "Not run";

        [ObservableProperty]
        private string _runtimeError = "";

        public bool HasRuntimeError => !string.IsNullOrWhiteSpace(RuntimeError);

        partial void OnRuntimeErrorChanged(string value)
            => OnPropertyChanged(nameof(HasRuntimeError));

        public override Dictionary<string, object> Serialize()
        {
            var data = new Dictionary<string, object>();
            if (!string.IsNullOrWhiteSpace(ScriptPath)) data["path"] = ScriptPath;
            if (!string.IsNullOrWhiteSpace(InlineCode)) data["code"] = InlineCode;
            return data;
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("path", out var path) && path is string text) ScriptPath = text;
            if (data.TryGetValue("code", out var code) && code is string source) InlineCode = source;
        }
    }
}
