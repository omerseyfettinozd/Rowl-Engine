using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Machine-local editor behaviour. It deliberately never travels with a game project.</summary>
public sealed class EditorSettingsProfile
{
    // İzinli arayüz ölçekleri (Faz 8 D1).
    public static readonly IReadOnlyList<double> AllowedUiScales = new[] { 1.0, 1.15, 1.3 };

    public bool AutoSaveEnabled { get; set; } = true;
    public int AutoSaveIntervalSeconds { get; set; } = 60;

    /// <summary>Faz 6: varlık ızgarası simge boyutu, piksel (40–128).</summary>
    public double AssetGridItemSize { get; set; } = 72;

    /// <summary>Faz 2b: editör arayüzü dili (oyun önizleme dilinden ayrı, K9).</summary>
    public string EditorLanguage { get; set; } = Strings.EditorStrings.SourceLanguage;

    /// <summary>Faz 5 D6: çalışma alanı panel geçişlerini azaltma / devre dışı bırakma tercihi.</summary>
    public bool ReduceMotion { get; set; }

    /// <summary>Faz 8 D1: arayüz ölçeği çarpanı (izinli değere oturtulur).</summary>
    public double UiScale { get; set; } = 1.0;

    /// <summary>Faz 8 D1: yüksek kontrast tercihi.</summary>
    public bool HighContrast { get; set; }

    /// <summary>
    /// Faz 5 D5: dock workspace (split tree, bottom-strip tabs, pinned panels) as
    /// <see cref="Workspace.WorkspaceStateJson"/> text, or <c>null</c> = default
    /// workspace. Kept as text with its own version so a layout the running editor
    /// does not understand (older/newer/corrupt) never breaks the rest of the
    /// profile — it only falls back to the default workspace. Personal preference
    /// of this machine, hence here and not in the project file.
    /// </summary>
    public string? WorkspaceState { get; set; }

    public EditorSettingsProfile Sanitized()
    {
        AutoSaveIntervalSeconds = Math.Clamp(AutoSaveIntervalSeconds, 15, 300);
        AssetGridItemSize = Math.Clamp(AssetGridItemSize, 40, 128);
        if (string.IsNullOrWhiteSpace(EditorLanguage))
            EditorLanguage = Strings.EditorStrings.SourceLanguage;
        UiScale = SnapUiScale(UiScale);
        return this;
    }

    // En yakın izinli değere oturtur (NaN/sonsuz → 1.0).
    private static double SnapUiScale(double value)
    {
        if (double.IsNaN(value) || double.IsInfinity(value))
            return 1.0;
        double best = AllowedUiScales[0];
        double bestDist = Math.Abs(value - best);
        for (int i = 1; i < AllowedUiScales.Count; i++)
        {
            double d = Math.Abs(value - AllowedUiScales[i]);
            if (d < bestDist)
            {
                bestDist = d;
                best = AllowedUiScales[i];
            }
        }
        return best;
    }

    public static EditorSettingsProfile Load(string path) => Load(path, onError: null);

    /// <summary>
    /// Like <see cref="Load(string)"/> (a missing or unreadable file gives the
    /// defaults, never an exception); <paramref name="onError"/> hears about a
    /// file that exists but cannot be read (corrupt JSON, I/O error).
    /// </summary>
    public static EditorSettingsProfile Load(string path, Action<string>? onError)
    {
        try { return (JsonSerializer.Deserialize<EditorSettingsProfile>(File.ReadAllText(path)) ?? new()).Sanitized(); }
        catch (Exception ex)
        {
            if (ex is not (FileNotFoundException or DirectoryNotFoundException))
                onError?.Invoke(ex.Message);
            return new EditorSettingsProfile();
        }
    }

    private static readonly object FileLock = new();

    /// <summary>
    /// Read-modify-write of the profile under one lock: every writer (settings
    /// changes, the debounced workspace save) goes through here so one never
    /// drops the other's fields.
    /// </summary>
    public static EditorSettingsProfile Update(string path, Action<EditorSettingsProfile> change)
    {
        lock (FileLock)
        {
            EditorSettingsProfile profile = Load(path);
            change(profile);
            profile.Save(path);
            return profile;
        }
    }

    public void Save(string path)
    {
        Sanitized();
        ProjectFileSystem.WriteAllTextAtomically(path, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}
