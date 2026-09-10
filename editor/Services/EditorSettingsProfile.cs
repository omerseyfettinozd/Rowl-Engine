using System;
using System.IO;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Machine-local editor behaviour. It deliberately never travels with a game project.</summary>
internal sealed class EditorSettingsProfile
{
    public bool AutoSaveEnabled { get; set; } = true;
    public int AutoSaveIntervalSeconds { get; set; } = 60;

    public EditorSettingsProfile Sanitized()
    {
        AutoSaveIntervalSeconds = Math.Clamp(AutoSaveIntervalSeconds, 15, 300);
        return this;
    }

    public static EditorSettingsProfile Load(string path)
    {
        try { return (JsonSerializer.Deserialize<EditorSettingsProfile>(File.ReadAllText(path)) ?? new()).Sanitized(); }
        catch { return new EditorSettingsProfile(); }
    }

    public void Save(string path)
    {
        Sanitized();
        ProjectFileSystem.WriteAllTextAtomically(path, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}
