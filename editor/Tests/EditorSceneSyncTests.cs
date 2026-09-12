using System;
using System.IO;
using System.Threading.Tasks;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorSceneSyncTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 14]: EditorSceneSyncService & EditorPlayModeCoordinator Lifecycle...");

        // 1. EditorSceneSyncService PushScene & Diagnostics
        var syncNode = new NodeViewModel(401, "Sync Node", 0, 0, bare: true);
        var syncDlg = syncNode.AddComponent<DialogueComponentViewModel>();
        syncDlg.DialogueText = "Sync test line";
        var syncChar = syncNode.AddComponent<CharacterComponentViewModel>();
        syncChar.Sprite = "Woman.png";
        syncChar.X = 150;
        syncChar.Y = 250;
        var syncScript = syncNode.AddComponent<ScriptComponentViewModel>();
        syncScript.InlineCode = "rowl.var_set('sync_var', 'ok_401')";

        if (!mainVm.EngineHost.IsInitialized)
        {
            mainVm.EngineHost.Initialize(1920, 1080, false);
        }
        mainVm.EngineHost.SetProjectDirectory(testProjectRoot);

        bool pushed = EditorSceneSyncService.PushSceneToEngine(mainVm.EngineHost, syncNode);
        if (!pushed)
            throw new Exception("EditorSceneSyncService.PushSceneToEngine returned false");

        mainVm.EngineHost.Step(0.016f);
        string syncVarValue = mainVm.EngineHost.GetVariable("sync_var");
        if (syncVarValue != "ok_401")
            throw new Exception($"Native variable from pushed script mismatch: expected 'ok_401', got '{syncVarValue}'");

        EditorSceneSyncService.ApplyScriptRuntimeDiagnostics(mainVm.EngineHost, syncNode);
        if (string.IsNullOrEmpty(syncScript.RuntimeState))
            throw new Exception("EditorSceneSyncService did not populate ScriptComponentViewModel.RuntimeState");

        if (EditorSceneSyncService.PushSceneToEngine(mainVm.EngineHost, null))
            throw new Exception("EditorSceneSyncService.PushSceneToEngine should return false for null node");

        // 2. EditorPlayModeCoordinator Play / Stop Lifecycle
        bool switchedToGameTab = false;
        bool playStarted = EditorPlayModeCoordinator.StartPlayModeAsync(
            mainVm.EngineHost,
            () => Task.FromResult(true),
            () => true,
            node => mainVm.SelectNodeQuiet(node),
            node => mainVm.PushSceneToEngine(node),
            () => mainVm.GetStartNode(),
            msg => { },
            () => { switchedToGameTab = true; },
            Path.Combine(testProjectRoot, "Assets", "json")).GetAwaiter().GetResult();

        if (!playStarted)
            throw new Exception("EditorPlayModeCoordinator.StartPlayModeAsync failed");
        if (!switchedToGameTab)
            throw new Exception("EditorPlayModeCoordinator did not trigger game tab switch callback");
        if (!mainVm.EngineHost.IsPlaying)
            throw new Exception("EngineHost.IsPlaying was not true after StartPlayModeAsync");

        EditorPlayModeCoordinator.StopPlayMode(
            mainVm.EngineHost,
            () => mainVm.GetStartNode(),
            node => mainVm.PushSceneToEngine(node),
            node => mainVm.SelectNode(node),
            msg => { });

        if (mainVm.EngineHost.IsPlaying)
            throw new Exception("EngineHost.IsPlaying was still true after StopPlayMode");

        Console.WriteLine("  ✅ [PASS] EditorSceneSyncService & EditorPlayModeCoordinator Lifecycle verified");
    }
}
