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
    /// <summary>
    /// Faz 5 D4 — pinned panels: locked against drag / menu moves
    /// (<see cref="WorkspaceLayout.DockView"/> rejects them as a source).
    /// Invariant: only panels in the tree; a closed panel loses its pin
    /// (prototype <c>closeView</c>). Not part of the tree (D5 persists it).
    /// </summary>
    public PanelSet Pinned { get; init; } = PanelSet.Empty;

    /// <summary>
    /// Faz 5 D5 — last known share of the side panels (Hierarchy / Inspector /
    /// bottom strip) that were closed, so reopening restores the size the user
    /// dragged them to. A layout preset resets it (presets are the "reset layout" path).
    /// </summary>
    public PanelShares Shares { get; init; } = PanelShares.Empty;

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

    public bool IsPinned(string panelId) => Pinned.Contains(panelId);

    /// <summary>A panel can be closed from its header unless it is the last one in the workspace.</summary>
    public bool CanClose(string panelId) => IsVisible(panelId) && Layout.Views.Count > 1;

    // ---- operations ----

    /// <summary>
    /// Faz 5 D4 — header drag / "Move" menu: docks <paramref name="source"/> on
    /// <paramref name="edge"/> of <paramref name="target"/> (<see cref="WorkspaceLayout.DockView"/>;
    /// the edge is logical: Left = the split's first side). Pinned sources,
    /// self drops and unknown panels return the same instance.
    /// </summary>
    public EditorWorkspaceState Dock(string source, string target, DockEdge edge) =>
        WithLayout(Layout.DockView(source, target, edge, Pinned));

    /// <summary>Faz 5 D4 — pins / unpins a panel that is in the workspace (others: same instance).</summary>
    public EditorWorkspaceState WithPinned(string panelId, bool pinned)
    {
        if (!IsVisible(panelId) || IsPinned(panelId) == pinned)
            return this;
        return this with { Pinned = pinned ? Pinned.With(panelId) : Pinned.Without(panelId) };
    }

    public EditorWorkspaceState TogglePin(string panelId) => WithPinned(panelId, !IsPinned(panelId));

    /// <summary>
    /// Faz 5 D4 — the header's close (×): side panels close like their menu
    /// toggle, the bottom strip closes all its tabs, a center view leaves the
    /// tree (its sibling takes the area). The last panel is never closed.
    /// Every panel comes back through the existing menu / shortcut actions
    /// (<see cref="HandlePanelAction"/>).
    /// </summary>
    public EditorWorkspaceState ClosePanel(string panelId)
    {
        if (!CanClose(panelId))
            return this;
        return panelId switch
        {
            PanelIds.Hierarchy or PanelIds.Inspector or PanelIds.Library or PanelIds.Lua or PanelIds.Store => WithPanel(panelId, false),
            PanelIds.Bottom => (this with
            {
                Layout = Layout.RemoveView(PanelIds.Bottom),
                Bottom = Bottom with { Open = BottomTabs.None },
                Shares = RememberShare(PanelIds.Bottom),
            }).Pruned(),
            _ => WithLayout(Layout.RemoveView(panelId)),
        };
    }

    /// <summary>
    /// Opens or closes a side / tool panel (<see cref="PanelIds.Hierarchy"/>, <see cref="PanelIds.Inspector"/>,
    /// Faz 7: <see cref="PanelIds.Library"/>, <see cref="PanelIds.Lua"/>; Faz 8: <see cref="PanelIds.Store"/>).
    /// </summary>
    public EditorWorkspaceState WithPanel(string panelId, bool visible)
    {
        if (panelId is not (PanelIds.Hierarchy or PanelIds.Inspector or PanelIds.Library or PanelIds.Lua or PanelIds.Store))
            throw new ArgumentException("Only side / tool panels are toggled directly; use WithBottomTab / WithCenter.", nameof(panelId));
        if (IsVisible(panelId) == visible)
            return this;
        WorkspaceLayout layout = visible
            ? panelId switch
            {
                PanelIds.Hierarchy => EditorWorkspaceLayouts.OpenHierarchy(Layout, Shares),
                PanelIds.Inspector => EditorWorkspaceLayouts.OpenInspector(Layout, Shares),
                PanelIds.Library => EditorWorkspaceLayouts.OpenLibrary(Layout, Shares),
                PanelIds.Store => EditorWorkspaceLayouts.OpenStore(Layout, Shares),
                _ => EditorWorkspaceLayouts.OpenLua(Layout, Shares),
            }
            : Layout.RemoveView(panelId);
        return (this with { Layout = layout, Shares = visible ? Shares : RememberShare(panelId) }).Pruned();
    }

    /// <summary>Opens or closes one bottom tab; the strip leaf follows (first tab opens it, last closes it).</summary>
    public EditorWorkspaceState WithBottomTab(int index, bool open)
    {
        BottomStripState bottom = Bottom.WithTab(index, open);
        if (bottom == Bottom)
            return this;
        WorkspaceLayout layout = Layout;
        PanelShares shares = Shares;
        if (bottom.AnyOpen && !IsBottomVisible)
            layout = EditorWorkspaceLayouts.OpenBottom(layout, Shares);
        else if (!bottom.AnyOpen && IsBottomVisible)
        {
            shares = RememberShare(PanelIds.Bottom);
            layout = layout.RemoveView(PanelIds.Bottom);
        }
        return (this with { Layout = layout, Bottom = bottom, Shares = shares }).Pruned();
    }

    /// <summary>
    /// Faz 5 D3 — a splitter was moved: the split's ratio is written to the tree
    /// (clamped by <see cref="WorkspaceLayout.ResizeSplit"/>). Unknown ids and
    /// unchanged ratios return the same instance.
    /// </summary>
    public EditorWorkspaceState WithSplitRatio(string splitId, double ratio) =>
        WithLayout(Layout.ResizeSplit(splitId, ratio));

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
            case "Library":
                next = WithPanel(PanelIds.Library, !IsVisible(PanelIds.Library));
                break;
            case "Lua":
                next = WithPanel(PanelIds.Lua, !IsVisible(PanelIds.Lua));
                break;
            case "Store":
                next = WithPanel(PanelIds.Store, !IsVisible(PanelIds.Store));
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
    /// <remarks>Faz 5 D4: pins of panels the preset keeps open stay (a preset is not a drag move).</remarks>
    public EditorWorkspaceState ApplyPreset(string? presetName) =>
        EditorWorkspaceLayouts.Preset(presetName) is { } preset
            ? (preset with { Pinned = Pinned }).Pruned()
            : this;

    private EditorWorkspaceState ToggleSelectedTab(int index) =>
        Bottom.IsOpen(index) && Bottom.ActiveTab == index
            ? WithBottomTab(index, false)
            : WithBottomTab(index, true).WithActiveBottomTab(index);

    /// <summary>Shares with <paramref name="panelId"/>'s current share in the tree (unchanged when it has no canonical split).</summary>
    private PanelShares RememberShare(string panelId) =>
        EditorWorkspaceLayouts.ShareOf(Layout, panelId) is { } share
            ? Shares.With(panelId, Math.Clamp(share, WorkspaceLayout.MinRatio, WorkspaceLayout.MaxRatio))
            : Shares;

    private EditorWorkspaceState WithLayout(WorkspaceLayout layout) =>
        ReferenceEquals(layout, Layout) || layout == Layout ? this : (this with { Layout = layout }).Pruned();

    /// <summary>Keeps <see cref="Pinned"/> ⊆ the tree's panels (same instance when already true).</summary>
    private EditorWorkspaceState Pruned()
    {
        PanelSet pinned = Pinned.Intersect(Layout.Views.ToArray());
        return ReferenceEquals(pinned, Pinned) ? this : this with { Pinned = pinned };
    }
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
/// Faz 5 D3: the window draws these ratios (star sizes, 6 px splitters, per-panel
/// pixel minimums). The defaults reproduce the pre-tree pixel layout in the
/// 1440×900 reference window: Hierarchy ≈ 200, Inspector ≈ 280, bottom strip
/// ≈ 180 px (workspace 1440×820); panels scale with the window from there.
/// </summary>
public static class EditorWorkspaceLayouts
{
    public const string DefaultPreset = "Varsayılan";
    public const string SceneFocusPreset = "Sahne Odağı";
    public const string PlayTestPreset = "Oyun Testi";

