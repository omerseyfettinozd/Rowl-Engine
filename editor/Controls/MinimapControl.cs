using System;
using System.Collections;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using RowlEngine.Editor.Services.Search;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 1 — lightweight bird's-eye canvas map. Renders every
    /// node as a single dot plus the viewport rect in one DrawingContext
    /// pass (no per-node controls, so it stays cheap at 2.000 nodes).
    /// Dragging moves the viewport through <see cref="ViewportRequested"/>.
    /// </summary>
    public class MinimapControl : Control
    {
        public static readonly StyledProperty<IEnumerable?> NodesSourceProperty =
            AvaloniaProperty.Register<MinimapControl, IEnumerable?>(nameof(NodesSource));

        public static readonly StyledProperty<Rect> WorldBoundsProperty =
            AvaloniaProperty.Register<MinimapControl, Rect>(nameof(WorldBounds));

        public static readonly StyledProperty<Rect> ViewportRectProperty =
            AvaloniaProperty.Register<MinimapControl, Rect>(nameof(ViewportRect));

        public static readonly StyledProperty<IBrush?> BackgroundProperty =
            AvaloniaProperty.Register<MinimapControl, IBrush?>(nameof(Background));

        /// <summary>
        /// Faz 4 Dilim 3 — group frames rendered as translucent backdrop
        /// boxes behind the node dots (editor metadata, same projection).
        /// </summary>
        public static readonly StyledProperty<IEnumerable?> GroupsSourceProperty =
            AvaloniaProperty.Register<MinimapControl, IEnumerable?>(nameof(GroupsSource));

        public IEnumerable? NodesSource
        {
            get => GetValue(NodesSourceProperty);
            set => SetValue(NodesSourceProperty, value);
        }

        public Rect WorldBounds
        {
            get => GetValue(WorldBoundsProperty);
            set => SetValue(WorldBoundsProperty, value);
        }

        public Rect ViewportRect
        {
            get => GetValue(ViewportRectProperty);
            set => SetValue(ViewportRectProperty, value);
        }

        public IBrush? Background
        {
            get => GetValue(BackgroundProperty);
            set => SetValue(BackgroundProperty, value);
        }

        public IEnumerable? GroupsSource
        {
            get => GetValue(GroupsSourceProperty);
            set => SetValue(GroupsSourceProperty, value);
        }

        /// <summary>Raised with canvas coordinates while the user drags.</summary>
        public event EventHandler<Point>? ViewportRequested;

        static MinimapControl()
        {
            AffectsRender<MinimapControl>(
                NodesSourceProperty, GroupsSourceProperty, WorldBoundsProperty,
                ViewportRectProperty, BoundsProperty, BackgroundProperty);
        }

        protected override void OnPropertyChanged(AvaloniaPropertyChangedEventArgs change)
        {
            base.OnPropertyChanged(change);
            if (change.Property == NodesSourceProperty)
            {
                if (change.OldValue is System.Collections.Specialized.INotifyCollectionChanged oldCollection)
                    oldCollection.CollectionChanged -= OnNodesCollectionChanged;
                UnhookNodeBrushes(change.OldValue as System.Collections.IEnumerable);
                if (change.NewValue is System.Collections.Specialized.INotifyCollectionChanged newCollection)
                    newCollection.CollectionChanged += OnNodesCollectionChanged;
                HookNodeBrushes(change.NewValue as System.Collections.IEnumerable);
                InvalidateVisual();
            }
            else if (change.Property == GroupsSourceProperty)
            {
                if (change.OldValue is System.Collections.Specialized.INotifyCollectionChanged oldGroups)
                    oldGroups.CollectionChanged -= OnGroupsCollectionChanged;
                UnhookGroupBoxes(change.OldValue as System.Collections.IEnumerable);
                if (change.NewValue is System.Collections.Specialized.INotifyCollectionChanged newGroups)
                    newGroups.CollectionChanged += OnGroupsCollectionChanged;
                HookGroupBoxes(change.NewValue as System.Collections.IEnumerable);
                InvalidateVisual();
            }
        }

        private void OnGroupsCollectionChanged(
            object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
        {
            UnhookGroupBoxes(e.OldItems);
            HookGroupBoxes(e.NewItems);
            InvalidateVisual();
        }

        private void HookGroupBoxes(System.Collections.IEnumerable? items)
        {
            if (items is null)
                return;
            foreach (var item in items)
            {
                if (item is ViewModels.CanvasGroupViewModel group)
                    group.PropertyChanged += OnGroupBoxPropertyChanged;
            }
        }

        private void UnhookGroupBoxes(System.Collections.IEnumerable? items)
        {
            if (items is null)
                return;
            foreach (var item in items)
            {
                if (item is ViewModels.CanvasGroupViewModel group)
                    group.PropertyChanged -= OnGroupBoxPropertyChanged;
            }
        }

        private void OnGroupBoxPropertyChanged(
            object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(ViewModels.CanvasGroupViewModel.X) ||
                e.PropertyName == nameof(ViewModels.CanvasGroupViewModel.Y) ||
                e.PropertyName == nameof(ViewModels.CanvasGroupViewModel.Width) ||
                e.PropertyName == nameof(ViewModels.CanvasGroupViewModel.Height) ||
                e.PropertyName == nameof(ViewModels.CanvasGroupViewModel.Color))
                InvalidateVisual();
        }

        private void OnNodesCollectionChanged(
            object? sender, System.Collections.Specialized.NotifyCollectionChangedEventArgs e)
        {
            UnhookNodeBrushes(e.OldItems);
            HookNodeBrushes(e.NewItems);
            InvalidateVisual();
        }

        private void HookNodeBrushes(System.Collections.IEnumerable? items)
        {
            if (items is null)
                return;
            foreach (var item in items)
            {
                if (item is NodeViewModel node)
                    node.PropertyChanged += OnNodeBrushPropertyChanged;
            }
        }

        private void UnhookNodeBrushes(System.Collections.IEnumerable? items)
        {
            if (items is null)
                return;
            foreach (var item in items)
            {
                if (item is NodeViewModel node)
                    node.PropertyChanged -= OnNodeBrushPropertyChanged;
            }
        }

        private void OnNodeBrushPropertyChanged(
            object? sender, System.ComponentModel.PropertyChangedEventArgs e)
        {
            if (e.PropertyName == nameof(NodeViewModel.ColorTag) ||
                e.PropertyName == nameof(NodeViewModel.FilterOpacity))
                InvalidateVisual();
        }

        public override void Render(DrawingContext context)
        {
            base.Render(context);
            Rect bounds = Bounds;
            if (bounds.Width <= 0 || bounds.Height <= 0)
                return;
            context.FillRectangle(Background ?? Brushes.Transparent, bounds);

            Rect world = WorldBounds;
            if (world.Width <= 0 || world.Height <= 0)
                return;
            double scale = Math.Min(bounds.Width / world.Width, bounds.Height / world.Height);
            if (!double.IsFinite(scale) || scale <= 0)
                return;
            double offsetX = (bounds.Width - world.Width * scale) / 2;
            double offsetY = (bounds.Height - world.Height * scale) / 2;

            // Faz 4 Dilim 3 — translucent group backdrops first (behind dots).
            if (GroupsSource is not null)
            {
                foreach (var item in GroupsSource)
                {
                    if (item is not ViewModels.CanvasGroupViewModel group)
                        continue;
                    var box = new Rect(
                        offsetX + (group.X - world.X) * scale,
                        offsetY + (group.Y - world.Y) * scale,
                        Math.Max(3.0, group.Width * scale),
                        Math.Max(3.0, group.Height * scale));
                    context.FillRectangle(GroupBrushFor(group.Color), box);
                }
            }

            if (NodesSource is not null)
            {
                foreach (var item in NodesSource)
                {
                    if (item is not NodeViewModel node)
                        continue;
                    double height = node.NodeCardHeight > 0 ? node.NodeCardHeight : 220.0;
                    var dot = new Rect(
                        offsetX + (node.X - 8.0 - world.X) * scale,
                        offsetY + (node.Y - world.Y) * scale,
                        Math.Max(2.0, 308.0 * scale),
                        Math.Max(2.0, height * scale));
                    // Faz 4 Dilim 2 — tagged nodes keep their card color;
                    // filtered-out nodes render dimmed, like on the canvas.
                    bool dimmed = node.FilterOpacity < 0.99;
                    IBrush brush = string.IsNullOrEmpty(node.ColorTag)
                        ? (dimmed ? s_nodeDimBrush : s_nodeBrush)
                        : NodeColorTags.GetBrush(node.ColorTag, dimmed);
                    context.FillRectangle(brush, dot);
                }
            }

            // Faz 6 Dilim 4: çerçeve dünyaya kelepçelenir — görünüm
            // dışarı taşsa bile mavi çerçeve harita dışına çıkmaz.
            Rect view = ViewportRect.Intersect(WorldBounds);
            if (view.Width <= 0 || view.Height <= 0)
                return;
            var viewport = new Rect(
                offsetX + (view.X - world.X) * scale,
                offsetY + (view.Y - world.Y) * scale,
                Math.Max(4.0, view.Width * scale),
                Math.Max(4.0, view.Height * scale));
            context.DrawRectangle(s_viewportPen, viewport);
        }

        protected override void OnPointerPressed(PointerPressedEventArgs e)
        {
            base.OnPointerPressed(e);
            if (e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                RequestViewport(e.GetPosition(this));
                e.Pointer.Capture(this);
                e.Handled = true;
            }
        }

        protected override void OnPointerMoved(PointerEventArgs e)
        {
            base.OnPointerMoved(e);
            if (e.Pointer.Captured == this &&
                e.GetCurrentPoint(this).Properties.IsLeftButtonPressed)
            {
                RequestViewport(e.GetPosition(this));
                e.Handled = true;
            }
        }

        protected override void OnPointerReleased(PointerReleasedEventArgs e)
        {
            base.OnPointerReleased(e);
            if (e.Pointer.Captured == this)
            {
                e.Pointer.Capture(null);
                e.Handled = true;
            }
        }

        private void RequestViewport(Point controlPoint)
        {
            Point? canvas = MinimapProjection.CanvasFromControlPoint(
                Bounds.Size, WorldBounds, controlPoint);
            if (canvas.HasValue)
                ViewportRequested?.Invoke(this, canvas.Value);
        }

        private static readonly IBrush s_nodeBrush =
            new SolidColorBrush(Color.FromArgb(160, 56, 189, 248));

        private static readonly IBrush s_nodeDimBrush =
            new SolidColorBrush(Color.FromArgb(48, 56, 189, 248));

        private static readonly IPen s_viewportPen =
            new Pen(new SolidColorBrush(Color.FromArgb(255, 0, 240, 255)), 1.5);

        private static readonly Dictionary<string, IBrush> s_groupBrushes = new(StringComparer.OrdinalIgnoreCase);

        private static readonly IBrush s_groupFallbackBrush =
            new SolidColorBrush(Color.FromArgb(40, 100, 116, 139));

        /// <summary>Translucent backdrop brush for a group color (slate fallback).</summary>
        internal static IBrush GroupBrushFor(string? color)
        {
            string key = string.IsNullOrWhiteSpace(color) ? "fallback" : color.Trim();
            if (s_groupBrushes.TryGetValue(key, out IBrush? cached))
                return cached;
            IBrush brush = s_groupFallbackBrush;
            if (Color.TryParse(key, out Color parsed))
                brush = new SolidColorBrush(Color.FromArgb(40, parsed.R, parsed.G, parsed.B));
            if (s_groupBrushes.Count >= 64)
                s_groupBrushes.Clear();
            s_groupBrushes[key] = brush;
            return brush;
        }
    }
}
