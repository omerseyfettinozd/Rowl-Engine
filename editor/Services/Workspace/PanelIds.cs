using System;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D1 — stable panel identifiers of the dockable workspace. These are
/// persisted in saved layouts and compared ordinally, so they are never
/// localized; display names come from the editor string catalog.
/// <para>
/// Faz 5 D2 — real editor panels: <see cref="Node"/> (NodeGraphView),
/// <see cref="Game"/> (EnginePreviewControl, "Oyun"), <see cref="Edit"/>
/// (LivePreviewControl, "Sahne Düzenle"), <see cref="Hierarchy"/>,
/// <see cref="Inspector"/> and <see cref="Bottom"/> (the tabbed bottom strip:
/// Assets / Log / Backlog / Save Slots / Issues). <see cref="Assets"/> and
/// <see cref="Console"/> name bottom-strip tabs that may become their own leaves
/// (D3 decision); <see cref="Lua"/>, <see cref="Library"/> and
/// <see cref="Store"/> were prototype-only (Faz 7: Library and Lua became real panels; Faz 8: Store too) and are kept so
/// the prototype layout rules and their ported tests stay intact.
/// </para>
/// </summary>
public static class PanelIds
{
    public const string Node = "node";
    public const string Game = "game";
    public const string Edit = "edit";
    public const string Hierarchy = "hierarchy";
    public const string Inspector = "inspector";
    public const string Assets = "assets";
    public const string Console = "console";
    public const string Lua = "lua";
    public const string Library = "library";
    public const string Store = "store";

    /// <summary>D2: the tabbed bottom strip (one leaf; its tabs live in <c>BottomStripState</c>).</summary>
    public const string Bottom = "bottom";

    /// <summary>The three main work areas.</summary>
    public static readonly string[] Core = { Node, Game, Edit };

    /// <summary>Tool panels (side column / bottom strip candidates).</summary>
    public static readonly string[] Tools = { Hierarchy, Inspector, Assets, Console, Lua, Library, Store, Bottom };

    /// <summary>Every panel, in canonical order (<see cref="Core"/> then <see cref="Tools"/>).</summary>
    public static readonly string[] All = { Node, Game, Edit, Hierarchy, Inspector, Assets, Console, Lua, Library, Store, Bottom };

    /// <summary>D2: panels the editor window actually hosts today (Faz 7: + Library, Lua; Faz 8: + Store).</summary>
    public static readonly string[] Editor = { Node, Game, Edit, Hierarchy, Inspector, Library, Lua, Store, Bottom };

    public static bool IsKnown(string? id) => id is not null && Array.IndexOf(All, id) >= 0;

    public static bool IsTool(string? id) => id is not null && Array.IndexOf(Tools, id) >= 0;
}
