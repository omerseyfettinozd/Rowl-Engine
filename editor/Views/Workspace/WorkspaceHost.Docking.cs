using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Automation;
using Avalonia.Controls;
using Avalonia.Controls.Presenters;
using Avalonia.Data;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services.Strings;
using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Views.Workspace;

/// <summary>A header drag (or the "Move" menu) asks to dock a panel next to another.</summary>
public sealed class WorkspacePanelDockRequestedEventArgs(string sourcePanelId, string targetPanelId, DockEdge edge) : EventArgs
{
    public string SourcePanelId { get; } = sourcePanelId;

    public string TargetPanelId { get; } = targetPanelId;

    /// <summary>Logical edge of the target (tree order; see <see cref="DockEdge"/>).</summary>
    public DockEdge Edge { get; } = edge;
}

/// <summary>The pin button / menu asks to pin or unpin a panel.</summary>
public sealed class WorkspacePanelPinRequestedEventArgs(string panelId, bool pinned) : EventArgs
{
    public string PanelId { get; } = panelId;

    public bool Pinned { get; } = pinned;
}

/// <summary>The close button / menu asks to close a panel.</summary>
public sealed class WorkspacePanelCloseRequestedEventArgs(string panelId) : EventArgs
{
    public string PanelId { get; } = panelId;
}

/// <summary>
/// Where a dragged panel would land: the target, the logical edge sent to the
/// owner, the edge as seen on screen (label) and the highlighted half of the
/// target (host layout coordinates).
/// </summary>
public sealed record WorkspaceDropTarget(string TargetPanelId, DockEdge Edge, DockEdge VisualEdge, Rect PreviewRect);

/// <summary>
/// Faz 5 D4 — moving panels by their header.
/// <list type="bullet">
/// <item>Press on a header's free area (not a button / tab / input inside it),
/// move more than <see cref="DragThreshold"/> px (|dx|+|dy|): the drag starts,
/// the panel's name follows the pointer and the half of the panel under the
/// pointer nearest to it is highlighted (prototype <c>dropTarget</c>). Release
/// over it: <see cref="PanelDockRequested"/>. Release over the dragged panel or
/// outside the workspace, Escape, or a lost capture: nothing changes.</item>
/// <item>Pinned panels do not start a drag; the owner's
/// <c>WorkspaceLayout.DockView</c> rejects them as well.</item>
/// <item>The pointer is measured in the host's layout space, so in a mirrored
/// right-to-left window the logical edge already matches what is under the
/// pointer; labels and menu entries use the visual edge
/// (<see cref="WorkspaceDockEdges"/>).</item>
/// <item>Keyboard / menu alternative: the header is focusable; Enter / Space on
/// it, Shift+F10, the Menu key or a right click open the panel menu: Move →
/// edge → target panel, Pin, Close.</item>
/// </list>
/// The host never edits the tree: requests go to the owner, the new tree and
/// pinned set flow back through <see cref="Layout"/> / <see cref="PinnedPanels"/>.
/// </summary>
public partial class WorkspaceHost
{
    /// <summary>Pointer travel (|dx|+|dy|, px) below which a header press stays a click.</summary>
    public const double DragThreshold = 4;

    /// <summary>Pinned panels (the owner's state; drawn, and locked against header drags).</summary>
    public static readonly StyledProperty<IReadOnlyCollection<string>?> PinnedPanelsProperty =
        AvaloniaProperty.Register<WorkspaceHost, IReadOnlyCollection<string>?>(nameof(PinnedPanels));

    private const int OverlayZIndex = 1000;
    private static readonly Point DragLabelOffset = new(14, 14);

    private readonly Dictionary<string, Control> _preparedHeaders = new(StringComparer.Ordinal);
    private Panel _dropPreview = null!;
    private TextBlock _dropText = null!;
    private Border _dragLabel = null!;
    private TextBlock _dragText = null!;
    private DragGesture? _gesture;
    private ContextMenu? _panelMenu;
    private TopLevel? _escapeScope;

