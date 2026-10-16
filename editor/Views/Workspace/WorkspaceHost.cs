using System;
using System.Collections.Generic;
using System.Collections.Specialized;
using System.Linq;
using Avalonia;
using Avalonia.Collections;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Interactivity;
using Avalonia.Metadata;
using Avalonia.VisualTree;
using RowlEngine.Editor.Services.Workspace;

namespace RowlEngine.Editor.Views.Workspace;

/// <summary>Raised when the user moved a splitter: the split's new ratio (A's share).</summary>
public sealed class WorkspaceSplitResizedEventArgs(string splitId, double ratio) : EventArgs
{
    public string SplitId { get; } = splitId;

    public double Ratio { get; } = ratio;
}

/// <summary>
/// Faz 5 D3 — draws the workspace split tree (<see cref="Layout"/>).
/// <para>
/// Two layers, one direction: the <b>skeleton</b> is rebuilt from the tree —
/// nested <see cref="Grid"/>s with star columns/rows (the tree's ratios), 6 px
/// <see cref="GridSplitter"/>s and an empty slot per leaf whose minimum size is
/// the panel's pixel minimum. The <b>panels</b> (<see cref="WorkspacePanel"/>,
/// declared once per id in <see cref="Panels"/>) are direct children of the
/// host that are never removed or re-parented: the host arranges each one over
/// its slot and hides panels that are not in the tree. So a tree change keeps
/// every control instance with its focus, scroll offset and selection, and a
/// panel exists exactly once (one <c>NodeGraphView</c> in split screen too).
/// </para>
/// <para>
/// The view never edits the tree: when a splitter drag (or splitter keyboard
/// move) ends, <see cref="SplitResized"/> reports the new ratio; the owner
/// applies <c>WorkspaceLayout.ResizeSplit</c> and the new tree flows back in.
/// </para>
/// <para>
/// Faz 5 D4: header drag &amp; drop, pin and close — see <c>WorkspaceHost.Docking.cs</c>.
/// </para>
/// </summary>
public partial class WorkspaceHost : Control
{
    /// <summary>Splitter thickness between two areas (px).</summary>
    public const double SplitterThickness = 6;

    public static readonly StyledProperty<WorkspaceLayout?> LayoutProperty =
        AvaloniaProperty.Register<WorkspaceHost, WorkspaceLayout?>(nameof(Layout));

    private readonly Dictionary<string, WorkspacePanel> _panels = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Control> _slots = new(StringComparer.Ordinal);
    private readonly Dictionary<string, SplitParts> _splits = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Rect> _panelRects = new(StringComparer.Ordinal);
    private Control? _skeleton;
    private string? _shape;
    private bool _applying;

    public WorkspaceHost()
    {
        Panels.CollectionChanged += OnPanelsChanged;
        InitializeDocking();
        InitializeMotion();
    }

    /// <summary>The split tree to draw (bind to the view model's layout; read-only for the view).</summary>
    public WorkspaceLayout? Layout
    {
        get => GetValue(LayoutProperty);
        set => SetValue(LayoutProperty, value);
    }

    /// <summary>Panel catalog: one <see cref="WorkspacePanel"/> per panel id.</summary>
    [Content]
    public AvaloniaList<WorkspacePanel> Panels { get; } = new();

    /// <summary>A splitter was moved; the owner writes the ratio to the tree.</summary>
    public event EventHandler<WorkspaceSplitResizedEventArgs>? SplitResized;

    /// <summary>The panel declared for <paramref name="panelId"/>, or <c>null</c>.</summary>
    public WorkspacePanel? FindPanel(string panelId) =>
        _panels.TryGetValue(panelId, out WorkspacePanel? panel) ? panel : null;

    /// <summary>Where the panel was last arranged (host coordinates, before RTL mirroring); empty when hidden.</summary>
    public Rect GetPanelRect(string panelId) =>
        _panelRects.TryGetValue(panelId, out Rect rect) ? rect : default;

    /// <summary>The splitter drawn for split <paramref name="splitId"/>, or <c>null</c>.</summary>
    public GridSplitter? FindSplitter(string splitId) =>
        _splits.TryGetValue(splitId, out SplitParts? parts) ? parts.Splitter : null;

