using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D1 — pure, UI-independent workspace layout: a binary split tree whose
/// leaves are panels (port of the prototype's <c>layout-state.mjs</c>). Every
/// operation returns a new layout (or the same instance when the request is
/// rejected / changes nothing); the tree is never mutated.
/// <para>
/// Invariants (checked by <see cref="Validate"/>): every panel appears in at
/// most one leaf, panel ids are known, every split has two children and a
/// ratio inside (0, 1), split ids are unique. Operations keep ratios inside
/// [<see cref="MinRatio"/>, <see cref="MaxRatio"/>] so no panel can collapse.
/// </para>
/// </summary>
/// <param name="Tree">Root node, or <c>null</c> when no panel is open.</param>
/// <param name="LastSplitId">Counter behind the generated <c>split-N</c> ids; persisted so ids stay unique.</param>
public sealed record WorkspaceLayout(LayoutNode? Tree, int LastSplitId)
{
    /// <summary>Smallest share a split may give either side.</summary>
    public const double MinRatio = 0.2;

    /// <summary>Largest share a split may give its first side.</summary>
    public const double MaxRatio = 0.8;

    private const string SplitIdPrefix = "split-";

    /// <summary>A single Node panel filling the workspace.</summary>
    public static WorkspaceLayout Initial() => new(new LeafNode(PanelIds.Node), 0);

    /// <summary>Panels in tree order (first child before second).</summary>
    public IReadOnlyList<string> Views => Leaves(Tree);

    /// <summary>Panels of <paramref name="tree"/> in tree order; empty for <c>null</c>.</summary>
    public static IReadOnlyList<string> Leaves(LayoutNode? tree)
    {
        var views = new List<string>();
        CollectLeaves(tree, views);
        return views;
    }

    private static void CollectLeaves(LayoutNode? node, List<string> into)
    {
        switch (node)
        {
            case LeafNode leaf:
                into.Add(leaf.View);
                break;
            case SplitNode split:
                CollectLeaves(split.A, into);
                CollectLeaves(split.B, into);
                break;
        }
    }

    /// <summary>
    /// Checks the structural invariants. Returns <c>null</c> when the layout is
    /// valid, otherwise a short English description of the first violation.
    /// </summary>
    public string? Validate()
    {
        if (LastSplitId < 0)
            return "lastSplitId is negative";
        var views = new HashSet<string>(StringComparer.Ordinal);
        var splitIds = new HashSet<string>(StringComparer.Ordinal);
        return Tree is null ? null : ValidateNode(Tree, views, splitIds);
    }

    /// <summary><c>true</c> when <see cref="Validate"/> finds no violation.</summary>
    public bool IsValid => Validate() is null;

    private string? ValidateNode(LayoutNode? node, HashSet<string> views, HashSet<string> splitIds)
    {
        switch (node)
        {
            case null:
                return "split has a missing child";
            case LeafNode leaf:
                if (!PanelIds.IsKnown(leaf.View))
                    return "unknown panel id";
                if (!views.Add(leaf.View))
                    return "panel appears more than once";
                return null;
            case SplitNode split:
                if (!(split.Ratio > 0 && split.Ratio < 1))
                    return "split ratio is outside (0, 1)";
                if (!Enum.IsDefined(split.Axis))
                    return "unknown split axis";
                if (string.IsNullOrEmpty(split.Id) || !splitIds.Add(split.Id))
                    return "split id is empty or duplicated";
                if (TryParseSplitNumber(split.Id, out int number) && number > LastSplitId)
                    return "split id is beyond lastSplitId";
                return ValidateNode(split.A, views, splitIds) ?? ValidateNode(split.B, views, splitIds);
            default:
                return "unknown node kind";
        }
    }

    private static bool TryParseSplitNumber(string id, out int number)
    {
        number = 0;
        return id.StartsWith(SplitIdPrefix, StringComparison.Ordinal)
            && int.TryParse(id.AsSpan(SplitIdPrefix.Length), NumberStyles.None, CultureInfo.InvariantCulture, out number);
    }

    // ---- operations ----

