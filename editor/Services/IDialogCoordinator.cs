using System;
using System.Threading.Tasks;
using Avalonia.Controls;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Dilim-6a: modal diyalog koordinasyonu için küçük enjeksiyon arayüzü.
/// Üretimde <see cref="DefaultDialogCoordinator"/> kullanılır; headless/
/// unit testler sahte uygulamasını enjekte edebilir. Worker/async/watcher
/// adımlarına dokunulmaz.
/// </summary>
public interface IDialogCoordinator
{
    Task OpenSettingsAsync(SettingsViewModel settings, Window? parentWindow);

    Task<bool> OpenProjectHubAsync(
        Window? currentWindow,
        Func<Task<bool>> resolveUnsavedChanges,
        Action<string>? onProjectOpened = null);
}

/// <summary>
/// Dilim-6a: <see cref="EditorModalDialogCoordinator"/> statiklerine delege
/// eden varsayılan üretim uygulaması (eski davranış birebir korunur).
/// </summary>
public sealed class DefaultDialogCoordinator : IDialogCoordinator
{
    public static DefaultDialogCoordinator Instance { get; } = new();

    private DefaultDialogCoordinator()
    {
    }

    public Task OpenSettingsAsync(SettingsViewModel settings, Window? parentWindow) =>
        EditorModalDialogCoordinator.OpenSettingsAsync(settings, parentWindow);

    public Task<bool> OpenProjectHubAsync(
        Window? currentWindow,
        Func<Task<bool>> resolveUnsavedChanges,
        Action<string>? onProjectOpened = null) =>
        EditorModalDialogCoordinator.OpenProjectHubAsync(
            currentWindow, resolveUnsavedChanges, onProjectOpened);
}
