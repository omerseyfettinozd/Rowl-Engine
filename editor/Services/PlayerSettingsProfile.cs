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
        try { return JsonSerializer.Deserialize<PlayerSettingsProfile>(File.ReadAllText(path)) ?? new(); }
        catch { return new(); }
    }

    public void Save(string path)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temp = path + ".tmp";
        File.WriteAllText(temp, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temp, path, true);
    }
}
