using System;
using System.IO;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Save-slot independent player preferences with atomic local persistence.</summary>
public sealed class PlayerSettingsProfile
{
    public float MasterVolume { get; set; } = 1;
    public float BgmVolume { get; set; } = 1;
    public float VoiceVolume { get; set; } = 1;
    public float SfxVolume { get; set; } = 1;
    public float TextSpeedMultiplier { get; set; } = 1;
    public float AutoAdvanceDelay { get; set; } = 2;

    public static PlayerSettingsProfile Load(string path) => Load(path, onError: null);

    /// <summary>A corrupt file is moved to a <c>.corrupt-*</c> backup (so a later Save cannot destroy it) and reported.</summary>
    public static PlayerSettingsProfile Load(string path, Action<string>? onError)
    {
        try { return (JsonSerializer.Deserialize<PlayerSettingsProfile>(File.ReadAllText(path)) ?? new()).Sanitized(); }
        catch (Exception ex)
        {
            if (ex is JsonException)
            {
                string? backup = SettingsFileBackup.TryMoveCorrupt(path);
                onError?.Invoke($"Player settings '{path}' are corrupt ({ex.Message}); defaults used" + (backup is null ? "." : $", old file kept as '{backup}'."));
            }
            else if (ex is not (FileNotFoundException or DirectoryNotFoundException))
                onError?.Invoke($"Player settings '{path}' could not be read ({ex.GetType().Name}: {ex.Message}); defaults used.");
            return new();
        }
    }

    /// <summary>Atomic save. Returns null on success, otherwise an error text (never throws on I/O failure).</summary>
    public string? Save(string path)
    {
        try
        {
            ProjectFileSystem.WriteAllTextAtomically(path, JsonSerializer.Serialize(Sanitized(), new JsonSerializerOptions { WriteIndented = true }));
            return null;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            return $"Player settings could not be saved to '{path}' ({ex.GetType().Name}: {ex.Message}).";
        }
    }

    public PlayerSettingsProfile Sanitized()
    {
        MasterVolume = Math.Clamp(MasterVolume, 0, 1);
        BgmVolume = Math.Clamp(BgmVolume, 0, 1);
        VoiceVolume = Math.Clamp(VoiceVolume, 0, 1);
        SfxVolume = Math.Clamp(SfxVolume, 0, 1);
        TextSpeedMultiplier = Math.Clamp(TextSpeedMultiplier, 0.25f, 4);
        AutoAdvanceDelay = Math.Clamp(AutoAdvanceDelay, 0, 60);
        return this;
    }
}
