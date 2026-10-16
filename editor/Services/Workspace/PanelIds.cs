using System;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D1 — stable panel identifiers of the dockable workspace. These are
/// persisted in saved layouts and compared ordinally, so they are never
/// localized; display names come from the editor string catalog.
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

    /// <summary>The three main work areas.</summary>
    public static readonly string[] Core = { Node, Game, Edit };

    /// <summary>Tool panels (side column / bottom strip candidates).</summary>
    public static readonly string[] Tools = { Hierarchy, Inspector, Assets, Console, Lua, Library, Store };

    /// <summary>Every panel, in canonical order (<see cref="Core"/> then <see cref="Tools"/>).</summary>
    public static readonly string[] All = { Node, Game, Edit, Hierarchy, Inspector, Assets, Console, Lua, Library, Store };

    public static bool IsKnown(string? id) => id is not null && Array.IndexOf(All, id) >= 0;

    public static bool IsTool(string? id) => id is not null && Array.IndexOf(Tools, id) >= 0;
}
