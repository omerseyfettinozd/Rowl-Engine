using System;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorLayoutAssistTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 6]: OBS Assist Transform & Alignment System...");
        if (mainVm.SelectedNode == null && mainVm.Nodes.Count > 0)
            mainVm.SelectNode(mainVm.Nodes[0]);

        if (mainVm.SelectedNode != null)
        {
            mainVm.FitBackgroundToScreen();
            if (mainVm.SelectedNode.BackgroundX != 0 || mainVm.SelectedNode.BackgroundY != 0 ||
                mainVm.SelectedNode.BackgroundWidth != 1920 || mainVm.SelectedNode.BackgroundHeight != 1080)
            {
                throw new Exception("FitBackgroundToScreen failed");
            }

            mainVm.CenterSelectedElement();
            var character = mainVm.SelectedNode.GetComponent<CharacterComponentViewModel>();
            if (character != null && character.X != (1920 - character.Width) / 2.0)
                throw new Exception("CenterSelectedElement failed for Character");

            mainVm.AlignCharacterToBottom();
            if (character != null && character.Y != 1080 - character.Height - 20)
                throw new Exception("AlignCharacterToBottom failed");

            bool initialSnap = mainVm.IsSnapAssistEnabled;
            mainVm.ToggleSnapAssist();
            if (mainVm.IsSnapAssistEnabled == initialSnap)
                throw new Exception("ToggleSnapAssist failed to flip boolean state");
            mainVm.ToggleSnapAssist();
        }

        Console.WriteLine("  ✅ [PASS] OBS Assist (Fit 1080p, Center, Ground Baseline, Snap Toggle) verified");
    }
}