    public IReadOnlyCollection<string>? PinnedPanels
    {
        get => GetValue(PinnedPanelsProperty);
        set => SetValue(PinnedPanelsProperty, value);
    }

    /// <summary>A header drag moved a panel onto another panel's edge.</summary>
    public event EventHandler<WorkspacePanelDockRequestedEventArgs>? PanelDockRequested;

    /// <summary>The pin button or menu was used.</summary>
    public event EventHandler<WorkspacePanelPinRequestedEventArgs>? PanelPinRequested;

    /// <summary>The close button or menu was used (never raised for the last panel).</summary>
    public event EventHandler<WorkspacePanelCloseRequestedEventArgs>? PanelCloseRequested;

    /// <summary>A header drag is past the threshold.</summary>
    public bool IsDragging => _gesture?.Dragging == true;

    /// <summary>Current drop target of the running drag (<c>null</c> when none / not dragging).</summary>
    public WorkspaceDropTarget? CurrentDropTarget => _gesture is { Dragging: true } g ? g.Drop : null;

    /// <summary>The drop highlight (shown while a drag is over a target).</summary>
    public Control DropPreview => _dropPreview;

    /// <summary>Label of the drop highlight ("Sola yerleştir" …).</summary>
    public TextBlock DropPreviewLabel => _dropText;

    /// <summary>The dragged panel's name next to the pointer.</summary>
    public Control DragLabel => _dragLabel;

    /// <summary>The open panel menu, or <c>null</c>.</summary>
    public ContextMenu? PanelMenu => _panelMenu is { IsOpen: true } ? _panelMenu : null;

    private void InitializeDocking()
    {
        var fill = new Border();
        fill.Classes.Add("workspaceDropFill");
        var outline = new Border();
        outline.Classes.Add("workspaceDropOutline");
        _dropText = new TextBlock();
        var label = new Border
        {
            Child = _dropText,
            HorizontalAlignment = HorizontalAlignment.Center,
            VerticalAlignment = VerticalAlignment.Center,
        };
        label.Classes.Add("workspaceDropLabel");
        _dropPreview = new Panel { IsHitTestVisible = false, IsVisible = false, ZIndex = OverlayZIndex };
        _dropPreview.Children.Add(fill);
        _dropPreview.Children.Add(outline);
        _dropPreview.Children.Add(label);

        _dragText = new TextBlock();
        _dragLabel = new Border { Child = _dragText, IsHitTestVisible = false, IsVisible = false, ZIndex = OverlayZIndex + 1 };
        _dragLabel.Classes.Add("workspaceDragLabel");

        foreach (Control overlay in new Control[] { _dropPreview, _dragLabel })
        {
            LogicalChildren.Add(overlay);
            VisualChildren.Add(overlay);
        }

        AddHandler(PointerPressedEvent, OnHeaderPointerPressed, RoutingStrategies.Bubble, handledEventsToo: true);
        AddHandler(PointerMovedEvent, OnDragPointerMoved, RoutingStrategies.Bubble, handledEventsToo: true);
        AddHandler(PointerReleasedEvent, OnDragPointerReleased, RoutingStrategies.Bubble, handledEventsToo: true);
        AddHandler(KeyDownEvent, OnHeaderKeyDown, RoutingStrategies.Bubble);
        AddHandler(KeyUpEvent, OnHeaderKeyUp, RoutingStrategies.Bubble);
        AddHandler(ContextRequestedEvent, OnHeaderContextRequested, RoutingStrategies.Bubble);
    }

    // ---- requests (one-way: the owner changes the state) ----

    internal void RequestPinned(string panelId, bool pinned) =>
        PanelPinRequested?.Invoke(this, new WorkspacePanelPinRequestedEventArgs(panelId, pinned));

    internal void RequestClose(string panelId)
    {
        if (FindPanel(panelId) is { CanClose: true })
            PanelCloseRequested?.Invoke(this, new WorkspacePanelCloseRequestedEventArgs(panelId));
    }

    private void RequestDock(string source, string target, DockEdge edge) =>
        PanelDockRequested?.Invoke(this, new WorkspacePanelDockRequestedEventArgs(source, target, edge));

