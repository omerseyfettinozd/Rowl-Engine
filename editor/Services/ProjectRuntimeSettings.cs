using System;
using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services;

/// <summary>Project-owned release settings, deliberately separate from player preferences.</summary>
public sealed class ProjectRuntimeSettings
{
    public const int DefaultSaveSlotCount = 10;
    // Canonical save-slot range (Faz 4.5 Dilim 2): slot indexes 0..99, i.e. at most 100 slots.
    public const int MinSaveSlotIndex = 0;
    public const int MaxSaveSlotIndex = 99;
    public const int MaxSaveSlotCount = MaxSaveSlotIndex + 1;
    public const string DefaultTransitionKind = "instant";
    public const float DefaultTransitionDurationSeconds = 1.0f;

    public int SaveSlotCount { get; set; } = DefaultSaveSlotCount;
    public string DefaultBgmTransition { get; set; } = DefaultTransitionKind;
    public float DefaultBgmTransitionDurationSeconds { get; set; } = DefaultTransitionDurationSeconds;

    public ProjectRuntimeSettings Sanitized()
    {
        SaveSlotCount = Math.Clamp(SaveSlotCount, 1, MaxSaveSlotCount);
        if (DefaultBgmTransition is not ("instant" or "fade" or "crossfade"))
            DefaultBgmTransition = DefaultTransitionKind;
        if (!float.IsFinite(DefaultBgmTransitionDurationSeconds))
            DefaultBgmTransitionDurationSeconds = DefaultTransitionDurationSeconds;
        else
            DefaultBgmTransitionDurationSeconds = Math.Clamp(DefaultBgmTransitionDurationSeconds, 0, 60);
        return this;
    }
}

public static class ProjectRuntimeSettingsService
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

    /// <summary>
    /// Writes the three runtime keys into the manifest. Returns null on success, otherwise an
    /// error text; a corrupt / non-object manifest is never replaced (every other key would be lost).
    /// </summary>
    public static string? Save(string manifestPath, ProjectRuntimeSettings settings)
    {
        settings.Sanitized();
        try
        {
            JsonObject manifest;
            if (File.Exists(manifestPath))
            {
                try
                {
                    manifest = JsonNode.Parse(File.ReadAllText(manifestPath)) as JsonObject
                        ?? throw new JsonException("root is not a JSON object");
                }
                catch (JsonException parseFailure)
                {
                    return $"Project manifest '{manifestPath}' is not a valid JSON object ({parseFailure.Message}); settings were not saved.";
                }
            }
            else
            {
                manifest = new JsonObject();
            }
            manifest["save_slot_count"] = settings.SaveSlotCount;
            manifest["default_bgm_transition"] = settings.DefaultBgmTransition;
            manifest["default_bgm_transition_duration_seconds"] = settings.DefaultBgmTransitionDurationSeconds;
            ProjectFileSystem.WriteAllTextAtomically(manifestPath, manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
            return null;
        }
        catch (Exception failure) when (failure is IOException or UnauthorizedAccessException)
        {
            return $"Project settings could not be saved to '{manifestPath}' ({failure.GetType().Name}: {failure.Message}).";
        }
    }
}
