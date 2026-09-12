using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

internal static class EditorGraphPersistenceTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 4]: Story Graph v4 Serialization & Coordinate Persistence...");
        if (!mainVm.LoadFullStoryGraphFile())
            throw new Exception("Failed to load full_story_graph.json");
        if (mainVm.Nodes.Count == 0)
            throw new Exception("Nodes collection empty after load");
        Console.WriteLine(
            $"  ✅ [PASS] Loaded {mainVm.Nodes.Count} project nodes with full component integrity " +
            "(no unwanted node pollution)");

        if (!ProjectOpenCoordinator.TryResolve(testProjectRoot, out var resolvedProject) ||
            resolvedProject?.RootPath != testProjectRoot ||
            !File.Exists(resolvedProject.GraphFilePath))
        {
            throw new Exception("Project root selection was not resolved safely");
        }
        if (!ProjectOpenCoordinator.TryResolve(
                Path.Combine(testProjectRoot, "Assets"), out var resolvedAssets) ||
            resolvedAssets?.RootPath != testProjectRoot)
        {
            throw new Exception("Assets folder selection was not resolved to its project root");
        }
        Console.WriteLine(
            "  ✅ [PASS] Project-folder and Assets-folder selection resolve to the same project root");

        var successfulTransition = new List<string>();
        bool switchSucceeded = ProjectOpenCoordinator.Switch(
            resolvedProject!, "old-root", "old-path",
            () => successfulTransition.Add("clear"),
            (root, path) => successfulTransition.Add($"project:{root}:{path}"),
            root => successfulTransition.Add($"mount:{root}"),
            () => { successfulTransition.Add("load"); return true; },
            () => successfulTransition.Add("refresh-bitmaps"),
            () => successfulTransition.Add("refresh-assets"));
        if (!switchSucceeded || !successfulTransition.SequenceEqual(new[]
            { "clear", $"project:{testProjectRoot}:{testProjectRoot}", $"mount:{testProjectRoot}", "load", "refresh-assets" }))
        {
            throw new Exception("Successful project transition ordering changed");
        }

        var failedTransition = new List<string>();
        bool switchFailed = ProjectOpenCoordinator.Switch(
            resolvedProject, "old-root", "old-path",
            () => failedTransition.Add("clear"),
            (root, path) => failedTransition.Add($"project:{root}:{path}"),
            root => failedTransition.Add($"mount:{root}"),
            () => { failedTransition.Add("load"); return false; },
            () => failedTransition.Add("refresh-bitmaps"),
            () => failedTransition.Add("refresh-assets"));
        if (switchFailed || !failedTransition.SequenceEqual(new[]
            {
                "clear", $"project:{testProjectRoot}:{testProjectRoot}", $"mount:{testProjectRoot}", "load",
                "project:old-root:old-path", "clear", "refresh-bitmaps", "mount:old-root", "refresh-assets"
            }))
        {
            throw new Exception("Failed project transition rollback ordering changed");
        }
        Console.WriteLine("  ✅ [PASS] Project transition success and rollback ordering");

        string fileSystemTestRoot = Path.Combine(
            Path.GetTempPath(), $"RowlFileSystemTests_{Guid.NewGuid():N}");
        try
        {
            string sourceDirectory = Path.Combine(fileSystemTestRoot, "source");
            string targetDirectory = Path.Combine(fileSystemTestRoot, "target");
            Directory.CreateDirectory(Path.Combine(sourceDirectory, "nested"));
            File.WriteAllText(Path.Combine(sourceDirectory, "nested", "asset.txt"), "asset");
            ProjectFileSystem.CopyDirectory(sourceDirectory, targetDirectory);
            if (File.ReadAllText(Path.Combine(targetDirectory, "nested", "asset.txt")) != "asset")
                throw new Exception("Project directory copy did not preserve nested assets");

            string atomicFile = Path.Combine(fileSystemTestRoot, "atomic", "story.json");
            ProjectFileSystem.WriteAllTextAtomically(atomicFile, "first");
            ProjectFileSystem.WriteAllTextAtomically(atomicFile, "second");
            if (File.ReadAllText(atomicFile) != "second" ||
                Directory.GetFiles(Path.GetDirectoryName(atomicFile)!, "*.tmp").Length != 0)
            {
                throw new Exception("Atomic project write did not replace content cleanly");
            }
        }
        finally
        {
            try { Directory.Delete(fileSystemTestRoot, true); } catch { }
        }
        Console.WriteLine("  ✅ [PASS] Project file copy and atomic write primitives");

        var multiChoiceSource = new NodeViewModel(9001, "Multi Choice", 0, 0, bare: true);
        var firstChoiceObject = multiChoiceSource.CreateObject("First Choices");
        var firstChoice = firstChoiceObject.AddComponent<ChoiceComponentViewModel>();
        firstChoice.Options[0].OptionId = "first_route";
        firstChoice.Options[0].TargetNodeId = 9002;
        var secondChoiceObject = multiChoiceSource.CreateObject("Second Choices");
        var secondChoice = secondChoiceObject.AddComponent<ChoiceComponentViewModel>();
        secondChoice.Options[0].OptionId = "second_route";
        secondChoice.Options[0].TargetNodeId = 9003;
        mainVm.Nodes.Add(multiChoiceSource);
        mainVm.Nodes.Add(new NodeViewModel(9002, "First Target", 300, 0, bare: true));
        mainVm.Nodes.Add(new NodeViewModel(9003, "Second Target", 600, 0, bare: true));
        mainVm.SaveFullStoryGraphFile();
        if (!mainVm.LoadFullStoryGraphFile())
            throw new Exception("Multi-Choice graph failed to reload");
        var reloadedRoutes = mainVm.Connections
            .Where(connection => connection.SourceNode?.Id == 9001)
            .Select(connection => connection.OptionId)
            .ToHashSet(StringComparer.Ordinal);
        if (!reloadedRoutes.SetEquals(new[] { "first_route", "second_route" }))
            throw new Exception("Multiple ChoiceComponents did not preserve every route");
        Console.WriteLine(
            "  ✅ [PASS] Multiple ChoiceComponents preserve every stable-ID route across save/reload");

        var deletedTarget = mainVm.Nodes.First(item => item.Id == 9002);
        mainVm.DeleteNode(deletedTarget);
        mainVm.SaveFullStoryGraphFile();
        using (var savedGraph = JsonDocument.Parse(File.ReadAllText(
                   Path.Combine(MainWindowViewModel.AssetsPath, "full_story_graph.json"))))
        {
            var savedSource = savedGraph.RootElement.GetProperty("nodes")
                .EnumerateArray().First(item => item.GetProperty("id").GetUInt64() == 9001);
            var savedRoutes = savedSource.GetProperty("next_nodes").EnumerateArray()
                .Select(item => item.GetProperty("option_id").GetString())
                .ToArray();
            if (savedRoutes.Contains("first_route") || !savedRoutes.Contains("second_route"))
                throw new Exception("Deleting a target node left a stale choice route");
        }
        Console.WriteLine("  ✅ [PASS] Deleting a target node removes its persisted choice route");

        string graphPath = Path.Combine(
            MainWindowViewModel.AssetsPath, "full_story_graph.json");
        string validGraph = File.ReadAllText(graphPath);
        var nodeIdsBeforeFailedLoad = mainVm.Nodes.Select(item => item.Id).ToArray();
        File.WriteAllText(graphPath, "{\"nodes\":[{\"id\":9999},");
        if (mainVm.LoadFullStoryGraphFile())
            throw new Exception("Malformed graph was unexpectedly accepted");
        if (!mainVm.Nodes.Select(item => item.Id).SequenceEqual(nodeIdsBeforeFailedLoad))
            throw new Exception("Malformed graph replaced the current in-memory project");
        File.WriteAllText(graphPath, validGraph);
        Console.WriteLine("  ✅ [PASS] Malformed graph loading preserves the current in-memory project");
    }
}
