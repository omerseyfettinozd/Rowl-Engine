using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using Avalonia;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Coordinates single and multi-selection across node graph and hierarchy:
    /// selection sets, marquee/box intersection hit-testing, inversion, and primary sync.
    /// </summary>
    public static class EditorSelectionCoordinator
    {
        public const double DefaultNodeWidth = 260.0;
        public const double DefaultNodeHeight = 180.0;

        public static void SelectNode(
            NodeViewModel node,
            bool addToSelection,
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<NodeViewModel> selectedNodes,
            Action<NodeViewModel?> onPrimarySelectedChanged)
        {
            if (node == null) return;

            if (!addToSelection)
            {
                foreach (var n in allNodes)
                {
                    n.IsSelected = false;
                }
                selectedNodes.Clear();

                node.IsSelected = true;
                selectedNodes.Add(node);
                onPrimarySelectedChanged(node);
            }
            else
            {
                if (selectedNodes.Contains(node))
                {
                    // Toggle off
                    node.IsSelected = false;
                    selectedNodes.Remove(node);
                    onPrimarySelectedChanged(selectedNodes.LastOrDefault());
                }
                else
                {
                    // Add
                    node.IsSelected = true;
                    selectedNodes.Add(node);
                    onPrimarySelectedChanged(node);
                }
            }
        }

        public static void SelectAllNodes(
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<NodeViewModel> selectedNodes,
            Action<NodeViewModel?> onPrimarySelectedChanged)
        {
            selectedNodes.Clear();
            foreach (var n in allNodes)
            {
                n.IsSelected = true;
                selectedNodes.Add(n);
            }
            onPrimarySelectedChanged(selectedNodes.LastOrDefault());
        }

        public static void ClearNodeSelection(
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<NodeViewModel> selectedNodes,
            Action<NodeViewModel?> onPrimarySelectedChanged)
        {
            foreach (var n in allNodes)
            {
                n.IsSelected = false;
            }
            selectedNodes.Clear();
            onPrimarySelectedChanged(null);
        }

        public static void InvertNodeSelection(
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<NodeViewModel> selectedNodes,
            Action<NodeViewModel?> onPrimarySelectedChanged)
        {
            var currentlySelected = new HashSet<NodeViewModel>(selectedNodes);
            selectedNodes.Clear();

            foreach (var n in allNodes)
            {
                if (currentlySelected.Contains(n))
                {
                    n.IsSelected = false;
                }
                else
                {
                    n.IsSelected = true;
                    selectedNodes.Add(n);
                }
            }

            onPrimarySelectedChanged(selectedNodes.LastOrDefault());
        }

        public static int SelectNodesInBox(
            Rect selectionBox,
            ObservableCollection<NodeViewModel> allNodes,
            ObservableCollection<NodeViewModel> selectedNodes,
            Action<NodeViewModel?> onPrimarySelectedChanged,
            bool append = false,
            double nodeWidth = DefaultNodeWidth,
            double nodeHeight = DefaultNodeHeight)
        {
            if (!append)
            {
                foreach (var n in allNodes)
                {
                    n.IsSelected = false;
                }
                selectedNodes.Clear();
            }

            int count = 0;
            // Normalize selection box in case dimensions are negative
            double minX = Math.Min(selectionBox.X, selectionBox.X + selectionBox.Width);
            double minY = Math.Min(selectionBox.Y, selectionBox.Y + selectionBox.Height);
            double width = Math.Abs(selectionBox.Width);
            double height = Math.Abs(selectionBox.Height);
            var normalizedBox = new Rect(minX, minY, width, height);

            foreach (var n in allNodes)
            {
                var nodeRect = new Rect(n.X, n.Y, nodeWidth, nodeHeight);
                if (normalizedBox.Intersects(nodeRect))
                {
                    if (!selectedNodes.Contains(n))
                    {
                        n.IsSelected = true;
                        selectedNodes.Add(n);
                        count++;
                    }
                }
            }

            onPrimarySelectedChanged(selectedNodes.LastOrDefault());
            return count;
        }

        public static void SelectObject(
            FrameObjectViewModel obj,
            bool addToSelection,
            ObservableCollection<FrameObjectViewModel> allObjects,
            ObservableCollection<FrameObjectViewModel> selectedObjects,
            Action<FrameObjectViewModel?> onPrimaryChanged)
        {
            if (obj == null) return;

            if (!addToSelection)
            {
                selectedObjects.Clear();
                selectedObjects.Add(obj);
                onPrimaryChanged(obj);
            }
            else
            {
                if (selectedObjects.Contains(obj))
                {
                    selectedObjects.Remove(obj);
                    onPrimaryChanged(selectedObjects.LastOrDefault());
                }
                else
                {
                    selectedObjects.Add(obj);
                    onPrimaryChanged(obj);
                }
            }
        }

        public static void ClearObjectSelection(
            ObservableCollection<FrameObjectViewModel> selectedObjects,
            Action<FrameObjectViewModel?> onPrimaryChanged)
        {
            selectedObjects.Clear();
            onPrimaryChanged(null);
        }
    }
}
