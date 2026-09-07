using RowlEngine.Editor.ViewModels;
using System.Collections.Generic;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Builds editor graph edges from persisted <c>next_nodes</c> entries.</summary>
internal static class StoryGraphConnectionHydrator
{
    public static IEnumerable<ConnectionViewModel> Create(JsonElement nodes, IReadOnlyDictionary<ulong, NodeViewModel> nodeMap)
    {
        foreach (var sourceJson in nodes.EnumerateArray())
        {
            ulong sourceId = sourceJson.TryGetProperty("id", out var id) ? id.GetUInt64() : 0;
            if (!nodeMap.TryGetValue(sourceId, out var source) ||
                !sourceJson.TryGetProperty("next_nodes", out var nextNodes) ||
                nextNodes.ValueKind != JsonValueKind.Array)
                continue;

            foreach (var next in nextNodes.EnumerateArray())
            {
                ulong targetId = next.TryGetProperty("id", out var target) ? target.GetUInt64() : 0;
                if (targetId == 0 || !nodeMap.TryGetValue(targetId, out var targetNode)) continue;
                string optionId = next.TryGetProperty("option_id", out var option) ? option.GetString() ?? string.Empty : string.Empty;
                yield return new ConnectionViewModel(source, targetNode, optionId);
            }
        }
    }
}
