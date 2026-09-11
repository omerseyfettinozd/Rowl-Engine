using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services
{
    public enum BatchAlignment
    {
        Left,
        Right,
        Top,
        Bottom,
        CenterHorizontal,
        CenterVertical
    }

    public enum BatchDistribution
    {
        Horizontal,
        Vertical
    }

    public class BatchDeleteNodesUndoAction : IUndoAction
    {
        private readonly ObservableCollection<NodeViewModel> _allNodes;
        private readonly ObservableCollection<ConnectionViewModel> _allConnections;
        private readonly List<NodeViewModel> _deletedNodes;
        private readonly List<ConnectionViewModel> _deletedConnections;
        private readonly Action? _updateStartNode;

        public string Description => $"Delete {_deletedNodes.Count} Nodes";

        public BatchDeleteNodesUndoAction(
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<ConnectionViewModel> allConnections,
            List<NodeViewModel> deletedNodes,
            List<ConnectionViewModel> deletedConnections,
            Action? updateStartNode = null)
        {
            _allNodes = allNodes;
            _allConnections = allConnections;
            _deletedNodes = new List<NodeViewModel>(deletedNodes);
            _deletedConnections = new List<ConnectionViewModel>(deletedConnections);
            _updateStartNode = updateStartNode;
        }

        public void Undo()
        {
            foreach (var node in _deletedNodes)
            {
                if (!_allNodes.Contains(node))
                    _allNodes.Add(node);
            }

            foreach (var conn in _deletedConnections)
            {
                if (!_allConnections.Contains(conn))
                    _allConnections.Add(conn);
            }

            _updateStartNode?.Invoke();
        }

        public void Redo()
        {
            foreach (var conn in _deletedConnections)
            {
                _allConnections.Remove(conn);
            }

            foreach (var node in _deletedNodes)
            {
                _allNodes.Remove(node);
            }

            _updateStartNode?.Invoke();
        }
    }

    public class BatchDuplicateNodesUndoAction : IUndoAction
    {
        private readonly ObservableCollection<NodeViewModel> _allNodes;
        private readonly ObservableCollection<ConnectionViewModel> _allConnections;
        private readonly List<NodeViewModel> _clonedNodes;
        private readonly List<ConnectionViewModel> _clonedConnections;
        private readonly Action? _updateStartNode;

        public string Description => $"Duplicate {_clonedNodes.Count} Nodes";

        public BatchDuplicateNodesUndoAction(
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<ConnectionViewModel> allConnections,
            List<NodeViewModel> clonedNodes,
            List<ConnectionViewModel> clonedConnections,
            Action? updateStartNode = null)
        {
            _allNodes = allNodes;
            _allConnections = allConnections;
            _clonedNodes = new List<NodeViewModel>(clonedNodes);
            _clonedConnections = new List<ConnectionViewModel>(clonedConnections);
            _updateStartNode = updateStartNode;
        }

        public void Undo()
        {
            foreach (var conn in _clonedConnections)
            {
                _allConnections.Remove(conn);
            }

            foreach (var node in _clonedNodes)
            {
                _allNodes.Remove(node);
            }

            _updateStartNode?.Invoke();
        }

        public void Redo()
        {
            foreach (var node in _clonedNodes)
            {
                if (!_allNodes.Contains(node))
                    _allNodes.Add(node);
            }

            foreach (var conn in _clonedConnections)
            {
                if (!_allConnections.Contains(conn))
                    _allConnections.Add(conn);
            }

            _updateStartNode?.Invoke();
        }
    }

    /// <summary>
    /// Service responsible for batch node and entity operations:
    /// multi-selection deletion, duplication with internal wire topology preservation,
    /// relative translation, alignments, and distributions.
    /// </summary>
    public static class EditorBatchOperationService
    {
        public static int BatchDeleteNodes(
            IEnumerable<NodeViewModel> nodesToDelete,
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<ConnectionViewModel> connections,
            Action<NodeViewModel, string, ulong>? setChoiceTarget = null,
            Action? updateStartNode = null)
        {
            var targetList = nodesToDelete.ToList();
            if (targetList.Count == 0) return 0;

            var targetSet = new HashSet<NodeViewModel>(targetList);
            var attachedConnections = connections.Where(c => 
                (c.SourceNode != null && targetSet.Contains(c.SourceNode)) || 
                (c.TargetNode != null && targetSet.Contains(c.TargetNode))).ToList();

            foreach (var conn in attachedConnections)
            {
                connections.Remove(conn);
                if (conn.SourceNode != null && !string.IsNullOrEmpty(conn.OptionId))
                {
                    if (setChoiceTarget != null)
                        setChoiceTarget(conn.SourceNode, conn.OptionId, 0);
                    else
                        StoryGraphCanvasService.SetChoiceTarget(conn.SourceNode, conn.OptionId, 0);
                }
            }

            foreach (var node in targetList)
            {
                allNodes.Remove(node);
            }

            updateStartNode?.Invoke();

            UndoRedoService.Instance.RecordAction(new BatchDeleteNodesUndoAction(
                allNodes, connections, targetList, attachedConnections, updateStartNode));

            return targetList.Count;
        }

        public static List<NodeViewModel> BatchDuplicateNodes(
            IEnumerable<NodeViewModel> sourceNodes,
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<ConnectionViewModel> connections,
            double offsetX = 40.0,
            double offsetY = 40.0,
            Action? updateStartNode = null)
        {
            var sources = sourceNodes.ToList();
            if (sources.Count == 0) return new List<NodeViewModel>();

            ulong maxExistingId = allNodes.Count > 0 ? allNodes.Max(n => n.Id) : 100;
            var clonedNodes = new List<NodeViewModel>();
            var clonedConnections = new List<ConnectionViewModel>();
            var oldToNewNodeMap = new Dictionary<NodeViewModel, NodeViewModel>();

            // 1. Clone nodes
            foreach (var src in sources)
            {
                maxExistingId++;
                var clone = new NodeViewModel(
                    maxExistingId,
                    $"{src.Title} (Copy)",
                    src.X + offsetX,
                    src.Y + offsetY,
                    bare: true);

                // Clone FrameObjects & Components
                foreach (var obj in src.Objects)
                {
                    var clonedObj = clone.CreateObject(obj.Name);
                    clonedObj.IsActive = obj.IsActive;
                    foreach (var comp in obj.Components)
                    {
                        var compCopy = ComponentRegistry.Create(comp.TypeKey);
                        compCopy.Deserialize(comp.Serialize().ToDictionary(
                            pair => pair.Key,
                            pair => (object?)pair.Value));
                        clonedObj.AddComponent(compCopy);
                    }
                }

                clonedNodes.Add(clone);
                oldToNewNodeMap[src] = clone;
                allNodes.Add(clone);
            }

            // 2. Clone internal wires between duplicated nodes
            var sourceSet = new HashSet<NodeViewModel>(sources);
            var internalWires = connections.Where(c =>
                c.SourceNode != null && sourceSet.Contains(c.SourceNode) &&
                c.TargetNode != null && sourceSet.Contains(c.TargetNode)).ToList();

            foreach (var wire in internalWires)
            {
                if (wire.SourceNode != null && wire.TargetNode != null &&
                    oldToNewNodeMap.TryGetValue(wire.SourceNode, out var newSource) &&
                    oldToNewNodeMap.TryGetValue(wire.TargetNode, out var newTarget))
                {
                    var newConn = new ConnectionViewModel(newSource, newTarget, wire.OptionId);
                    connections.Add(newConn);
                    clonedConnections.Add(newConn);

                    if (!string.IsNullOrEmpty(wire.OptionId))
                    {
                        StoryGraphCanvasService.SetChoiceTarget(newSource, wire.OptionId, newTarget.Id);
                    }
                }
            }

            updateStartNode?.Invoke();

            UndoRedoService.Instance.RecordAction(new BatchDuplicateNodesUndoAction(
                allNodes, connections, clonedNodes, clonedConnections, updateStartNode));

            return clonedNodes;
        }

        public static void BatchMoveNodes(IEnumerable<NodeViewModel> nodes, double deltaX, double deltaY)
        {
            foreach (var node in nodes)
            {
                node.X += deltaX;
                node.Y += deltaY;
            }
        }

        public static void BatchAlignNodes(IEnumerable<NodeViewModel> nodes, BatchAlignment alignment)
        {
            var list = nodes.ToList();
            if (list.Count < 2) return;

            switch (alignment)
            {
                case BatchAlignment.Left:
                    double minX = list.Min(n => n.X);
                    foreach (var n in list) n.X = minX;
                    break;
                case BatchAlignment.Right:
                    double maxX = list.Max(n => n.X);
                    foreach (var n in list) n.X = maxX;
                    break;
                case BatchAlignment.Top:
                    double minY = list.Min(n => n.Y);
                    foreach (var n in list) n.Y = minY;
                    break;
                case BatchAlignment.Bottom:
                    double maxY = list.Max(n => n.Y);
                    foreach (var n in list) n.Y = maxY;
                    break;
                case BatchAlignment.CenterHorizontal:
                    double avgX = list.Average(n => n.X);
                    foreach (var n in list) n.X = Math.Round(avgX);
                    break;
                case BatchAlignment.CenterVertical:
                    double avgY = list.Average(n => n.Y);
                    foreach (var n in list) n.Y = Math.Round(avgY);
                    break;
            }
        }

        public static void BatchDistributeNodes(IEnumerable<NodeViewModel> nodes, BatchDistribution distribution)
        {
            var list = nodes.ToList();
            if (list.Count < 3) return;

            if (distribution == BatchDistribution.Horizontal)
            {
                var sorted = list.OrderBy(n => n.X).ToList();
                double startX = sorted.First().X;
                double endX = sorted.Last().X;
                double step = (endX - startX) / (sorted.Count - 1);

                for (int i = 0; i < sorted.Count; i++)
                {
                    sorted[i].X = Math.Round(startX + (i * step));
                }
            }
            else if (distribution == BatchDistribution.Vertical)
            {
                var sorted = list.OrderBy(n => n.Y).ToList();
                double startY = sorted.First().Y;
                double endY = sorted.Last().Y;
                double step = (endY - startY) / (sorted.Count - 1);

                for (int i = 0; i < sorted.Count; i++)
                {
                    sorted[i].Y = Math.Round(startY + (i * step));
                }
            }
        }

        public static int BatchDeleteObjects(IEnumerable<FrameObjectViewModel> objects, NodeViewModel? node)
        {
            if (node == null) return 0;
            var list = objects.ToList();
            foreach (var obj in list)
            {
                node.RemoveObject(obj);
            }
            return list.Count;
        }

        public static int BatchToggleActiveObjects(IEnumerable<FrameObjectViewModel> objects)
        {
            int count = 0;
            foreach (var obj in objects)
            {
                obj.IsActive = !obj.IsActive;
                count++;
            }
            return count;
        }

        public static List<FrameObjectViewModel> BatchDuplicateObjects(
            IEnumerable<FrameObjectViewModel> objects,
            NodeViewModel? node)
        {
            if (node == null) return new List<FrameObjectViewModel>();
            var list = objects.ToList();
            var result = new List<FrameObjectViewModel>();

            foreach (var obj in list)
            {
                var copy = node.DuplicateObject(obj);
                result.Add(copy);
            }

            return result;
        }
    }
}