    /// <summary>Pushes the owner's pinned set and "can close" (not the last panel) to the panels.</summary>
    private void UpdatePanelStates()
    {
        IReadOnlyList<string> views = WorkspaceLayout.Leaves(Layout?.Tree);
        IReadOnlyCollection<string>? pinned = PinnedPanels;
        foreach ((string id, WorkspacePanel panel) in _panels)
        {
            bool visible = views.Contains(id, StringComparer.Ordinal);
            panel.IsPinned = visible && pinned is not null && pinned.Contains(id, StringComparer.Ordinal);
            panel.CanClose = visible && views.Count > 1;
        }
        if (_gesture is not null && FindPanel(_gesture.Source) is not { IsVisible: true, IsPinned: false })
            CancelDrag();
    }

    // ---- header regions ----

    private WorkspacePanel? PanelOf(Visual? visual)
    {
        for (Visual? v = visual; v is not null && !ReferenceEquals(v, this); v = v.GetVisualParent())
        {
            if (v is WorkspacePanel panel && ReferenceEquals(panel.Host, this))
                return panel;
        }
        return null;
    }

    /// <summary>The tab row of the bottom strip's <see cref="TabControl"/> (its header).</summary>
    private static ItemsPresenter? TabStripOf(TabControl tabs) =>
        tabs.GetVisualDescendants().OfType<ItemsPresenter>().FirstOrDefault(p => ReferenceEquals(p.TemplatedParent, tabs));

    /// <summary>
    /// The header's area in host layout coordinates: the header element; for a
    /// tab strip, the tab row across the whole strip width (empty space included).
    /// </summary>
    private bool TryGetHeaderRect(WorkspacePanel panel, out Rect rect)
    {
        rect = default;
        Control? header = panel.PanelId is { } id ? FindHeader(id) : null;
        if (header is TabControl tabs)
        {
            ItemsPresenter? strip = TabStripOf(tabs);
            if (strip is null || strip.TranslatePoint(default, this) is not { } stripAt || tabs.TranslatePoint(default, this) is not { } tabsAt)
                return false;
            rect = new Rect(tabsAt.X, stripAt.Y, tabs.Bounds.Width, strip.Bounds.Height);
            return true;
        }
        if (header is null || !header.IsEffectivelyVisible || header.TranslatePoint(default, this) is not { } at)
            return false;
        rect = new Rect(at, header.Bounds.Size);
        return true;
    }

    /// <summary><paramref name="visual"/> is the header or inside it (a tab strip: inside its tab row).</summary>
    private static bool IsWithinHeader(Visual visual, Control header)
    {
        Visual? root = header is TabControl tabs ? TabStripOf(tabs) : header;
        return root is not null && (ReferenceEquals(visual, root) || root.IsVisualAncestorOf(visual));
    }

    /// <summary>
    /// The press landed on something with its own pointer behaviour inside the
    /// header — a button, tab, text box… (any focusable element between the
    /// source and the header): it keeps working and no drag starts.
    /// </summary>
    private static bool IsInteractive(Visual source, Control header, WorkspacePanel panel)
    {
        for (Visual? v = source; v is not null && !ReferenceEquals(v, header) && !ReferenceEquals(v, panel); v = v.GetVisualParent())
        {
            if (v is InputElement { Focusable: true })
                return true;
        }
        return false;
    }

    /// <summary>
    /// Headers drawn by panel content (Hierarchy / Inspector <c>Border.panelHeader</c>)
    /// become keyboard-focusable drag handles with an accessible name, like the host header.
    /// </summary>
    private void PrepareHeaders()
    {
        foreach ((string id, WorkspacePanel panel) in _panels)
        {
            if (!panel.OwnsHeader || !panel.IsVisible || _preparedHeaders.ContainsKey(id))
                continue;
            if (FindHeader(id) is not Border header)
                continue;
            _preparedHeaders[id] = header;
            header.Focusable = true;
            header.Classes.Add("workspaceDragHandle");
            header.Bind(AutomationProperties.NameProperty, panel.GetObservable(WorkspacePanel.TitleProperty));
            header.Bind(AutomationProperties.HelpTextProperty,
                new Binding("[Workspace.Header.Help]") { Source = EditorStrings.Instance, Mode = BindingMode.OneWay });
        }
    }

