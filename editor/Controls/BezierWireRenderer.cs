using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;
using System;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// High-performance Bezier wire renderer with geometry and pen memoization.
    /// Eliminates garbage collection pressure and heap allocations during pan/zoom.
    /// </summary>
    public class BezierWireRenderer : Control
    {
        public static readonly StyledProperty<Point> StartPointProperty =
            AvaloniaProperty.Register<BezierWireRenderer, Point>(nameof(StartPoint));

        public static readonly StyledProperty<Point> EndPointProperty =
            AvaloniaProperty.Register<BezierWireRenderer, Point>(nameof(EndPoint));

        public static readonly StyledProperty<IBrush> WireBrushProperty =
            AvaloniaProperty.Register<BezierWireRenderer, IBrush>(nameof(WireBrush), Brushes.Lime);

        public static readonly StyledProperty<double> WireThicknessProperty =
            AvaloniaProperty.Register<BezierWireRenderer, double>(nameof(WireThickness), 3.0);

        public Point StartPoint
        {
            get => GetValue(StartPointProperty);
            set => SetValue(StartPointProperty, value);
        }

        public Point EndPoint
        {
            get => GetValue(EndPointProperty);
            set => SetValue(EndPointProperty, value);
        }

        public IBrush WireBrush
        {
            get => GetValue(WireBrushProperty);
            set => SetValue(WireBrushProperty, value);
        }

        public double WireThickness
        {
            get => GetValue(WireThicknessProperty);
            set => SetValue(WireThicknessProperty, value);
        }

        static BezierWireRenderer()
        {
            AffectsRender<BezierWireRenderer>(StartPointProperty, EndPointProperty, WireBrushProperty, WireThicknessProperty);
        }

        // ── Geometry & Pen Caching (Zero GC per frame) ──
        private StreamGeometry? _cachedGeometry;
        private Point _cachedStartPoint;
        private Point _cachedEndPoint;

        private Pen? _cachedShadowPen;
        private Pen? _cachedGlowPen;
        private Pen? _cachedMainPen;
        private IBrush? _cachedWireBrush;
        private double _cachedWireThickness;

        private static readonly IBrush DotFillBrush = Brushes.White;
        private static readonly IBrush ShadowBrush = new SolidColorBrush(Color.FromArgb(128, 0, 0, 0));

        public override void Render(DrawingContext context)
        {
            base.Render(context);

            Point start = StartPoint;
            Point end = EndPoint;

            if (start.X == 0 && start.Y == 0 && end.X == 0 && end.Y == 0)
                return;

            // 1. Memoized Geometry Generation (only rebuilds if endpoints moved)
            if (_cachedGeometry == null || start != _cachedStartPoint || end != _cachedEndPoint)
            {
                _cachedStartPoint = start;
                _cachedEndPoint = end;

                double deltaX = Math.Max(80, Math.Abs(end.X - start.X) * 0.5);
                Point controlPoint1 = new Point(start.X + deltaX, start.Y);
                Point controlPoint2 = new Point(end.X - deltaX, end.Y);

                var geometry = new StreamGeometry();
                using (StreamGeometryContext ctx = geometry.Open())
                {
                    ctx.BeginFigure(start, false);
                    ctx.CubicBezierTo(controlPoint1, controlPoint2, end);
                }
                _cachedGeometry = geometry;
            }

            // 2. Memoized Pens (only rebuilds if brush or thickness changed)
            IBrush brush = WireBrush;
            double thickness = WireThickness;

            if (_cachedMainPen == null || brush != _cachedWireBrush || thickness != _cachedWireThickness)
            {
                _cachedWireBrush = brush;
                _cachedWireThickness = thickness;

                _cachedShadowPen = new Pen(ShadowBrush, thickness + 4, lineCap: PenLineCap.Round);
                _cachedGlowPen = new Pen(brush, thickness + 2, lineCap: PenLineCap.Round);
                _cachedMainPen = new Pen(brush, thickness, lineCap: PenLineCap.Round);
            }

            // 3. Render Layers with cached geometry and pens
            context.DrawGeometry(null, _cachedShadowPen, _cachedGeometry);
            context.DrawGeometry(null, _cachedGlowPen, _cachedGeometry);
            context.DrawGeometry(null, _cachedMainPen, _cachedGeometry);

            // 4. Terminal Connector Dots
            context.DrawEllipse(DotFillBrush, _cachedMainPen, start, 4, 4);
            context.DrawEllipse(DotFillBrush, _cachedMainPen, end, 4, 4);
        }
    }
}
