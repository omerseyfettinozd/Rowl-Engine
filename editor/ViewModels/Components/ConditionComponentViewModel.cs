using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>One entry of the fail-target picker. <see cref="Exists"/> is false for a stored id no project node has.</summary>
    public sealed record ConditionTargetOption(string Id, string Label, bool Exists);

    /// <summary>
    /// Component for evaluating Lua conditional logic or gating node execution.
    /// </summary>
    public partial class ConditionComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayNameKey => "Component.Condition.DisplayName";
        protected override string DisplayNameTrFallback => "Koşul (Lua)";
        protected override string DisplayNameEnFallback => "Condition (Lua)";
        public override string Icon => "Koşul";
        public override string TypeKey => "condition";

        [ObservableProperty]
        private string _expression = "gold >= 10";

        [ObservableProperty]
        private string _failTargetNodeId = "";

        private IReadOnlyList<ConditionTargetOption>? _targetOptions;
        private List<(ulong Id, string Title)>? _projectNodes;

        /// <summary>
        /// Hands the component the project's nodes for the fail-target picker (the view calls it when it
        /// appears and when the picker opens). Never null-writes the stored target.
        /// </summary>
        public void RefreshTargets(IEnumerable<NodeViewModel> nodes)
        {
            _projectNodes = nodes.OrderBy(node => node.Id).Select(node => (node.Id, node.Title ?? "")).ToList();
            _targetOptions = null;
            NotifyTargetOptions();
        }

        /// <summary>First entry is the empty choice (the node still plays), then every project node, then the stored id when no node has it.</summary>
        public IReadOnlyList<ConditionTargetOption> TargetOptions => _targetOptions ??= BuildTargetOptions();

        private string StoredTarget => (FailTargetNodeId ?? "").Trim();

        /// <summary>The stored target is not empty and no project node has that id (the node list is known).</summary>
        public bool TargetMissing =>
            _projectNodes is not null && StoredTarget.Length > 0 &&
            !TargetOptions.Any(option => option.Exists && option.Id == StoredTarget);

        public ConditionTargetOption? SelectedTargetOption
        {
            get
            {
                string stored = StoredTarget;
                return TargetOptions.FirstOrDefault(option => option.Id == stored);
            }
            set
            {
                // The ComboBox reports null whenever its item list is replaced; writing that back
                // would erase the target (the D06-2 Denetçi lesson). The empty choice is a real
                // option with Id "", so "no target" is still pickable.
                if (value is not null) FailTargetNodeId = value.Id;
            }
        }

        partial void OnFailTargetNodeIdChanged(string value)
        {
            // Keep the list stable while a pick is applied; rebuild only when the stored id is new to it.
            if (_targetOptions is not null && value.Trim().Length > 0 &&
                _targetOptions.All(option => option.Id != value.Trim()))
                _targetOptions = null;
            OnPropertyChanged(nameof(SelectedTargetOption));
            OnPropertyChanged(nameof(TargetMissing));
            if (_targetOptions is null) OnPropertyChanged(nameof(TargetOptions));
        }

        private void NotifyTargetOptions()
        {
            OnPropertyChanged(nameof(TargetOptions));
            OnPropertyChanged(nameof(TargetMissing));
            OnPropertyChanged(nameof(SelectedTargetOption));
        }

        private IReadOnlyList<ConditionTargetOption> BuildTargetOptions()
        {
            var options = new List<ConditionTargetOption>
            {
                new("", EditorStrings.T("Component.Condition.Target.None",
                    "— (düğüm yine oynar)", "— (the node still plays)"), true)
            };
            if (_projectNodes is not null)
            {
                foreach (var (id, title) in _projectNodes)
                    options.Add(new ConditionTargetOption(id.ToString(), title.Length > 0 ? $"{id}  -  {title}" : id.ToString(), true));
            }
            string stored = StoredTarget;
            if (stored.Length > 0 && _projectNodes is not null && options.All(option => option.Id != stored))
            {
                string note = EditorStrings.T("Component.Condition.Target.Missing.Option", "düğüm yok", "node missing");
                options.Add(new ConditionTargetOption(stored, $"{stored}  ({note})", false));
            }
            return options;
        }

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
