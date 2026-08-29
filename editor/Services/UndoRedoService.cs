using System;
using System.Collections.Generic;
using System.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using RowlEngine.Editor.ViewModels;

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
        private readonly MainWindowViewModel _vm;
        private readonly List<ConnectionViewModel> _connections;

        public string Description => "Disconnect Cables";

        public DisconnectCablesUndoAction(MainWindowViewModel vm, List<ConnectionViewModel> connections)
        {
            _vm = vm;
            _connections = new List<ConnectionViewModel>(connections);
        }

        public void Undo()
        {
            foreach (var conn in _connections)
            {
                if (!_vm.Connections.Contains(conn))
                    _vm.Connections.Add(conn);
            }
            _vm.UpdateStartNodeState();
        }

        public void Redo()
        {
            foreach (var conn in _connections)
            {
                _vm.Connections.Remove(conn);
            }
            _vm.UpdateStartNodeState();
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
