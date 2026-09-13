using System;
using System.Collections.ObjectModel;
using System.Linq;
using RowlEngine.Editor.Services;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor;

// MS-1 undo integrity: every fact runs headless (no Avalonia platform) and
// owns the shared UndoRedoService stack via Clear() in try/finally.
public sealed class EditorUndoIntegrityFacts
{
    private static void WithCleanUndo(Action body)
    {
        UndoRedoService.Instance.Clear();
        try
        {
            body();
        }
        finally
        {
            UndoRedoService.Instance.Clear();
        }
    }

    private static NodeViewModel BareNode(ulong id, double x, double y)
        => new(id, $"Node {id}", x, y, bare: true);

    [Fact]
    public void MoveNodesAction_RoundTripsPositions()
    {
        WithCleanUndo(() =>
        {
            var a = BareNode(1, 10, 10);
            var b = BareNode(2, 50, 60);
            var before = new[] { new NodePosition(a, 10, 10), new NodePosition(b, 50, 60) };
            a.X = 111; a.Y = 112; b.X = 113; b.Y = 114;
            var after = new[] { new NodePosition(a, 111, 112), new NodePosition(b, 113, 114) };

            UndoRedoService.Instance.RecordAction(new MoveNodesAction(before, after));
            UndoRedoService.Instance.Undo();
            Assert.Equal(10, a.X); Assert.Equal(10, a.Y);
            Assert.Equal(50, b.X); Assert.Equal(60, b.Y);
            UndoRedoService.Instance.Redo();
            Assert.Equal(111, a.X); Assert.Equal(112, a.Y);
            Assert.Equal(113, b.X); Assert.Equal(114, b.Y);
        });
    }

    [Fact]
    public void ConnectWireAction_RoundTripsPlainWire()
    {
        WithCleanUndo(() =>
        {
            var nodes = new ObservableCollection<NodeViewModel>
            {
                BareNode(1, 100, 100),
                BareNode(2, 500, 100),
            };
            var conns = new ObservableCollection<ConnectionViewModel>();

            var added = StoryGraphCanvasService.TryConnectWire(
                nodes[0], new Avalonia.Point(510, 160), nodes, conns);
            Assert.NotNull(added);
            UndoRedoService.Instance.RecordAction(new ConnectWireAction(
                conns, added!, Array.Empty<ConnectionViewModel>(),
                Array.Empty<ChoiceTargetChange>()));

            UndoRedoService.Instance.Undo();
            Assert.Empty(conns);
            UndoRedoService.Instance.Redo();
            Assert.Single(conns);
            Assert.Same(added, conns[0]);
        });
    }

    [Fact]
    public void ConnectWireAction_RestoresReplacedChoiceTarget()
    {
        WithCleanUndo(() =>
        {
            var src = BareNode(1, 100, 100);
            var n2 = BareNode(2, 500, 100);
            var n3 = BareNode(3, 500, 400);
            var nodes = new ObservableCollection<NodeViewModel> { src, n2, n3 };
            var conns = new ObservableCollection<ConnectionViewModel>();

            var holder = src.CreateObject("Choices");
            var choice = new ChoiceComponentViewModel();
            holder.AddComponent(choice);
            string optionId = choice.Options[0].OptionId;

            var first = StoryGraphCanvasService.TryConnectWire(
                src, new Avalonia.Point(510, 160), nodes, conns, optionId);
            Assert.NotNull(first);
            Assert.Equal(n2.Id, StoryGraphCanvasService.GetChoiceTarget(src, optionId));

            ulong beforeTarget = StoryGraphCanvasService.GetChoiceTarget(src, optionId);
            var second = StoryGraphCanvasService.TryConnectWire(
                src, new Avalonia.Point(510, 460), nodes, conns, optionId);
            Assert.NotNull(second);
            Assert.DoesNotContain(first!, conns);
            ulong afterTarget = StoryGraphCanvasService.GetChoiceTarget(src, optionId);
            Assert.Equal(n3.Id, afterTarget);

            UndoRedoService.Instance.RecordAction(new ConnectWireAction(
                conns, second!, new[] { first! },
                new[] { new ChoiceTargetChange(src, optionId, beforeTarget, afterTarget) }));

            UndoRedoService.Instance.Undo();
            Assert.Contains(first!, conns);
            Assert.DoesNotContain(second!, conns);
            Assert.Equal(n2.Id, StoryGraphCanvasService.GetChoiceTarget(src, optionId));

            UndoRedoService.Instance.Redo();
            Assert.Contains(second!, conns);
            Assert.DoesNotContain(first!, conns);
            Assert.Equal(n3.Id, StoryGraphCanvasService.GetChoiceTarget(src, optionId));
        });
    }

