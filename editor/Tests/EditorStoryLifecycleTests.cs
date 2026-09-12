using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorStoryLifecycleTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 17]: StoryGraphLifecycleCoordinator & EditorWorkspaceLayoutService Isolation...");

        // Step 17.1: StoryGraphLifecycleCoordinator Start Node Resolution and State Propagation
        Console.WriteLine("    [Step 17.1]: StoryGraphLifecycleCoordinator Start Node Resolution...");
        var testNodes17 = new ObservableCollection<NodeViewModel>();
        var testConns17 = new ObservableCollection<ConnectionViewModel>();

        var node17A = new NodeViewModel(101, "Root Node", 0, 0, bare: true);
        var node17B = new NodeViewModel(102, "Child Node", 200, 0, bare: true);
        var node17C = new NodeViewModel(103, "Leaf Node", 400, 0, bare: true);
        testNodes17.Add(node17A);
        testNodes17.Add(node17B);
        testNodes17.Add(node17C);

        // Connect 101 -> 102 -> 103
        testConns17.Add(new ConnectionViewModel(node17A, node17B, "opt_a"));
        testConns17.Add(new ConnectionViewModel(node17B, node17C, "opt_b"));

        StoryGraphLifecycleCoordinator.UpdateStartNodeState(testNodes17, testConns17);
        var resolvedStart = StoryGraphLifecycleCoordinator.ResolveStartNode(testNodes17, testConns17);
        if (resolvedStart == null || resolvedStart.Id != 101 || !node17A.IsStartNode || node17B.IsStartNode || node17C.IsStartNode)
            throw new Exception("StoryGraphLifecycleCoordinator failed to resolve in-degree zero start node.");

        // Cyclic graph (all nodes have in-degree > 0): fallback to lowest ID
        var cycleConn = new ConnectionViewModel(node17C, node17A, "opt_cycle");
        testConns17.Add(cycleConn);
        StoryGraphLifecycleCoordinator.UpdateStartNodeState(testNodes17, testConns17);
        var cycleStart = StoryGraphLifecycleCoordinator.ResolveStartNode(testNodes17, testConns17);
        if (cycleStart == null || cycleStart.Id != 101)
            throw new Exception("StoryGraphLifecycleCoordinator cyclic graph lowest ID fallback failed.");
        testConns17.Remove(cycleConn);
        StoryGraphLifecycleCoordinator.UpdateStartNodeState(testNodes17, testConns17);

        // Step 17.2: Transactional Rollback on Corrupted Graph Document
        Console.WriteLine("    [Step 17.2]: Transactional Rollback on Corrupt Graph...");
        string corruptProjectDir = Path.Combine(Path.GetTempPath(), "RowlTest_Rollback_" + Guid.NewGuid().ToString("N"));
        string corruptAssetsJson = Path.Combine(corruptProjectDir, "Assets", "json");
        Directory.CreateDirectory(corruptAssetsJson);
        File.WriteAllText(Path.Combine(corruptAssetsJson, "full_story_graph.json"), "{ invalid json syntax !! }}}");

        int originalNodeCount = testNodes17.Count;
        int originalConnCount = testConns17.Count;

        bool test17RollbackResult = StoryGraphLifecycleCoordinator.LoadGraphWithRollback(
            Path.Combine(corruptProjectDir, "Assets"),
            corruptAssetsJson,
            testNodes17,
            testConns17,
            (s, e) => { },
            () => { },
            node => { },
            msg => { });

        if (test17RollbackResult)
            throw new Exception("LoadGraphWithRollback should fail on corrupt JSON.");
        if (testNodes17.Count != originalNodeCount || testConns17.Count != originalConnCount)
            throw new Exception("LoadGraphWithRollback failed to restore previous nodes/connections on error.");
        try { Directory.Delete(corruptProjectDir, true); } catch { }

        // Step 17.3: SaveProject Round-Trip Verification
        Console.WriteLine("    [Step 17.3]: SaveProject Round-Trip...");
        string saveTestDir = Path.Combine(Path.GetTempPath(), "RowlTest_SaveCoord_" + Guid.NewGuid().ToString("N"));
        string saveAssetsDir = Path.Combine(saveTestDir, "Assets");
        string saveAssetsJsonDir = Path.Combine(saveAssetsDir, "json");
        Directory.CreateDirectory(saveAssetsJsonDir);

        bool saveSuccess = StoryGraphLifecycleCoordinator.SaveProject(
            saveAssetsDir,
            saveAssetsJsonDir,
            testNodes17,
            testConns17,
            node17A,
            101,
            msg => { });

        if (!saveSuccess ||
            !File.Exists(Path.Combine(saveAssetsJsonDir, "full_story_graph.json")) ||
            !File.Exists(Path.Combine(saveAssetsJsonDir, "active_story.json")))
            throw new Exception("StoryGraphLifecycleCoordinator.SaveProject failed to create required files.");
        try { Directory.Delete(saveTestDir, true); } catch { }

        // Step 17.4: EditorWorkspaceLayoutService Panel Toggling & Tab Routing
        Console.WriteLine("    [Step 17.4]: EditorWorkspaceLayoutService Panel Toggling & Tabs...");
        bool isHierarchy = true, isAssets = false, isInspector = true, isLog = false;
        bool isBacklog = false, isSaveSlots = false, isIssues = false;
        int activeTab = 0;
        bool isGraph = true, isPrev = false, isEngPrev = false;
        int split = 0;

        // Toggle Hierarchy off
        EditorWorkspaceLayoutService.HandlePanelAction("Hierarchy",
            ref isHierarchy, ref isAssets, ref isInspector, ref isLog,
            ref isBacklog, ref isSaveSlots, ref isIssues,
            ref activeTab, ref isGraph, ref isPrev, ref isEngPrev, ref split);
        if (isHierarchy) throw new Exception("Hierarchy panel should have been toggled to false.");

        // Toggle Assets tab (tab 1)
        EditorWorkspaceLayoutService.HandlePanelAction("Assets",
            ref isHierarchy, ref isAssets, ref isInspector, ref isLog,
            ref isBacklog, ref isSaveSlots, ref isIssues,
            ref activeTab, ref isGraph, ref isPrev, ref isEngPrev, ref split);
        if (!isAssets || activeTab != 1) throw new Exception("Assets drawer should be visible and activeTab == 1.");

        // Toggle Assets tab again while active -> closes drawer
        EditorWorkspaceLayoutService.HandlePanelAction("Assets",
            ref isHierarchy, ref isAssets, ref isInspector, ref isLog,
            ref isBacklog, ref isSaveSlots, ref isIssues,
            ref activeTab, ref isGraph, ref isPrev, ref isEngPrev, ref split);
        if (isAssets) throw new Exception("Clicking active Assets tab again should have closed it.");

        // Step 17.5: Split Screen Cycling & Dimensions
        Console.WriteLine("    [Step 17.5]: Split Screen Cycling & Dimensions...");
        int mode0 = 0;
        int mode1 = EditorWorkspaceLayoutService.CycleSplitScreen(mode0, out bool g1, out bool ep1, out bool p1);
        int mode2 = EditorWorkspaceLayoutService.CycleSplitScreen(mode1, out _, out _, out _);
        int mode3 = EditorWorkspaceLayoutService.CycleSplitScreen(mode2, out _, out _, out _);

        if (mode1 != 1 || mode2 != 2 || mode3 != 0 || !g1)
            throw new Exception($"Split screen cycle invalid: {mode1}, {mode2}, {mode3}");

        var bottomOpen = EditorWorkspaceLayoutService.CalculateBottomPanelHeight(true, 180);
        var bottomClosed = EditorWorkspaceLayoutService.CalculateBottomPanelHeight(false, 180);
        if (bottomOpen.Value != 180 || bottomClosed.Value != 0)
            throw new Exception("CalculateBottomPanelHeight failed.");

        // Step 17.6: Quick Search Matching, Pan Target & Smooth Step
        Console.WriteLine("    [Step 17.6]: Quick Search & Smooth Camera Interpolation...");
        var searchNode = new NodeViewModel(777, "Secret Laboratory", 1200, 800)
        {
            Speaker = "Professor",
            DialogueText = "The prototype is almost ready!"
        };
        var searchList = new List<NodeViewModel> { node17A, node17B, searchNode };

        var foundByTitle = EditorWorkspaceLayoutService.FindMatchingNode(searchList, "secret");
        var foundBySpeaker = EditorWorkspaceLayoutService.FindMatchingNode(searchList, "professor");
        var foundByText = EditorWorkspaceLayoutService.FindMatchingNode(searchList, "prototype");
        var foundNone = EditorWorkspaceLayoutService.FindMatchingNode(searchList, "nonexistent");

        if (foundByTitle != searchNode || foundBySpeaker != searchNode || foundByText != searchNode || foundNone != null)
            throw new Exception("EditorWorkspaceLayoutService.FindMatchingNode failed.");

        var (targetX, targetY) = EditorWorkspaceLayoutService.CalculatePanTargetForNode(searchNode, 1.5, 300, 200);
        double expectedX = -1200 * 1.5 + 300;
        double expectedY = -800 * 1.5 + 200;
        if (Math.Abs(targetX - expectedX) > 0.001 || Math.Abs(targetY - expectedY) > 0.001)
            throw new Exception("CalculatePanTargetForNode calculation mismatch.");

        double curZoom = 1.0, curPanX = 0, curPanY = 0;
        bool reached = false;
        for (int step = 0; step < 200 && !reached; step++)
        {
            reached = EditorWorkspaceLayoutService.ComputeSmoothStep(
                ref curZoom, ref curPanX, ref curPanY,
                1.5, targetX, targetY, 0.22);
        }
        if (!reached || Math.Abs(curZoom - 1.5) > 0.001 || Math.Abs(curPanX - targetX) > 0.05)
            throw new Exception("ComputeSmoothStep failed to converge to target pan and zoom.");

        Console.WriteLine("  ✅ [PASS] StoryGraphLifecycleCoordinator & EditorWorkspaceLayoutService Isolation verified");
    }
}
