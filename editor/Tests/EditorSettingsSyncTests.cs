using System;
using System.IO;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorSettingsSyncTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 15]: EditorSettingsSyncService & EditorComponentService Lifecycle...");

        // 1. EditorSettingsSyncService Round-trip & Clamping
        string testEditorSettingsPath = Path.Combine(testProjectRoot, "test_editor_settings.json");
        string testPlayerSettingsPath = Path.Combine(testProjectRoot, "test_player_settings.json");
        var testSettingsVm = new SettingsViewModel();

        var syncEditorProfile = new EditorSettingsProfile { AutoSaveEnabled = false, AutoSaveIntervalSeconds = 120 };
        syncEditorProfile.Save(testEditorSettingsPath);
        EditorSettingsSyncService.LoadEditorSettings(testEditorSettingsPath, testSettingsVm);
        if (testSettingsVm.AutoSaveEnabled || testSettingsVm.AutoSaveIntervalSeconds != 120)
            throw new Exception("EditorSettingsSyncService.LoadEditorSettings mismatch");

        var playerProfile = new PlayerSettingsProfile
        {
            MasterVolume = 0.75f,
            BgmVolume = 0.5f,
            VoiceVolume = 0.8f,
            SfxVolume = 0.6f,
            TextSpeedMultiplier = 1.5f,
            AutoAdvanceDelay = 3.5f
        };
        playerProfile.Save(testPlayerSettingsPath);
        EditorSettingsSyncService.LoadPlayerSettings(testPlayerSettingsPath, testSettingsVm);
        if (Math.Abs(testSettingsVm.MasterVolume - 0.75f) > 0.001f ||
            Math.Abs(testSettingsVm.BgmVolume - 0.5f) > 0.001f ||
            Math.Abs(testSettingsVm.VoiceVolume - 0.8f) > 0.001f ||
            Math.Abs(testSettingsVm.SfxVolume - 0.6f) > 0.001f ||
            Math.Abs(testSettingsVm.TextSpeedMultiplier - 1.5f) > 0.001f ||
            Math.Abs(testSettingsVm.AutoAdvanceDelay - 3.5f) > 0.001f)
            throw new Exception("EditorSettingsSyncService.LoadPlayerSettings mismatch");

        // Clamping check on out-of-range player profile
        var badPlayerProfile = new PlayerSettingsProfile
        {
            MasterVolume = 5.0f,
            BgmVolume = -2.0f,
            TextSpeedMultiplier = 99.0f,
            AutoAdvanceDelay = -10.0f
        }.Sanitized();
        if (badPlayerProfile.MasterVolume > 1.0f || badPlayerProfile.BgmVolume < 0.0f ||
            badPlayerProfile.TextSpeedMultiplier > 4.0f || badPlayerProfile.AutoAdvanceDelay < 0.0f)
            throw new Exception("PlayerSettingsProfile.Sanitized failed to clamp out-of-range values");

        // Project runtime settings
        var testRuntimeSettings = new ProjectRuntimeSettings
        {
            SaveSlotCount = 20,
            DefaultBgmTransition = "fade",
            DefaultBgmTransitionDurationSeconds = 2.5f
        };
        EditorSettingsSyncService.LoadProjectRuntimeSettings(testRuntimeSettings, testSettingsVm);
        if (testSettingsVm.ProjectSaveSlotCount != 20 ||
            testSettingsVm.ProjectDefaultBgmTransition != "fade" ||
            Math.Abs(testSettingsVm.ProjectDefaultBgmTransitionDurationSeconds - 2.5f) > 0.001f)
            throw new Exception("EditorSettingsSyncService.LoadProjectRuntimeSettings mismatch");

        // HandleSettingsPropertyChanged event handler verification
        bool saveSlotsRefreshed = false;
        var currentProjSettings = testRuntimeSettings;
        EditorSettingsSyncService.HandleSettingsPropertyChanged(
            nameof(SettingsViewModel.ProjectSaveSlotCount),
            testSettingsVm,
            testEditorSettingsPath,
            testPlayerSettingsPath,
            testProjectRoot,
            mainVm.EngineHost,
            () => currentProjSettings,
            updated => currentProjSettings = updated,
            () => { saveSlotsRefreshed = true; });

        if (!saveSlotsRefreshed)
            throw new Exception("HandleSettingsPropertyChanged did not invoke refreshSaveSlots callback");

        // ApplyPlayerSettingsToEngine verification
        EditorSettingsSyncService.ApplyPlayerSettingsToEngine(mainVm.EngineHost, testSettingsVm);

        // 2. EditorComponentService Target Object Resolution, Add, Move, Remove
        var compTestNode = new NodeViewModel(777, "CompTest Node", 0, 0, bare: true);
        var compObj = EditorComponentService.EnsureTargetObject(compTestNode, null);
        if (compObj == null || !compTestNode.Objects.Contains(compObj))
            throw new Exception("EditorComponentService.EnsureTargetObject failed to create fallback GameObject");

        // Add dialogue
        var compDlg = EditorComponentService.AddComponent(compTestNode, compObj, "dialogue") as DialogueComponentViewModel;
        if (compDlg == null || !compObj.Components.Contains(compDlg))
            throw new Exception("EditorComponentService.AddComponent failed to add dialogue component");

        // Add background
        var compBg = EditorComponentService.AddComponent(compTestNode, compObj, "background") as BackgroundComponentViewModel;
        if (compBg == null || !compObj.Components.Contains(compBg))
            throw new Exception("EditorComponentService.AddComponent failed to add background component");

        // Add choice
        var compChoice = EditorComponentService.AddComponent(compTestNode, compObj, "choice") as ChoiceComponentViewModel;
        if (compChoice == null || !compObj.Components.Contains(compChoice))
            throw new Exception("EditorComponentService.AddComponent failed to add choice component");

        // Unknown type should return null and not crash
        var unknownComp = EditorComponentService.AddComponent(compTestNode, compObj, "nonexistent_component_xyz", msg => { });
        if (unknownComp != null)
            throw new Exception("EditorComponentService.AddComponent should return null for unknown component type");

        // Move Up & Down
        int initialBgIndex = compObj.Components.IndexOf(compBg);
        bool movedUp = EditorComponentService.MoveComponentUp(compTestNode, compBg);
        if (!movedUp || compObj.Components.IndexOf(compBg) >= initialBgIndex)
            throw new Exception("EditorComponentService.MoveComponentUp failed to move background component up");

        bool movedDown = EditorComponentService.MoveComponentDown(compTestNode, compBg);
        if (!movedDown || compObj.Components.IndexOf(compBg) != initialBgIndex)
            throw new Exception("EditorComponentService.MoveComponentDown failed to restore background component index");

        // Remove component
        bool removed = EditorComponentService.RemoveComponent(compTestNode, compDlg);
        if (!removed || compObj.Components.Contains(compDlg))
            throw new Exception("EditorComponentService.RemoveComponent failed to remove dialogue component");

        Console.WriteLine("  ✅ [PASS] EditorSettingsSyncService & EditorComponentService Lifecycle verified");
    }
}
