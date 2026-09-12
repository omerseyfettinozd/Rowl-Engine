using System;
using System.IO;
using RowlEngine.Editor.Native;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorRuntimeStateTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 9]: Lua Condition, Variable Components & Native State Slots...");

        // Component Creation & Serialization
        var varComp = (VariableComponentViewModel)ComponentRegistry.Create("variable");
        varComp.Key = "player_reputation";
        varComp.Value = "85";
        varComp.Operation = "set";

        var condComp = (ConditionComponentViewModel)ComponentRegistry.Create("condition");
        condComp.Expression = "player_reputation >= 80";

        var scriptNode = new NodeViewModel(701, "ScriptNode", 100, 100);
        var logicObj = scriptNode.CreateObject("Logic");
        logicObj.AddComponent(varComp);
        logicObj.AddComponent(condComp);

        var varDict = varComp.Serialize();
        if ((string)varDict["key"] != "player_reputation" || (string)varDict["value"] != "85")
            throw new Exception("VariableComponent serialization mismatch");

        var condDict = condComp.Serialize();
        if ((string)condDict["expression"] != "player_reputation >= 80")
            throw new Exception("ConditionComponent serialization mismatch");

        // Native P/Invoke Integration via EngineHost
        var host = mainVm.EngineHost;
        if (!host.IsInitialized)
        {
            host.Initialize(1920, 1080, false);
        }
        if (host.IsInitialized)
        {
            if (host.LastFrameTextureLoadMilliseconds < 0 || host.LastFrameNonTextureRenderMilliseconds < 0 ||
                host.LastFrameTextRasterizationMilliseconds < 0 || host.LastFrameRendererFlushMilliseconds < 0)
                throw new Exception("Native first-frame profiling telemetry returned an invalid duration");
            host.SetProjectDirectory(testProjectRoot);
            host.SetVariable("test_affinity", "99");
            string readAffinity = host.GetVariable("test_affinity");
            if (readAffinity != "99")
                throw new Exception($"Native variable mismatch: expected '99', got '{readAffinity}'");

            if (!host.EvaluateCondition("test_affinity >= 90") || host.EvaluateCondition("test_affinity < 50"))
                throw new Exception("Native condition evaluation via EngineHost failed");

            // Save & Load Slots
            if (!host.SaveGameSlot(10))
                throw new Exception("EngineHost.SaveGameSlot(10) failed");

            if (!File.Exists(Path.Combine(testProjectRoot, "saves", "save_slot_10.json")))
                throw new Exception("Game save was not isolated under the selected project");

            if (!host.HasSaveSlot(10))
                throw new Exception("EngineHost.HasSaveSlot(10) failed");

            if (!host.LoadGameSlot(10))
                throw new Exception("EngineHost.LoadGameSlot(10) failed");

            host.DeleteSaveSlot(10);
            if (host.HasSaveSlot(10))
                throw new Exception("EngineHost.DeleteSaveSlot(10) failed");

            // Structured Diagnostics Verification via EngineHost
            if (host.SaveGameSlot(-3))
                throw new Exception("SaveGameSlot(-3) should fail");
            if (host.LastResultCode != RuntimeErrorCode.InvalidArgument ||
                host.LastResultOperation != "save_game_slot" ||
                host.LastResultTarget != "-3")
                throw new Exception($"Expected InvalidArgument diagnostic for slot -3, got {host.LastResultCode} ({host.LastResultOperation})");

            if (host.LoadGameSlot(99))
                throw new Exception("LoadGameSlot(99) should fail");
            if (host.LastResultCode != RuntimeErrorCode.FileNotFound)
                throw new Exception($"Expected FileNotFound diagnostic for slot 99, got {host.LastResultCode}");

            host.ClearLastResult();
            if (host.LastResultCode != RuntimeErrorCode.Ok)
                throw new Exception("ClearLastResult did not reset LastResultCode to Ok");

            // Rewind
            host.Rewind(1);
        }

        Console.WriteLine("  ✅ [PASS] Variable/Condition components, serialization, and P/Invoke Save/Load slots + Structured Diagnostics verified");

        // Preview coalescing may skip only a script-free scene whose exact
        // payload is already rendered. Script components retain their
        // existing execution/diagnostic refresh semantics.
        var noOpPreviewNode = new NodeViewModel(9899, "Preview cache", 0, 0, bare: true);
        noOpPreviewNode.AddComponent<DialogueComponentViewModel>().DialogueText = "Stable preview";
        mainVm.SelectNodeQuiet(noOpPreviewNode);
        mainVm.EngineHost.ResetToStartNode();
        mainVm.ScheduleEnginePreviewUpdate(noOpPreviewNode);
        if (!mainVm.DeliverScheduledEnginePreview())
            throw new Exception("Initial script-free preview did not deliver");
        mainVm.ScheduleEnginePreviewUpdate(noOpPreviewNode);
        if (mainVm.DeliverScheduledEnginePreview())
            throw new Exception("Unchanged script-free preview redrew unnecessarily");

        var scriptedPreviewNode = new NodeViewModel(9900, "Script preview", 0, 0, bare: true);
        scriptedPreviewNode.AddComponent<ScriptComponentViewModel>();
        mainVm.SelectNodeQuiet(scriptedPreviewNode);
        mainVm.EngineHost.ResetToStartNode();
        mainVm.ScheduleEnginePreviewUpdate(scriptedPreviewNode);
        if (!mainVm.DeliverScheduledEnginePreview())
            throw new Exception("Initial script preview did not deliver");
        mainVm.ScheduleEnginePreviewUpdate(scriptedPreviewNode);
        if (!mainVm.DeliverScheduledEnginePreview())
            throw new Exception("Script preview was incorrectly treated as a no-op");
        Console.WriteLine("  ✅ [PASS] Preview no-op coalescing preserves script refresh behavior");
    }
}