    /// <summary>Preset names in menu order (command parameters; never translated).</summary>
    public static readonly IReadOnlyList<string> PresetNames = new[] { DefaultPreset, SceneFocusPreset, PlayTestPreset };

    /// <summary>Hierarchy's share of the window: 0.14 × (1440 − 6) ≈ 201 px.</summary>
    public const double HierarchyRatio = 0.14;

    /// <summary>Center column's share next to the Inspector: Inspector = 0.228 × (1233 − 6) ≈ 280 px.</summary>
    public const double InspectorSplitRatio = 0.772;

    /// <summary>Center's share above the bottom strip: strip = 0.22 × (820 − 6) ≈ 179 px.</summary>
    public const double BottomSplitRatio = 0.78;

    /// <summary>Split screen halves.</summary>
    public const double SplitScreenRatio = 0.5;

    /// <summary>Faz 7: center area's share beside the Node library (prototype library-categories: ≈ ½ … ⅔).</summary>
    public const double LibrarySplitRatio = 0.62;

    /// <summary>Faz 7: center area's share above the Lua editor (prototype desktop.jpg: Node and Lua editor ≈ half each).</summary>
    public const double LuaSplitRatio = 0.5;

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
    internal static WorkspaceLayout OpenHierarchy(WorkspaceLayout layout, PanelShares? shares = null)
    {
        var leaf = new LeafNode(PanelIds.Hierarchy);
        int last = layout.LastSplitId;
        LayoutNode tree = layout.Tree is null
            ? leaf
            : new SplitNode(NextId(ref last), SplitAxis.Horizontal, RememberedOr(shares, PanelIds.Hierarchy, HierarchyRatio, firstSide: true), leaf, layout.Tree);
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>Inspector opens as the rightmost column, inside the Hierarchy column when that is present.</summary>
    internal static WorkspaceLayout OpenInspector(WorkspaceLayout layout, PanelShares? shares = null)
    {
        double ratio = RememberedOr(shares, PanelIds.Inspector, InspectorSplitRatio, firstSide: false);
        var leaf = new LeafNode(PanelIds.Inspector);
        int last = layout.LastSplitId;
        LayoutNode tree = layout.Tree switch
        {
            null => leaf,
            SplitNode { Axis: SplitAxis.Horizontal, A: LeafNode { View: PanelIds.Hierarchy } } root =>
                root with { B = new SplitNode(NextId(ref last), SplitAxis.Horizontal, ratio, root.B, leaf) },
            LayoutNode other => new SplitNode(NextId(ref last), SplitAxis.Horizontal, ratio, other, leaf),
        };
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>Faz 7: the Node library opens right of the center area (falls back to right of the whole tree).</summary>
    internal static WorkspaceLayout OpenLibrary(WorkspaceLayout layout, PanelShares? shares = null) =>
        OpenBesideCenter(layout, PanelIds.Library, SplitAxis.Horizontal,
            RememberedOr(shares, PanelIds.Library, LibrarySplitRatio, firstSide: false));

    /// <summary>Faz 8: Assets Store ("Yakında") opens right of the center area, like the Node library.</summary>
    internal static WorkspaceLayout OpenStore(WorkspaceLayout layout, PanelShares? shares = null) =>
        OpenBesideCenter(layout, PanelIds.Store, SplitAxis.Horizontal,
            RememberedOr(shares, PanelIds.Store, LibrarySplitRatio, firstSide: false));

    /// <summary>Faz 7: the Lua editor opens under the center area (prototype desktop.jpg: under Node).</summary>
    internal static WorkspaceLayout OpenLua(WorkspaceLayout layout, PanelShares? shares = null) =>
        OpenBesideCenter(layout, PanelIds.Lua, SplitAxis.Vertical,
            RememberedOr(shares, PanelIds.Lua, LuaSplitRatio, firstSide: false));

    private static WorkspaceLayout OpenBesideCenter(WorkspaceLayout layout, string panelId, SplitAxis axis, double ratio)
    {
        var leaf = new LeafNode(panelId);
        int last = layout.LastSplitId;
        LayoutNode? center = FindCenter(layout.Tree);
        LayoutNode tree = layout.Tree is null
            ? leaf
            : center is null
                ? new SplitNode(NextId(ref last), axis, ratio, layout.Tree, leaf)
                : Replace(layout.Tree, center, new SplitNode(NextId(ref last), axis, ratio, center, leaf));
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>The bottom strip opens under the center area (falls back to under the whole tree).</summary>
    internal static WorkspaceLayout OpenBottom(WorkspaceLayout layout, PanelShares? shares = null)
    {
        double ratio = RememberedOr(shares, PanelIds.Bottom, BottomSplitRatio, firstSide: false);
        var leaf = new LeafNode(PanelIds.Bottom);
        int last = layout.LastSplitId;
        LayoutNode? center = FindCenter(layout.Tree);
        LayoutNode tree = layout.Tree is null
            ? leaf
            : center is null
                ? new SplitNode(NextId(ref last), SplitAxis.Vertical, ratio, layout.Tree, leaf)
                : Replace(layout.Tree, center, new SplitNode(NextId(ref last), SplitAxis.Vertical, ratio, center, leaf));
        return new WorkspaceLayout(tree, last);
    }

    /// <summary>
    /// Faz 5 D5 — the share a side panel takes in its parent split: the panel's
    /// leaf must be a direct child of a split along the axis the canonical
    /// layout opens it on (Hierarchy / Inspector: columns, bottom strip: rows).
    /// <c>null</c> when it sits elsewhere (e.g. after a drag), so the previous
    /// remembered value stays.
    /// </summary>
    internal static double? ShareOf(WorkspaceLayout layout, string panelId)
    {
        SplitAxis axis = panelId is PanelIds.Bottom or PanelIds.Lua ? SplitAxis.Vertical : SplitAxis.Horizontal;
        return ShareIn(layout.Tree, panelId, axis);
    }

    private static double? ShareIn(LayoutNode? node, string panelId, SplitAxis axis)
    {
        if (node is not SplitNode split)
            return null;
        if (split.Axis == axis)
        {
            if (split.A is LeafNode a && a.View == panelId)
                return split.Ratio;
            if (split.B is LeafNode b && b.View == panelId)
                return 1 - split.Ratio;
        }
        return ShareIn(split.A, panelId, axis) ?? ShareIn(split.B, panelId, axis);
    }

    /// <summary>
    /// Ratio for a split that reopens a panel: the remembered share (when valid),
    /// else <paramref name="fallback"/>. The ratio is that of the split, so a
    /// panel on the second side gets <c>1 - share</c>.
    /// </summary>
    private static double RememberedOr(PanelShares? shares, string panelId, double fallback, bool firstSide)
    {
        if (shares is null || !shares.TryGet(panelId, out double share) || !(share > 0 && share < 1))
            return fallback;
        double ratio = firstSide ? share : 1 - share;
        return Math.Clamp(ratio, WorkspaceLayout.MinRatio, WorkspaceLayout.MaxRatio);
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
