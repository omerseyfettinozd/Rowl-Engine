namespace RowlEngine.Editor.Services.Workspace;

/// <summary>Direction a <see cref="SplitNode"/> divides its area (prototype axis "x" / "y").</summary>
public enum SplitAxis
{
    /// <summary>Children side by side (A left, B right).</summary>
    Horizontal,

    /// <summary>Children stacked (A top, B bottom).</summary>
    Vertical,
}

/// <summary>
/// Edge of a target panel that a dragged panel docks to.
/// <para>
/// Faz 5 D4: the edge is <b>logical</b> (tree order), not visual: <see cref="Left"/>
/// / <see cref="Top"/> put the source in the split's first side (A), <see cref="Right"/>
/// / <see cref="Bottom"/> in the second (B). In a left-to-right window that is the
/// visual left / right; in a right-to-left window the drawn workspace is mirrored,
/// so <see cref="Left"/> appears on the visual right. The view converts visual
/// edges (pointer position, menu labels) with <c>WorkspaceDockEdges.FromVisual</c>.
/// </para>
/// </summary>
public enum DockEdge
{
    Left,
    Right,
    Top,
    Bottom,
}

/// <summary>
/// Faz 5 D1 — node of the binary split tree. Immutable records: equality is
/// structural, so two layouts with the same shape and ratios compare equal.
/// </summary>
public abstract record LayoutNode;

/// <summary>Leaf: one panel (identifier from <see cref="PanelIds"/>).</summary>
public sealed record LeafNode(string View) : LayoutNode;

/// <summary>
/// Split: divides its area between <see cref="A"/> (first / left / top) and
/// <see cref="B"/>. <see cref="Ratio"/> is A's share of the area.
/// </summary>
public sealed record SplitNode(string Id, SplitAxis Axis, double Ratio, LayoutNode A, LayoutNode B) : LayoutNode;