    [Fact]
    public void DisconnectCablesUndoAction_RestoresChoiceTarget()
    {
        WithCleanUndo(() =>
        {
            var src = BareNode(1, 100, 100);
            var dst = BareNode(2, 500, 100);
            var nodes = new ObservableCollection<NodeViewModel> { src, dst };
            var conns = new ObservableCollection<ConnectionViewModel>();

            var holder = src.CreateObject("Choices");
            var choice = new ChoiceComponentViewModel();
            holder.AddComponent(choice);
            string optionId = choice.Options[0].OptionId;

            var wire = StoryGraphCanvasService.TryConnectWire(
                src, new Avalonia.Point(510, 160), nodes, conns, optionId);
            Assert.NotNull(wire);
            var removed = conns.ToList();
            Assert.Equal(1, StoryGraphCanvasService.DisconnectNodeOutputs(src, conns, optionId));
            Assert.Empty(conns);
            Assert.Equal(0UL, StoryGraphCanvasService.GetChoiceTarget(src, optionId));

            UndoRedoService.Instance.RecordAction(new DisconnectCablesUndoAction(conns, removed));
            UndoRedoService.Instance.Undo();
            Assert.Single(conns);
            Assert.Equal(dst.Id, StoryGraphCanvasService.GetChoiceTarget(src, optionId));
            UndoRedoService.Instance.Redo();
            Assert.Empty(conns);
        });
    }

    [Fact]
    public void BatchDeleteNodesUndoAction_RestoresChoiceTarget()
    {
        WithCleanUndo(() =>
        {
            var src = BareNode(1, 100, 100);
            var dst = BareNode(2, 500, 100);
            var nodes = new ObservableCollection<NodeViewModel> { src, dst };
            var conns = new ObservableCollection<ConnectionViewModel>();

            var holder = src.CreateObject("Choices");
            var choice = new ChoiceComponentViewModel();
            holder.AddComponent(choice);
            string optionId = choice.Options[0].OptionId;
            var wire = StoryGraphCanvasService.TryConnectWire(
                src, new Avalonia.Point(510, 160), nodes, conns, optionId);
            Assert.NotNull(wire);

            Assert.Equal(1, EditorBatchOperationService.BatchDeleteNodes(
                new[] { dst }, nodes, conns));
            Assert.Empty(conns);

            UndoRedoService.Instance.Undo();
            Assert.Contains(dst, nodes);
            Assert.Single(conns);
            Assert.Equal(dst.Id, StoryGraphCanvasService.GetChoiceTarget(src, optionId));

            UndoRedoService.Instance.Redo();
            Assert.DoesNotContain(dst, nodes);
        });
    }

    [Fact]
    public void ObjectActions_RoundTripDeleteDuplicateToggle()
    {
        WithCleanUndo(() =>
        {
            var node = BareNode(1, 0, 0);
            var o1 = node.CreateObject("A");
            var o2 = node.CreateObject("B");
            Assert.Equal(2, node.Objects.Count);

            UndoRedoService.Instance.RecordAction(new ObjectDeleteAction(
                node, new[] { (o1, node.Objects.IndexOf(o1)) }));
            node.RemoveObject(o1);
            Assert.Single(node.Objects);
            UndoRedoService.Instance.Undo();
            Assert.Equal(2, node.Objects.Count);
            Assert.Equal(0, node.Objects.IndexOf(o1));
            UndoRedoService.Instance.Redo();
            Assert.Single(node.Objects);

            var copy = node.DuplicateObject(o2);
            UndoRedoService.Instance.RecordAction(new ObjectDuplicateAction(
                node, new[] { (copy, node.Objects.IndexOf(copy)) }));
            Assert.Equal(2, node.Objects.Count);
            UndoRedoService.Instance.Undo();
            Assert.Single(node.Objects);
            UndoRedoService.Instance.Redo();
            Assert.Equal(2, node.Objects.Count);

            bool before = o2.IsActive;
            o2.IsActive = !before;
            UndoRedoService.Instance.RecordAction(new ObjectToggleAction(new[] { o2 }));
            UndoRedoService.Instance.Undo();
            Assert.Equal(before, o2.IsActive);
            UndoRedoService.Instance.Redo();
            Assert.Equal(!before, o2.IsActive);
        });
    }

    [Fact]
    public void ComponentActions_RoundTripAddRemove()
    {
        WithCleanUndo(() =>
        {
            var node = BareNode(1, 0, 0);
            var holder = node.CreateObject("Holder");
            var comp = new ChoiceComponentViewModel();
            holder.AddComponent(comp);
            int index = holder.Components.IndexOf(comp);
            UndoRedoService.Instance.RecordAction(new ComponentAddAction(holder, comp, index));

            UndoRedoService.Instance.Undo();
            Assert.DoesNotContain(comp, holder.Components);
            UndoRedoService.Instance.Redo();
            Assert.Contains(comp, holder.Components);
            Assert.Equal(index, holder.Components.IndexOf(comp));

            int removeIndex = holder.Components.IndexOf(comp);
            holder.RemoveComponent(comp);
            UndoRedoService.Instance.RecordAction(new ComponentRemoveAction(holder, comp, removeIndex));
            UndoRedoService.Instance.Undo();
            Assert.Contains(comp, holder.Components);
            Assert.Equal(removeIndex, holder.Components.IndexOf(comp));
            UndoRedoService.Instance.Redo();
            Assert.DoesNotContain(comp, holder.Components);
        });
    }
}
