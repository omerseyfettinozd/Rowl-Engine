using System;
using System.IO;
using System.Linq;
using System.Threading;
using Avalonia;
using Avalonia.Threading;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

// MS-5 regression gate: drag cancellation, canvas keyboard tour, and the
// asset watcher + missing-asset badge. Runs inside the headless suite
// (dispatcher available, no display) as Test 31.
internal static class EditorMs5UxTests
{
    public static void Run(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("\n📌 [Test 31]: MS-5 editor UX — drag cancel, keyboard tour, asset watcher...");

        var scratchA = new NodeViewModel(9101, "MS5 Scratch A", 100, 100, bare: true);
        var scratchB = new NodeViewModel(9102, "MS5 Scratch B", 600, 100, bare: true);
        mainVm.Nodes.Add(scratchA);
        mainVm.Nodes.Add(scratchB);
        try
        {
            DragCancelRevertsWithoutUndo(mainVm, scratchA);
            WireCancelRestoresWithoutUndo(mainVm, scratchA, scratchB);
            KeyboardTourRoundTrips(mainVm, scratchA, scratchB);
            MissingBadgeRoundTrips(mainVm, testProjectRoot);
            WatcherAutoRefreshes(mainVm, testProjectRoot);
        }
        finally
        {
            foreach (var conn in mainVm.Connections
                .Where(c => c.SourceNode == scratchA || c.TargetNode == scratchA
                    || c.SourceNode == scratchB || c.TargetNode == scratchB).ToList())
                mainVm.Connections.Remove(conn);
            mainVm.SelectedNodes.Remove(scratchA);
            mainVm.SelectedNodes.Remove(scratchB);
            mainVm.Nodes.Remove(scratchA);
            mainVm.Nodes.Remove(scratchB);
            if (mainVm.SelectedNode == scratchA || mainVm.SelectedNode == scratchB)
                mainVm.SelectedNode = mainVm.Nodes.FirstOrDefault();
        }

        Console.WriteLine("  ✅ [PASS] MS-5 editor UX verified");
    }

    private static void UndoBaseline(out bool canUndo, out string description)
    {
        canUndo = UndoRedoService.Instance.CanUndo;
        description = UndoRedoService.Instance.UndoDescription;
    }

    private static void AssertNoNewUndo(string step, bool canUndoBefore, string descBefore)
    {
        if (UndoRedoService.Instance.CanUndo != canUndoBefore
            || UndoRedoService.Instance.UndoDescription != descBefore)
            throw new Exception($"{step}: cancelled gesture must not record undo.");
    }

    // Step 31.1: Alt+Tab-style abort — positions revert, no undo record.
    private static void DragCancelRevertsWithoutUndo(MainWindowViewModel mainVm, NodeViewModel node)
    {
        Console.WriteLine("    [Step 31.1]: Node-drag cancel reverts + no undo...");
        mainVm.SelectNodeQuiet(node);
        double x0 = node.X, y0 = node.Y;
        UndoBaseline(out bool canUndoBefore, out string descBefore);

        mainVm.BeginNodeDragSnapshot();
        if (!mainVm.HasPendingNodeDrag)
            throw new Exception("BeginNodeDragSnapshot did not arm a pending drag.");
        node.X += 250; node.Y += 130;
        mainVm.CancelNodeDrag();

        if (node.X != x0 || node.Y != y0)
            throw new Exception($"CancelNodeDrag did not restore start pos (got {node.X},{node.Y}, want {x0},{y0}).");
        if (mainVm.HasPendingNodeDrag)
            throw new Exception("CancelNodeDrag left a pending drag armed.");
        AssertNoNewUndo("CancelNodeDrag", canUndoBefore, descBefore);

        // No-op path must be safe.
        mainVm.CancelNodeDrag();
        mainVm.CancelWireDrag();
        AssertNoNewUndo("Cancel-noop", canUndoBefore, descBefore);
    }

