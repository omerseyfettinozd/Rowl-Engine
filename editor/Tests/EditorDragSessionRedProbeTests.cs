using System;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor;

// D17 RED probu: çıkarılmış StoryGraphDragSession'a başvurur.
// Pre-fix öngörü: CS0246 (tip yok), exit 1. Post-fix: yeşil.
// Bilerek-boz vektörü: EndWireDrag'daki replaced/unplugged restore
// yolu bozulursa MS-5 (Test 31) kırmızıya döner.
internal static class EditorDragSessionRedProbeTests
{
    public static void Run()
    {
        Console.WriteLine("\n[Test 44]: D17 drag-session RED probu (StoryGraphDragSession)...");

        var nodes = new ObservableCollection<NodeViewModel>();
        var connections = new ObservableCollection<ConnectionViewModel>();
        var src = new NodeViewModel(4401, "D17 Probe Src", 100, 100, bare: true);
        var dst = new NodeViewModel(4402, "D17 Probe Dst", 600, 100, bare: true);
        nodes.Add(src);
        nodes.Add(dst);
        var session = new StoryGraphDragSession();

        // Step 44.1: unplug-cancel restores the cable, records no undo.
        Console.WriteLine("    [Step 44.1]: unplug cancel restores cable + no undo...");
        UndoBaseline(out bool canUndoBefore, out string descBefore);
        var wire = StoryGraphCanvasService.TryConnectWire(
            src, new Point(dst.X + 10, dst.Y + 60), nodes, connections);
        if (wire == null)
            throw new Exception("TryConnectWire failed to build the probe fixture.");
        connections.Remove(wire);
        Point start = session.StartWireDrag(src, string.Empty, wire);
        if (Math.Abs(start.X - (src.X + 265)) > 0.001)
            throw new Exception($"Wire start X wrong (got {start.X}, want {src.X + 265}).");
        if (!session.IsWireDragActive)
            throw new Exception("StartWireDrag did not arm the session.");
        session.CancelWireDrag(connections, null, _ => { });
        if (!connections.Contains(wire))
            throw new Exception("CancelWireDrag did not restore the unplugged cable.");
        if (session.IsWireDragActive)
            throw new Exception("CancelWireDrag left the session armed.");
        AssertNoNewUndo("CancelWireDrag", canUndoBefore, descBefore);
        connections.Remove(wire);

        // Step 44.2: node-drag cancel restores positions, records no undo.
        Console.WriteLine("    [Step 44.2]: node-drag cancel restores positions + no undo...");
        double x0 = src.X, y0 = src.Y;
        UndoBaseline(out canUndoBefore, out descBefore);
        session.BeginNodeDragSnapshot(new[] { src });
        if (!session.HasPendingNodeDrag)
            throw new Exception("BeginNodeDragSnapshot did not arm a pending drag.");
        src.X += 250;
        src.Y += 130;
        session.CancelNodeDrag(nodes, _ => { });
        if (src.X != x0 || src.Y != y0)
            throw new Exception($"CancelNodeDrag did not restore start pos (got {src.X},{src.Y}, want {x0},{y0}).");
        if (session.HasPendingNodeDrag)
            throw new Exception("CancelNodeDrag left a pending drag armed.");
        AssertNoNewUndo("CancelNodeDrag", canUndoBefore, descBefore);

        // Step 44.3: EndWireDrag connects + records one undo step; Undo reverts.
        Console.WriteLine("    [Step 44.3]: EndWireDrag connects with undo...");
        session.StartWireDrag(src);
        session.EndWireDrag(
            new Point(dst.X + 10, dst.Y + 60), nodes, connections,
            null, _ => { }, () => { });
        var added = connections.FirstOrDefault(c => c.SourceNode == src && c.TargetNode == dst);
        if (added == null)
            throw new Exception("EndWireDrag did not connect the wire.");
        if (session.IsWireDragActive)
            throw new Exception("EndWireDrag left the session armed.");
        if (!UndoRedoService.Instance.CanUndo)
            throw new Exception("EndWireDrag did not record an undo step.");
        UndoRedoService.Instance.Undo();
        if (connections.Contains(added))
            throw new Exception("Undo did not remove the connected wire.");

        Console.WriteLine("  [PASS] D17 drag-session RED probu verified");
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
}