    /// <summary>The skeleton grid drawn for split <paramref name="splitId"/>, or <c>null</c>.</summary>
    public Grid? FindSplitGrid(string splitId) =>
        _splits.TryGetValue(splitId, out SplitParts? parts) ? parts.Grid : null;

    /// <summary>
    /// Header of a panel (D4 drag handle seam): the host header, or for a panel
    /// that owns its header the first <c>Border.panelHeader</c> in its content.
    /// </summary>
    public Control? FindHeader(string panelId)
    {
        WorkspacePanel? panel = FindPanel(panelId);
        if (panel is null)
            return null;
        if (!panel.OwnsHeader)
            return panel.HostHeader;
        if (panel.Content is not Control content)
            return null;
        // Bottom strip: the content is the TabControl; its tab row is the header.
        if (content is TabControl tabs)
            return tabs;
        return content.GetVisualDescendants().OfType<Border>().FirstOrDefault(b => b.Classes.Contains("panelHeader"));
    }

    protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
    {
        base.OnPropertyChanged(change);
        if (change.Property == LayoutProperty)
            Apply();
        else if (change.Property == PinnedPanelsProperty)
            UpdatePanelStates();
        else if (change.Property == FlowDirectionProperty && _gesture is not null)
            CancelDrag();
        else if (change.Property == ReduceMotionProperty)
            OnReduceMotionChanged();
    }

    // ---- panels ----

