using System;
using System.IO;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Machine-local editor behaviour. It deliberately never travels with a game project.</summary>
public sealed class EditorSettingsProfile
{
    public bool AutoSaveEnabled { get; set; } = true;
    public int AutoSaveIntervalSeconds { get; set; } = 60;

    /// <summary>Faz 6: varlık ızgarası simge boyutu, piksel (40–128).</summary>
    public double AssetGridItemSize { get; set; } = 72;

    /// <summary>Faz 2b: editör arayüzü dili (oyun önizleme dilinden ayrı, K9).</summary>
    public string EditorLanguage { get; set; } = Strings.EditorStrings.SourceLanguage;

    public EditorSettingsProfile Sanitized()
    {
        AutoSaveIntervalSeconds = Math.Clamp(AutoSaveIntervalSeconds, 15, 300);
        AssetGridItemSize = Math.Clamp(AssetGridItemSize, 40, 128);
        if (string.IsNullOrWhiteSpace(EditorLanguage))
            EditorLanguage = Strings.EditorStrings.SourceLanguage;
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
