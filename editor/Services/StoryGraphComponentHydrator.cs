using RowlEngine.Editor.ViewModels.Components;
using System;
using System.Collections.Generic;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Creates an editor component from its persisted graph representation.</summary>
internal static class StoryGraphComponentHydrator
{
    /// <param name="preserveUnknown">
    /// True for a graph document being opened: a component type without an editor view
    /// model becomes an <see cref="UnknownComponentViewModel"/> that keeps its raw data (and
    /// still reports the type in <paramref name="unknownType"/>), so a save never deletes it.
    /// False (node-library templates): such a type is skipped and reported.
    /// </param>
    public static bool TryCreate(JsonElement componentJson, out NodeComponentViewModel? component, out string? unknownType,
        bool preserveUnknown = false)
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
            if (!preserveUnknown) return false;
            var kept = new UnknownComponentViewModel(typeKey);
            ReadEnvelope(componentJson, kept);
            var rawValues = new Dictionary<string, object?>();
            if (componentJson.TryGetProperty("data", out var rawData) && rawData.ValueKind == JsonValueKind.Object)
                foreach (var value in rawData.EnumerateObject())
                    rawValues[value.Name] = value.Value;
            kept.Deserialize(rawValues);
            component = kept;
            return true;
        }

        ReadEnvelope(componentJson, component);
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

    private static void ReadEnvelope(JsonElement componentJson, NodeComponentViewModel component)
    {
        if (componentJson.TryGetProperty("id", out var id)) component.ComponentId = id.GetString() ?? component.ComponentId;
        if (componentJson.TryGetProperty("enabled", out var enabled)) component.IsEnabled = enabled.GetBoolean();
    }
}
