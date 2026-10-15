using System;
using System.Collections.Generic;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.Primitives;
using Avalonia.VisualTree;

namespace RowlEngine.Editor.Tools.RowlEye.Ai;

/// <summary>
/// Configuration options for AI semantic tree generation.
/// </summary>
public sealed class AiContextOptions
{
    public int MaxNodes { get; set; } = 150;
    public int MaxDepth { get; set; } = 8;
    public double MinInteractiveDimension { get; set; } = 24.0;
}

/// <summary>
/// Transforms Avalonia visual trees into token-efficient, semantic representations
/// and detects UI layout/accessibility anomalies for AI models.
/// </summary>
public static class AiContextBuilder
{
    private static readonly HashSet<string> DecorativeTypes = new(StringComparer.OrdinalIgnoreCase)
    {
        "Border", "Panel", "Grid", "StackPanel", "DockPanel", "Canvas", "Decorator",
        "ContentPresenter", "ItemsPresenter", "VisualLayerManager", "AdornerLayer"
    };

    public static AiContextDto Build(
        Visual rootVisual,
        string target,
        string theme,
        double scale,
        string sessionId = "session_default",
        AiContextOptions? options = null)
    {
        ArgumentNullException.ThrowIfNull(rootVisual);
        options ??= new AiContextOptions();

        var anomalies = new List<AiAnomalyDto>();
        var flatNodes = new List<SemanticNodeDto>();

        // 1. Walk and extract semantic tree
        var semanticRoot = Walk(rootVisual, rootVisual, "", 0, options, anomalies, flatNodes);

        // 2. Perform layout anomaly checks on collected nodes
        DetectLayoutAnomalies(flatNodes, anomalies, options);

        // 3. Rank & prune if node count exceeds limit
        int totalNodes = flatNodes.Count;

        var bounds = rootVisual.Bounds;
        int resW = (int)Math.Round(bounds.Width * scale);
        int resH = (int)Math.Round(bounds.Height * scale);

        var meta = new AiContextMetaDto(
            SessionId: sessionId,
            Timestamp: DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(),
            Target: target,
            Theme: theme,
            Scale: scale,
            Resolution: new[] { Math.Max(resW, 100), Math.Max(resH, 100) },
            NodeCount: totalNodes
        );

        return new AiContextDto(
            Meta: meta,
            ActiveState: AiTelemetryHub.Instance.CurrentState,
            SemanticTree: semanticRoot,
            Anomalies: anomalies
        );
    }

    private static SemanticNodeDto Walk(
        Visual visual,
        Visual root,
        string parentPath,
        int depth,
        AiContextOptions options,
        List<AiAnomalyDto> anomalies,
        List<SemanticNodeDto> collectedNodes)
    {
        string rawType = visual.GetType().Name;
        string? controlName = (visual as Control)?.Name;
        string nodeName = !string.IsNullOrWhiteSpace(controlName) ? controlName : DeriveName(visual);
        string currentSegment = !string.IsNullOrWhiteSpace(controlName) ? controlName : rawType;
        string fullPath = string.IsNullOrEmpty(parentPath) ? currentSegment : $"{parentPath}/{currentSegment}";

        // Calculate absolute bounds relative to root
        var rootPoint = visual.TranslatePoint(new Point(0, 0), root);
        double x = rootPoint.HasValue ? Math.Round(rootPoint.Value.X, 1) : Math.Round(visual.Bounds.X, 1);
        double y = rootPoint.HasValue ? Math.Round(rootPoint.Value.Y, 1) : Math.Round(visual.Bounds.Y, 1);
        double w = Math.Round(visual.Bounds.Width, 1);
        double h = Math.Round(visual.Bounds.Height, 1);
        var bounds = new RectDto(x, y, w, h);

        string role = DeriveRole(visual);
        string? val = DeriveValue(visual);
        string state = DeriveState(visual);
        var actions = DeriveActions(visual);
        double importance = CalculateImportance(role, visual, w, h);

        var childrenList = new List<SemanticNodeDto>();

        if (depth < options.MaxDepth)
        {
            foreach (var child in visual.GetVisualChildren())
            {
                if (child is Visual childVisual && childVisual.IsVisible && childVisual.Opacity > 0.01)
                {
                    // Skip completely hidden or 0x0 decorative layers
                    if (childVisual.Bounds.Width <= 0 && childVisual.Bounds.Height <= 0 && DecorativeTypes.Contains(childVisual.GetType().Name))
                    {
                        continue;
                    }

                    var childNode = Walk(childVisual, root, fullPath, depth + 1, options, anomalies, collectedNodes);
                    childrenList.Add(childNode);
                }
            }
        }

        var node = new SemanticNodeDto(
            Path: fullPath,
            Role: role,
            Name: nodeName,
            Value: val,
            State: state,
            Bounds: bounds,
            Importance: importance,
            Actions: actions,
            Children: childrenList.Count > 0 ? childrenList : null
        );

        collectedNodes.Add(node);
        return node;
    }