    /// <summary>Removes the panel's leaf; its sibling takes the freed area.</summary>
    public WorkspaceLayout RemoveView(string view)
    {
        LayoutNode? tree = MapLeaf(Tree, view, _ => null);
        return ReferenceEquals(tree, Tree) ? this : this with { Tree = tree };
    }

    /// <summary>
    /// Moves <paramref name="source"/> next to <paramref name="target"/> on the
    /// given edge by splitting the target's area (50/50). Rejected — the same
    /// instance is returned — when the source is pinned, equals the target, or
    /// either panel is not in the layout, or the edge is not defined.
    /// </summary>
    public WorkspaceLayout DockView(string source, string target, DockEdge edge, IReadOnlyCollection<string>? pinned = null)
    {
        IReadOnlyList<string> views = Views;
        if ((pinned is not null && pinned.Contains(source, StringComparer.Ordinal))
            || string.Equals(source, target, StringComparison.Ordinal)
            || !views.Contains(source, StringComparer.Ordinal)
            || !views.Contains(target, StringComparer.Ordinal)
            || !Enum.IsDefined(edge))
        {
            return this;
        }

        LayoutNode? withoutSource = MapLeaf(Tree, source, _ => null);
        bool before = edge is DockEdge.Left or DockEdge.Top;
        SplitAxis axis = edge is DockEdge.Left or DockEdge.Right ? SplitAxis.Horizontal : SplitAxis.Vertical;
        var ids = new IdSource(LastSplitId);
        LayoutNode? tree = MapLeaf(withoutSource, target, node =>
            before
                ? NewSplit(ids, new LeafNode(source), node, axis, 0.5)
                : NewSplit(ids, node, new LeafNode(source), axis, 0.5));
        return new WorkspaceLayout(tree, ids.Last);
    }

    /// <summary>
    /// Sets the ratio of the split with the given id, clamped to
    /// [<see cref="MinRatio"/>, <see cref="MaxRatio"/>]. Unknown ids and
    /// non-finite ratios leave the layout unchanged (same instance).
    /// </summary>
    public WorkspaceLayout ResizeSplit(string splitId, double ratio)
    {
        if (double.IsNaN(ratio))
            return this;
        double clamped = Math.Max(MinRatio, Math.Min(MaxRatio, ratio));
        LayoutNode? tree = Visit(Tree);
        return ReferenceEquals(tree, Tree) ? this : this with { Tree = tree };

        LayoutNode? Visit(LayoutNode? node)
        {
            if (node is not SplitNode split)
                return node;
            double newRatio = string.Equals(split.Id, splitId, StringComparison.Ordinal) ? clamped : split.Ratio;
            LayoutNode a = Visit(split.A)!;
            LayoutNode b = Visit(split.B)!;
            if (newRatio == split.Ratio && ReferenceEquals(a, split.A) && ReferenceEquals(b, split.B))
                return split;
            return split with { Ratio = newRatio, A = a, B = b };
        }
    }

    /// <summary>
    /// Brings the tree in line with a visibility change: panels that left
    /// <paramref name="previousVisible"/> are removed, panels that joined
    /// <paramref name="nextVisible"/> are placed by the prototype's rules
    /// (Lua beside Node; tools stacked in a shared column; Edit under Game;
    /// anything else splits the whole workspace along <paramref name="axis"/>).
    /// </summary>
    public WorkspaceLayout Sync(
        IReadOnlyList<string> previousVisible,
        IReadOnlyList<string> nextVisible,
        SplitAxis axis = SplitAxis.Horizontal)
    {
        var ids = new IdSource(LastSplitId);
        LayoutNode? tree = Tree;

        foreach (string view in previousVisible.Where(v => !nextVisible.Contains(v, StringComparer.Ordinal)))
            tree = MapLeaf(tree, view, _ => null);

        foreach (string view in nextVisible.Where(v => !previousVisible.Contains(v, StringComparer.Ordinal)))
            tree = PlaceView(tree, view, axis, ids);

        // Mobile start (stacked, 1-2 visible before): three panels share the area equally.
        if (axis == SplitAxis.Vertical
            && previousVisible.Count <= 2
            && Leaves(tree).Count == 3
            && tree is SplitNode root
            && root.A is LeafNode { View: PanelIds.Node })
        {
            tree = root with { Ratio = 1d / 3d };
        }

        return ReferenceEquals(tree, Tree) && ids.Last == LastSplitId ? this : new WorkspaceLayout(tree, ids.Last);
    }

