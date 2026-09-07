using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using RowlEngine.Editor.Models;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Service responsible for canvas-level node operations:
    /// calculating spawn positions, disconnecting cables, and deleting nodes.
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
                    setChoiceTarget?.Invoke(conn.SourceNode, conn.OptionId, 0);
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
