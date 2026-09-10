using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia.Controls;
using Avalonia.Platform.Storage;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Coordinates audio file picker dialogs, project directory ingestion, and
/// AudioComponentViewModel configuration for BGM and SFX tracks.
/// </summary>
public static class EditorAudioAssetPickerService
{
    /// <summary>
    /// Ensures the 'Assets/audio' directory exists inside the active project assets folder.
    /// </summary>
    public static string EnsureAssetsAudioFolder(string assetsPath)
    {
        string audioFolder = Path.Combine(assetsPath, "audio");
        Directory.CreateDirectory(audioFolder);
        return audioFolder;
    }

    /// <summary>
    /// Assigns a selected audio file name and optional volume to an AudioComponentViewModel.
    /// Supports trackType: "bgm" or "sfx". Clamps volume between 0.0f and 1.0f.
    /// </summary>
    public static bool ApplyAudioTrackToComponent(
        AudioComponentViewModel? component,
        string fileName,
        string trackType = "bgm",
        float? volume = null,
        Action<string>? logAction = null)
    {
        if (component == null || string.IsNullOrWhiteSpace(fileName)) return false;

        string normalizedType = trackType.Trim().ToLowerInvariant();
        if (normalizedType == "bgm")
        {
            component.BgmTrack = fileName;
            logAction?.Invoke($"🎵 Selected BGM track '{fileName}' for Audio Component");
        }
        else if (normalizedType == "sfx")
        {
            component.SfxTrack = fileName;
            logAction?.Invoke($"🔊 Selected SFX track '{fileName}' for Audio Component");
        }
        else
        {
            return false;
        }

        if (volume.HasValue)
        {
            component.Volume = Math.Clamp(volume.Value, 0.0f, 1.0f);
        }

        return true;
    }

    /// <summary>
    /// Opens an OS file picker dialog to let the user select an audio file (.ogg, .wav, .mp3)
    /// for an audio component, automatically copies it into Assets/audio, and updates the component.
    /// </summary>
    public static async Task SelectAudioForComponentAsync(
        AudioComponentViewModel? component,
        string trackType,
        Window? window,
        string assetsPath,
        Func<string, string> importAssetFunc,
        Action refreshAssets,
        Action scheduleSave,
        Action<string>? logAction = null)
    {
        if (component == null || window == null) return;

        try
        {
            string audioFolder = EnsureAssetsAudioFolder(assetsPath);
            var startFolder = await window.StorageProvider.TryGetFolderFromPathAsync(new Uri(audioFolder));

            var files = await window.StorageProvider.OpenFilePickerAsync(new FilePickerOpenOptions
            {
                Title = $"Select {(trackType.Equals("bgm", StringComparison.OrdinalIgnoreCase) ? "BGM" : "SFX")} Audio Track (Will auto-copy to Assets/audio)",
                SuggestedStartLocation = startFolder,
                AllowMultiple = false,
                FileTypeFilter = new[]
                {
                    new FilePickerFileType("Audio Files (*.ogg, *.wav, *.mp3, *.flac)")
                    {
                        Patterns = new[] { "*.ogg", "*.wav", "*.mp3", "*.flac" }
                    }
                }
            });

            if (files != null && files.Count > 0)
            {
                string fullPath = files[0].Path.LocalPath;
                string fileName = importAssetFunc(fullPath);

                if (ApplyAudioTrackToComponent(component, fileName, trackType, null, logAction))
                {
                    refreshAssets();
                    scheduleSave();
                }
            }
        }
        catch (Exception ex)
        {
            logAction?.Invoke($"⚠️ Failed to pick audio file: {ex.Message}");
        }
    }
}