    private void OnPanelsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e.OldItems is not null)
        {
            foreach (WorkspacePanel panel in e.OldItems)
                Detach(panel);
        }
        if (e.Action == NotifyCollectionChangedAction.Reset)
        {
            foreach (WorkspacePanel panel in VisualChildren.OfType<WorkspacePanel>().ToArray())
                Detach(panel);
        }
        if (e.NewItems is not null)
        {
            foreach (WorkspacePanel panel in e.NewItems)
            {
                LogicalChildren.Add(panel);
                VisualChildren.Add(panel);
                panel.PropertyChanged += OnPanelPropertyChanged;
                panel.Host = this;
            }
        }
        Reindex();
    }

    private void Detach(WorkspacePanel panel)
    {
        panel.PropertyChanged -= OnPanelPropertyChanged;
        panel.Host = null;
        LogicalChildren.Remove(panel);
        VisualChildren.Remove(panel);
    }

    private void OnPanelPropertyChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        if (e.Property == WorkspacePanel.PanelIdProperty
            || e.Property == WorkspacePanel.PanelMinWidthProperty
            || e.Property == WorkspacePanel.PanelMinHeightProperty)
        {
            Reindex();
        }
    }

    /// <summary>Panel id → the one panel declared for it (ordinal ids; a duplicate is a programming error).</summary>
    private void Reindex()
    {
        _panels.Clear();
        foreach (WorkspacePanel panel in Panels)
        {
            if (panel.PanelId is not { } id)
                continue;
            if (!_panels.TryAdd(id, panel))
                throw new InvalidOperationException($"Panel '{id}' is declared twice; a workspace panel exists once.");
        }
        _panelRects.Clear();
        _shape = null; // panel minimums live in the skeleton
        Apply();
    }

    // ---- tree → skeleton ----

    private void Apply()
    {
        CaptureMotion();
        LayoutNode? tree = Layout?.Tree;
        string shape = Shape(tree);
        if (shape == _shape && _skeleton is not null)
        {
            UpdateRatios(tree);
        }
        else
        {
            Rebuild(tree);
            _shape = shape;
        }

        var visible = new HashSet<string>(WorkspaceLayout.Leaves(tree), StringComparer.Ordinal);
        foreach ((string id, WorkspacePanel panel) in _panels)
        {
            bool show = visible.Contains(id);
            panel.IsVisible = show;
            if (!show)
                _panelRects.Remove(id);
        }
        UpdatePanelStates();
        InvalidateMeasure();
        InvalidateArrange();
    }

    private void Rebuild(LayoutNode? tree)
    {
        if (_skeleton is not null)
        {
            LogicalChildren.Remove(_skeleton);
            VisualChildren.Remove(_skeleton);
        }
        _slots.Clear();
        _splits.Clear();
        _skeleton = tree is null ? null : Build(tree);
        if (_skeleton is not null)
        {
            // Index 0: drawn under the panels; splitters never overlap a panel's area.
            LogicalChildren.Insert(0, _skeleton);
            VisualChildren.Insert(0, _skeleton);
        }
    }

    private Control Build(LayoutNode node)
    {
        if (node is LeafNode leaf)
        {
            WorkspacePanel? panel = FindPanel(leaf.View);
            var slot = new Border
            {
                Tag = leaf.View,
                MinWidth = panel?.PanelMinWidth ?? WorkspacePanel.DefaultMinWidth,
                MinHeight = panel?.PanelMinHeight ?? WorkspacePanel.DefaultMinHeight,
            };
            slot.PropertyChanged += OnSkeletonBoundsChanged;
            _slots[leaf.View] = slot;
            return slot;
        }

        var split = (SplitNode)node;
        Control a = Build(split.A);
        Control b = Build(split.B);
        bool columns = split.Axis == SplitAxis.Horizontal;
        var grid = new Grid();
        var splitter = new GridSplitter
        {
            ResizeDirection = columns ? GridResizeDirection.Columns : GridResizeDirection.Rows,
            Tag = split.Id,
        };
        splitter.Classes.Add("workspaceSplitter");
        DefinitionBase defA, defB;
        if (columns)
        {
            var colA = new ColumnDefinition(split.Ratio, GridUnitType.Star) { MinWidth = MinWidthOf(split.A) };
            var colB = new ColumnDefinition(1 - split.Ratio, GridUnitType.Star) { MinWidth = MinWidthOf(split.B) };
            grid.ColumnDefinitions.Add(colA);
            grid.ColumnDefinitions.Add(new ColumnDefinition(SplitterThickness, GridUnitType.Pixel));
            grid.ColumnDefinitions.Add(colB);
            Grid.SetColumn(splitter, 1);
            Grid.SetColumn(b, 2);
            splitter.Width = SplitterThickness;
            defA = colA;
            defB = colB;
        }
        else
        {
            var rowA = new RowDefinition(split.Ratio, GridUnitType.Star) { MinHeight = MinHeightOf(split.A) };
            var rowB = new RowDefinition(1 - split.Ratio, GridUnitType.Star) { MinHeight = MinHeightOf(split.B) };
            grid.RowDefinitions.Add(rowA);
            grid.RowDefinitions.Add(new RowDefinition(SplitterThickness, GridUnitType.Pixel));
            grid.RowDefinitions.Add(rowB);
            Grid.SetRow(splitter, 1);
            Grid.SetRow(b, 2);
            splitter.Height = SplitterThickness;
            defA = rowA;
            defB = rowB;
        }
        grid.Children.Add(a);
        grid.Children.Add(splitter);
        grid.Children.Add(b);
        grid.PropertyChanged += OnSkeletonBoundsChanged;

        var parts = new SplitParts(split.Id, grid, splitter, defA, defB, columns);
        splitter.DragCompleted += (_, _) => Commit(parts);
        // Keyboard moves (arrow keys on a focused splitter) resize without drag events.
        splitter.AddHandler(KeyUpEvent, (_, _) => Commit(parts), RoutingStrategies.Bubble, handledEventsToo: true);
        splitter.AddHandler(KeyDownEvent, (_, e) => OnSplitterKeyDown(parts, e), RoutingStrategies.Tunnel);
        _splits[split.Id] = parts;
        return grid;
    }

    /// <summary>Keyboard step for a focused splitter (px), and with Shift.</summary>
    public const double KeyboardStep = 16;

    public const double KeyboardStepLarge = 64;

    /// <summary>
    /// Arrow keys move the focused splitter by <see cref="KeyboardStep"/> px (Shift:
    /// <see cref="KeyboardStepLarge"/>). The arrow means the <i>screen</i> direction: in a
    /// right-to-left (mirrored) window the divider still follows the arrow it was given.
    /// The ratio is written to the tree on key up, as before (no animation).
    /// </summary>
    private void OnSplitterKeyDown(SplitParts parts, KeyEventArgs e)
    {
        int screen = e.Key switch
        {
            Key.Left or Key.Up => -1,
            Key.Right or Key.Down => 1,
            _ => 0,
        };
        bool along = parts.Columns ? e.Key is Key.Left or Key.Right : e.Key is Key.Up or Key.Down;
        if (screen == 0 || !along)
            return;
        e.Handled = true; // with any modifier: the splitter's own fixed 10 px step must not run
        if (e.KeyModifiers is not (KeyModifiers.None or KeyModifiers.Shift))
            return;
        double step = e.KeyModifiers == KeyModifiers.Shift ? KeyboardStepLarge : KeyboardStep;
        bool mirrored = parts.Columns && parts.Splitter.FlowDirection == Avalonia.Media.FlowDirection.RightToLeft;
        parts.MovePixels(screen * step * (mirrored ? -1 : 1));
    }

    private void UpdateRatios(LayoutNode? node)
    {
        if (node is not SplitNode split)
            return;
        if (_splits.TryGetValue(split.Id, out SplitParts? parts) && Math.Abs(parts.CurrentRatio() - split.Ratio) > 1e-9)
        {
            _applying = true;
            try
            {
                parts.SetRatio(split.Ratio);
            }
            finally
            {
                _applying = false;
            }
        }
        UpdateRatios(split.A);
        UpdateRatios(split.B);
    }

    private void Commit(SplitParts parts)
    {
        if (_applying)
            return;
        double ratio = parts.CurrentRatio();
        if (double.IsNaN(ratio))
            return;
        SplitNode? split = FindSplit(Layout?.Tree, parts.Id);
        if (split is null || Math.Abs(split.Ratio - ratio) <= 1e-9)
            return;
        // The user already moved the splitter: whatever the owner writes back (a clamped ratio)
        // must not be animated (D6: splitter drags and keyboard moves stay instant).
        _suppressMotion = true;
        try
        {
            SplitResized?.Invoke(this, new WorkspaceSplitResizedEventArgs(parts.Id, ratio));
        }
        finally
        {
            _suppressMotion = false;
        }
    }

    private static SplitNode? FindSplit(LayoutNode? node, string id) => node switch
    {
        SplitNode split when string.Equals(split.Id, id, StringComparison.Ordinal) => split,
        SplitNode split => FindSplit(split.A, id) ?? FindSplit(split.B, id),
        _ => null,
    };

    private double MinWidthOf(LayoutNode node) => node switch
    {
        LeafNode leaf => FindPanel(leaf.View)?.PanelMinWidth ?? WorkspacePanel.DefaultMinWidth,
        SplitNode { Axis: SplitAxis.Horizontal } split => MinWidthOf(split.A) + SplitterThickness + MinWidthOf(split.B),
        SplitNode split => Math.Max(MinWidthOf(split.A), MinWidthOf(split.B)),
        _ => 0,
    };

    private double MinHeightOf(LayoutNode node) => node switch
    {
        LeafNode leaf => FindPanel(leaf.View)?.PanelMinHeight ?? WorkspacePanel.DefaultMinHeight,
        SplitNode { Axis: SplitAxis.Vertical } split => MinHeightOf(split.A) + SplitterThickness + MinHeightOf(split.B),
        SplitNode split => Math.Max(MinHeightOf(split.A), MinHeightOf(split.B)),
        _ => 0,
    };

    /// <summary>Tree structure without ratios: a rebuild is needed only when this changes.</summary>
    private static string Shape(LayoutNode? node) => node switch
    {
        null => "-",
        LeafNode leaf => leaf.View,
        SplitNode split => $"{split.Id}:{(split.Axis == SplitAxis.Horizontal ? 'x' : 'y')}({Shape(split.A)},{Shape(split.B)})",
        _ => "?",
    };

    private void OnSkeletonBoundsChanged(object? sender, AvaloniaPropertyChangedEventArgs e)
    {
        // A splitter drag re-arranges only the skeleton grid; the panels must follow.
        if (e.Property == BoundsProperty)
            InvalidateArrange();
    }

    // ---- layout ----

    protected override Size MeasureOverride(Size availableSize)
    {
        _skeleton?.Measure(availableSize);
        foreach ((string id, WorkspacePanel panel) in _panels)
        {
            Size size = _panelRects.TryGetValue(id, out Rect rect) ? rect.Size : availableSize;
            panel.Measure(size);
        }
        MeasureDockingOverlays(availableSize);
        return _skeleton?.DesiredSize ?? default;
    }

    protected override Size ArrangeOverride(Size finalSize)
    {
        _skeleton?.Arrange(new Rect(finalSize));
        ArrangePanelsAndSplitters();
        ArrangeDockingOverlays();
        PrepareHeaders();
        return finalSize;
    }

    /// <summary>Slot bounds in host layout space (sum of layout offsets; RTL mirroring happens above the host).</summary>
    private Rect SlotRect(Control slot)
    {
        Point offset = default;
        Visual? current = slot;
        while (current is not null && !ReferenceEquals(current, this))
        {
            offset += current.Bounds.Position;
            current = current.GetVisualParent();
        }
        return new Rect(offset, slot.Bounds.Size);
    }

    private sealed class SplitParts(string id, Grid grid, GridSplitter splitter, DefinitionBase a, DefinitionBase b, bool columns)
    {
        public string Id { get; } = id;

        public Grid Grid { get; } = grid;

        public GridSplitter Splitter { get; } = splitter;

        /// <summary>A's share: from the star values (GridSplitter writes pixel-proportional stars), else actual sizes.</summary>
        public double CurrentRatio()
        {
            GridLength la = Length(a);
            GridLength lb = Length(b);
            if (la.IsStar && lb.IsStar && la.Value + lb.Value > 0)
                return la.Value / (la.Value + lb.Value);
            double sa = Actual(a);
            double sb = Actual(b);
            return sa + sb > 0 ? sa / (sa + sb) : double.NaN;
        }

        /// <summary>
        /// Moves the divider by <paramref name="pixels"/> in layout space (positive = A grows), within both
        /// sides' minimum sizes; false when nothing moved. Writes pixel-proportional stars like GridSplitter.
        /// </summary>
        public bool MovePixels(double pixels)
        {
            double sa = Actual(a);
            double sb = Actual(b);
            double total = sa + sb;
            double minA = Min(a);
            double minB = Min(b);
            if (total <= 0 || total - minB < minA)
                return false;
            double next = Math.Clamp(sa + pixels, minA, total - minB);
            if (Math.Abs(next - sa) < 1e-6)
                return false;
            if (columns)
            {
                ((ColumnDefinition)a).Width = new GridLength(next, GridUnitType.Star);
                ((ColumnDefinition)b).Width = new GridLength(total - next, GridUnitType.Star);
            }
            else
            {
                ((RowDefinition)a).Height = new GridLength(next, GridUnitType.Star);
                ((RowDefinition)b).Height = new GridLength(total - next, GridUnitType.Star);
            }
            return true;
        }

        public bool Columns => columns;

        private double Min(DefinitionBase d) =>
            columns ? ((ColumnDefinition)d).MinWidth : ((RowDefinition)d).MinHeight;

        public void SetRatio(double ratio)
        {
            if (columns)
            {
                ((ColumnDefinition)a).Width = new GridLength(ratio, GridUnitType.Star);
                ((ColumnDefinition)b).Width = new GridLength(1 - ratio, GridUnitType.Star);
            }
            else
            {
                ((RowDefinition)a).Height = new GridLength(ratio, GridUnitType.Star);
                ((RowDefinition)b).Height = new GridLength(1 - ratio, GridUnitType.Star);
            }
        }

        private GridLength Length(DefinitionBase d) =>
            columns ? ((ColumnDefinition)d).Width : ((RowDefinition)d).Height;

        private double Actual(DefinitionBase d) =>
            columns ? ((ColumnDefinition)d).ActualWidth : ((RowDefinition)d).ActualHeight;
    }
}
