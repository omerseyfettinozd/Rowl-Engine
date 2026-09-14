using System;
using System.Collections;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
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

        /// <summary>Raised with canvas coordinates while the user drags.</summary>
        public event EventHandler<Point>? ViewportRequested;

        static MinimapControl()
        {
            AffectsRender<MinimapControl>(
                NodesSourceProperty, WorldBoundsProperty, ViewportRectProperty,
                BoundsProperty, BackgroundProperty);
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
                    context.FillRectangle(s_nodeBrush, dot);
                }
            }

            Rect view = ViewportRect;
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

        private static readonly IPen s_viewportPen =
            new Pen(new SolidColorBrush(Color.FromArgb(255, 0, 240, 255)), 1.5);
    }
}
