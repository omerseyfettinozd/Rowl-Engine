using System;
using System.Collections.Generic;
using System.Linq;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Service responsible for managing node component lifecycle, including target object resolution,
/// component creation, visual bitmap refresh, removal, and reordering.
/// </summary>
public static class EditorComponentService
{
    /// <summary>
    /// Ensures that a valid target GameObject exists within the specified node.
    /// If targetObj is null or not in the node, returns the first GameObject in the node, or creates a new one.
    /// </summary>
    public static FrameObjectViewModel EnsureTargetObject(NodeViewModel node, FrameObjectViewModel? targetObj)
    {
        if (targetObj != null && node.Objects.Contains(targetObj))
            return targetObj;

        var existing = node.Objects.FirstOrDefault();
        if (existing != null)
            return existing;

        return node.CreateObject("GameObject");
    }

    /// <summary>
    /// Creates and adds a component of the specified typeKey to the target GameObject in the node.
    /// Returns the created component, or null if the type was unknown or parameters were invalid.
    /// </summary>
    public static NodeComponentViewModel? AddComponent(
        NodeViewModel? node,
        FrameObjectViewModel? targetObj,
        string typeKey,
        Action<string>? log = null)
    {
        if (node == null || string.IsNullOrEmpty(typeKey)) return null;

        var resolvedTarget = EnsureTargetObject(node, targetObj);

        try
        {
            var component = ComponentRegistry.Create(typeKey);
            resolvedTarget.AddComponent(component);

            // Refresh bitmap on visual components so the image loads immediately
            if (component is BackgroundComponentViewModel bg) bg.RefreshBitmap();
            else if (component is CharacterComponentViewModel ch) ch.RefreshBitmap();

            log?.Invoke($"➕ Added {component.DisplayName} component to '{resolvedTarget.Name}' in Node #{node.Id}");
            return component;
        }
        catch (KeyNotFoundException)
        {
            log?.Invoke($"⚠️ Unknown component type: {typeKey}");
            return null;
        }
    }

    /// <summary>
    /// Removes a component from its parent GameObject or node.
    /// </summary>
    public static bool RemoveComponent(
        NodeViewModel? node,
        NodeComponentViewModel? component,
        Action<string>? log = null)
    {
        if (node == null || component == null) return false;
        string name = component.DisplayName;

        if (component.OwnerObject != null)
        {
            component.OwnerObject.RemoveComponent(component);
        }
        else
        {
            node.RemoveComponent(component);
        }

        log?.Invoke($"🗑️ Removed {name} component from Node #{node.Id}");
        return true;
    }

    /// <summary>
    /// Moves a component up in the parent object or node render order.
    /// </summary>
    public static bool MoveComponentUp(NodeViewModel? node, NodeComponentViewModel? component)
    {
        if (node == null || component == null) return false;
        node.MoveComponentUp(component);
        return true;
    }

    /// <summary>
    /// Moves a component down in the parent object or node render order.
    /// </summary>
    public static bool MoveComponentDown(NodeViewModel? node, NodeComponentViewModel? component)
    {
        if (node == null || component == null) return false;
        node.MoveComponentDown(component);
        return true;
    }
}
