using System;
using System.Collections.Generic;
using System.Text.Json;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

public sealed class StoryGraphLoadResult
{
    public bool Success { get; set; }
    public int FormatVersion { get; set; }
    public List<NodeViewModel> Nodes { get; set; } = new();
    public List<ConnectionViewModel> Connections { get; set; } = new();
    public string? ErrorMessage { get; set; }
    public List<string> Warnings { get; set; } = new();
}

public static class StoryGraphLoaderService
{
    public static StoryGraphLoadResult Load(JsonDocument document)
    {
        var result = new StoryGraphLoadResult();

        try
        {
            var root = document.RootElement;

            int formatVersion = 0;
            if (root.TryGetProperty("format_version", out var fv))
                formatVersion = fv.GetInt32();

            result.FormatVersion = formatVersion;

            if (!root.TryGetProperty("nodes", out var nodesArray) || nodesArray.ValueKind != JsonValueKind.Array)
            {
                result.ErrorMessage = "Document does not contain a valid 'nodes' array.";
                return result;
            }

            var nodeMap = new Dictionary<ulong, NodeViewModel>();

            foreach (var nodeJson in nodesArray.EnumerateArray())
            {
                var node = StoryGraphNodeHydrator.CreateShell(nodeJson, nodeMap.Count);
                ulong nodeId = node.Id;

                // ── V3 Format: Objects array (Unity GameObject style) ──
                if (nodeJson.TryGetProperty("objects", out var objsArray) && objsArray.ValueKind == JsonValueKind.Array)
                {
                    foreach (var objJson in objsArray.EnumerateArray())
                    {
                        string objName = objJson.TryGetProperty("name", out var onProp) ? onProp.GetString() ?? "GameObject" : "GameObject";
                        var frameObj = node.CreateObject(objName);

                        if (objJson.TryGetProperty("id", out var oidProp))
                            frameObj.Id = oidProp.GetString() ?? frameObj.Id;

                        if (objJson.TryGetProperty("is_active", out var actProp))
                            frameObj.IsActive = actProp.GetBoolean();

                        if (objJson.TryGetProperty("components", out var compsArray) && compsArray.ValueKind == JsonValueKind.Array)
                        {
                            foreach (var compJson in compsArray.EnumerateArray())
                            {
                                if (StoryGraphComponentHydrator.TryCreate(compJson, out var component, out var unknownType))
                                {
                                    frameObj.AddComponent(component!);
                                }
                                else if (unknownType is not null)
                                {
                                    result.Warnings.Add($"⚠️ Unknown component type '{unknownType}' in Node #{nodeId}, skipping.");
                                }
                            }
                        }
                    }
                }
                // ── V2 Format: Components array directly under node (auto-migrate into objects) ──
                else if (nodeJson.TryGetProperty("components", out var compsArray) && compsArray.ValueKind == JsonValueKind.Array)
                {
                    foreach (var compJson in compsArray.EnumerateArray())
                    {
                        if (StoryGraphComponentHydrator.TryCreate(compJson, out var component, out var unknownType))
                        {
                            string objName = component!.DisplayName;
                            var frameObj = node.CreateObject(objName);
                            frameObj.AddComponent(component);
                        }
                        else if (unknownType is not null)
                        {
                            result.Warnings.Add($"⚠️ Unknown component type '{unknownType}' in Node #{nodeId}, skipping.");
                        }
                    }
                }
                // ── V1 Format: Create objects & components from flat fields ──
                else
                {
                    StoryGraphNodeHydrator.PopulateLegacyFields(node, nodeJson);
                }

                StoryGraphNodeHydrator.EnsureDefaultObjects(node);
                result.Nodes.Add(node);
                nodeMap[nodeId] = node;
            }

            foreach (var connection in StoryGraphConnectionHydrator.Create(nodesArray, nodeMap))
            {
                result.Connections.Add(connection);
            }

            result.Success = true;
            return result;
        }
        catch (Exception ex)
        {
            result.Success = false;
            result.ErrorMessage = ex.Message;
            return result;
        }
    }
}
