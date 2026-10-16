using System.Collections.Generic;
using System.Linq;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D1 — pure panel visibility state (port of the prototype's
/// <c>workspace-state.mjs</c>): which panels are open, which are pinned
/// (locked against drag-moves), the focused panel and the focus history.
/// Immutable; every operation returns the same instance when nothing changes.
/// </summary>
public sealed record WorkspaceViewState(
    IReadOnlyList<string> Visible,
    IReadOnlyList<string> Pinned,
    string? Focused,
    IReadOnlyList<string> Recent)
{
    /// <summary>Only the Node graph open and focused.</summary>
    public static WorkspaceViewState Initial() =>
        new(new[] { PanelIds.Node }, new string[0], PanelIds.Node, new[] { PanelIds.Node });

    public bool IsVisible(string view) => Visible.Contains(view, System.StringComparer.Ordinal);

    public WorkspaceViewState FocusView(string view)
    {
        if (!IsVisible(view))
            return this;
        return this with
        {
            Focused = view,
            Recent = Recent.Where(v => v != view).Append(view).ToArray(),
        };
    }

    public WorkspaceViewState OpenView(string view)
    {
        if (!PanelIds.IsKnown(view))
            return this;
        if (IsVisible(view))
            return FocusView(view);
        return (this with { Visible = Visible.Append(view).ToArray() }).FocusView(view);
    }

    public WorkspaceViewState CloseView(string view)
    {
        if (!IsVisible(view))
            return this;
        string[] visible = Visible.Where(v => v != view).ToArray();
        string[] recent = Recent.Where(v => v != view).ToArray();
        string? focused = Focused is not null && visible.Contains(Focused)
            ? Focused
            : recent.Reverse().FirstOrDefault(v => visible.Contains(v));
        return this with
        {
            Visible = visible,
            Recent = recent,
            Focused = focused,
            Pinned = Pinned.Where(v => v != view).ToArray(),
        };
    }

    public WorkspaceViewState ToggleView(string view) => IsVisible(view) ? CloseView(view) : OpenView(view);

    public WorkspaceViewState TogglePin(string view)
    {
        if (!IsVisible(view))
            return this;
        string[] pinned = Pinned.Contains(view)
            ? Pinned.Where(v => v != view).ToArray()
            : Pinned.Append(view).ToArray();
        return (this with { Pinned = pinned }).FocusView(view);
    }
}
