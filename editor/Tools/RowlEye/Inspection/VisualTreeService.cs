using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.VisualTree;

namespace RowlEngine.Editor.Tools.RowlEye.Inspection;

/// <summary>
/// Extracts a hierarchical snapshot of the Avalonia Visual Tree with root-relative bounding boxes.
/// </summary>
public static class VisualTreeService
{
    public static VisualNodeDto Extract(Visual rootVisual, bool includeHidden = true)
    {
        ArgumentNullException.ThrowIfNull(rootVisual);
        var rootNode = Walk(rootVisual, rootVisual, 0, includeHidden);

        // Scan overlays & popups (ContextMenu, ComboBox dropdowns, Flyouts)
        try
        {
            if (rootVisual is TopLevel topLevel)
            {
                foreach (var descendant in topLevel.GetVisualDescendants())
                {
                    if (descendant is Avalonia.Controls.Primitives.PopupRoot popupRoot && (includeHidden || popupRoot.IsVisible))
                    {
                        var popupNode = Walk(popupRoot, popupRoot, 0, includeHidden);
                        popupNode.IsPopup = true;
                        rootNode.Overlays.Add(popupNode);
                    }
                }
            }
        }
        catch
        {
            // Fail-safe: ensure overlay scan never breaks base tree extraction
        }

        return rootNode;
    }

    private static VisualNodeDto Walk(Visual visual, Visual root, int depth, bool includeHidden)
    {
        var control = visual as Control;
        var styled = visual as StyledElement;
        var input = visual as Avalonia.Input.InputElement;

        var classes = new List<string>();
        if (styled != null)
        {
            foreach (var cls in styled.Classes)
            {
                if (!string.IsNullOrEmpty(cls)) classes.Add(cls);
            }
        }

        var node = new VisualNodeDto
        {
            Id = control?.Name ?? $"{visual.GetType().Name}_{depth}_{visual.GetHashCode():x4}",
            Type = visual.GetType().Name,
            Name = control?.Name,
            IsVisible = visual.IsVisible,
            ZIndex = visual.ZIndex,
            Opacity = Math.Round(visual.Opacity, 3),
            ClipToBounds = visual.ClipToBounds,
            IsHitTestVisible = input?.IsHitTestVisible ?? true,
            IsEnabled = input?.IsEnabled ?? true,
            Classes = classes
        };

        var rootPoint = visual.TranslatePoint(new Point(0, 0), root);
        if (rootPoint.HasValue)
        {
            node.X = Math.Round(rootPoint.Value.X, 2);
            node.Y = Math.Round(rootPoint.Value.Y, 2);
        }
        else
        {
            node.X = Math.Round(visual.Bounds.X, 2);
            node.Y = Math.Round(visual.Bounds.Y, 2);
        }

        node.Width = Math.Round(visual.Bounds.Width, 2);
        node.Height = Math.Round(visual.Bounds.Height, 2);

        // Walk visual children
        var children = visual.GetVisualChildren();
        foreach (var child in children)
        {
            if (child is Visual childVisual)
            {
                if (!includeHidden && !childVisual.IsVisible) continue;

                var childNode = Walk(childVisual, root, depth + 1, includeHidden);

                // Detect overflow: child rect outside parent bounds when parent does not clip
                if (!visual.ClipToBounds && visual.Bounds.Width > 0 && visual.Bounds.Height > 0)
                {
                    if (childVisual.Bounds.Right > visual.Bounds.Width ||
                        childVisual.Bounds.Bottom > visual.Bounds.Height ||
                        childVisual.Bounds.Left < 0 ||
                        childVisual.Bounds.Top < 0)
                    {
                        node.HasOverflow = true;
                    }
                }

                node.Children.Add(childNode);
            }
        }

        return node;
    }
}
