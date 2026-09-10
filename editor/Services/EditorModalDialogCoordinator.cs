using System;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views;
using RowlEngine.Editor.Views.Dialogs;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Coordinates top-level modal dialogs and window transitions (Settings, Project Hub)
/// without coupling ViewModels directly to window lifecycle internals.
/// </summary>
public static class EditorModalDialogCoordinator
{
    /// <summary>
    /// Displays the editor settings dialog modally if parent window is available,
    /// or as a modeless window otherwise.
    /// </summary>
    public static async Task OpenSettingsAsync(SettingsViewModel settings, Window? parentWindow)
    {
        var dialog = new SettingsDialog(settings);
        if (parentWindow != null)
        {
            await dialog.ShowDialog(parentWindow);
        }
        else
        {
            dialog.Show();
        }
    }

    /// <summary>
    /// Transitions from the editor workspace to the Project Hub window, ensuring
    /// unsaved changes are resolved and managing Avalonia desktop window lifetime.
    /// </summary>
    public static async Task<bool> OpenProjectHubAsync(
        Window? currentWindow,
        Func<Task<bool>> resolveUnsavedChanges,
        Action<string>? onProjectOpened = null)
    {
        if (currentWindow != null && !await resolveUnsavedChanges())
        {
            return false;
        }

        if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var hubVm = new ProjectHubViewModel();
            var hubWin = new ProjectHubWindow(hubVm);

            hubVm.ProjectOpened += (path) =>
            {
                var newMain = new MainWindow(path);
                desktop.MainWindow = newMain;
                newMain.Show();
                hubWin.Close();
                onProjectOpened?.Invoke(path);
            };

            desktop.MainWindow = hubWin;
            hubWin.Show();

            if (currentWindow != null)
            {
                currentWindow.Close();
            }
            else
            {
                desktop.Windows.FirstOrDefault(w => w is MainWindow)?.Close();
            }

            return true;
        }

        return false;
    }
}
