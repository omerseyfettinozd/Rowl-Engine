using System.Collections.Generic;

namespace RowlEngine.Editor.Tools.RowlEye.Inspection;

/// <summary>
/// Serializable DTO representing a visual element in the Avalonia control tree with its root-relative bounds.
/// </summary>
public sealed class VisualNodeDto
{
    public string Id { get; set; } = string.Empty;
    public string Type { get; set; } = string.Empty;
    public string? Name { get; set; }
    public double X { get; set; }
    public double Y { get; set; }
    public double Width { get; set; }
    public double Height { get; set; }
    public bool IsVisible { get; set; } = true;
    public int ZIndex { get; set; }
    public double Opacity { get; set; } = 1.0;
    public bool ClipToBounds { get; set; }
    public bool IsHitTestVisible { get; set; } = true;
    public bool IsEnabled { get; set; } = true;
    public IReadOnlyList<string> Classes { get; set; } = [];
    public bool HasOverflow { get; set; }
    public bool IsPopup { get; set; }
    public List<VisualNodeDto> Children { get; set; } = [];
    public List<VisualNodeDto> Overlays { get; set; } = [];
}
