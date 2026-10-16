using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Views.Workspace;

/// <summary>
/// Faz 5 D4 — visual ↔ logical dock edges. <see cref="DockEdge"/> in the model is
/// logical (Left = the split's first side); the workspace is drawn mirrored in a
/// right-to-left window, so there the visual left is the logical
/// <see cref="DockEdge.Right"/>. Top / Bottom never change. The mapping is its
/// own inverse.
/// </summary>
public static class WorkspaceDockEdges
{
    /// <summary>Visual edges in menu order (always Left, Right, Top, Bottom as seen on screen).</summary>
    public static readonly DockEdge[] VisualOrder = { DockEdge.Left, DockEdge.Right, DockEdge.Top, DockEdge.Bottom };

    /// <summary>The logical edge for an edge seen on screen.</summary>
    public static DockEdge FromVisual(DockEdge visual, bool rightToLeft) => Swap(visual, rightToLeft);

    /// <summary>The edge seen on screen for a logical edge.</summary>
    public static DockEdge ToVisual(DockEdge logical, bool rightToLeft) => Swap(logical, rightToLeft);

    /// <summary>Drop preview label ("Sola yerleştir" …) for a visual edge.</summary>
    public static string DropLabelKey(DockEdge visual) => "Workspace.Drop." + visual;

    /// <summary>"Move" submenu entry ("Soluna" …) for a visual edge.</summary>
    public static string MoveMenuKey(DockEdge visual) => "Workspace.Move." + visual;

    private static DockEdge Swap(DockEdge edge, bool rightToLeft) => !rightToLeft
        ? edge
        : edge switch
        {
            DockEdge.Left => DockEdge.Right,
            DockEdge.Right => DockEdge.Left,
            _ => edge,
        };
}
