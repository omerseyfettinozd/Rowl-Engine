using System;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorComponentHierarchyTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 1]: NodeViewModel & Modular Component Trash Can Button...");
        var node = new NodeViewModel(101, "Test Node", 100, 150, bare: false);
        if (node.Components.Count < 4)
            throw new Exception("Expected at least 4 default components");
        node.IsStartNode = true;
        if (node.BorderColor != "#10B981")
            throw new Exception("Start node visual state was not applied");
        node.IsSelected = true;
        if (node.BorderColor != "#F09A78")
            throw new Exception("Selected node visual state did not take priority");
        node.IsSelected = false;
        if (node.BorderColor != "#10B981" || !node.ComponentSummary.EndsWith("BİLEŞEN"))
            throw new Exception("Node card visual summary state is inconsistent");

        var dialogue = node.GetComponent<DialogueComponentViewModel>();
        if (dialogue == null)
            throw new Exception("DialogueComponentViewModel missing");
        dialogue.Speaker = "TestSpeaker";
        dialogue.DialogueText = "Hello Unit Test!";
        dialogue.X = 120;
        dialogue.Y = 820;
        if (node.Speaker != "TestSpeaker" || node.DialogueText != "Hello Unit Test!" ||
            node.DialogueBoxX != 120 || node.DialogueBoxY != 820)
        {
            throw new Exception("Proxy dialogue properties mismatch");
        }

        var secondCharacter = node.AddComponent<CharacterComponentViewModel>();
        secondCharacter.Sprite = "Margot.jpg";
        secondCharacter.X = 1200;
        if (node.Components.Count(component => component is CharacterComponentViewModel) != 2)
            throw new Exception("Multi-character addition failed");

        var secondDialogue = node.AddComponent<DialogueComponentViewModel>();
        secondDialogue.Speaker = "SecondSpeaker";
        secondDialogue.DialogueText = "I am the second dialogue!";
        if (node.DialogueComponents.Count != 2)
            throw new Exception("Multi-dialogue addition failed: expected 2 dialogue components");
        secondDialogue.RemoveSelfCommand.Execute(null);
        if (node.DialogueComponents.Count != 1)
            throw new Exception("Dialogue removal failed");

        var script = node.AddComponent<ScriptComponentViewModel>();
        script.ScriptPath = "scripts/intro.lua";
        script.InlineCode = "function on_enter() rowl.var_set('seen_intro', '1') end";
        var scriptData = script.Serialize();
        var restoredScript = new ScriptComponentViewModel();
        restoredScript.Deserialize(
            scriptData.ToDictionary(pair => pair.Key, pair => (object?)pair.Value));
        if (restoredScript.ScriptPath != script.ScriptPath ||
            restoredScript.InlineCode != script.InlineCode)
        {
            throw new Exception("ScriptComponent serialization mismatch");
        }
        script.RuntimeState = "failed";
        script.RuntimeError = "syntax error";
        if (!script.HasRuntimeError || script.Serialize().ContainsKey("runtime_error"))
            throw new Exception("Script runtime feedback leaked into story serialization");

        var audioSettings = node.GetComponent<AudioComponentViewModel>();
        if (audioSettings == null)
            throw new Exception("Audio component missing");
        audioSettings.BgmTransition = "crossfade";
        audioSettings.BgmTransitionDurationSeconds = 2.5f;
        var serializedAudio = audioSettings.Serialize();
        var restoredAudio = new AudioComponentViewModel();
        restoredAudio.Deserialize(
            serializedAudio.ToDictionary(pair => pair.Key, pair => (object?)pair.Value));
        if (restoredAudio.BgmTransition != "crossfade" ||
            restoredAudio.BgmTransitionDurationSeconds != 2.5f)
        {
            throw new Exception("Audio transition settings serialization mismatch");
        }

        secondCharacter.RemoveSelfCommand.Execute(null);
        if (node.Components.Count(component => component is CharacterComponentViewModel) != 1)
        {
            throw new Exception(
                "Trash can button (RemoveSelfCommand) failed to remove component!");
        }
        using (var previewDocument = JsonDocument.Parse(
                   StoryGraphSerializer.SerializePreviewComponents(node)))
        {
            if (previewDocument.RootElement.ValueKind != JsonValueKind.Array ||
                previewDocument.RootElement.GetArrayLength() !=
                node.AllComponents.Count(component => component.IsEnabled) ||
                previewDocument.RootElement.EnumerateArray().Any(component =>
                    !component.TryGetProperty("type", out _) ||
                    !component.TryGetProperty("data", out _)))
            {
                throw new Exception("Preview component serialization lost its engine contract");
            }
        }
        Console.WriteLine(
            "  ✅ [PASS] Component addition, script serialization, proxy sync, and " +
            "Trash Can (RemoveSelfCommand) verified");

        var emptyHierarchyNode = new NodeViewModel(
            102, "Empty Hierarchy", 0, 0, bare: true);
        mainVm.SelectedNode = emptyHierarchyNode;
        if (mainVm.HierarchyViewModel.HasObjects)
            throw new Exception("Hierarchy reported objects for an empty node");
        emptyHierarchyNode.Title = "Renamed Empty Hierarchy";
        if (mainVm.HierarchyViewModel.CurrentNodeTitle != "Renamed Empty Hierarchy")
            throw new Exception("Hierarchy header did not update after the selected node was renamed");
        var hierarchyObject = emptyHierarchyNode.CreateObject("Temporary Object");
        if (!mainVm.HierarchyViewModel.HasObjects)
            throw new Exception("Hierarchy empty state did not update after object creation");
        emptyHierarchyNode.RemoveObject(hierarchyObject);
        if (mainVm.HierarchyViewModel.HasObjects)
            throw new Exception("Hierarchy empty state did not update after object deletion");
        mainVm.SelectedNode = null;
        if (mainVm.HierarchyViewModel.IsCurrentNodeEmpty)
            throw new Exception("Hierarchy showed an empty-frame state without a selected node");
        Console.WriteLine(
            "  ✅ [PASS] Hierarchy empty state tracks GameObject creation and deletion");
    }
}
