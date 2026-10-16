using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia.Controls;
using RowlEngine.Editor.Services.Workspace;
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

        public static GridLength CalculateHierarchyPanelWidth(bool isVisible, double width = 200) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        public static GridLength CalculateHierarchySplitterWidth(bool isVisible, double width = 6) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        public static GridLength CalculateInspectorPanelWidth(bool isVisible, double width = 280) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        public static GridLength CalculateInspectorSplitterWidth(bool isVisible, double width = 6) =>
            isVisible ? new GridLength(width) : new GridLength(0);

        /// <summary>
        /// Legacy flag API (kept for callers/tests that hold the workspace as
        /// loose flags). Faz 5 D2: no logic of its own — the flags become an
        /// <see cref="EditorWorkspaceState"/> (canonical tree), the pure
        /// <see cref="EditorWorkspaceState.HandlePanelAction"/> runs, and the
        /// derived flags are written back. Bottom tab ids / Ctrl+3..7: 0 = Varlıklar,
        /// 1 = Günlük, 2 = Diyalog Geçmişi, 3 = Kayıt Slotları, 4 = Sorunlar.
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
            EditorWorkspaceState state = FromFlags(
                isHierarchyVisible, isAssetsVisible, isInspectorVisible, isLogVisible,
                isBacklogVisible, isSaveSlotsVisible, isProjectIssuesVisible, bottomPanelActiveTab,
                isNodeGraphActive, isPreviewActive, isEnginePreviewActive, splitScreenMode);
            ToFlags(
                state.HandlePanelAction(panelName, refreshSaveSlots),
                out isHierarchyVisible, out isAssetsVisible, out isInspectorVisible, out isLogVisible,
                out isBacklogVisible, out isSaveSlotsVisible, out isProjectIssuesVisible, out bottomPanelActiveTab,
                out isNodeGraphActive, out isPreviewActive, out isEnginePreviewActive, out splitScreenMode);
        }

        /// <summary>
        /// Legacy flag API: düzen önayarı — panelleri AÇIK/KAPALI olarak atar
        /// (geçiş yapmaz); bilinmeyen ad no-op'tur. Faz 5 D2: önayarlar
        /// <see cref="EditorWorkspaceLayouts.Preset"/> ile ağaç üretir; burada
        /// yalnız bayraklara çevrilir.
        /// </summary>
        public static void ApplyLayoutPreset(
            string presetName,
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
            ref int splitScreenMode)
        {
            EditorWorkspaceState? preset = EditorWorkspaceLayouts.Preset(presetName);
            if (preset is null)
                return;
            ToFlags(
                preset,
                out isHierarchyVisible, out isAssetsVisible, out isInspectorVisible, out isLogVisible,
                out isBacklogVisible, out isSaveSlotsVisible, out isProjectIssuesVisible, out bottomPanelActiveTab,
                out isNodeGraphActive, out isPreviewActive, out isEnginePreviewActive, out splitScreenMode);
        }

        /// <summary>
        /// Legacy flags → canonical workspace. Center: split mode 1/2 puts Node
        /// first and Game (if active) or Sahne Düzenle second; single mode shows
        /// the first active of Node, Sahne Düzenle, Game (Node if none).
        /// </summary>
        public static EditorWorkspaceState FromFlags(
            bool isHierarchyVisible,
            bool isAssetsVisible,
            bool isInspectorVisible,
            bool isLogVisible,
            bool isBacklogVisible,
            bool isSaveSlotsVisible,
            bool isProjectIssuesVisible,
            int bottomPanelActiveTab,
            bool isNodeGraphActive,
            bool isPreviewActive,
            bool isEnginePreviewActive,
            int splitScreenMode)
        {
            BottomTabs tabs = BottomTabs.None;
            if (isAssetsVisible) tabs |= BottomTabs.Assets;
            if (isLogVisible) tabs |= BottomTabs.Log;
            if (isBacklogVisible) tabs |= BottomTabs.Backlog;
            if (isSaveSlotsVisible) tabs |= BottomTabs.SaveSlots;
            if (isProjectIssuesVisible) tabs |= BottomTabs.Issues;

            bool split = splitScreenMode is 1 or 2;
            string center = split
                ? (isEnginePreviewActive ? PanelIds.Game : PanelIds.Edit)
                : isNodeGraphActive ? PanelIds.Node
                : isPreviewActive ? PanelIds.Edit
                : isEnginePreviewActive ? PanelIds.Game
                : PanelIds.Node;
            return EditorWorkspaceLayouts.Build(
                isHierarchyVisible, isInspectorVisible, tabs, bottomPanelActiveTab,
                center, split ? splitScreenMode : 0);
        }

        /// <summary>Workspace → the legacy flags (all derived from the tree / strip state).</summary>
        public static void ToFlags(
            EditorWorkspaceState state,
            out bool isHierarchyVisible,
            out bool isAssetsVisible,
            out bool isInspectorVisible,
            out bool isLogVisible,
            out bool isBacklogVisible,
            out bool isSaveSlotsVisible,
            out bool isProjectIssuesVisible,
            out int bottomPanelActiveTab,
            out bool isNodeGraphActive,
            out bool isPreviewActive,
            out bool isEnginePreviewActive,
            out int splitScreenMode)
        {
            isHierarchyVisible = state.IsHierarchyVisible;
            isInspectorVisible = state.IsInspectorVisible;
            isAssetsVisible = state.Bottom.IsOpen(BottomStripState.AssetsTab);
            isLogVisible = state.Bottom.IsOpen(BottomStripState.LogTab);
            isBacklogVisible = state.Bottom.IsOpen(BottomStripState.BacklogTab);
            isSaveSlotsVisible = state.Bottom.IsOpen(BottomStripState.SaveSlotsTab);
            isProjectIssuesVisible = state.Bottom.IsOpen(BottomStripState.IssuesTab);
            bottomPanelActiveTab = state.Bottom.ActiveTab;
            isNodeGraphActive = state.IsNodeGraphActive;
            isPreviewActive = state.IsPreviewActive;
            isEnginePreviewActive = state.IsEnginePreviewActive;
            splitScreenMode = state.SplitScreenMode;
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
            // Every mode (split or back to off) shows the Node graph; split's second pane is Sahne Düzenle.
            isNodeGraphActive = true;
            isEnginePreviewActive = false;
            isPreviewActive = false;
            return EditorWorkspaceState.NextSplitScreenMode(currentMode);
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