    /// <summary><see cref="Sync(IReadOnlyList{string}, IReadOnlyList{string}, SplitAxis)"/> driven by two view states.</summary>
    public WorkspaceLayout Sync(WorkspaceViewState previous, WorkspaceViewState next, SplitAxis axis = SplitAxis.Horizontal) =>
        Sync(previous.Visible, next.Visible, axis);

    private static LayoutNode? PlaceView(LayoutNode? tree, string view, SplitAxis axis, IdSource ids)
    {
        var leaf = new LeafNode(view);

        if (view == PanelIds.Lua && tree is not null)
        {
            IReadOnlyList<string> views = Leaves(tree);
            string target = views.Contains(PanelIds.Node) ? PanelIds.Node : views[0];
            SplitAxis luaAxis = axis == SplitAxis.Horizontal && views.Count == 1 ? SplitAxis.Horizontal : SplitAxis.Vertical;
            return MapLeaf(tree, target, node => NewSplit(ids, node, leaf, luaAxis, 0.42));
        }

        if (PanelIds.IsTool(view) && tree is not null)
        {
            // Stack onto the first subtree that holds only (non-Lua) tools; otherwise open a new column.
            bool inserted = false;
            LayoutNode result = AddTool(tree);
            if (!inserted)
            {
                double ratio = axis == SplitAxis.Horizontal ? 0.74 : view == PanelIds.Library ? 0.4 : 0.6;
                result = NewSplit(ids, result, leaf, axis, ratio);
            }
            return result;

            LayoutNode AddTool(LayoutNode node)
            {
                IReadOnlyList<string> views = Leaves(node);
                if (!inserted && views.All(v => v != PanelIds.Lua && PanelIds.IsTool(v)))
                {
                    inserted = true;
                    return NewSplit(ids, node, leaf, SplitAxis.Vertical, Math.Min(MaxRatio, (double)views.Count / (views.Count + 1)));
                }
                if (node is not SplitNode split)
                    return node;
                LayoutNode a = AddTool(split.A);
                LayoutNode b = AddTool(split.B);
                return ReferenceEquals(a, split.A) && ReferenceEquals(b, split.B) ? split : split with { A = a, B = b };
            }
        }

        if (view == PanelIds.Edit && Leaves(tree).Contains(PanelIds.Game))
            return MapLeaf(tree, PanelIds.Game, node => NewSplit(ids, node, leaf, SplitAxis.Vertical, 0.5));

        return tree is not null ? NewSplit(ids, tree, leaf, axis, 0.5) : leaf;
    }

    /// <summary>
    /// Replaces the leaf holding <paramref name="view"/> with
    /// <paramref name="transform"/>'s result (<c>null</c> removes it, the
    /// sibling takes over). Untouched subtrees keep their instance.
    /// </summary>
    private static LayoutNode? MapLeaf(LayoutNode? tree, string view, Func<LeafNode, LayoutNode?> transform)
    {
        switch (tree)
        {
            case null:
                return null;
            case LeafNode leaf:
                return string.Equals(leaf.View, view, StringComparison.Ordinal) ? transform(leaf) : leaf;
            case SplitNode split:
                LayoutNode? a = MapLeaf(split.A, view, transform);
                LayoutNode? b = MapLeaf(split.B, view, transform);
                if (a is null)
                    return b;
                if (b is null)
                    return a;
                return ReferenceEquals(a, split.A) && ReferenceEquals(b, split.B) ? split : split with { A = a, B = b };
            default:
                return tree;
        }
    }

    private static SplitNode NewSplit(IdSource ids, LayoutNode a, LayoutNode b, SplitAxis axis, double ratio) =>
        new(ids.Next(), axis, ratio, a, b);

    /// <summary>Hands out <c>split-N</c> ids, continuing from the layout's persisted counter.</summary>
    private sealed class IdSource(int last)
    {
        public int Last { get; private set; } = last;

        public string Next()
        {
            Last++;
            return SplitIdPrefix + Last.ToString(CultureInfo.InvariantCulture);
        }
    }
}
