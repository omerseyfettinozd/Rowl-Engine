using Avalonia;
using Avalonia.Controls;
using Avalonia.Media;

namespace RowlEngine.Editor.Controls
{
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

        public override void Render(DrawingContext context)
        {
            base.Render(context);

            if (StartPoint.X == 0 && StartPoint.Y == 0 && EndPoint.X == 0 && EndPoint.Y == 0)
                return;

            double deltaX = System.Math.Max(80, System.Math.Abs(EndPoint.X - StartPoint.X) * 0.5);
            Point controlPoint1 = new Point(StartPoint.X + deltaX, StartPoint.Y);
            Point controlPoint2 = new Point(EndPoint.X - deltaX, EndPoint.Y);

            StreamGeometry geometry = new StreamGeometry();
            using (StreamGeometryContext ctx = geometry.Open())
            {
                ctx.BeginFigure(StartPoint, false);
                ctx.CubicBezierTo(controlPoint1, controlPoint2, EndPoint);
            }

            // Layer 1: Dark Ambient Drop Shadow for ComfyUI depth
            Pen shadowPen = new Pen(new SolidColorBrush(Color.FromArgb(128, 0, 0, 0)), WireThickness + 4, lineCap: PenLineCap.Round);
            context.DrawGeometry(null, shadowPen, geometry);

            // Layer 2: Glowing Outer Aura
            Pen glowPen = new Pen(WireBrush, WireThickness + 2, lineCap: PenLineCap.Round);
            context.DrawGeometry(null, glowPen, geometry);

            // Layer 3: Vibrant Main Core Cable
            Pen mainPen = new Pen(WireBrush, WireThickness, lineCap: PenLineCap.Round);
            context.DrawGeometry(null, mainPen, geometry);

            // Terminal Connector Dots (Boncuk Bağlantı Noktaları)
            IBrush dotFill = Brushes.White;
            context.DrawEllipse(dotFill, mainPen, StartPoint, 4, 4);
            context.DrawEllipse(dotFill, mainPen, EndPoint, 4, 4);
        }
    }
}
