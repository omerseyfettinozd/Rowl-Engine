using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Service managing workspace layout states, panel visibility, bottom tabs routing,
    /// split-screen modes, quick-search node matching, and smooth canvas camera transitions.
    /// </summary>
    public static class EditorWorkspaceLayoutService
    {
        public static GridLength CalculateBottomPanelHeight(bool isVisible, double height = 180) =>
            isVisible ? new GridLength(height) : new GridLength(0);

        public static GridLength CalculateBottomSplitterHeight(bool isVisible, double height = 6) =>
            isVisible ? new GridLength(height) : new GridLength(0);

        public static GridLength CalculateHierarchyPanelWidth(bool isVisible, double width = 240) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        public static GridLength CalculateHierarchySplitterWidth(bool isVisible, double width = 6) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        public static GridLength CalculateInspectorPanelWidth(bool isVisible, double width = 280) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        public static GridLength CalculateInspectorSplitterWidth(bool isVisible, double width = 6) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        /// <summary>
        /// Toggles a bottom panel tab. If the tab is already active and visible, it closes it.
        /// Otherwise, opens the drawer and switches to the target tab index.
        /// </summary>
        public static void ToggleBottomTab(
            ref bool panelVisible,
            ref int activeTab,
            int targetTabIndex)
        {
            if (panelVisible && activeTab == targetTabIndex)
            {
                panelVisible = false;
            }
            else
            {
                panelVisible = true;
                activeTab = targetTabIndex;
            }
        }

        /// <summary>
        /// Handles switching and toggling of workspace panels and views.
        /// </summary>
        public static void HandlePanelAction(
            string panelName,
            ref bool isHierarchyVisible,
            ref bool isAssetsVisible,
            ref bool isInspectorVisible,
            ref bool isLogVisible,
            ref bool isBacklogVisible,
            ref bool isSaveSlotsVisible,
            ref bool isProjectIssuesVisible,
            ref int bottomPanelActiveTab,
            ref bool isNodeGraphActive,
            ref bool isPreviewActive,
            ref bool isEnginePreviewActive,
            ref int splitScreenMode,
            Action? refreshSaveSlots = null)
        {
            switch (panelName)
            {
                case "Hierarchy":
                    isHierarchyVisible = !isHierarchyVisible;
                    break;
                case "Assets":
                    ToggleBottomTab(ref isAssetsVisible, ref bottomPanelActiveTab, 1);
                    break;
                case "Inspector":
                    isInspectorVisible = !isInspectorVisible;
                    break;
                case "Log":
                    ToggleBottomTab(ref isLogVisible, ref bottomPanelActiveTab, 0);
                    break;
                case "Backlog":
                    ToggleBottomTab(ref isBacklogVisible, ref bottomPanelActiveTab, 2);
                    break;
                case "SaveSlots":
                    isSaveSlotsVisible = !isSaveSlotsVisible;
                    if (isSaveSlotsVisible)
                    {
                        refreshSaveSlots?.Invoke();
                        bottomPanelActiveTab = 3;
                    }
                    break;
                case "ProjectIssues":
                    isProjectIssuesVisible = !isProjectIssuesVisible;
                    if (isProjectIssuesVisible) bottomPanelActiveTab = 4;
                    break;
                case "NodeGraph":
                    isNodeGraphActive = true;
                    isPreviewActive = false;
                    isEnginePreviewActive = false;
                    splitScreenMode = 0;
                    break;
                case "Preview":
                    isPreviewActive = true;
                    isNodeGraphActive = false;
                    isEnginePreviewActive = false;
                    splitScreenMode = 0;
                    break;
                case "EnginePreview":
                    isEnginePreviewActive = true;
                    isNodeGraphActive = false;
                    isPreviewActive = false;
                    splitScreenMode = 0;
                    break;
                case "SplitScreen":
                    splitScreenMode = CycleSplitScreen(splitScreenMode, out isNodeGraphActive, out isEnginePreviewActive, out isPreviewActive);
                    break;
            }
        }

        /// <summary>
        /// Cycles split screen mode (0: Off -> 1: Horizontal -> 2: Vertical -> 0: Off).
        /// </summary>
        public static int CycleSplitScreen(
            int currentMode,
            out bool isNodeGraphActive,
            out bool isEnginePreviewActive,
            out bool isPreviewActive)
        {
            int nextMode = (currentMode + 1) % 3;
            if (nextMode > 0)
            {
                isNodeGraphActive = true;
                isEnginePreviewActive = false;
                isPreviewActive = false;
            }
            else
            {
                isNodeGraphActive = true;
                isEnginePreviewActive = false;
                isPreviewActive = false;
            }
            return nextMode;
        }

        /// <summary>
        /// Finds the first node matching the search query in Title, Speaker, or DialogueText.
        /// </summary>
        public static NodeViewModel? FindMatchingNode(IEnumerable<NodeViewModel> nodes, string query)
        {
            if (string.IsNullOrWhiteSpace(query)) return null;
            return nodes.FirstOrDefault(n =>
                (n.Title?.Contains(query, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (n.Speaker?.Contains(query, StringComparison.OrdinalIgnoreCase) ?? false) ||
                (n.DialogueText?.Contains(query, StringComparison.OrdinalIgnoreCase) ?? false));
        }

        /// <summary>
        /// Computes target PanX and PanY coordinates to center the canvas on a specific node.
        /// </summary>
        public static (double TargetPanX, double TargetPanY) CalculatePanTargetForNode(
            NodeViewModel node,
            double zoomScale,
            double viewportOffsetX = 300,
            double viewportOffsetY = 200)
        {
            double targetX = -node.X * zoomScale + viewportOffsetX;
            double targetY = -node.Y * zoomScale + viewportOffsetY;
            return (targetX, targetY);
        }

        /// <summary>
        /// Computes a smooth step interpolation towards target zoom and pan values.
        /// Returns true if convergence is reached and animation can stop.
        /// </summary>
        public static bool ComputeSmoothStep(
            ref double currentZoom,
            ref double currentPanX,
            ref double currentPanY,
            double targetZoom,
            double targetPanX,
            double targetPanY,
            double factor = 0.22)
        {
            double zoomDiff = targetZoom - currentZoom;
            double panXDiff = targetPanX - currentPanX;
            double panYDiff = targetPanY - currentPanY;

            if (Math.Abs(zoomDiff) > 0.0001 || Math.Abs(panXDiff) > 0.05 || Math.Abs(panYDiff) > 0.05)
            {
                currentZoom += zoomDiff * factor;
                currentPanX += panXDiff * factor;
                currentPanY += panYDiff * factor;
                return false;
            }
            else
            {
                currentZoom = targetZoom;
                currentPanX = targetPanX;
                currentPanY = targetPanY;
                return true;
            }
        }
    }
}