    // ---- drag gesture ----

    private void OnHeaderPointerPressed(object? sender, PointerPressedEventArgs e)
    {
        if (_gesture is not null || e.Source is not Visual source)
            return;
        if (!e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            return;
        WorkspacePanel? panel = PanelOf(source);
        if (panel?.PanelId is not { } id || FindHeader(id) is not { } header)
            return;
        Point p = e.GetPosition(this);
        if (!TryGetHeaderRect(panel, out Rect headerRect) || !headerRect.Contains(p) || IsInteractive(source, header, panel))
            return;

        if (header.Focusable)
            header.Focus(NavigationMethod.Pointer);
        if (panel.IsPinned)
            return; // pinned: the header is not a drag handle

        _gesture = new DragGesture(id, e.Pointer, p);
        e.Pointer.Capture(this);
        _escapeScope = TopLevel.GetTopLevel(this);
        _escapeScope?.AddHandler(KeyDownEvent, OnDragEscape, RoutingStrategies.Tunnel, handledEventsToo: true);
        e.Handled = true;
    }

    private void OnDragPointerMoved(object? sender, PointerEventArgs e)
    {
        DragGesture? g = _gesture;
        if (g is null || !ReferenceEquals(e.Pointer, g.Pointer))
            return;
        Point p = e.GetPosition(this);
        g.Last = p;
        if (!g.Dragging)
        {
            if (Math.Abs(p.X - g.Start.X) + Math.Abs(p.Y - g.Start.Y) <= DragThreshold)
                return;
            g.Dragging = true;
            _dragText.Text = TitleOf(g.Source);
            _dragLabel.IsVisible = true;
        }
        g.Drop = new Rect(Bounds.Size).Contains(p) ? DropAt(p, g.Source) : null;
        _dropPreview.IsVisible = g.Drop is not null;
        NoteDropTarget(g.Drop?.PreviewRect);
        if (g.Drop is { } drop)
            _dropText.Text = EditorStrings.T(WorkspaceDockEdges.DropLabelKey(drop.VisualEdge));
        InvalidateArrange();
        e.Handled = true;
    }

    private void OnDragPointerReleased(object? sender, PointerReleasedEventArgs e)
    {
        DragGesture? g = _gesture;
        if (g is null || !ReferenceEquals(e.Pointer, g.Pointer))
            return;
        EndDrag();
        if (g.Dragging)
        {
            e.Handled = true;
            if (g.Drop is { } drop)
                RequestDock(g.Source, drop.TargetPanelId, drop.Edge);
        }
    }

    protected override void OnPointerCaptureLost(PointerCaptureLostEventArgs e)
    {
        base.OnPointerCaptureLost(e);
        if (_gesture is not null && ReferenceEquals(e.Pointer, _gesture.Pointer))
            CancelDrag();
    }

    private void OnDragEscape(object? sender, KeyEventArgs e)
    {
        if (_gesture is null || e.Key != Key.Escape)
            return;
        CancelDrag();
        e.Handled = true;
    }

    /// <summary>Stops a header drag without changing anything (Escape, lost capture, panel gone).</summary>
    public void CancelDrag() => EndDrag();

    private void EndDrag()
    {
        DragGesture? g = _gesture;
        if (g is null)
            return;
        _gesture = null; // first: releasing the capture below must not re-enter
        _dropPreview.IsVisible = false;
        _dropPreview.Opacity = 1;
        NoteDropTarget(null);
        _dragLabel.IsVisible = false;
        if (ReferenceEquals(g.Pointer.Captured, this))
            g.Pointer.Capture(null);
        _escapeScope?.RemoveHandler(KeyDownEvent, OnDragEscape);
        _escapeScope = null;
        InvalidateArrange();
    }

    /// <summary>
    /// The panel under <paramref name="p"/> (not the dragged one) and its edge
    /// nearest to the pointer (prototype <c>dropTarget</c>: smallest of the
    /// relative distances left, right, top, bottom; ties in that order).
    /// </summary>
    private WorkspaceDropTarget? DropAt(Point p, string source)
    {
        bool rtl = FlowDirection == FlowDirection.RightToLeft;
        foreach (string id in WorkspaceLayout.Leaves(Layout?.Tree))
        {
            if (string.Equals(id, source, StringComparison.Ordinal)
                || !_panelRects.TryGetValue(id, out Rect r)
                || r.Width <= 0 || r.Height <= 0 || !r.Contains(p))
            {
                continue;
            }
            double px = (p.X - r.X) / r.Width;
            double py = (p.Y - r.Y) / r.Height;
            DockEdge edge = DockEdge.Left;
            double best = px;
            if (1 - px < best) { best = 1 - px; edge = DockEdge.Right; }
            if (py < best) { best = py; edge = DockEdge.Top; }
            if (1 - py < best) { edge = DockEdge.Bottom; }
            Rect preview = edge switch
            {
                DockEdge.Left => new Rect(r.X, r.Y, r.Width / 2, r.Height),
                DockEdge.Right => new Rect(r.X + r.Width / 2, r.Y, r.Width / 2, r.Height),
                DockEdge.Top => new Rect(r.X, r.Y, r.Width, r.Height / 2),
                _ => new Rect(r.X, r.Y + r.Height / 2, r.Width, r.Height / 2),
            };
            return new WorkspaceDropTarget(id, edge, WorkspaceDockEdges.ToVisual(edge, rtl), preview);
        }
        return null;
    }

    private string TitleOf(string panelId) =>
        FindPanel(panelId)?.Title is { Length: > 0 } title ? title : panelId;

    private void MeasureDockingOverlays(Size available)
    {
        _dropPreview.Measure(CurrentDropTarget?.PreviewRect.Size ?? default);
        _dragLabel.Measure(available);
    }

    private void ArrangeDockingOverlays()
    {
        WorkspaceDropTarget? drop = CurrentDropTarget;
        Rect shown = drop is null ? default : ShownDropRect(drop.PreviewRect);
        _dropPreview.Measure(shown.Size);
        _dropPreview.Arrange(shown);
        if (_gesture is { Dragging: true } g)
            _dragLabel.Arrange(new Rect(g.Last + DragLabelOffset, _dragLabel.DesiredSize));
        else
            _dragLabel.Arrange(default);
    }

    // ---- keyboard / context menu ----

    private void OnHeaderKeyDown(object? sender, KeyEventArgs e)
    {
        bool menuKey = e.Key == Key.F10 && e.KeyModifiers == KeyModifiers.Shift;
        bool activate = e.Key is Key.Enter or Key.Space && e.KeyModifiers == KeyModifiers.None;
        if (menuKey || activate)
            HandleHeaderMenuKey(e, activate);
        else if (e.Key == Key.Apps && !e.Handled && PanelMenu is null && IsHeaderKeySource(e.Source))
            e.Handled = true; // opened on key up (the open menu would close itself on the Menu key's release)
    }

    private void OnHeaderKeyUp(object? sender, KeyEventArgs e)
    {
        if (e.Key == Key.Apps)
            HandleHeaderMenuKey(e, activate: false);
    }

    private bool IsHeaderKeySource(object? source) =>
        source is Visual visual
        && PanelOf(visual) is { PanelId: { } id }
        && FindHeader(id) is { } header
        && (ReferenceEquals(visual, header) || IsWithinHeader(visual, header));

    private void HandleHeaderMenuKey(KeyEventArgs e, bool activate)
    {
        if (e.Handled || e.Source is not Visual source)
            return;
        WorkspacePanel? panel = PanelOf(source);
        if (panel?.PanelId is not { } id || FindHeader(id) is not { } header)
            return;
        bool onHeader = ReferenceEquals(source, header);
        if (activate ? !onHeader : !(onHeader || IsWithinHeader(source, header)))
            return;
        OpenPanelMenu(id, source as Control ?? header);
        e.Handled = true;
    }

    private void OnHeaderContextRequested(object? sender, ContextRequestedEventArgs e)
    {
        if (e.Handled || e.Source is not Visual source)
            return;
        WorkspacePanel? panel = PanelOf(source);
        if (panel?.PanelId is not { } id || FindHeader(id) is not { } header)
            return;
        bool inside = IsWithinHeader(source, header)
            || (e.TryGetPosition(this, out Point p) && TryGetHeaderRect(panel, out Rect rect) && rect.Contains(p));
        if (!inside)
            return;
        OpenPanelMenu(id, source as Control ?? header);
        e.Handled = true;
    }

    /// <summary>
    /// Opens the panel menu (keyboard / right-click alternative to dragging):
    /// Move → visual edge → target panel, Pin, Close. Returns the open menu, or
    /// <c>null</c> for a panel that is not shown.
    /// </summary>
    public ContextMenu? OpenPanelMenu(string panelId, Control? placementTarget = null)
    {
        if (FindPanel(panelId) is not { IsVisible: true } panel)
            return null;
        if (PanelMenu is { } open)
        {
            if (ReferenceEquals(open.Tag, panelId))
                return open;
            open.Close();
        }
        ContextMenu menu = BuildPanelMenu(panelId, panel);
        menu.Closed += (_, _) =>
        {
            if (ReferenceEquals(_panelMenu, menu))
                _panelMenu = null;
        };
        _panelMenu = menu;
        menu.Open(placementTarget ?? FindHeader(panelId) ?? panel);
        return menu;
    }

    private ContextMenu BuildPanelMenu(string id, WorkspacePanel panel)
    {
        bool rtl = FlowDirection == FlowDirection.RightToLeft;
        bool pinned = panel.IsPinned;
        string[] targets = WorkspaceLayout.Leaves(Layout?.Tree)
            .Where(v => !string.Equals(v, id, StringComparison.Ordinal) && _panels.ContainsKey(v))
            .ToArray();
        bool canMove = !pinned && targets.Length > 0;

        var move = new MenuItem { Header = EditorStrings.T("Workspace.Move"), Tag = "move", IsEnabled = canMove };
        foreach (DockEdge visual in WorkspaceDockEdges.VisualOrder)
        {
            DockEdge logical = WorkspaceDockEdges.FromVisual(visual, rtl);
            var edgeItem = new MenuItem { Header = EditorStrings.T(WorkspaceDockEdges.MoveMenuKey(visual)), Tag = visual, IsEnabled = canMove };
            foreach (string target in targets)
            {
                var item = new MenuItem { Header = TitleOf(target), Tag = target };
                item.Click += (_, _) => RequestDock(id, target, logical);
                edgeItem.Items.Add(item);
            }
            move.Items.Add(edgeItem);
        }

        var pin = new MenuItem
        {
            Header = EditorStrings.T("Workspace.Pin"),
            Tag = "pin",
            ToggleType = MenuItemToggleType.CheckBox,
            IsChecked = pinned,
        };
        pin.Click += (_, _) => RequestPinned(id, !pinned);

        var close = new MenuItem { Header = EditorStrings.T("Workspace.Close"), Tag = "close", IsEnabled = panel.CanClose };
        close.Click += (_, _) => RequestClose(id);

        var menu = new ContextMenu { Tag = id };
        menu.Classes.Add("workspacePanelMenu");
        menu.Items.Add(move);
        menu.Items.Add(pin);
        menu.Items.Add(new Separator());
        menu.Items.Add(close);
        return menu;
    }

    private sealed class DragGesture(string source, IPointer pointer, Point start)
    {
        public string Source { get; } = source;

        public IPointer Pointer { get; } = pointer;

        public Point Start { get; } = start;

        public Point Last { get; set; } = start;

        public bool Dragging { get; set; }

        public WorkspaceDropTarget? Drop { get; set; }
    }
}
