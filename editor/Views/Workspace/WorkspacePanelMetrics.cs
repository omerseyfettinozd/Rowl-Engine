namespace RowlEngine.Editor.Views.Workspace;

/// <summary>
/// Faz 5 D3 — pixel minimums of the editor's workspace panels (K2 desktop
/// density). They keep every panel reachable at any split ratio (the purpose
/// of the prototype's 0.2 ratio bound) and are used by MainWindow's panel
/// catalog and the workspace tests alike.
/// </summary>
public static class WorkspacePanelMetrics
{
    /// <summary>Hierarchy column (default ≈ 200 px at 1440).</summary>
    public const double HierarchyMinWidth = 200;

    /// <summary>Inspector column (default ≈ 280 px at 1440).</summary>
    public const double InspectorMinWidth = 240;

    /// <summary>Node / Game / Sahne Düzenle: two side by side keep the old 520 px center minimum.</summary>
    public const double CenterMinWidth = 320;

    /// <summary>Center views: header (40) + a usable canvas.</summary>
    public const double CenterMinHeight = 160;

    /// <summary>Bottom strip (old MinHeight 140; default ≈ 180 px at 900).</summary>
    public const double BottomMinHeight = 140;
}
