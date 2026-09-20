using System;
using Avalonia;

namespace RowlEngine.Editor.Controls
{
    /// <summary>
    /// Faz 4 Dilim 1 — thread-safe minimap projection math (no Avalonia
    /// control initialization), shared by <see cref="MinimapControl"/> and
    /// unit tests. Maps between minimap control points and canvas points
    /// with the same letterbox fit used for rendering.
    /// </summary>
    public static class MinimapProjection
    {
        /// <summary>
        /// Maps a minimap control point to canvas coordinates. Returns null
        /// for degenerate inputs instead of throwing. Faz 6 Dilim 4: hedef
        /// dünyaya kelepçelenir — harita dışına sürükleme görünümü dışarı
        /// taşıyamaz.
        /// </summary>
        public static Point? CanvasFromControlPoint(
            Size controlSize, Rect world, Point controlPoint)
        {
            if (world.Width <= 0 || world.Height <= 0 ||
                controlSize.Width <= 0 || controlSize.Height <= 0)
                return null;
            double scale = Math.Min(
                controlSize.Width / world.Width, controlSize.Height / world.Height);
            if (!double.IsFinite(scale) || scale <= 0)
                return null;
            double offsetX = (controlSize.Width - world.Width * scale) / 2;
            double offsetY = (controlSize.Height - world.Height * scale) / 2;
            double x = world.X + (controlPoint.X - offsetX) / scale;
            double y = world.Y + (controlPoint.Y - offsetY) / scale;
            return new Point(
                Math.Clamp(x, world.X, world.X + world.Width),
                Math.Clamp(y, world.Y, world.Y + world.Height));
        }
    }
}
