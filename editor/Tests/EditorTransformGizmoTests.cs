using System;
using System.Linq;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorTransformGizmoTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 22]: Visual Transform Gizmo, Rotation & Scale Controls...");

        // Step 22.1: CharacterComponentViewModel Rotation & Scale properties
        Console.WriteLine("    [Step 22.1]: CharacterComponentViewModel Rotation & Scale...");
        var charComp = new CharacterComponentViewModel();
        if (charComp.Rotation != 0.0 || charComp.ScaleX != 1.0 || charComp.ScaleY != 1.0 || !charComp.MaintainAspectRatio)
            throw new Exception("CharacterComponentViewModel default values mismatch");

        charComp.Rotation = 45.0;
        charComp.ScaleX = 1.5;
        // With MaintainAspectRatio == true, setting ScaleX should synchronize Scale and ScaleY
        if (Math.Abs(charComp.Scale - 1.5) > 0.001 || Math.Abs(charComp.ScaleY - 1.5) > 0.001)
            throw new Exception($"MaintainAspectRatio failed to sync ScaleY: Scale={charComp.Scale}, ScaleY={charComp.ScaleY}");

        charComp.MaintainAspectRatio = false;
        charComp.ScaleY = 2.0;
        if (Math.Abs(charComp.ScaleX - 1.5) > 0.001 || Math.Abs(charComp.ScaleY - 2.0) > 0.001)
            throw new Exception("Non-proportional scaling failed");

        charComp.ResetRotation();
        if (charComp.Rotation != 0.0)
            throw new Exception("ResetRotation failed on CharacterComponentViewModel");

        // Step 22.2: BackgroundComponentViewModel Rotation & Reset
        Console.WriteLine("    [Step 22.2]: BackgroundComponentViewModel Rotation...");
        var bgComp = new BackgroundComponentViewModel();
        if (bgComp.Rotation != 0.0)
            throw new Exception("BackgroundComponentViewModel default rotation mismatch");

        bgComp.Rotation = 180.0;
        if (bgComp.Rotation != 180.0)
            throw new Exception("BackgroundComponentViewModel rotation set failed");

        bgComp.ResetRotation();
        if (bgComp.Rotation != 0.0)
            throw new Exception("ResetRotation failed on BackgroundComponentViewModel");

        // Step 22.3: Component Serialization & Deserialization
        Console.WriteLine("    [Step 22.3]: Serialization & Deserialization roundtrip...");
        charComp.Rotation = 72.5;
        charComp.ScaleX = 1.25;
        charComp.ScaleY = 1.75;
        charComp.MaintainAspectRatio = false;

        var serializedChar = charComp.Serialize();
        if (!serializedChar.ContainsKey("rotation") || !serializedChar.ContainsKey("scale_x") || !serializedChar.ContainsKey("scale_y"))
            throw new Exception("CharacterComponentViewModel serialization missing rotation or scale keys");

        var newCharComp = new CharacterComponentViewModel();
        newCharComp.Deserialize(serializedChar.ToDictionary(k => k.Key, v => (object?)v.Value));
        if (Math.Abs(newCharComp.Rotation - 72.5) > 0.001 || Math.Abs(newCharComp.ScaleX - 1.25) > 0.001 || Math.Abs(newCharComp.ScaleY - 1.75) > 0.001)
            throw new Exception("CharacterComponentViewModel deserialization value mismatch");

        // Step 22.4: NodeViewModel Proxy Properties & Change Propagation
        Console.WriteLine("    [Step 22.4]: NodeViewModel Proxy Properties & Propagation...");
        var testNode = new NodeViewModel(1022, "Transform Test Node", 0, 0, bare: false);
        bool bgRotNotified = false;
        bool charRotNotified = false;
        testNode.PropertyChanged += (s, e) =>
        {
            if (e.PropertyName == nameof(NodeViewModel.BackgroundRotation)) bgRotNotified = true;
            if (e.PropertyName == nameof(NodeViewModel.CharacterRotation)) charRotNotified = true;
        };

        testNode.BackgroundRotation = 90.0;
        testNode.CharacterRotation = 270.0;
        if (!bgRotNotified || !charRotNotified)
            throw new Exception("NodeViewModel failed to notify BackgroundRotation or CharacterRotation changes");
        if (Math.Abs(testNode.BackgroundRotation - 90.0) > 0.001 || Math.Abs(testNode.CharacterRotation - 270.0) > 0.001)
            throw new Exception("NodeViewModel proxy property getters returned incorrect values");

        // Step 22.5: ResetRotation Command in MainWindowViewModel
        Console.WriteLine("    [Step 22.5]: ResetRotationCommand integration...");
        mainVm.SelectNodeQuiet(testNode);
        mainVm.ResetRotation();
        if (testNode.BackgroundRotation != 0.0 || testNode.CharacterRotation != 0.0)
            throw new Exception("ResetRotationCommand failed to reset rotations on selected node components");

        Console.WriteLine("  ✅ [PASS] Visual Transform Gizmo, Rotation & Scale Controls verified");
    }
}
