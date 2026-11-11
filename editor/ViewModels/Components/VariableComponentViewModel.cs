using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.ViewModels.Components
{
    /// <summary>One entry of the operation picker: the JSON <c>operation</c> plus its editor-string name.</summary>
    public sealed class VariableOperationOption : ObservableObject
    {
        internal VariableOperationOption(string operation) => Operation = operation;

        /// <summary>The value written to the component's <c>operation</c> key (set, add).</summary>
        public string Operation { get; }

        public string DisplayName => EditorStrings.T("Component.Variable.Operation." + Operation, Operation);

        internal void RaiseDisplayNameChanged() => OnPropertyChanged(nameof(DisplayName));
    }

    /// <summary>
    /// Component for modifying or setting dynamic game variables (Lua & GameState).
    /// </summary>
    public partial class VariableComponentViewModel : NodeComponentViewModel
    {
        public override string DisplayNameKey => "Component.Variable.DisplayName";
        protected override string DisplayNameTrFallback => "Değişken Değiştirici";
        protected override string DisplayNameEnFallback => "Variable Modifier";
        public override string Icon => "Değişken";
        public override string TypeKey => "variable";

        [ObservableProperty]
        private string _key = "gold";

        [ObservableProperty]
        private string _value = "10";

        [ObservableProperty]
        private string _operation = "set"; // "set" or "add"

        private static readonly VariableOperationOption[] s_operationOptions =
        {
            new("set"),
            new("add"),
        };

        private static bool s_stringsHooked;
        private static readonly object s_hookGate = new();

        public VariableComponentViewModel()
        {
            lock (s_hookGate)
            {
                if (s_stringsHooked) return;
                s_stringsHooked = true;
                EditorStrings.Instance.PropertyChanged += (_, e) =>
                {
                    if (!string.IsNullOrEmpty(e.PropertyName) &&
                        e.PropertyName != EditorStrings.IndexerPropertyName &&
                        e.PropertyName != nameof(EditorStrings.Language))
                        return;
                    foreach (var option in s_operationOptions) option.RaiseDisplayNameChanged();
                };
            }
        }

        /// <summary>The operations native understands: "add" adds a number to the variable, anything else sets it.</summary>
        public static IReadOnlyList<VariableOperationOption> OperationOptions => s_operationOptions;

        /// <summary>
        /// Operation picker selection. A stored operation the picker does not know (a hand-edited
        /// file; native treats it as set) shows no selection and is kept until the author picks one.
        /// </summary>
        public VariableOperationOption? SelectedOperationOption
        {
            get => s_operationOptions.FirstOrDefault(option => option.Operation == Operation);
            set
            {
                if (value is not null) Operation = value.Operation;
            }
        }

        public bool IsAdd => Operation == "add";

        /// <summary>
        /// "add" needs a finite number (native parses it with stod and rejects the node otherwise).
        /// </summary>
        public bool AddValueInvalid =>
            IsAdd && !(double.TryParse(Value?.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out double number)
                       && double.IsFinite(number));

        /// <summary>Native ignores a variable component with an empty key.</summary>
        public bool KeyEmpty => string.IsNullOrWhiteSpace(Key);

        partial void OnOperationChanged(string value)
        {
            OnPropertyChanged(nameof(SelectedOperationOption));
            OnPropertyChanged(nameof(IsAdd));
            OnPropertyChanged(nameof(AddValueInvalid));
        }

        partial void OnValueChanged(string value) => OnPropertyChanged(nameof(AddValueInvalid));

        partial void OnKeyChanged(string value) => OnPropertyChanged(nameof(KeyEmpty));

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
