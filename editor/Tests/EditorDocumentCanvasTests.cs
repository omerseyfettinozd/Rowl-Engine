using System;
using System.Collections.Generic;
using System.IO;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorDocumentCanvasTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 13]: StoryGraphDocumentWriter, StoryGraphCanvasService & Layout Assist Presets...");

        // 1. StoryGraphDocumentWriter Persistence
        string testWriterAssetsDir = Path.Combine(Path.GetTempPath(), $"RowlTestWriter_Assets_{Guid.NewGuid():N}");
        string testWriterJsonDir = Path.Combine(testWriterAssetsDir, "json");
        var testNodeA = new NodeViewModel(101, "Writer Node A", 100, 100, bare: true);
        testNodeA.AddComponent<DialogueComponentViewModel>().DialogueText = "Hello from Node A";
        var testNodeB = new NodeViewModel(102, "Writer Node B", 400, 200, bare: true);
        testNodeB.AddComponent<DialogueComponentViewModel>().DialogueText = "Hello from Node B";
        var testNodesList = new List<NodeViewModel> { testNodeA, testNodeB };
        var testConnList = new List<ConnectionViewModel> { new ConnectionViewModel(testNodeA, testNodeB, "optA") };

        bool fullSaved = StoryGraphDocumentWriter.SaveFullStoryGraph(
            testWriterAssetsDir,
            testWriterJsonDir,
            testNodesList,
            testConnList,
            101);
        if (!fullSaved)
            throw new Exception("StoryGraphDocumentWriter.SaveFullStoryGraph returned false");

        string fullGraphPath1 = Path.Combine(testWriterAssetsDir, "full_story_graph.json");
        string fullGraphPath2 = Path.Combine(testWriterJsonDir, "full_story_graph.json");
        if (!File.Exists(fullGraphPath1) || !File.Exists(fullGraphPath2))
            throw new Exception("StoryGraphDocumentWriter did not produce full_story_graph.json in both target directories");

        string fullGraphContent = File.ReadAllText(fullGraphPath1);
        if (!fullGraphContent.Contains("\"start_node_id\": 101") || !fullGraphContent.Contains("\"Writer Node A\""))
            throw new Exception("full_story_graph.json content verification failed");

        bool activeSaved = StoryGraphDocumentWriter.SaveActiveStory(testWriterJsonDir, testNodeA);
        if (!activeSaved)
            throw new Exception("StoryGraphDocumentWriter.SaveActiveStory returned false");

        string activeStoryPath = Path.Combine(testWriterJsonDir, "active_story.json");
        if (!File.Exists(activeStoryPath))
            throw new Exception("StoryGraphDocumentWriter did not produce active_story.json");
        string activeStoryContent = File.ReadAllText(activeStoryPath);
        if (!activeStoryContent.Contains("Hello from Node A"))
            throw new Exception("active_story.json content verification failed");

        // Null node should return false without crashing
        if (StoryGraphDocumentWriter.SaveActiveStory(testWriterJsonDir, null))
            throw new Exception("StoryGraphDocumentWriter.SaveActiveStory should return false for null node");

        // Clean up writer test dir
        try { Directory.Delete(testWriterAssetsDir, true); } catch { }

        // 2. StoryGraphCanvasService Wire Routing & Hit-Testing
        var canvasNodes = new System.Collections.ObjectModel.ObservableCollection<NodeViewModel>();
        var canvasConns = new System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel>();
        var srcNode = new NodeViewModel(201, "Source Node", 100, 100, bare: true);
        var choiceComp = srcNode.AddComponent<ChoiceComponentViewModel>();
        string optionId = choiceComp.Options[0].OptionId;

        var dstNode = new NodeViewModel(202, "Target Node", 500, 300, bare: true);
        canvasNodes.Add(srcNode);
        canvasNodes.Add(dstNode);

        // Left input pin of dstNode is at (X + 10, Y + 60) -> (510, 360).
        // Case A: Successful hit (within 75px radius, e.g. at 520, 370 ~ 14px away)
        var hitPoint = new Avalonia.Point(520, 370);
        var connected = StoryGraphCanvasService.TryConnectWire(srcNode, hitPoint, canvasNodes, canvasConns, optionId);
        if (connected == null || canvasConns.Count != 1)
            throw new Exception("StoryGraphCanvasService.TryConnectWire failed to connect within snap radius");
        if (connected.SourceNode != srcNode || connected.TargetNode != dstNode)
            throw new Exception("StoryGraphCanvasService.TryConnectWire established invalid source/target");
        if (choiceComp.Options[0].TargetNodeId != dstNode.Id)
            throw new Exception("StoryGraphCanvasService did not update ChoiceOption.TargetNodeId on connect");

        // Case B: Discard drop outside radius (e.g. at 800, 800)
        var missPoint = new Avalonia.Point(800, 800);
        var missedConn = StoryGraphCanvasService.TryConnectWire(srcNode, missPoint, canvasNodes, canvasConns, optionId);
        if (missedConn != null)
            throw new Exception("StoryGraphCanvasService.TryConnectWire should return null for release point outside snap radius");

        // Case C: Disconnect Node Inputs & Outputs
        if (StoryGraphCanvasService.DisconnectNodeInputs(dstNode, canvasConns) != 1)
            throw new Exception("StoryGraphCanvasService.DisconnectNodeInputs failed to remove targeting connections");
        if (canvasConns.Count != 0)
            throw new Exception("Connections collection was not empty after DisconnectNodeInputs");

        // Reconnect and test DisconnectNodeOutputs
        var reconnected = StoryGraphCanvasService.TryConnectWire(srcNode, hitPoint, canvasNodes, canvasConns, optionId);
        if (reconnected == null || canvasConns.Count != 1)
            throw new Exception("Reconnection failed");
        if (StoryGraphCanvasService.DisconnectNodeOutputs(srcNode, canvasConns) != 1)
            throw new Exception("StoryGraphCanvasService.DisconnectNodeOutputs failed to remove outgoing connections");
        if (canvasConns.Count != 0)
            throw new Exception("Connections collection was not empty after DisconnectNodeOutputs");

        // Case D: Deduplication via EnforceSingleOutgoingWireRule
        var conn1 = new ConnectionViewModel(srcNode, dstNode, optionId);
        var conn2 = new ConnectionViewModel(srcNode, dstNode, optionId);
        canvasConns.Add(conn1);
        canvasConns.Add(conn2);
        if (canvasConns.Count != 2) throw new Exception("Setup for deduplication failed");
        StoryGraphCanvasService.EnforceSingleOutgoingWireRule(canvasConns);
        if (canvasConns.Count != 1)
            throw new Exception($"StoryGraphCanvasService.EnforceSingleOutgoingWireRule failed: expected 1 connection, got {canvasConns.Count}");

        // 3. EditorLayoutAssistService Presets Verification
        var layoutNode = new NodeViewModel(301, "Layout Node", 0, 0, bare: true);
        layoutNode.AddComponent<DialogueComponentViewModel>();
        layoutNode.AddComponent<CharacterComponentViewModel>();
        layoutNode.AddComponent<BackgroundComponentViewModel>();

        EditorLayoutAssistService.PresetDialogueBox(layoutNode, "Square");
        if (layoutNode.DialogueBoxWidth != 500.0 || layoutNode.DialogueBoxHeight != 500.0)
            throw new Exception("PresetDialogueBox('Square') failed to set 500x500 dimensions");

        EditorLayoutAssistService.PresetDialogueBox(layoutNode, "Standard");
        if (layoutNode.DialogueBoxWidth != 1760.0 || layoutNode.DialogueBoxHeight != 180.0 ||
            layoutNode.DialogueBoxX != 80.0 || layoutNode.DialogueBoxY != 860.0)
            throw new Exception("PresetDialogueBox('Standard') failed to set 1760x180 at (80, 860)");

        EditorLayoutAssistService.FitBackgroundToScreen(layoutNode);
        if (layoutNode.BackgroundWidth != 1920.0 || layoutNode.BackgroundHeight != 1080.0 ||
            layoutNode.BackgroundX != 0.0 || layoutNode.BackgroundY != 0.0)
            throw new Exception("FitBackgroundToScreen failed to set 1920x1080 at (0, 0)");

        EditorLayoutAssistService.ResetCharacterDimensions(layoutNode);
        if (layoutNode.CharacterWidth != 360.0 || layoutNode.CharacterHeight != 540.0 ||
            layoutNode.CharacterScale != 1.0)
            throw new Exception("ResetCharacterDimensions failed to set 360x540 scale 1.0");

        Console.WriteLine("  ✅ [PASS] StoryGraphDocumentWriter, StoryGraphCanvasService & Layout Assist Presets verified");
    }
}
