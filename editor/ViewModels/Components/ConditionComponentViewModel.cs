using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>
    /// Component for evaluating Lua conditional logic or gating node execution.
    /// </summary>
    public partial class ConditionComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayName => "Condition (Lua)";
        public override string Icon => "⚖️";
        public override string TypeKey => "condition";

        [ObservableProperty]
        private string _expression = "gold >= 10";

        [ObservableProperty]
        private string _failTargetNodeId = "";

        public override Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["expression"]          = Expression,
                ["fail_target_node_id"] = FailTargetNodeId
            };
        }

        public override void Deserialize(Dictionary<string, object?> data)
        {
            if (data.TryGetValue("expression", out var exp) && exp is string expStr)
                Expression = expStr;
            if (data.TryGetValue("fail_target_node_id", out var fail) && fail != null)
                FailTargetNodeId = fail.ToString() ?? "";
        }
    }
}
