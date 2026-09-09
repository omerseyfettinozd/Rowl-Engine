using System;
using System.IO;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Save-slot independent player preferences with atomic local persistence.</summary>
internal sealed class PlayerSettingsProfile
{
    public float MasterVolume { get; set; } = 1;
    public float BgmVolume { get; set; } = 1;
    public float VoiceVolume { get; set; } = 1;
    public float SfxVolume { get; set; } = 1;
    public float TextSpeedMultiplier { get; set; } = 1;
    public float AutoAdvanceDelay { get; set; } = 2;

    public static PlayerSettingsProfile Load(string path)
    {
        try { return (JsonSerializer.Deserialize<PlayerSettingsProfile>(File.ReadAllText(path)) ?? new()).Sanitized(); }
        catch { return new(); }
    }

    public void Save(string path)
    {
        var directory = Path.GetDirectoryName(path);
        if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
        var temp = path + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(Sanitized(), new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temp, path, true);
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
