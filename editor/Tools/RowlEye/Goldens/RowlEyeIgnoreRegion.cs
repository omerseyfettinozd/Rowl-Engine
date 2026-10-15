namespace RowlEngine.Editor.Tools.RowlEye.Goldens;

/// <summary>
/// Defines a pixel bounding box to ignore during visual regression comparisons.
/// Used for masking dynamic timestamps, cursor blinks, or noisy rendering regions.
/// </summary>
public sealed record RowlEyeIgnoreRegion(int X, int Y, int Width, int Height, string? Reason = null)
{
    public bool Contains(int px, int py) =>
        px >= X && px < X + Width && py >= Y && py < Y + Height;
}
