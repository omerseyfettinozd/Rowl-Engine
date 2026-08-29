using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Views
{
    public partial class MainWindow : Window
    {
        public MainWindow() : this(new MainWindowViewModel())
        {
        }

        public MainWindow(string projectPath) : this(new MainWindowViewModel(projectPath))
        {
        }

        public MainWindow(MainWindowViewModel vm)
        {
            InitializeComponent();
            DataContext = vm;
            vm.TopLevelHint = this;
            KeyDown += MainWindow_KeyDown;
        }

        private void MainWindow_KeyDown(object? sender, KeyEventArgs e)
        {
            if (DataContext is not MainWindowViewModel vm) return;

            bool ctrl = e.KeyModifiers.HasFlag(KeyModifiers.Control);
            bool shift = e.KeyModifiers.HasFlag(KeyModifiers.Shift);

            // ── Undo / Redo ──────────────────────────────────────────
            if (ctrl && !shift && e.Key == Key.Z)
            {
                vm.UndoCommand.Execute(null);
                e.Handled = true;
            }
            else if ((ctrl && e.Key == Key.Y) || (ctrl && shift && e.Key == Key.Z))
            {
                vm.RedoCommand.Execute(null);
                e.Handled = true;
            }

            // ── Dosya İşlemleri ─────────────────────────────────────
            else if (ctrl && !shift && e.Key == Key.S)
            {
                vm.SaveProjectCommand.Execute(null);
                e.Handled = true;
            }
            else if (ctrl && shift && e.Key == Key.S)
            {
                _ = vm.SaveProjectAsCommand.ExecuteAsync(null);
                e.Handled = true;
            }

            // ── Node İşlemleri ──────────────────────────────────────
            else if (ctrl && e.Key == Key.N)
            {
                vm.AddNodeCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.Delete)
            {
                vm.DeleteSelectedNodeCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.F2)
            {
                vm.AssetBrowserViewModel?.StartRenameCommand?.Execute(null);
                e.Handled = true;
            }

            // ── Build & Play ────────────────────────────────────────
            else if (ctrl && e.Key == Key.B)
            {
                _ = vm.BuildGameCommand.ExecuteAsync(null);
                e.Handled = true;
            }
            else if (e.Key == Key.F5)
            {
                vm.TogglePlayStandaloneCommand.Execute(null);
                e.Handled = true;
            }

            // ── Arama ───────────────────────────────────────────────
            else if (ctrl && e.Key == Key.F)
            {
                vm.ToggleSearchCommand.Execute(null);
                e.Handled = true;
            }
            else if (e.Key == Key.Escape && vm.IsSearchVisible)
            {
                vm.IsSearchVisible = false;
                vm.SearchQuery = "";
                e.Handled = true;
            }

            // ── Tam Ekran ───────────────────────────────────────────
            else if (e.Key == Key.F11)
            {
                vm.ToggleFullscreenCommand.Execute(null);
                e.Handled = true;
            }

            // ── Ayarlar ─────────────────────────────────────────────
            else if (ctrl && e.Key == Key.OemComma)
            {
                _ = vm.OpenSettingsCommand.ExecuteAsync(null);
                e.Handled = true;
            }

            // ── Hub ─────────────────────────────────────────────────
            else if (ctrl && e.Key == Key.H)
            {
                vm.OpenProjectHubCommand.Execute(null);
                e.Handled = true;
            }
        }
    }
}