using System;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using RowlEngine.Editor.ViewModels.Localization;
using RowlEngine.Editor.Views.Localization;

namespace RowlEngine.Editor.Services.Localization;

/// <summary>
/// Faz 3 Dilim 4 — opens the Translation Desk as a standalone modeless
/// window. Resolves the project root and the host window internally so
/// <c>MainWindowViewModel</c> stays a one-line delegation and
/// <c>EngineHost</c> is untouched. Re-focuses an already open desk
/// instead of stacking duplicates.
/// </summary>
public static class LocalizationDeskCoordinator
{
    /// <summary>Opens (or focuses) the desk for <paramref name="projectRoot"/>.</summary>
    public static void OpenDesk(string? projectRoot)
    {
        if (string.IsNullOrWhiteSpace(projectRoot))
            return;
        if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop)
            return;
        Window? existing = desktop.Windows.FirstOrDefault(w => w is LocalizationDeskWindow);
        if (existing is not null)
        {
            existing.Focus();
            return;
        }
        var viewModel = new LocalizationDeskViewModel(projectRoot);
        var window = new LocalizationDeskWindow(viewModel);
        Window? owner = desktop.MainWindow;
        if (owner is not null)
            window.Show(owner);
        else
            window.Show();
    }
}
