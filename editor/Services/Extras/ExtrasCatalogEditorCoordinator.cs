using System;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.Views.Extras;

namespace RowlEngine.Editor.Services.Extras;

/// <summary>
/// Opens the extras catalog editor as a standalone modeless window (the pattern of the
/// Translation Desk): one window per project session, re-focused instead of stacked.
/// </summary>
public static class ExtrasCatalogEditorCoordinator
{
    /// <summary>Opens (or focuses) the editor for the project's Assets folder.</summary>
    public static void Open(string? assetsPath)
    {
        if (string.IsNullOrWhiteSpace(assetsPath)) return;
        if (Application.Current?.ApplicationLifetime is not IClassicDesktopStyleApplicationLifetime desktop) return;
        Window? existing = desktop.Windows.FirstOrDefault(w => w is ExtrasCatalogEditorWindow);
        if (existing is not null)
        {
            existing.Activate();
            return;
        }
        var window = new ExtrasCatalogEditorWindow(new ExtrasCatalogEditorViewModel(assetsPath));
        Window? owner = desktop.MainWindow;
        if (owner is not null) window.Show(owner); else window.Show();
    }
}
