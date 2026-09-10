using System;
using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using RowlEngine.Editor.Views.Dialogs;

namespace RowlEngine.Editor.Services;

public sealed record ProjectOpenResult(bool Succeeded, string? RootPath, string? ErrorMessage);

/// <summary>
/// Coordinates high-level project lifecycle events: opening, switching with rollback,
/// save-as branching, folder exploration, unsaved changes resolution, and confirmation dialogs.
/// </summary>
public static class EditorProjectLifecycleCoordinator
{
    /// <summary>
    /// Evaluates unsaved changes state. If dirty, invokes the dialog function.
    /// Handles discard (clears dirty), save (invokes save and re-checks dirty), or cancel.
    /// </summary>
    public static async Task<bool> ResolveUnsavedChangesAsync(
        Func<Task<string?>> showUnsavedDialog,
        Action saveAction,
        Func<bool> isDirtyGetter,
        Action<bool> isDirtySetter)
    {
        if (!isDirtyGetter()) return true;

        string? result = await showUnsavedDialog();
        if (result == "discard")
        {
            isDirtySetter(false);
            return true;
        }

        if (result != "save")
        {
            return false;
        }

        saveAction();
        return !isDirtyGetter();
    }

    /// <summary>
    /// Avalonia Window overload for resolving unsaved changes via UnsavedChangesDialog.
    /// </summary>
    public static async Task<bool> ResolveUnsavedChangesAsync(
        Window? window,
        Action saveAction,
        Func<bool> isDirtyGetter,
        Action<bool> isDirtySetter)
    {
        if (!isDirtyGetter()) return true;
        if (window == null) return true;

        return await ResolveUnsavedChangesAsync(
            () => new UnsavedChangesDialog().ShowDialog<string?>(window),
            saveAction,
            isDirtyGetter,
            isDirtySetter);
    }

    /// <summary>
    /// Coordinates save slot deletion confirmation dialog.
    /// </summary>
    public static async Task<bool> ConfirmDeleteSaveSlotAsync(Func<Task<bool?>> showConfirmDialog)
    {
        var result = await showConfirmDialog();
        return result == true;
    }

    /// <summary>
    /// Avalonia Window overload for save slot deletion confirmation.
    /// </summary>
    public static async Task<bool> ConfirmDeleteSaveSlotAsync(Window? window, int slotIndex)
    {
        if (window == null) return false;

        var dialog = new ConfirmDialog(
            "Kayıt Slotunu Sil",
            $"Slot {slotIndex + 1} içindeki kayıt kalıcı olarak silinecek.",
            "Sil",
            true);

        return await ConfirmDeleteSaveSlotAsync(() => dialog.ShowDialog<bool?>(window));
    }

    /// <summary>
    /// Opens the project root folder in the host operating system's native file explorer.
    /// </summary>
    public static bool OpenProjectFolder(string projectRoot, Action<string>? logAction = null)
    {
        try
        {
            if (OperatingSystem.IsLinux())
                Process.Start("xdg-open", projectRoot);
            else if (OperatingSystem.IsWindows())
                Process.Start("explorer.exe", projectRoot);
            else if (OperatingSystem.IsMacOS())
                Process.Start("open", projectRoot);

            logAction?.Invoke($"📂 Proje klasörü açıldı: {projectRoot}");
            return true;
        }
        catch (Exception ex)
        {
            logAction?.Invoke($"⚠️ Klasör açılamadı: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Generates a standardized timestamped directory name for Save-As operations.
    /// </summary>
    public static string GenerateSaveAsTargetDirectory(string parentDir, DateTime? timestamp = null)
    {
        DateTime time = timestamp ?? DateTime.Now;
        string saveFolderName = $"RowlProject_{time:yyyy-MM-dd_HH-mm}";
        return Path.Combine(parentDir, saveFolderName);
    }

    /// <summary>
    /// Executes a complete Save-As operation: creates target directory, clones project assets and
    /// manifests via ProjectSaveAsCoordinator, and logs results.
    /// </summary>
    public static SaveAsResult ExecuteSaveAs(
        string sourceProjectRoot,
        string targetDirectory,
        int nodeCount,
        ulong startNodeId,
        Action saveSourceProjectAction,
        Action<string>? logAction = null)
    {
        try
        {
            Directory.CreateDirectory(targetDirectory);
            var result = ProjectSaveAsCoordinator.SaveProjectCopy(
                sourceProjectRoot,
                targetDirectory,
                nodeCount,
                startNodeId,
                saveSourceProjectAction);

            if (result.Succeeded)
            {
                logAction?.Invoke($"💾 [FARKLI KAYDET] Proje başarıyla kopyalandı: {targetDirectory}");
            }
            else
            {
                logAction?.Invoke($"⚠️ Farklı kaydetme hatası: {result.Message}");
            }

            return result;
        }
        catch (Exception ex)
        {
            logAction?.Invoke($"⚠️ Farklı kaydetme hatası: {ex.Message}");
            return new SaveAsResult(false, targetDirectory, ex.Message);
        }
    }

    /// <summary>
    /// Resolves and executes switching to a different project folder with transactional rollback on failure.
    /// </summary>
    public static ProjectOpenResult ExecuteOpenProject(
        string selectedDirectory,
        string previousProjectRoot,
        string previousProjectPath,
        Action clearAssetCache,
        Action<string, string> setProjectPaths,
        Action<string> remountEngine,
        Func<bool> loadGraph,
        Action refreshNodeBitmaps,
        Action refreshAssets,
        Action<string>? logAction = null)
    {
        if (!ProjectOpenCoordinator.TryResolve(selectedDirectory, out var project))
        {
            logAction?.Invoke("⚠️ Seçilen klasörde geçerli bir Rowl Engine projesi bulunamadı.");
            logAction?.Invoke("   Beklenen yapı: [KlasörAdı]/Assets/json/full_story_graph.json");
            return new ProjectOpenResult(false, null, "Geçerli bir Rowl Engine projesi bulunamadı.");
        }

        bool loaded = ProjectOpenCoordinator.Switch(
            project!,
            previousProjectRoot,
            previousProjectPath,
            clearAssetCache,
            setProjectPaths,
            remountEngine,
            loadGraph,
            refreshNodeBitmaps,
            refreshAssets);

        if (loaded)
        {
            logAction?.Invoke($"📂 [PROJE AÇILDI] {project!.RootPath}");
            return new ProjectOpenResult(true, project!.RootPath, null);
        }
        else
        {
            logAction?.Invoke($"⚠️ Hikaye grafiği yüklenemedi: {project!.GraphFilePath}");
            return new ProjectOpenResult(false, project!.RootPath, "Hikaye grafiği yüklenemedi.");
        }
    }
}
