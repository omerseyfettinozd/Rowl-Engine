using System;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

internal static class EditorSelectionBatchTests
{
    public static void Run(MainWindowViewModel mainVm)
    {
        Console.WriteLine("\n📌 [Test 20]: EditorSelectionCoordinator & EditorBatchOperationService Multi-Selection & Batch Ops...");

        // Step 20.1: EditorSelectionCoordinator Multi-Selection & Toggle
        Console.WriteLine("    [Step 20.1]: SelectionCoordinator Single, Multi, Invert & Clear...");
        var testNodes = new ObservableCollection<NodeViewModel>
        {
            new NodeViewModel(1001, "Node A", 100, 100, bare: true),
            new NodeViewModel(1002, "Node B", 400, 100, bare: true),
            new NodeViewModel(1003, "Node C", 1000, 1000, bare: true)
        };
        var selNodes = new ObservableCollection<NodeViewModel>();
        NodeViewModel? primary = null;

        EditorSelectionCoordinator.SelectNode(testNodes[0], false, testNodes, selNodes, p => primary = p);
        if (selNodes.Count != 1 || primary != testNodes[0] || !testNodes[0].IsSelected)
            throw new Exception("Single selection failed.");

        EditorSelectionCoordinator.SelectNode(testNodes[1], true, testNodes, selNodes, p => primary = p);
        if (selNodes.Count != 2 || !testNodes[0].IsSelected || !testNodes[1].IsSelected)
            throw new Exception("Multi-selection append failed.");

        // Toggle off node B
        EditorSelectionCoordinator.SelectNode(testNodes[1], true, testNodes, selNodes, p => primary = p);
        if (selNodes.Count != 1 || testNodes[1].IsSelected || !testNodes[0].IsSelected)
            throw new Exception("Multi-selection toggle off failed.");

        EditorSelectionCoordinator.SelectAllNodes(testNodes, selNodes, p => primary = p);
        if (selNodes.Count != 3 || !testNodes.All(n => n.IsSelected))
            throw new Exception("SelectAllNodes failed.");

        EditorSelectionCoordinator.InvertNodeSelection(testNodes, selNodes, p => primary = p);
        if (selNodes.Count != 0 || testNodes.Any(n => n.IsSelected))
            throw new Exception("InvertNodeSelection on all selected failed.");

        // Step 20.2: Marquee Box Selection Hit-Testing
        Console.WriteLine("    [Step 20.2]: Box Selection Intersects Hit-Testing...");
        int hitCount = EditorSelectionCoordinator.SelectNodesInBox(
            new Avalonia.Rect(50, 50, 450, 250), testNodes, selNodes, p => primary = p);
        if (hitCount != 2 || selNodes.Count != 2 || !selNodes.Contains(testNodes[0]) || !selNodes.Contains(testNodes[1]))
            throw new Exception($"Box selection expected 2 hits, got {hitCount}");

        // Step 20.3: Batch Duplicate with Internal Wire Preservation & Undo/Redo
        Console.WriteLine("    [Step 20.3]: Batch Duplicate with Internal Wire Topology...");
        var testConnections = new ObservableCollection<ConnectionViewModel>();
        var wire = new ConnectionViewModel(testNodes[0], testNodes[1]);
        testConnections.Add(wire);

        var cloned = EditorBatchOperationService.BatchDuplicateNodes(
            new[] { testNodes[0], testNodes[1] }, testNodes, testConnections, 50.0, 50.0);

        if (cloned.Count != 2 || testNodes.Count != 5)
            throw new Exception($"Batch duplicate expected 2 new nodes, got {cloned.Count}");

        // Check that cloned wire exists between the cloned nodes
        var clonedWire = testConnections.FirstOrDefault(c => c.SourceNode == cloned[0] && c.TargetNode == cloned[1]);
        if (clonedWire == null)
            throw new Exception("Batch duplicate failed to preserve internal cable between duplicated nodes.");

        // Verify Undo/Redo
        UndoRedoService.Instance.Undo();
        if (testNodes.Count != 3 || testConnections.Contains(clonedWire))
            throw new Exception("Undo on BatchDuplicate failed to cleanly remove clones and wires.");

        UndoRedoService.Instance.Redo();
        if (testNodes.Count != 5 || !testConnections.Any(c => c.SourceNode == cloned[0] && c.TargetNode == cloned[1]))
            throw new Exception("Redo on BatchDuplicate failed to re-add clones and wires.");

        // Step 20.4: Batch Delete with Single-Step Undo
        Console.WriteLine("    [Step 20.4]: Batch Delete with Single-Step Undo/Redo...");
        int deleted = EditorBatchOperationService.BatchDeleteNodes(cloned, testNodes, testConnections);
        if (deleted != 2 || testNodes.Count != 3)
            throw new Exception("BatchDeleteNodes failed to remove target nodes.");

        UndoRedoService.Instance.Undo();
        if (testNodes.Count != 5)
            throw new Exception("Undo on BatchDeleteNodes failed to restore deleted nodes.");

        UndoRedoService.Instance.Redo();
        if (testNodes.Count != 3)
            throw new Exception("Redo on BatchDeleteNodes failed to re-delete nodes.");

        // Step 20.5: Batch Align & Distribute
        Console.WriteLine("    [Step 20.5]: Batch Align & Distribute Calculations...");
        testNodes[0].X = 100; testNodes[0].Y = 50;
        testNodes[1].X = 300; testNodes[1].Y = 200;
        testNodes[2].X = 500; testNodes[2].Y = 350;

        EditorBatchOperationService.BatchAlignNodes(testNodes, BatchAlignment.Left);
        if (testNodes[0].X != 100 || testNodes[1].X != 100 || testNodes[2].X != 100)
            throw new Exception("BatchAlign Left failed.");

        testNodes[0].X = 0; testNodes[1].X = 1000; testNodes[2].X = 200;
        EditorBatchOperationService.BatchDistributeNodes(testNodes, BatchDistribution.Horizontal);
        var sorted = testNodes.OrderBy(n => n.X).Select(n => n.X).ToList();
        if (sorted[0] != 0 || sorted[1] != 500 || sorted[2] != 1000)
            throw new Exception($"BatchDistribute Horizontal mismatch: {string.Join(",", sorted)}");

        // Step 20.6: Hierarchy Batch Operations
        Console.WriteLine("    [Step 20.6]: Hierarchy Batch Object Ops...");
        var hierarchyNode = new NodeViewModel(2001, "Hierarchy Test", 0, 0, bare: true);
        var obj1 = hierarchyNode.CreateObject("Obj1");
        var obj2 = hierarchyNode.CreateObject("Obj2");
        obj1.IsActive = true;
        obj2.IsActive = true;

        EditorBatchOperationService.BatchToggleActiveObjects(new[] { obj1, obj2 });
        if (obj1.IsActive || obj2.IsActive)
            throw new Exception("BatchToggleActiveObjects failed to toggle off.");

        var dupes = EditorBatchOperationService.BatchDuplicateObjects(new[] { obj1, obj2 }, hierarchyNode);
        if (dupes.Count != 2 || hierarchyNode.Objects.Count != 4)
            throw new Exception("BatchDuplicateObjects failed.");

        EditorBatchOperationService.BatchDeleteObjects(dupes, hierarchyNode);
        if (hierarchyNode.Objects.Count != 2)
            throw new Exception("BatchDeleteObjects failed.");

        Console.WriteLine("  ✅ [PASS] EditorSelectionCoordinator & EditorBatchOperationService verified");
    }
}
