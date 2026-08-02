using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views
{
    public partial class MainWindow : Window
    {
        public MainWindow()
        {
            InitializeComponent();
            KeyDown += MainWindow_KeyDown;
        }

        private void MainWindow_KeyDown(object? sender, KeyEventArgs e)
        {
            if (e.Key == Key.F2)
            {
                if (DataContext is MainWindowViewModel vm)
                {
                    vm.AssetBrowserViewModel.StartRenameCommand.Execute(null);
                    e.Handled = true;
                }
            }
        }
    }
}