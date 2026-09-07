using RowlEngine.Editor.ViewModels.Components;
using System;
using System.Collections.Generic;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Creates an editor component from its persisted graph representation.</summary>
internal static class StoryGraphComponentHydrator
{
    public static bool TryCreate(JsonElement componentJson, out NodeComponentViewModel? component, out string? unknownType)
    {
        component = null;
        unknownType = null;
        string typeKey = componentJson.TryGetProperty("type", out var type) ? type.GetString() ?? string.Empty : string.Empty;
        if (string.IsNullOrEmpty(typeKey)) return false;

        try
        {
            component = ComponentRegistry.Create(typeKey);
        }
        catch (KeyNotFoundException)
        {
            unknownType = typeKey;
            return false;
        }

        if (componentJson.TryGetProperty("id", out var id)) component.ComponentId = id.GetString() ?? component.ComponentId;
        if (componentJson.TryGetProperty("enabled", out var enabled)) component.IsEnabled = enabled.GetBoolean();
        if (componentJson.TryGetProperty("data", out var data) && data.ValueKind == JsonValueKind.Object)
        {
            var values = new Dictionary<string, object?>();
            foreach (var value in data.EnumerateObject())
                values[value.Name] = value.Value.ValueKind switch
                {
                    JsonValueKind.String => value.Value.GetString(),
                    JsonValueKind.Number => value.Value.GetDouble(),
                    JsonValueKind.True => true,
                    JsonValueKind.False => false,
                    _ => value.Value.GetRawText()
                };
            component.Deserialize(values);
        }
        return true;
    }
}
