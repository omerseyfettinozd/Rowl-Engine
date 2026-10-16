using System;
using System.Collections.Generic;
using System.Linq;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D2 — tabs of the bottom strip (the single <see cref="PanelIds.Bottom"/>
/// leaf). Bit <c>1 &lt;&lt; i</c> is tab index <c>i</c>, the same indices the
/// strip's <c>TabControl</c> and the Ctrl+3..7 shortcuts use.
/// </summary>
[Flags]
public enum BottomTabs
{
    None = 0,
    Assets = 1 << 0,
    Log = 1 << 1,
    Backlog = 1 << 2,
    SaveSlots = 1 << 3,
    Issues = 1 << 4,
}

/// <summary>
/// Faz 5 D2 — inner state of the bottom-strip leaf: which tabs are open and
/// which one is selected. It is not part of the split tree (the strip is one
/// leaf, tabbed inside); <see cref="EditorWorkspaceState"/> keeps the leaf and
/// the open tabs consistent.
/// </summary>
/// <param name="Open">Open tabs.</param>
/// <param name="ActiveTab">Selected tab index (0 = Assets … 4 = Issues). Stored as given, like the old field.</param>
public sealed record BottomStripState(BottomTabs Open, int ActiveTab)
{
    public const int AssetsTab = 0;
    public const int LogTab = 1;
    public const int BacklogTab = 2;
    public const int SaveSlotsTab = 3;
    public const int IssuesTab = 4;
    public const int TabCount = 5;

    /// <summary>Flag of tab <paramref name="index"/>; <see cref="BottomTabs.None"/> outside 0..4.</summary>
    public static BottomTabs Flag(int index) =>
        index is >= 0 and < TabCount ? (BottomTabs)(1 << index) : BottomTabs.None;

    /// <summary>Stale indices (old saves, actions) are clamped into 0..4.</summary>
    public static int ClampTab(int index) => Math.Clamp(index, 0, TabCount - 1);

    public bool IsOpen(int index)
    {
        BottomTabs flag = Flag(index);
        return flag != BottomTabs.None && (Open & flag) == flag;
    }

    public bool AnyOpen => Open != BottomTabs.None;

    /// <summary>First open tab, or 0 when the strip is empty (collapsed; index irrelevant).</summary>
    public int FirstOpenTab()
    {
        for (int i = 0; i < TabCount; i++)
        {
            if (IsOpen(i))
                return i;
        }
        return 0;
    }

    public BottomStripState WithTab(int index, bool open)
    {
        BottomTabs flag = Flag(index);
        if (flag == BottomTabs.None)
            return this;
        return this with { Open = open ? Open | flag : Open & ~flag };
    }
}

/// <summary>
/// Faz 5 D2 — the editor workspace as one immutable value: the split tree
/// (<see cref="Layout"/>, the single source of truth for which panels are
/// open and where) plus the bottom-strip leaf's tabs (<see cref="Bottom"/>).
/// <para>
/// Invariant: the tree holds a <see cref="PanelIds.Bottom"/> leaf exactly when
/// at least one bottom tab is open. Every operation returns a new state that
/// keeps it (or the same instance when nothing changes). The old view-model
/// flags (<c>IsHierarchyPanelVisible</c>, <c>IsNodeGraphActive</c>,
/// <c>SplitScreenMode</c>…) are read from here, never stored separately.
/// </para>
/// </summary>
public sealed record EditorWorkspaceState(WorkspaceLayout Layout, BottomStripState Bottom)
{
    /// <summary>"Varsayılan" preset — also the editor's start-up workspace.</summary>
    public static EditorWorkspaceState Default => EditorWorkspaceLayouts.Preset(EditorWorkspaceLayouts.DefaultPreset)!;

    // ---- derived (read) ----

    public bool IsVisible(string panelId) => Layout.Views.Contains(panelId, StringComparer.Ordinal);

    public bool IsHierarchyVisible => IsVisible(PanelIds.Hierarchy);

    public bool IsInspectorVisible => IsVisible(PanelIds.Inspector);

    /// <summary>The strip leaf is in the tree (⇔ a bottom tab is open).</summary>
    public bool IsBottomVisible => IsVisible(PanelIds.Bottom);

    /// <summary>0 = single view, 1 = Node above the preview (rows), 2 = side by side (columns).</summary>
    public int SplitScreenMode => EditorWorkspaceLayouts.SplitScreenModeOf(Layout.Tree);

    public bool IsNodeGraphActive => IsVisible(PanelIds.Node);

    /// <summary>"Sahne Düzenle" tab highlight: only in single-view mode (as before D2).</summary>
    public bool IsPreviewActive => SplitScreenMode == 0 && IsVisible(PanelIds.Edit);

    public bool IsEnginePreviewActive => IsVisible(PanelIds.Game);

    // ---- operations ----

    /// <summary>Opens or closes a side panel (<see cref="PanelIds.Hierarchy"/> / <see cref="PanelIds.Inspector"/>).</summary>
    public EditorWorkspaceState WithPanel(string panelId, bool visible)
    {
        if (panelId != PanelIds.Hierarchy && panelId != PanelIds.Inspector)
            throw new ArgumentException("Only side panels are toggled directly; use WithBottomTab / WithCenter.", nameof(panelId));
        if (IsVisible(panelId) == visible)
            return this;
        WorkspaceLayout layout = visible
            ? (panelId == PanelIds.Hierarchy ? EditorWorkspaceLayouts.OpenHierarchy(Layout) : EditorWorkspaceLayouts.OpenInspector(Layout))
            : Layout.RemoveView(panelId);
        return this with { Layout = layout };
    }

    /// <summary>Opens or closes one bottom tab; the strip leaf follows (first tab opens it, last closes it).</summary>
    public EditorWorkspaceState WithBottomTab(int index, bool open)
    {
        BottomStripState bottom = Bottom.WithTab(index, open);
        if (bottom == Bottom)
            return this;
        WorkspaceLayout layout = Layout;
        if (bottom.AnyOpen && !IsBottomVisible)
            layout = EditorWorkspaceLayouts.OpenBottom(layout);
        else if (!bottom.AnyOpen && IsBottomVisible)
            layout = layout.RemoveView(PanelIds.Bottom);
        return new EditorWorkspaceState(layout, bottom);
    }

    public EditorWorkspaceState WithActiveBottomTab(int index) =>
        Bottom.ActiveTab == index ? this : this with { Bottom = Bottom with { ActiveTab = index } };

    /// <summary>Single-view mode showing <paramref name="centerView"/> (Node / Game / Edit); leaves split screen.</summary>
    public EditorWorkspaceState WithCenter(string centerView)
    {
        if (!EditorWorkspaceLayouts.IsCenter(centerView))
            throw new ArgumentException("Not a center view.", nameof(centerView));
        return WithLayout(EditorWorkspaceLayouts.ReplaceCenter(Layout, _ => new LeafNode(centerView)));
    }

    /// <summary>Split screen 1 (rows) / 2 (columns): Node + Sahne Düzenle; 0 returns to the Node graph alone.</summary>
    public EditorWorkspaceState WithSplitScreen(int mode) =>
        mode is 1 or 2
            ? WithLayout(EditorWorkspaceLayouts.ReplaceCenter(Layout, ids => EditorWorkspaceLayouts.CenterContent(mode, PanelIds.Edit, ids)))
            : WithCenter(PanelIds.Node);

    /// <summary>Off → rows → columns → off.</summary>
    public static int NextSplitScreenMode(int mode) => (mode + 1) % 3;

    /// <summary>
    /// The panel menu / Ctrl+1..7 / capsule actions (formerly
    /// <c>EditorWorkspaceLayoutService.HandlePanelAction</c>): side panels and
    /// Save Slots / Issues toggle; Assets / Log / Backlog close only when they
    /// are the selected tab, otherwise open and get selected; center views pick
    /// single-view mode; "SplitScreen" cycles. Afterwards a hidden selected tab
    /// falls back to the first open one. Unknown names change nothing but that
    /// fallback.
    /// </summary>
    /// <param name="onSaveSlotsOpened">Called when the Save Slots tab is opened (refresh hook).</param>
    public EditorWorkspaceState HandlePanelAction(string panelName, Action? onSaveSlotsOpened = null)
    {
        EditorWorkspaceState next = this;
        switch (panelName)
        {
            case "Hierarchy":
                next = WithPanel(PanelIds.Hierarchy, !IsHierarchyVisible);
                break;
            case "Inspector":
                next = WithPanel(PanelIds.Inspector, !IsInspectorVisible);
                break;
            case "Assets":
                next = ToggleSelectedTab(BottomStripState.AssetsTab);
                break;
            case "Log":
                next = ToggleSelectedTab(BottomStripState.LogTab);
                break;
            case "Backlog":
                next = ToggleSelectedTab(BottomStripState.BacklogTab);
                break;
            case "SaveSlots":
                next = WithBottomTab(BottomStripState.SaveSlotsTab, !Bottom.IsOpen(BottomStripState.SaveSlotsTab));
                if (next.Bottom.IsOpen(BottomStripState.SaveSlotsTab))
                {
                    onSaveSlotsOpened?.Invoke();
                    next = next.WithActiveBottomTab(BottomStripState.SaveSlotsTab);
                }
                break;
            case "ProjectIssues":
                next = WithBottomTab(BottomStripState.IssuesTab, !Bottom.IsOpen(BottomStripState.IssuesTab));
                if (next.Bottom.IsOpen(BottomStripState.IssuesTab))
                    next = next.WithActiveBottomTab(BottomStripState.IssuesTab);
                break;
            case "NodeGraph":
                next = WithCenter(PanelIds.Node);
                break;
            case "Preview":
                next = WithCenter(PanelIds.Edit);
                break;
            case "EnginePreview":
                next = WithCenter(PanelIds.Game);
                break;
            case "SplitScreen":
                next = WithSplitScreen(NextSplitScreenMode(SplitScreenMode));
                break;
        }

        // A closed selected tab would leave the strip showing a headerless page: pick the first open tab.
        if (!next.Bottom.IsOpen(next.Bottom.ActiveTab))
            next = next.WithActiveBottomTab(next.Bottom.FirstOpenTab());
        return next;
    }

    /// <summary>Applies a layout preset; unknown names return the same instance.</summary>
    public EditorWorkspaceState ApplyPreset(string? presetName) =>
        EditorWorkspaceLayouts.Preset(presetName) ?? this;

    private EditorWorkspaceState ToggleSelectedTab(int index) =>
        Bottom.IsOpen(index) && Bottom.ActiveTab == index
            ? WithBottomTab(index, false)
            : WithBottomTab(index, true).WithActiveBottomTab(index);

    private EditorWorkspaceState WithLayout(WorkspaceLayout layout) =>
        ReferenceEquals(layout, Layout) || layout == Layout ? this : this with { Layout = layout };
}

/// <summary>
/// Faz 5 D2 — pure builders and tree edits behind <see cref="EditorWorkspaceState"/>.
/// The canonical tree mirrors the current MainWindow grid:
/// <code>
/// split(x) ─ hierarchy
///          └ split(x) ─ split(y) ─ center   (node | game | edit | split(node, edit))
///                     │          └ bottom
///                     └ inspector
/// </code>
/// Ratios are nominal in D2 (the window still sizes panels in pixels: 200 / 280 /
/// 180); they approximate the 1440×900 reference window inside the model's
/// [<see cref="WorkspaceLayout.MinRatio"/>, <see cref="WorkspaceLayout.MaxRatio"/>] range.
/// </summary>
public static class EditorWorkspaceLayouts
{
    public const string DefaultPreset = "Varsayılan";
    public const string SceneFocusPreset = "Sahne Odağı";
    public const string PlayTestPreset = "Oyun Testi";

    /// <summary>Preset names in menu order (command parameters; never translated).</summary>
    public static readonly IReadOnlyList<string> PresetNames = new[] { DefaultPreset, SceneFocusPreset, PlayTestPreset };

    /// <summary>Hierarchy's share of the window (≈ 200 px of 1440, clamped to the model minimum).</summary>
    public const double HierarchyRatio = WorkspaceLayout.MinRatio;

    /// <summary>Center column's share next to the Inspector (≈ 948 / 1234 px).</summary>
    public const double InspectorSplitRatio = 0.77;

    /// <summary>Center's share above the bottom strip (≈ 634 / 820 px).</summary>
    public const double BottomSplitRatio = 0.77;

    /// <summary>Split screen halves.</summary>
    public const double SplitScreenRatio = 0.5;

    private static readonly string[] CenterViews = { PanelIds.Node, PanelIds.Game, PanelIds.Edit };

    public static bool IsCenter(string? view) => view is not null && Array.IndexOf(CenterViews, view) >= 0;

    /// <summary>
    /// Builds the state of a layout preset (pure; a fresh tree each call), or
    /// <c>null</c> for an unknown name. "Varsayılan": full working layout;
    /// "Sahne Odağı": side panels closed, canvas + Assets strip; "Oyun Testi":
    /// Game in the center, Inspector open, strip closed.
    /// </summary>
    public static EditorWorkspaceState? Preset(string? presetName) => presetName switch
    {
        DefaultPreset => Build(hierarchy: true, inspector: true, BottomTabs.Assets | BottomTabs.Log, activeTab: 0, PanelIds.Node, splitScreenMode: 0),
        SceneFocusPreset => Build(hierarchy: false, inspector: false, BottomTabs.Assets, activeTab: 0, PanelIds.Node, splitScreenMode: 0),
        PlayTestPreset => Build(hierarchy: false, inspector: true, BottomTabs.None, activeTab: 0, PanelIds.Game, splitScreenMode: 0),
        _ => null,
    };

    /// <summary>
    /// Canonical workspace for the given panel set (pure). <paramref name="centerView"/>
    /// is shown alone when <paramref name="splitScreenMode"/> is 0; otherwise
    /// it is the split's second pane next to the Node graph.
    /// </summary>
    public static EditorWorkspaceState Build(
        bool hierarchy,
        bool inspector,
        BottomTabs bottomTabs,
        int activeTab,
        string centerView,
        int splitScreenMode)
    {
        if (!IsCenter(centerView))
            throw new ArgumentException("Not a center view.", nameof(centerView));
        int last = 0;
        var layout = new WorkspaceLayout(CenterContent(splitScreenMode, centerView, () => NextId(ref last)), last);
        var state = new EditorWorkspaceState(layout, new BottomStripState(BottomTabs.None, activeTab));
        for (int i = 0; i < BottomStripState.TabCount; i++)
        {
            if ((bottomTabs & BottomStripState.Flag(i)) != 0)
                state = state.WithBottomTab(i, true);
        }
        if (inspector)
            state = state.WithPanel(PanelIds.Inspector, true);
        if (hierarchy)
            state = state.WithPanel(PanelIds.Hierarchy, true);
        return state;
    }

    /// <summary>Center subtree for a split-screen mode (0: <paramref name="singleView"/> alone).</summary>
    internal static LayoutNode CenterContent(int splitScreenMode, string singleView, Func<string> nextId) => splitScreenMode switch
    {
        1 => new SplitNode(nextId(), SplitAxis.Vertical, SplitScreenRatio, new LeafNode(PanelIds.Node), new LeafNode(singleView == PanelIds.Node ? PanelIds.Edit : singleView)),
        2 => new SplitNode(nextId(), SplitAxis.Horizontal, SplitScreenRatio, new LeafNode(PanelIds.Node), new LeafNode(singleView == PanelIds.Node ? PanelIds.Edit : singleView)),
        _ => new LeafNode(singleView),
    };

    /// <summary>
    /// Split-screen mode read back from a tree: the smallest subtree holding all
    /// center views is <c>split(node, game|edit)</c> → 1 for rows (y), 2 for
    /// columns (x); anything else is single-view (0).
    /// </summary>
    public static int SplitScreenModeOf(LayoutNode? tree) =>
        FindCenter(tree) is SplitNode { A: LeafNode { View: PanelIds.Node }, B: LeafNode second } split
            && (second.View == PanelIds.Edit || second.View == PanelIds.Game)
            ? (split.Axis == SplitAxis.Vertical ? 1 : 2)
            : 0;

    // ---- tree edits ----

    /// <summary>Hierarchy opens as the leftmost column (wraps the whole tree).</summary>
    internal static WorkspaceLayout OpenHierarchy(WorkspaceLayout layout)
    {
        var leaf = new LeafNode(PanelIds.Hierarchy);
        int last = layout.LastSplitId;
        LayoutNode tree = layout.Tree is null
            ? leaf
            : new SplitNode(NextId(ref last), SplitAxis.Horizontal, HierarchyRatio, leaf, layout.Tree);
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>Inspector opens as the rightmost column, inside the Hierarchy column when that is present.</summary>
    internal static WorkspaceLayout OpenInspector(WorkspaceLayout layout)
    {
        var leaf = new LeafNode(PanelIds.Inspector);
        int last = layout.LastSplitId;
        LayoutNode tree = layout.Tree switch
        {
            null => leaf,
            SplitNode { Axis: SplitAxis.Horizontal, A: LeafNode { View: PanelIds.Hierarchy } } root =>
                root with { B = new SplitNode(NextId(ref last), SplitAxis.Horizontal, InspectorSplitRatio, root.B, leaf) },
            LayoutNode other => new SplitNode(NextId(ref last), SplitAxis.Horizontal, InspectorSplitRatio, other, leaf),
        };
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>The bottom strip opens under the center area (falls back to under the whole tree).</summary>
    internal static WorkspaceLayout OpenBottom(WorkspaceLayout layout)
    {
        var leaf = new LeafNode(PanelIds.Bottom);
        int last = layout.LastSplitId;
        LayoutNode? center = FindCenter(layout.Tree);
        LayoutNode tree = layout.Tree is null
            ? leaf
            : center is null
                ? new SplitNode(NextId(ref last), SplitAxis.Vertical, BottomSplitRatio, layout.Tree, leaf)
                : Replace(layout.Tree, center, new SplitNode(NextId(ref last), SplitAxis.Vertical, BottomSplitRatio, center, leaf));
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>
    /// Replaces the center area (smallest subtree holding every center view)
    /// with <paramref name="content"/>. If that subtree also holds other panels
    /// (not produced in D2), the extra center views are removed and the first
    /// one is replaced; with no center view at all the content docks right of
    /// the tree. Returns the same instance when the result is structurally equal.
    /// </summary>
    internal static WorkspaceLayout ReplaceCenter(WorkspaceLayout layout, Func<Func<string>, LayoutNode> content)
    {
        int last = layout.LastSplitId;
        string Next() => NextId(ref last);
        LayoutNode? center = FindCenter(layout.Tree);
        LayoutNode? tree;
        if (layout.Tree is null)
        {
            tree = content(Next);
        }
        else if (center is null)
        {
            tree = new SplitNode(Next(), SplitAxis.Horizontal, SplitScreenRatio, layout.Tree, content(Next));
        }
        else if (WorkspaceLayout.Leaves(center).All(IsCenter))
        {
            LayoutNode replacement = content(Next);
            if (replacement == center)
                return layout;
            tree = Replace(layout.Tree, center, replacement);
        }
        else
        {
            IReadOnlyList<string> centers = WorkspaceLayout.Leaves(layout.Tree).Where(IsCenter).ToArray();
            WorkspaceLayout trimmed = layout;
            foreach (string extra in centers.Skip(1))
                trimmed = trimmed.RemoveView(extra);
            LeafNode first = FindLeaf(trimmed.Tree, centers[0])!;
            tree = Replace(trimmed.Tree!, first, content(Next));
        }
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>Smallest subtree containing every center view; <c>null</c> when there is none.</summary>
    internal static LayoutNode? FindCenter(LayoutNode? tree)
    {
        int total = CountCenters(tree);
        if (total == 0)
            return null;
        LayoutNode node = tree!;
        while (node is SplitNode split)
        {
            if (CountCenters(split.A) == total)
                node = split.A;
            else if (CountCenters(split.B) == total)
                node = split.B;
            else
                break;
        }
        return node;
    }

    private static int CountCenters(LayoutNode? node) => node switch
    {
        LeafNode leaf => IsCenter(leaf.View) ? 1 : 0,
        SplitNode split => CountCenters(split.A) + CountCenters(split.B),
        _ => 0,
    };

    private static LeafNode? FindLeaf(LayoutNode? node, string view) => node switch
    {
        LeafNode leaf when leaf.View == view => leaf,
        SplitNode split => FindLeaf(split.A, view) ?? FindLeaf(split.B, view),
        _ => null,
    };

    /// <summary>Replaces the node instance <paramref name="target"/> (by reference) with <paramref name="replacement"/>.</summary>
    private static LayoutNode Replace(LayoutNode node, LayoutNode target, LayoutNode replacement)
    {
        if (ReferenceEquals(node, target))
            return replacement;
        if (node is not SplitNode split)
            return node;
        LayoutNode a = Replace(split.A, target, replacement);
        LayoutNode b = Replace(split.B, target, replacement);
        return ReferenceEquals(a, split.A) && ReferenceEquals(b, split.B) ? split : split with { A = a, B = b };
    }

    private static string NextId(ref int last)
    {
        last++;
        return "split-" + last.ToString(System.Globalization.CultureInfo.InvariantCulture);
    }
}
