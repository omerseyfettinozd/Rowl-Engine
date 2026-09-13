using System.Collections.Generic;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Platform.Storage;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Owns host-window resolution and Avalonia storage pickers (file/folder
/// dialogs) so ViewModels request user file choices without touching
/// application lifetime or StorageProvider internals. All pickers resolve
/// the main window internally and report absence or cancellation as null.
/// </summary>
internal static class EditorDialogService
{
    internal static Window? GetMainWindow()
        => (Application.Current?.ApplicationLifetime as IClassicDesktopStyleApplicationLifetime)?.MainWindow;

    internal static async Task<IReadOnlyList<IStorageFile>?> PickAssetFilesAsync()
    {
        var window = GetMainWindow();
        if (window == null) return null;
        return await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
        {
            Title = "Import Asset Files into Rowl Engine Project",
            AllowMultiple = true
        });
    }

    internal static async Task<string?> PickFolderAsync(string title)
    {
        var window = GetMainWindow();
        if (window == null) return null;
        var folders = await window.StorageProvider.OpenFolderPickerAsync(new FolderPickerOpenOptions
        {
            Title = title,
            AllowMultiple = false
        });
        if (folders == null || folders.Count == 0) return null;
        return folders[0].Path.LocalPath;
    }
}
