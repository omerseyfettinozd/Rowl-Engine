using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services;

/// <summary>Project-owned release settings, deliberately separate from player preferences.</summary>
internal sealed class ProjectRuntimeSettings
{
    public const int DefaultSaveSlotCount = 10;
    public const string DefaultTransitionKind = "instant";
    public const float DefaultTransitionDurationSeconds = 1.0f;

    public int SaveSlotCount { get; set; } = DefaultSaveSlotCount;
    public string DefaultBgmTransition { get; set; } = DefaultTransitionKind;
    public float DefaultBgmTransitionDurationSeconds { get; set; } = DefaultTransitionDurationSeconds;

    public ProjectRuntimeSettings Sanitized()
    {
        SaveSlotCount = Math.Clamp(SaveSlotCount, 1, 100);
        if (DefaultBgmTransition is not ("instant" or "fade" or "crossfade"))
            DefaultBgmTransition = DefaultTransitionKind;
        if (!float.IsFinite(DefaultBgmTransitionDurationSeconds))
            DefaultBgmTransitionDurationSeconds = DefaultTransitionDurationSeconds;
        else
            DefaultBgmTransitionDurationSeconds = Math.Clamp(DefaultBgmTransitionDurationSeconds, 0, 60);
        return this;
    }
}

internal static class ProjectRuntimeSettingsService
{
    public static ProjectRuntimeSettings Load(string manifestPath)
    {
        try
        {
            using var document = JsonDocument.Parse(File.ReadAllText(manifestPath));
            var root = document.RootElement;
            var settings = new ProjectRuntimeSettings
            {
                SaveSlotCount = root.TryGetProperty("save_slot_count", out var slots) && slots.TryGetInt32(out var count)
                    ? count : ProjectRuntimeSettings.DefaultSaveSlotCount,
                DefaultBgmTransition = root.TryGetProperty("default_bgm_transition", out var transition) && transition.ValueKind == JsonValueKind.String
                    ? transition.GetString() ?? ProjectRuntimeSettings.DefaultTransitionKind : ProjectRuntimeSettings.DefaultTransitionKind,
                DefaultBgmTransitionDurationSeconds = root.TryGetProperty("default_bgm_transition_duration_seconds", out var duration) && duration.TryGetSingle(out var seconds)
                    ? seconds : ProjectRuntimeSettings.DefaultTransitionDurationSeconds
            };
            return settings.Sanitized();
        }
        catch { return new ProjectRuntimeSettings(); }
    }

    public static void Save(string manifestPath, ProjectRuntimeSettings settings)
    {
        settings.Sanitized();
        JsonObject manifest;
        try { manifest = JsonNode.Parse(File.ReadAllText(manifestPath)) as JsonObject ?? new JsonObject(); }
        catch { manifest = new JsonObject(); }
        manifest["save_slot_count"] = settings.SaveSlotCount;
        manifest["default_bgm_transition"] = settings.DefaultBgmTransition;
        manifest["default_bgm_transition_duration_seconds"] = settings.DefaultBgmTransitionDurationSeconds;
        var directory = Path.GetDirectoryName(manifestPath);
        if (!string.IsNullOrWhiteSpace(directory)) Directory.CreateDirectory(directory);
        var temporary = manifestPath + ".tmp";
        File.WriteAllText(temporary, manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
        File.Move(temporary, manifestPath, true);
    }
}
