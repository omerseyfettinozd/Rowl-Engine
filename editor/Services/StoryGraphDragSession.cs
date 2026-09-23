using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// D17: wire-drag + node-drag gesture state extracted from
    /// MainWindowViewModel (:752-928). The ViewModel keeps thin delegates
    /// plus the XAML-bound IsDraggingWire / WireStartPoint / WireEndPoint
    /// properties (NodeGraphView.axaml:90); all gesture state and the
    /// commit/cancel/undo choreography live here. Heavy canvas work was
    /// already delegated to <see cref="StoryGraphCanvasService"/>; this
    /// session owns only the in-flight gesture bookkeeping.
    /// Callbacks (log / schedule-save / start-node refresh) are parameters
    /// so the session stays decoupled from the ViewModel, mirroring the
    /// StoryGraphCanvasService convention.
    /// </summary>
    public sealed class StoryGraphDragSession
    {
        private NodeViewModel? _sourceNode;
        private ConnectionViewModel? _removedConn;
        private string _optionId = string.Empty;
        private Dictionary<NodeViewModel, (double X, double Y)>? _nodeSnapshot;

        /// <summary>True while a wire gesture is in flight.</summary>
        public bool IsWireDragActive => _sourceNode != null || _removedConn != null;

        /// <summary>True while a node-drag snapshot is pending commit or cancel.</summary>
        public bool HasPendingNodeDrag => _nodeSnapshot != null && _nodeSnapshot.Count > 0;

        /// <summary>
        /// Arms a wire gesture from a green output pin. Returns the wire
        /// start point so the owner can publish it to its bound property.
        /// Pass the previously removed cable for the unplug re-route path.
        /// </summary>
        public Point StartWireDrag(NodeViewModel sourceNode, string optionId = "", ConnectionViewModel? removedConn = null)
        {
            _sourceNode = sourceNode;
            _optionId = optionId;
            _removedConn = removedConn;
            return new Point(sourceNode.X + 265, sourceNode.Y + sourceNode.GetOutputPortY(optionId));
        }

        /// <summary>
        /// Commits a wire gesture: snap-connects (recording one undoable
        /// ConnectWireAction), records an unplug-disconnect when dropped in
        /// empty space, and clears the gesture state. No-op when no gesture
        /// is armed.
        /// </summary>
        public void EndWireDrag(
            Point releasePos,
            ObservableCollection<NodeViewModel> nodes,
            ObservableCollection<ConnectionViewModel> connections,
            Action? updateStartNodeState,
            Action<string> log,
            Action scheduleSave)
        {
            if (_sourceNode == null) return;

            var sourceNode = _sourceNode;
            var optionId = _optionId;
            var unplugged = _removedConn;

            var replacedBefore = connections
                .Where(c => c.SourceNode == sourceNode &&
                    (string.IsNullOrEmpty(optionId) || c.OptionId == optionId))
                .ToList();
            ulong targetBefore = StoryGraphCanvasService.GetChoiceTarget(sourceNode, optionId);

            var newConn = StoryGraphCanvasService.TryConnectWire(
                sourceNode,
                releasePos,
                nodes,
                connections,
                optionId);

            if (newConn != null)
            {
                var replaced = replacedBefore.Where(c => !connections.Contains(c)).ToList();
                if (unplugged != null && !replaced.Contains(unplugged))
                    replaced.Insert(0, unplugged);
                var changes = new List<ChoiceTargetChange>();
                if (!string.IsNullOrEmpty(optionId))
                {
                    changes.Add(new ChoiceTargetChange(
                        sourceNode, optionId, targetBefore,
                        newConn.TargetNode?.Id ?? 0));
                }
                UndoRedoService.Instance.RecordAction(new ConnectWireAction(
                    connections, newConn, replaced, changes, updateStartNodeState));
                log($"Connected Wire: Node #{sourceNode.Id} ---> Node #{newConn.TargetNode?.Id} (Total cables: {connections.Count})");
            }
            else if (unplugged != null)
            {
                UndoRedoService.Instance.RecordAction(
                    new DisconnectCablesUndoAction(connections, new List<ConnectionViewModel> { unplugged }, updateStartNodeState));
                log("Connection dropped in empty space (cable unplugged / removed).");
            }
            else
            {
                log("Connection dropped in empty space (cable unplugged / removed).");
            }

            updateStartNodeState?.Invoke();
            scheduleSave();
            _sourceNode = null;
            _optionId = string.Empty;
            _removedConn = null;
        }

        /// <summary>
        /// Cancels an in-flight wire gesture (Escape / capture loss / focus loss).
        /// A previously unplugged cable is restored silently: no undo record is
        /// produced because the gesture never committed.
        /// Safe to call when no wire drag is active (no-op).
        /// </summary>
        public void CancelWireDrag(
            ObservableCollection<ConnectionViewModel> connections,
            Action? updateStartNodeState,
            Action<string> log)
        {
            if (_sourceNode == null && _removedConn == null) return;

            var unplugged = _removedConn;
            _sourceNode = null;
            _optionId = string.Empty;
            _removedConn = null;

            if (unplugged != null && !connections.Contains(unplugged))
            {
                connections.Add(unplugged);
                UndoChoiceTarget.RestoreFor(new[] { unplugged });
            }

            updateStartNodeState?.Invoke();
            log("Geri Al: Kablo çekme iptal edildi (değişiklik yok).");
        }

        /// <summary>
        /// Snapshots drag-affected node positions. Call on pointer-press before any move.
        /// </summary>
        public void BeginNodeDragSnapshot(IEnumerable<NodeViewModel> affected)
        {
            _nodeSnapshot = affected.ToDictionary(n => n, n => (n.X, n.Y));
        }

        /// <summary>
        /// Cancels an in-flight node drag (Escape / capture loss / focus loss).
        /// Positions revert to <see cref="BeginNodeDragSnapshot"/> state and no
        /// undo record is produced because the gesture never committed.
        /// Safe to call when no drag snapshot exists (no-op).
        /// </summary>
        public void CancelNodeDrag(ICollection<NodeViewModel> nodes, Action<string> log)
        {
            var snapshot = _nodeSnapshot;
            _nodeSnapshot = null;
            if (snapshot == null || snapshot.Count == 0) return;

            foreach (var (node, pos) in snapshot)
            {
                if (!nodes.Contains(node)) continue;
                node.X = pos.X;
                node.Y = pos.Y;
            }
            log("Geri Al: Sürükleme iptal edildi, düğümler başlangıç konumuna döndü.");
        }

        /// <summary>
        /// Records one atomic MoveNodesAction when the gesture actually moved nodes.
        /// </summary>
        public void EndNodeDragSnapshot(ICollection<NodeViewModel> nodes, Action scheduleSave)
        {
            var snapshot = _nodeSnapshot;
            _nodeSnapshot = null;
            if (snapshot == null || snapshot.Count == 0) return;

            var before = new List<NodePosition>();
            var after = new List<NodePosition>();
            foreach (var (node, pos) in snapshot)
            {
                if (!nodes.Contains(node)) continue;
                if (node.X != pos.X || node.Y != pos.Y)
                {
                    before.Add(new NodePosition(node, pos.X, pos.Y));
                    after.Add(new NodePosition(node, node.X, node.Y));
                }
            }
            if (before.Count > 0)
            {
                UndoRedoService.Instance.RecordAction(new MoveNodesAction(before, after));
                scheduleSave();
            }
        }
    }
}
