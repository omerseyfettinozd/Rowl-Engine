using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for modifying or setting dynamic game variables (Lua & GameState).
    /// </summary>
    public partial class VariableComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Variable Modifier";
        public override string Icon => "📊";
        public override string TypeKey => "variable";

        [ObservableProperty]
        private string _key = "gold";

        [ObservableProperty]
        private string _value = "10";

        [ObservableProperty]
        private string _operation = "set"; // "set" or "add"

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["key"]       = Key,
                ["value"]     = Value,
                ["operation"] = Operation
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("key", out var k) && k is string keyStr)
                Key = keyStr;
            if (data.TryGetValue("value", out var v) && v != null)
                Value = v.ToString() ?? "";
            if (data.TryGetValue("operation", out var op) && op is string opStr)
                Operation = opStr;
        }
    }
}