    // Step 31.2: Escape during wire drag — plain abort + unplug restore.
    private static void WireCancelRestoresWithoutUndo(MainWindowViewModel mainVm, NodeViewModel src, NodeViewModel dst)
    {
        Console.WriteLine("    [Step 31.2]: Wire-drag cancel restores + no undo...");
        int connsBefore = mainVm.Connections.Count;
        UndoBaseline(out bool canUndoBefore, out string descBefore);

        mainVm.StartWireDrag(src, new Point(src.X + 265, src.Y + 60));
        if (!mainVm.IsDraggingWire)
            throw new Exception("StartWireDrag did not arm a wire drag.");
        mainVm.CancelWireDrag();
        if (mainVm.IsDraggingWire)
            throw new Exception("CancelWireDrag left IsDraggingWire set.");
        if (mainVm.Connections.Count != connsBefore)
            throw new Exception("Plain wire cancel changed the connection set.");
        AssertNoNewUndo("CancelWireDrag", canUndoBefore, descBefore);

        // Unplug path: NodeControl removes the cable first, then a cancel
        // must silently put it back (EndWireDrag would record a disconnect).
        var wire = StoryGraphCanvasService.TryConnectWire(
            src, new Point(dst.X + 10, dst.Y + 60), mainVm.Nodes, mainVm.Connections);
        if (wire == null)
            throw new Exception("TryConnectWire failed to build the unplug fixture.");
        mainVm.Connections.Remove(wire);
        mainVm.StartUnplugWireDrag(src, new Point(src.X + 300, src.Y + 300), string.Empty, wire);
        mainVm.CancelWireDrag();
        if (!mainVm.Connections.Contains(wire))
            throw new Exception("Wire cancel did not restore the unplugged cable.");
        if (mainVm.IsDraggingWire)
            throw new Exception("Unplug cancel left IsDraggingWire set.");
        AssertNoNewUndo("CancelUnplugDrag", canUndoBefore, descBefore);
        mainVm.Connections.Remove(wire);
    }

    // Step 31.3: Ctrl+A / C / V / D + arrows, each undoable in one step.
    private static void KeyboardTourRoundTrips(MainWindowViewModel mainVm, NodeViewModel a, NodeViewModel b)
    {
        Console.WriteLine("    [Step 31.3]: Keyboard tour (select/copy/paste/nudge/duplicate)...");

        mainVm.SelectAllNodesCommand.Execute(null);
        if (mainVm.SelectedNodes.Count != mainVm.Nodes.Count || mainVm.SelectedNodes.Count < 2)
            throw new Exception($"Ctrl+A selected {mainVm.SelectedNodes.Count}/{mainVm.Nodes.Count}.");

        mainVm.SelectNodeQuiet(a);
        mainVm.SelectNodeQuiet(b, addToSelection: true);
        mainVm.CopySelectedNodesCommand.Execute(null);
        if (mainVm.ClipboardNodeCount != 2)
            throw new Exception($"Ctrl+C clipboard holds {mainVm.ClipboardNodeCount}, want 2.");

        int nodesBefore = mainVm.Nodes.Count;
        int connsBefore = mainVm.Connections.Count;
        mainVm.PasteClipboardNodesCommand.Execute(null);
        if (mainVm.Nodes.Count != nodesBefore + 2)
            throw new Exception($"Ctrl+V pasted {mainVm.Nodes.Count - nodesBefore}, want 2.");
        var clones = mainVm.SelectedNodes.ToList();
        if (clones.Count != 2 || clones.Any(c => c == a || c == b))
            throw new Exception("Pasted clones were not selected.");
        foreach (var clone in clones)
        {
            var src = Math.Abs(clone.X - 40 - a.X) < 0.001 ? a : b;
            if (Math.Abs(clone.X - (src.X + 40)) > 0.001 || Math.Abs(clone.Y - (src.Y + 40)) > 0.001)
                throw new Exception($"Paste offset wrong for clone #{clone.Id} ({clone.X},{clone.Y}).");
        }
        UndoRedoService.Instance.Undo();
        if (mainVm.Nodes.Count != nodesBefore || mainVm.Connections.Count != connsBefore)
            throw new Exception("Undo of paste did not remove exactly the clones.");

        // Ctrl+D on a single node duplicates exactly one node, one undo step.
        mainVm.SelectNodeQuiet(a);
        mainVm.DuplicateSelectedNodesCommand.Execute(null);
        if (mainVm.Nodes.Count != nodesBefore + 1)
            throw new Exception("Ctrl+D did not duplicate exactly one node.");
        UndoRedoService.Instance.Undo();
        if (mainVm.Nodes.Count != nodesBefore)
            throw new Exception("Undo of Ctrl+D did not remove the duplicate.");

        // Arrow-key nudge: one atomic undo step for the whole selection.
        mainVm.SelectNodeQuiet(a);
        mainVm.SelectNodeQuiet(b, addToSelection: true);
        double ax0 = a.X, ay0 = a.Y, bx0 = b.X, by0 = b.Y;
        mainVm.NudgeSelectedNodes(5, -3);
        if (a.X != ax0 + 5 || a.Y != ay0 - 3 || b.X != bx0 + 5 || b.Y != by0 - 3)
            throw new Exception("Nudge did not shift the selection by (5,-3).");
        UndoRedoService.Instance.Undo();
        if (a.X != ax0 || a.Y != ay0 || b.X != bx0 || b.Y != by0)
            throw new Exception("Single undo did not revert the whole nudge.");
    }

