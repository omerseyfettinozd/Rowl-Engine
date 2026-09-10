using System;
using System.IO;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for synchronizing settings profiles between the editor UI, disk files, and the native runtime engine.
/// </summary>
public static class EditorSettingsSyncService
{
    public static void LoadPlayerSettings(string path, SettingsViewModel settings)
    {
        var profile = PlayerSettingsProfile.Load(path);
        settings.MasterVolume = profile.MasterVolume;
        settings.BgmVolume = profile.BgmVolume;
        settings.VoiceVolume = profile.VoiceVolume;
        settings.SfxVolume = profile.SfxVolume;
        settings.TextSpeedMultiplier = profile.TextSpeedMultiplier;
        settings.AutoAdvanceDelay = profile.AutoAdvanceDelay;
    }

    public static void LoadEditorSettings(string path, SettingsViewModel settings)
    {
        var profile = EditorSettingsProfile.Load(path);
        settings.AutoSaveEnabled = profile.AutoSaveEnabled;
        settings.AutoSaveIntervalSeconds = profile.AutoSaveIntervalSeconds;
    }

    public static void LoadProjectRuntimeSettings(ProjectRuntimeSettings projectSettings, SettingsViewModel settings)
    {
        settings.ProjectSaveSlotCount = projectSettings.SaveSlotCount;
        settings.ProjectDefaultBgmTransition = projectSettings.DefaultBgmTransition;
        settings.ProjectDefaultBgmTransitionDurationSeconds = projectSettings.DefaultBgmTransitionDurationSeconds;
    }

    public static void HandleSettingsPropertyChanged(
        string? propertyName,
        SettingsViewModel settings,
        string editorSettingsPath,
        string playerSettingsPath,
        string projectPath,
        EngineHost host,
        Func<ProjectRuntimeSettings> getCurrentProjectSettings,
        Action<ProjectRuntimeSettings> updateProjectSettings,
        Action refreshSaveSlots)
    {
        if (string.IsNullOrEmpty(propertyName)) return;

        if (propertyName is nameof(SettingsViewModel.AutoSaveEnabled) or nameof(SettingsViewModel.AutoSaveIntervalSeconds))
        {
            new EditorSettingsProfile
            {
                AutoSaveEnabled = settings.AutoSaveEnabled,
                AutoSaveIntervalSeconds = settings.AutoSaveIntervalSeconds
            }.Save(editorSettingsPath);
            return;
        }

        if (propertyName is nameof(SettingsViewModel.ProjectSaveSlotCount) or nameof(SettingsViewModel.ProjectDefaultBgmTransition)
            or nameof(SettingsViewModel.ProjectDefaultBgmTransitionDurationSeconds))
        {
            var updated = new ProjectRuntimeSettings
            {
                SaveSlotCount = settings.ProjectSaveSlotCount,
                DefaultBgmTransition = settings.ProjectDefaultBgmTransition,
                DefaultBgmTransitionDurationSeconds = settings.ProjectDefaultBgmTransitionDurationSeconds
            }.Sanitized();

            ProjectRuntimeSettingsService.Save(Path.Combine(projectPath, "project.rowlproj"), updated);
            updateProjectSettings(updated);
            LoadProjectRuntimeSettings(updated, settings);

            if (host.IsInitialized)
            {
                host.SetBgmTransitionDefaults(updated.DefaultBgmTransition, updated.DefaultBgmTransitionDurationSeconds);
            }
            refreshSaveSlots();
            return;
        }

        if (propertyName is not (nameof(SettingsViewModel.MasterVolume) or nameof(SettingsViewModel.BgmVolume)
            or nameof(SettingsViewModel.VoiceVolume) or nameof(SettingsViewModel.SfxVolume)
            or nameof(SettingsViewModel.TextSpeedMultiplier) or nameof(SettingsViewModel.AutoAdvanceDelay)))
            return;

        var playerProfile = new PlayerSettingsProfile
        {
            MasterVolume = settings.MasterVolume,
            BgmVolume = settings.BgmVolume,
            VoiceVolume = settings.VoiceVolume,
            SfxVolume = settings.SfxVolume,
            TextSpeedMultiplier = settings.TextSpeedMultiplier,
            AutoAdvanceDelay = settings.AutoAdvanceDelay
        }.Sanitized();

        playerProfile.Save(playerSettingsPath);
        ApplyPlayerSettingsToEngine(host, settings, playerProfile);
    }

    public static void ApplyPlayerSettingsToEngine(EngineHost host, SettingsViewModel settings, PlayerSettingsProfile? profile = null)
    {
        if (!host.IsInitialized) return;

        profile ??= new PlayerSettingsProfile
        {
            MasterVolume = settings.MasterVolume,
            BgmVolume = settings.BgmVolume,
            VoiceVolume = settings.VoiceVolume,
            SfxVolume = settings.SfxVolume,
            TextSpeedMultiplier = settings.TextSpeedMultiplier,
            AutoAdvanceDelay = settings.AutoAdvanceDelay
        }.Sanitized();

        host.SetMasterVolume(profile.MasterVolume);
        host.SetBgmVolume(profile.BgmVolume);
        host.SetVoiceVolume(profile.VoiceVolume);
        host.SetSfxVolume(profile.SfxVolume);
        host.SetTextSpeedMultiplier(profile.TextSpeedMultiplier);
        host.SetAutoAdvanceDelayOffset(profile.AutoAdvanceDelay);
    }
}
