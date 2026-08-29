using Avalonia.Controls;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views.Dialogs
{
    public partial class SettingsDialog : Window
    {
        public SettingsDialog()
        {
            InitializeComponent();
            DataContext = new SettingsViewModel();

            var closeBtn = this.FindControl<Button>("CloseButton");
            if (closeBtn != null)
            {
                closeBtn.Click += (_, _) => Close();
            }
        }

        public SettingsDialog(SettingsViewModel vm) : this()
        {
            DataContext = vm;
        }
    }
}