    private static string DeriveRole(Visual visual) => visual switch
    {
        Window => "Window",
        CheckBox => "CheckBox",
        RadioButton => "RadioButton",
        ToggleButton => "ToggleButton",
        Button => "Button",
        TextBox => "TextBox",
        ComboBox => "ComboBox",
        Slider => "Slider",
        TextBlock => "Text",
        TabControl => "TabControl",
        TabItem => "TabItem",
        ListBoxItem or TreeViewItem => "ListItem",
        ListBox or TreeView => "List",
        Image => "Image",
        ScrollViewer => "ScrollViewer",
        _ => visual.GetType().Name
    };

    private static string DeriveName(Visual visual)
    {
        if (visual is ContentControl cc && cc.Content is string str && !string.IsNullOrWhiteSpace(str))
            return str.Trim();
        if (visual is TextBlock tb && !string.IsNullOrWhiteSpace(tb.Text))
            return tb.Text.Trim();
        if (visual is TextBox tbox && !string.IsNullOrWhiteSpace(tbox.Watermark))
            return tbox.Watermark;
        return visual.GetType().Name;
    }

    private static string? DeriveValue(Visual visual) => visual switch
    {
        TextBox tb => tb.Text,
        ComboBox cb => cb.SelectedItem?.ToString(),
        CheckBox chk => chk.IsChecked.ToString(),
        Slider s => s.Value.ToString("F1"),
        _ => null
    };

    private static string DeriveState(Visual visual)
    {
        var states = new List<string>();
        if (visual is Control c)
        {
            if (c.IsEnabled) states.Add("enabled"); else states.Add("disabled");
            if (c.IsFocused) states.Add("focused");
        }
        if (visual is ToggleButton tb && tb.IsChecked == true) states.Add("checked");
        return string.Join(",", states);
    }

    private static IReadOnlyList<string> DeriveActions(Visual visual) => visual switch
    {
        CheckBox or RadioButton => new[] { "toggle" },
        Button => new[] { "click" },
        TextBox => new[] { "focus", "type", "clear" },
        ComboBox => new[] { "expand", "select" },
        Slider => new[] { "slide" },
        _ => Array.Empty<string>()
    };

    private static double CalculateImportance(string role, Visual visual, double w, double h)
    {
        if (role is "Button" or "TextBox" or "ComboBox" or "CheckBox" or "Slider")
            return 0.95;
        if (role is "TabItem" or "ListItem")
            return 0.85;
        if (role == "Text" && (visual as Control)?.Name != null)
            return 0.75;
        if (DecorativeTypes.Contains(role))
            return 0.2;
        return 0.5;
    }

    private static void DetectLayoutAnomalies(
        List<SemanticNodeDto> nodes,
        List<AiAnomalyDto> anomalies,
        AiContextOptions options)
    {
        var interactiveRoles = new HashSet<string> { "Button", "ComboBox", "CheckBox", "TextBox", "Slider" };

        foreach (var node in nodes)
        {
            // 1. Small touch target check (< 24px)
            if (interactiveRoles.Contains(node.Role))
            {
                if (node.Bounds.W > 0 && node.Bounds.H > 0 &&
                    (node.Bounds.W < options.MinInteractiveDimension || node.Bounds.H < options.MinInteractiveDimension))
                {
                    anomalies.Add(new AiAnomalyDto(
                        Type: "SmallInteractiveTarget",
                        Severity: AnomalySeverity.Warning,
                        Path: node.Path,
                        Detail: $"{node.Role} boyutu ({node.Bounds.W}x{node.Bounds.H}px) minimum önerilen {options.MinInteractiveDimension}px altında.",
                        Bounds: node.Bounds
                    ));
                }
            }

            // 2. Zero dimension on interactive control
            if (interactiveRoles.Contains(node.Role) && (node.Bounds.W <= 0 || node.Bounds.H <= 0))
            {
                anomalies.Add(new AiAnomalyDto(
                    Type: "ZeroDimensionControl",
                    Severity: AnomalySeverity.High,
                    Path: node.Path,
                    Detail: $"Etkileşimli {node.Role} kontrolü görünür işaretlenmiş ancak boyutu 0 ({node.Bounds.W}x{node.Bounds.H}px).",
                    Bounds: node.Bounds
                ));
            }
        }
    }
}
