using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services
{
    public interface IUndoAction
    {
        string Description { get; }
        void Undo();
        void Redo();
    }

    public class AddNodeUndoAction : IUndoAction
    {
        private readonly MainWindowViewModel _vm;
        private readonly NodeViewModel _node;

        public string Description => $"Add Node #{_node.Id}";

        public AddNodeUndoAction(MainWindowViewModel vm, NodeViewModel node)
        {
            _vm = vm;
            _node = node;
        }

        public void Undo()
        {
            _vm.Nodes.Remove(_node);
            _vm.SelectedNode = _vm.Nodes.FirstOrDefault();
            _vm.UpdateStartNodeState();
        }

        public void Redo()
        {
            if (!_vm.Nodes.Contains(_node))
                _vm.Nodes.Add(_node);
            _vm.SelectedNode = _node;
            _vm.UpdateStartNodeState();
        }
    }

    public class DeleteNodeUndoAction : IUndoAction
    {
        private readonly MainWindowViewModel _vm;
        private readonly NodeViewModel _node;
        private readonly List<ConnectionViewModel> _connections;

        public string Description => $"Delete Node #{_node.Id}";

        public DeleteNodeUndoAction(MainWindowViewModel vm, NodeViewModel node, List<ConnectionViewModel> connections)
        {
            _vm = vm;
            _node = node;
            _connections = new List<ConnectionViewModel>(connections);
        }

        public void Undo()
        {
            if (!_vm.Nodes.Contains(_node))
                _vm.Nodes.Add(_node);

            foreach (var conn in _connections)
            {
                if (!_vm.Connections.Contains(conn))
                    _vm.Connections.Add(conn);
            }
            UndoChoiceTarget.RestoreFor(_connections);

            _vm.SelectedNode = _node;
            _vm.UpdateStartNodeState();
        }

        public void Redo()
        {
            _vm.DeleteNode(_node);
        }
    }

    public class DisconnectCablesUndoAction : IUndoAction
    {
        private readonly System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel> _allConnections;
        private readonly List<ConnectionViewModel> _connections;
        private readonly Action? _updateStartNode;

        public string Description => "Disconnect Cables";

        public DisconnectCablesUndoAction(
            System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel> allConnections,
            IEnumerable<ConnectionViewModel> connections,
            Action? updateStartNode = null)
        {
            _allConnections = allConnections;
            _connections = new List<ConnectionViewModel>(connections);
            _updateStartNode = updateStartNode;
        }

        public void Undo()
        {
            foreach (var conn in _connections)
            {
                if (!_allConnections.Contains(conn))
                    _allConnections.Add(conn);
            }
            UndoChoiceTarget.RestoreFor(_connections);
            _updateStartNode?.Invoke();
        }

        public void Redo()
        {
            foreach (var conn in _connections)
            {
                _allConnections.Remove(conn);
            }
            _updateStartNode?.Invoke();
        }
    }

    /// <summary>
    /// Restores choice-option routing after connections are re-added.
    /// Disconnect/delete paths reset TargetNodeId to 0; undo must put it back.
    /// </summary>
    internal static class UndoChoiceTarget
    {
        public static void RestoreFor(IEnumerable<ConnectionViewModel> connections)
        {
            foreach (var conn in connections)
            {
                if (conn.SourceNode != null && conn.TargetNode != null &&
                    !string.IsNullOrEmpty(conn.OptionId))
                {
                    StoryGraphCanvasService.SetChoiceTarget(conn.SourceNode, conn.OptionId, conn.TargetNode.Id);
                }
            }
        }
    }

    public sealed class NodePosition
    {
        public NodeViewModel Node { get; }
        public double X { get; }
        public double Y { get; }

        public NodePosition(NodeViewModel node, double x, double y)
        {
            Node = node;
            X = x;
            Y = y;
        }
    }

    /// <summary>
    /// One atomic drag/align/distribute step. Recorded once per gesture, never per move event.
    /// </summary>
    public class MoveNodesAction : IUndoAction
    {
        private readonly List<NodePosition> _before;
        private readonly List<NodePosition> _after;

        public string Description => $"Move {_before.Count} Node(s)";

        public MoveNodesAction(IEnumerable<NodePosition> before, IEnumerable<NodePosition> after)
        {
            _before = new List<NodePosition>(before);
            _after = new List<NodePosition>(after);
        }

        public void Undo()
        {
            foreach (var pos in _before)
            {
                pos.Node.X = pos.X;
                pos.Node.Y = pos.Y;
            }
        }

        public void Redo()
        {
            foreach (var pos in _after)
            {
                pos.Node.X = pos.X;
                pos.Node.Y = pos.Y;
            }
        }
    }

    public sealed class ChoiceTargetChange
    {
        public NodeViewModel SourceNode { get; }
        public string OptionId { get; }
        public ulong BeforeTargetId { get; }
        public ulong AfterTargetId { get; }

        public ChoiceTargetChange(NodeViewModel sourceNode, string optionId, ulong beforeTargetId, ulong afterTargetId)
        {
            SourceNode = sourceNode;
            OptionId = optionId;
            BeforeTargetId = beforeTargetId;
            AfterTargetId = afterTargetId;
        }
    }

    /// <summary>
    /// A completed wire gesture: the added cable, same-option cables it replaced,
    /// and every choice-target routing change it caused.
    /// </summary>
    public class ConnectWireAction : IUndoAction
    {
        private readonly System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel> _allConnections;
        private readonly ConnectionViewModel _added;
        private readonly List<ConnectionViewModel> _replaced;
        private readonly List<ChoiceTargetChange> _targetChanges;
        private readonly Action? _updateStartNode;

        public string Description => $"Connect Wire #{_added.SourceNode?.Id} -> #{_added.TargetNode?.Id}";

        public ConnectWireAction(
            System.Collections.ObjectModel.ObservableCollection<ConnectionViewModel> allConnections,
            ConnectionViewModel added,
            IEnumerable<ConnectionViewModel> replaced,
            IEnumerable<ChoiceTargetChange> targetChanges,
            Action? updateStartNode = null)
        {
            _allConnections = allConnections;
            _added = added;
            _replaced = new List<ConnectionViewModel>(replaced);
            _targetChanges = new List<ChoiceTargetChange>(targetChanges);
            _updateStartNode = updateStartNode;
        }

        public void Undo()
        {
            _allConnections.Remove(_added);
            foreach (var conn in _replaced)
            {
                if (!_allConnections.Contains(conn))
                    _allConnections.Add(conn);
            }
            UndoChoiceTarget.RestoreFor(_replaced);
            foreach (var change in _targetChanges)
            {
                StoryGraphCanvasService.SetChoiceTarget(change.SourceNode, change.OptionId, change.BeforeTargetId);
            }
            _updateStartNode?.Invoke();
        }

        public void Redo()
        {
            foreach (var conn in _replaced)
            {
                _allConnections.Remove(conn);
            }
            if (!_allConnections.Contains(_added))
                _allConnections.Add(_added);
            foreach (var change in _targetChanges)
            {
                StoryGraphCanvasService.SetChoiceTarget(change.SourceNode, change.OptionId, change.AfterTargetId);
            }
            _updateStartNode?.Invoke();
        }
    }

    public class ObjectDeleteAction : IUndoAction
    {
        private readonly NodeViewModel _node;
        private readonly List<(FrameObjectViewModel Obj, int Index)> _removed;

        public string Description => $"Delete {_removed.Count} Object(s)";

        public ObjectDeleteAction(NodeViewModel node, IEnumerable<(FrameObjectViewModel Obj, int Index)> removed)
        {
            _node = node;
            _removed = new List<(FrameObjectViewModel, int)>(removed);
        }

        public void Undo()
        {
            foreach (var (obj, index) in _removed.OrderBy(r => r.Index))
            {
                if (!_node.Objects.Contains(obj))
                {
                    _node.AddObject(obj);
                    int current = _node.Objects.IndexOf(obj);
                    int target = Math.Min(index, _node.Objects.Count - 1);
                    if (current != target)
                        _node.Objects.Move(current, target);
                }
            }
        }

        public void Redo()
        {
            foreach (var (obj, _) in _removed)
            {
                _node.RemoveObject(obj);
            }
        }
    }

    public class ObjectDuplicateAction : IUndoAction
    {
        private readonly NodeViewModel _node;
        private readonly List<(FrameObjectViewModel Obj, int Index)> _copies;

        public string Description => $"Duplicate {_copies.Count} Object(s)";

        public ObjectDuplicateAction(NodeViewModel node, IEnumerable<(FrameObjectViewModel Obj, int Index)> copies)
        {
            _node = node;
            _copies = new List<(FrameObjectViewModel, int)>(copies);
        }

        public void Undo()
        {
            foreach (var (obj, _) in _copies)
            {
                _node.RemoveObject(obj);
            }
        }

        public void Redo()
        {
            foreach (var (obj, index) in _copies.OrderBy(r => r.Index))
            {
                if (!_node.Objects.Contains(obj))
                {
                    _node.AddObject(obj);
                    int current = _node.Objects.IndexOf(obj);
                    int target = Math.Min(index, _node.Objects.Count - 1);
                    if (current != target)
                        _node.Objects.Move(current, target);
                }
            }
        }
    }

    public class ObjectToggleAction : IUndoAction
    {
        private readonly List<FrameObjectViewModel> _objects;

        public string Description => $"Toggle {_objects.Count} Object(s)";

        public ObjectToggleAction(IEnumerable<FrameObjectViewModel> objects)
        {
            _objects = new List<FrameObjectViewModel>(objects);
        }

        public void Undo()
        {
            foreach (var obj in _objects)
            {
                obj.IsActive = !obj.IsActive;
            }
        }

        public void Redo()
        {
            foreach (var obj in _objects)
            {
                obj.IsActive = !obj.IsActive;
            }
        }
    }

    public class ComponentAddAction : IUndoAction
    {
        private readonly FrameObjectViewModel _owner;
        private readonly NodeComponentViewModel _component;
        private readonly int _index;

        public string Description => $"Add {_component.DisplayName}";

        public ComponentAddAction(FrameObjectViewModel owner, NodeComponentViewModel component, int index)
        {
            _owner = owner;
            _component = component;
            _index = index;
        }

        public void Undo()
        {
            _owner.RemoveComponent(_component);
        }

        public void Redo()
        {
            if (!_owner.Components.Contains(_component))
            {
                _owner.AddComponent(_component);
                int current = _owner.Components.IndexOf(_component);
                int target = Math.Min(_index, _owner.Components.Count - 1);
                if (current != target)
                    _owner.Components.Move(current, target);
            }
        }
    }

    public class ComponentRemoveAction : IUndoAction
    {
        private readonly FrameObjectViewModel _owner;
        private readonly NodeComponentViewModel _component;
        private readonly int _index;

        public string Description => $"Remove {_component.DisplayName}";

        public ComponentRemoveAction(FrameObjectViewModel owner, NodeComponentViewModel component, int index)
        {
            _owner = owner;
            _component = component;
            _index = index;
        }

        public void Undo()
        {
            if (!_owner.Components.Contains(_component))
            {
                _owner.AddComponent(_component);
                int current = _owner.Components.IndexOf(_component);
                int target = Math.Min(_index, _owner.Components.Count - 1);
                if (current != target)
                    _owner.Components.Move(current, target);
            }
        }

        public void Redo()
        {
            _owner.RemoveComponent(_component);
        }
    }

    public partial class UndoRedoService : ObservableObject
    {
        private static readonly Lazy<UndoRedoService> _instance = new(() => new UndoRedoService());
        public static UndoRedoService Instance => _instance.Value;

        private readonly Stack<IUndoAction> _undoStack = new();
        private readonly Stack<IUndoAction> _redoStack = new();
        private const int MaxHistory = 50;

        [ObservableProperty]
        private bool _canUndo;

        [ObservableProperty]
        private bool _canRedo;

        [ObservableProperty]
        private string _undoDescription = "";

        [ObservableProperty]
        private string _redoDescription = "";

        public bool IsExecuting { get; private set; } = false;

        public void RecordAction(IUndoAction action)
        {
            if (IsExecuting) return;

            _undoStack.Push(action);
            _redoStack.Clear();

            if (_undoStack.Count > MaxHistory)
            {
                var items = new List<IUndoAction>(_undoStack);
                _undoStack.Clear();
                for (int i = Math.Min(items.Count - 1, MaxHistory - 1); i >= 0; i--)
                {
                    _undoStack.Push(items[i]);
                }
            }

            UpdateState();
        }

        public void Undo()
        {
            if (_undoStack.Count == 0 || IsExecuting) return;

            IsExecuting = true;
            try
            {
                var action = _undoStack.Pop();
                action.Undo();
                _redoStack.Push(action);
            }
            finally
            {
                IsExecuting = false;
                UpdateState();
            }
        }

        public void Redo()
        {
            if (_redoStack.Count == 0 || IsExecuting) return;

            IsExecuting = true;
            try
            {
                var action = _redoStack.Pop();
                action.Redo();
                _undoStack.Push(action);
            }
            finally
            {
                IsExecuting = false;
                UpdateState();
            }
        }

        public void Clear()
        {
            _undoStack.Clear();
            _redoStack.Clear();
            UpdateState();
        }

        private void UpdateState()
        {
            CanUndo = _undoStack.Count > 0;
            CanRedo = _redoStack.Count > 0;
            UndoDescription = _undoStack.Count > 0 ? _undoStack.Peek().Description : "";
            RedoDescription = _redoStack.Count > 0 ? _redoStack.Peek().Description : "";
        }
    }
}
