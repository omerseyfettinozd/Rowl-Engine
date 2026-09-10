using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Service responsible for canvas-level node and wire operations:
    /// spawn coordinates, wire drag hit-testing, cable routing, and node deletion.
    /// </summary>
    public static class StoryGraphCanvasService
    {
        public static (double x, double y) CalculateSpawnPosition(int nodeCount, double panX, double panY, double zoomScale)
        {
            double zoom = zoomScale > 0 ? zoomScale : 1.0;
            double spawnX = (-panX + 300) / zoom + ((nodeCount % 5) * 40);
            double spawnY = (-panY + 180) / zoom + ((nodeCount % 5) * 30);
            return (spawnX, spawnY);
        }

        public static NodeViewModel CreateDefaultNode(ulong nextId, double spawnX, double spawnY)
        {
            return new NodeViewModel(nextId, $"Dialogue Node #{nextId}", spawnX, spawnY)
            {
                Speaker = "Narrator",
                DialogueText = $"New dialogue block #{nextId}. Drag green pin to connect!",
                BackgroundTexture = "bg_beach_sunset.png",
                CharacterSprite = "spr_evelyn.png"
            };
        }

        public static ConnectionViewModel? TryConnectWire(
            NodeViewModel sourceNode,
            Point releasePos,
            IEnumerable<NodeViewModel> nodes,
            ObservableCollection<ConnectionViewModel> connections,
            string optionId = "",
            double snapDistance = 75.0)
        {
            if (sourceNode == null) return null;

            NodeViewModel? targetNode = null;
            foreach (var node in nodes)
            {
                if (node == sourceNode) continue;
                Point leftPinPos = new Point(node.X + 10, node.Y + 60);
                double distance = Math.Sqrt(Math.Pow(releasePos.X - leftPinPos.X, 2) + Math.Pow(releasePos.Y - leftPinPos.Y, 2));
                if (distance < snapDistance)
                {
                    targetNode = node;
                    break;
                }
            }

            if (targetNode == null) return null;

            if (!string.IsNullOrEmpty(optionId))
            {
                var existingCables = connections.Where(c => c.SourceNode == sourceNode && c.OptionId == optionId).ToList();
                foreach (var existing in existingCables)
                {
                    connections.Remove(existing);
                }
                SetChoiceTarget(sourceNode, optionId, targetNode.Id);
            }

            var newConn = new ConnectionViewModel(sourceNode, targetNode, optionId);
            connections.Add(newConn);
            EnforceSingleOutgoingWireRule(connections);
            return newConn;
        }

        public static void SetChoiceTarget(NodeViewModel node, string optionId, ulong targetNodeId)
        {
            if (node == null || string.IsNullOrEmpty(optionId)) return;
            var option = node.GetComponents<ChoiceComponentViewModel>()
                .SelectMany(choice => choice.Options)
                .FirstOrDefault(candidate => candidate.OptionId == optionId);
            if (option != null) option.TargetNodeId = targetNodeId;
        }

        public static int DisconnectNodeInputs(
            NodeViewModel node,
            ObservableCollection<ConnectionViewModel> connections)
        {
            if (node == null) return 0;
            var toRemove = connections.Where(c => c.TargetNode == node).ToList();
            foreach (var conn in toRemove)
            {
                connections.Remove(conn);
                if (conn.SourceNode != null && !string.IsNullOrEmpty(conn.OptionId))
                    SetChoiceTarget(conn.SourceNode, conn.OptionId, 0);
            }
            return toRemove.Count;
        }

        public static int DisconnectNodeOutputs(
            NodeViewModel node,
            ObservableCollection<ConnectionViewModel> connections,
            string optionId = "")
        {
            if (node == null) return 0;
            var toRemove = connections.Where(c => c.SourceNode == node &&
                (string.IsNullOrEmpty(optionId) || c.OptionId == optionId)).ToList();
            foreach (var conn in toRemove)
            {
                connections.Remove(conn);
                if (conn.SourceNode != null && !string.IsNullOrEmpty(conn.OptionId))
                    SetChoiceTarget(conn.SourceNode, conn.OptionId, 0);
            }
            return toRemove.Count;
        }

        public static void EnforceSingleOutgoingWireRule(
            ObservableCollection<ConnectionViewModel> connections,
            Action? updateStartNodeState = null)
        {
            var duplicates = connections.GroupBy(c => (c.SourceNode, c.TargetNode, c.OptionId))
                .SelectMany(group => group.Skip(1)).ToList();
            foreach (var duplicate in duplicates) connections.Remove(duplicate);
            updateStartNodeState?.Invoke();
        }

        public static int DisconnectAllNodeCables(
            NodeViewModel node,
            ObservableCollection<ConnectionViewModel> connections,
            Action<NodeViewModel, string, ulong>? setChoiceTarget = null)
        {
            var toRemove = connections.Where(c => c.SourceNode == node || c.TargetNode == node).ToList();
            foreach (var conn in toRemove)
            {
                connections.Remove(conn);
                if (conn.SourceNode != null && !string.IsNullOrEmpty(conn.OptionId))
                {
                    if (setChoiceTarget != null)
                        setChoiceTarget(conn.SourceNode, conn.OptionId, 0);
                    else
                        SetChoiceTarget(conn.SourceNode, conn.OptionId, 0);
                }
            }
            return toRemove.Count;
        }

        public static void DeleteNode(
            NodeViewModel node,
            ObservableCollection<NodeViewModel> nodes,
            ObservableCollection<ConnectionViewModel> connections,
            Action<NodeViewModel, string, ulong>? setChoiceTarget = null)
        {
            DisconnectAllNodeCables(node, connections, setChoiceTarget);
            nodes.Remove(node);
        }
    }
}
