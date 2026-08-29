using Avalonia.Controls;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views
{
    public partial class ProjectHubWindow : Window
    {
        public ProjectHubWindow()
        {
            InitializeComponent();
        }

        public ProjectHubWindow(ProjectHubViewModel vm) : this()
        {
            DataContext = vm;
            vm.HubWindow = this;
        }

        protected override void OnDataContextChanged(System.EventArgs e)
        {
            base.OnDataContextChanged(e);
            if (DataContext is ProjectHubViewModel vm)
                vm.HubWindow = this;
        }
    }
}