    // Step 31.4: diskten silinen dosya → kayıp rozeti; geri gelince kaybolur.
    private static void MissingBadgeRoundTrips(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("    [Step 31.4]: Missing-asset badge round-trip...");
        string assetsDir = Path.Combine(testProjectRoot, "Assets");
        string probe = Path.Combine(assetsDir, "ms5_probe.txt");
        var browser = mainVm.AssetBrowserViewModel;

        File.WriteAllText(probe, "ms5");
        browser.RefreshAssets();
        DrainWatcherQueue();
        if (!browser.AssetNames.Contains("ms5_probe.txt"))
            throw new Exception("Probe file was not listed after creation.");

        File.Delete(probe);
        browser.RefreshAssets();
        DrainWatcherQueue();
        if (browser.MissingAssetCount != 1 || !browser.MissingAssetPaths.Contains(probe))
            // Tur-7: count alone never names the 3 extra ghosts (Windows CI
            // saw count=4). Dump the full missing set + asset census so the
            // next red run identifies the stale entries directly.
            throw new Exception($"Delete did not raise a missing badge (count={browser.MissingAssetCount}, want probe='{probe}'; missing=[{string.Join(";", browser.MissingAssetPaths)}]; assets={browser.AssetNames.Count}).");
        var ghostGroup = browser.AssetTree.FirstOrDefault(n => n.RelativePath == "__missing__");
        var ghost = ghostGroup?.Children.FirstOrDefault(c => c.FullPath == probe);
        if (ghost == null || !ghost.IsMissing)
            throw new Exception("Missing ghost node with badge not found in the tree.");

        File.WriteAllText(probe, "ms5");
        browser.RefreshAssets();
        DrainWatcherQueue();
        if (browser.MissingAssetCount != 0)
            throw new Exception("Restored file did not clear the missing badge.");
    }

    // Step 31.5: FileSystemWatcher shell-değişikliğini 2 sn hedefiyle yansıtır.
    private static void WatcherAutoRefreshes(MainWindowViewModel mainVm, string testProjectRoot)
    {
        Console.WriteLine("    [Step 31.5]: FileSystemWatcher auto-refresh (debounced)...");
        var browser = mainVm.AssetBrowserViewModel;
        if (!browser.IsWatching)
            throw new Exception("Asset watcher is not active.");

        string assetsDir = Path.Combine(testProjectRoot, "Assets");
        string watch = Path.Combine(assetsDir, "ms5_watch.txt");
        try
        {
            File.WriteAllText(watch, "watch");
            if (!WaitFor(() => browser.AssetNames.Contains("ms5_watch.txt"), TimeSpan.FromSeconds(10)))
                throw new Exception("Watcher did not pick up the created file within 10 s.");

            File.Delete(watch);
            if (!WaitFor(() => browser.MissingAssetPaths.Contains(watch), TimeSpan.FromSeconds(10)))
                throw new Exception("Watcher did not raise the missing badge within 10 s.");
        }
        finally
        {
            // Leave an honest end state: watch file present, probe deleted and
            // truthfully badged as missing (temp root is discarded anyway).
            File.WriteAllText(watch, "watch");
            File.Delete(Path.Combine(assetsDir, "ms5_probe.txt"));
            browser.RefreshAssets();
            DrainWatcherQueue();
        }
    }

    private static void DrainWatcherQueue()
    {
        try { Dispatcher.UIThread.RunJobs(); } catch { }
    }

    private static bool WaitFor(Func<bool> condition, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            DrainWatcherQueue();
            try { if (condition()) return true; }
            catch { }
            Thread.Sleep(200);
        }
        DrainWatcherQueue();
        try { return condition(); }
        catch { return false; }
    }
}
