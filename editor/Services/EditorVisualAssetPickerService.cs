using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Coordinates file picker dialogs, local folder ingestion, and component model updates
/// for visual assets (character sprites, background textures).
/// </summary>
public static class EditorVisualAssetPickerService
{
    /// <summary>
    /// Ensures the 'Assets/images' directory exists inside the active project assets folder.
    /// </summary>
    public static string EnsureAssetsImagesFolder(string assetsPath)
    {
        string imagesFolder = Path.Combine(assetsPath, "images");
        Directory.CreateDirectory(imagesFolder);
        return imagesFolder;
    }

    /// <summary>
    /// Pure application of an asset file name to a supported visual component model,
    /// triggering its local bitmap cache refresh.
    /// </summary>
    public static bool ApplyImageAssetToComponent(
        NodeComponentViewModel? component,
        string fileName,
        Action<string>? logAction = null)
    {
        if (component == null || string.IsNullOrWhiteSpace(fileName)) return false;

        if (component is CharacterComponentViewModel charComp)
        {
            charComp.Sprite = fileName;
            charComp.RefreshBitmap();
            logAction?.Invoke($"🖼️ Selected Sprite '{fileName}' for Character Component (Auto-copied to Assets/images)");
            return true;
        }

        if (component is BackgroundComponentViewModel bgComp)
        {
            bgComp.Texture = fileName;
            bgComp.RefreshBitmap();
            logAction?.Invoke($"🖼️ Selected Texture '{fileName}' for Background Component (Auto-copied to Assets/images)");
            return true;
        }

        return false;
    }

    /// <summary>
    /// Opens an OS file picker dialog, imports selected image into project Assets/images,
    /// and assigns it to the target component.
    /// </summary>
    public static async Task SelectImageForComponentAsync(
        NodeComponentViewModel? component,
        Window? window,
        string assetsPath,
        Func<string, string> importImageFunc,
        Action refreshAssets,
        Action scheduleSave,
        Action pushSceneToEngine,
        Action<string>? logAction = null)
    {
        if (component == null || window == null) return;

        try
        {
            string imagesFolder = EnsureAssetsImagesFolder(assetsPath);
            var startFolder = await window.StorageProvider.TryGetFolderFromPathAsync(new Uri(imagesFolder));

            var files = await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = "Select Image Asset (Will auto-copy to project Assets/images)",
                SuggestedStartLocation = startFolder,
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType("Image Files (*.png, *.jpg, *.jpeg, *.bmp, *.webp, *.tga)")
                    {
                        Patterns = new[] { "*.png", "*.jpg", "*.jpeg", "*.bmp", "*.webp", "*.tga" }
                    }
                }
            });

            if (files != null && files.Count > 0)
            {
                string fullPath = files[0].Path.LocalPath;
                string fileName = importImageFunc(fullPath);

                if (ApplyImageAssetToComponent(component, fileName, logAction))
                {
                    refreshAssets();
                    scheduleSave();
                    pushSceneToEngine();
                }
            }
        }
        catch (Exception ex)
        {
            logAction?.Invoke($"⚠️ Failed to pick image file: {ex.Message}");
        }
    }
}
