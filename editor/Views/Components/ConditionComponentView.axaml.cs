using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Views.Components
{
    public partial class ConditionComponentView : UserControl
    {
        public ConditionComponentView()
        {
            InitializeComponent();
            // The node list changes while the picker is closed (nodes added or deleted).
            var picker = this.FindControl<ComboBox>("TargetPicker");
            if (picker != null)
                picker.DropDownOpened += (_, _) => RefreshTargets();
        }

        protected override void OnAttachedToVisualTree(VisualTreeAttachmentEventArgs e)
        {
            base.OnAttachedToVisualTree(e);
            RefreshTargets();
        }

        private void RefreshTargets()
        {
            if (DataContext is not ConditionComponentViewModel vm) return;
            if (this.GetVisualRoot() is Window { DataContext: MainWindowViewModel main })
                vm.RefreshTargets(main.Nodes);
        }
    }
}
