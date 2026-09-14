using System.Collections.Generic;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Parses the optional v5 "groups" / "subgraphs" / "chapters" arrays.
/// Missing arrays mean "no structure" (the v4 case) and succeed with an
/// empty document. Shape violations are collected as errors; semantic rules
/// (references, ports, cycles) belong to <see cref="GraphStructureValidator"/>.
/// </summary>
internal static class GraphStructureHydrator
{
    public static bool TryParse(
        JsonElement root,
        out GraphStructureDocument structure,
        out List<string> errors)
    {
        structure = new GraphStructureDocument();
        errors = new List<string>();

        if (root.TryGetProperty("groups", out var groupsElement))
            ParseGroups(groupsElement, structure, errors);
        if (errors.Count == 0 && root.TryGetProperty("subgraphs", out var subgraphsElement))
            ParseSubgraphs(subgraphsElement, structure, errors);
        if (errors.Count == 0 && root.TryGetProperty("chapters", out var chaptersElement))
            ParseChapters(chaptersElement, structure, errors);

        return errors.Count == 0;
    }

    private static void ParseGroups(
        JsonElement element, GraphStructureDocument structure, List<string> errors)
    {
        if (!RequireArray(element, "groups", GraphStructureLimits.MaxGroups, errors, out var items))
            return;
        foreach (var item in items)
        {
            if (!RequireObject(item, "group", errors, out var obj))
                return;
            if (!ReadId(obj, "group", errors, out string id))
                return;
            if (!ReadCapped(obj, "title", GraphStructureLimits.MaxTitleChars, "group", errors, out string title))
                return;
            if (!ReadCapped(obj, "color", GraphStructureLimits.MaxTitleChars, "group", errors, out string color))
                return;
            if (!ReadDouble(obj, "x", "group", errors, out double x) ||
                !ReadDouble(obj, "y", "group", errors, out double y) ||
                !ReadDouble(obj, "width", "group", errors, out double width) ||
                !ReadDouble(obj, "height", "group", errors, out double height))
                return;
            if (!ReadNodeIds(obj, "node_ids", "group", errors, required: false, out var nodeIds))
                return;
            structure.Groups.Add(new CanvasGroup(id, title, color, x, y, width, height, nodeIds));
        }
    }

    private static void ParseSubgraphs(
        JsonElement element, GraphStructureDocument structure, List<string> errors)
    {
        if (!RequireArray(element, "subgraphs", GraphStructureLimits.MaxSubgraphs, errors, out var items))
            return;
        foreach (var item in items)
        {
            if (!RequireObject(item, "subgraph", errors, out var obj))
                return;
            if (!ReadId(obj, "subgraph", errors, out string id))
                return;
            if (!ReadCapped(obj, "title", GraphStructureLimits.MaxTitleChars, "subgraph", errors, out string title))
                return;
            if (!obj.TryGetProperty("entry_node_id", out var entryElement) ||
                !entryElement.TryGetUInt64(out ulong entryNodeId) || entryNodeId == 0)
            {
                errors.Add($"Story graph subgraph '{id}' needs a nonzero entry_node_id; rejected.");
                return;
            }
            string context = $"subgraph '{id}'";
            if (!ReadNodeIds(obj, "exit_node_ids", context, errors, required: false, out var exitIds))
                return;
            if (exitIds.Count > GraphStructureLimits.MaxPortsPerSubgraph)
            {
                errors.Add($"Story graph {context} exceeds its exit port limit; rejected.");
                return;
            }
            if (!ReadNodeIds(obj, "node_ids", context, errors, required: false, out var nodeIds))
                return;
            structure.Subgraphs.Add(new SubgraphDefinition(id, title, entryNodeId, exitIds, nodeIds));
        }
    }

    private static void ParseChapters(
        JsonElement element, GraphStructureDocument structure, List<string> errors)
    {
        if (!RequireArray(element, "chapters", GraphStructureLimits.MaxChapters, errors, out var items))
            return;
        foreach (var item in items)
        {
            if (!RequireObject(item, "chapter", errors, out var obj))
                return;
            if (!ReadId(obj, "chapter", errors, out string id))
                return;
            if (!ReadCapped(obj, "title", GraphStructureLimits.MaxTitleChars, "chapter", errors, out string title))
                return;
            if (!ReadCapped(obj, "summary", GraphStructureLimits.MaxSummaryChars, "chapter", errors, out string summary))
                return;
            int order = 0;
            if (obj.TryGetProperty("order", out var orderElement))
            {
                if (orderElement.ValueKind != JsonValueKind.Number || !orderElement.TryGetInt32(out order))
                {
                    errors.Add($"Story graph chapter '{id}' order must be an integer; rejected.");
                    return;
                }
            }
            ulong? startNodeId = null;
            if (obj.TryGetProperty("start_node_id", out var startElement))
            {
                if (!startElement.TryGetUInt64(out ulong parsed) || parsed == 0)
                {
                    errors.Add($"Story graph chapter '{id}' start_node_id must be a nonzero node id; rejected.");
                    return;
                }
                startNodeId = parsed;
            }
            structure.Chapters.Add(new ChapterDefinition(id, title, order, summary, startNodeId));
        }
    }

    private static bool RequireArray(
        JsonElement element, string name, int max, List<string> errors,
        out List<JsonElement> items)
    {
        items = new List<JsonElement>();
        if (element.ValueKind != JsonValueKind.Array)
        {
            errors.Add($"Story graph {name} must be an array; rejected.");
            return false;
        }
        foreach (var item in element.EnumerateArray())
        {
            if (items.Count >= max)
            {
                errors.Add($"Story graph exceeds the maximum {name} count; rejected.");
                return false;
            }
            items.Add(item);
        }
        return true;
    }

    private static bool RequireObject(
        JsonElement item, string kind, List<string> errors, out JsonElement obj)
    {
        obj = item;
        if (item.ValueKind != JsonValueKind.Object)
        {
            errors.Add($"Story graph contains a non-object {kind}; rejected.");
            return false;
        }
        return true;
    }

    private static bool ReadId(
        JsonElement obj, string kind, List<string> errors, out string id)
    {
        id = string.Empty;
        if (!obj.TryGetProperty("id", out var idElement) ||
            idElement.ValueKind != JsonValueKind.String)
        {
            errors.Add($"Story graph {kind} is missing a string id; rejected.");
            return false;
        }
        id = idElement.GetString() ?? string.Empty;
        if (id.Length == 0 || id.Length > GraphStructureLimits.MaxIdChars)
        {
            errors.Add($"Story graph {kind} id must be 1..128 characters; rejected.");
            return false;
        }
        return true;
    }

    private static bool ReadCapped(
        JsonElement obj, string key, int max, string kind, List<string> errors,
        out string value)
    {
        value = string.Empty;
        if (!obj.TryGetProperty(key, out var element))
            return true;
        if (element.ValueKind != JsonValueKind.String)
        {
            errors.Add($"Story graph {kind} '{key}' must be a string; rejected.");
            return false;
        }
        value = element.GetString() ?? string.Empty;
        if (value.Length > max)
        {
            errors.Add($"Story graph {kind} '{key}' exceeds its length limit; rejected.");
            return false;
        }
        return true;
    }

    private static bool ReadDouble(
        JsonElement obj, string key, string kind, List<string> errors, out double value)
    {
        value = 0;
        if (!obj.TryGetProperty(key, out var element))
            return true;
        if (!element.TryGetDouble(out value))
        {
            errors.Add($"Story graph {kind} '{key}' must be a number; rejected.");
            return false;
        }
        return true;
    }

    private static bool ReadNodeIds(
        JsonElement obj, string key, string context, List<string> errors,
        bool required, out List<ulong> nodeIds)
    {
        nodeIds = new List<ulong>();
        if (!obj.TryGetProperty(key, out var element))
        {
            if (required)
                errors.Add($"Story graph {context} is missing '{key}'; rejected.");
            return !required;
        }
        if (element.ValueKind != JsonValueKind.Array)
        {
            errors.Add($"Story graph {context} '{key}' must be an array; rejected.");
            return false;
        }
        foreach (var item in element.EnumerateArray())
        {
            if (nodeIds.Count >= GraphStructureLimits.MaxMembersPerEntry)
            {
                errors.Add($"Story graph {context} '{key}' exceeds its entry limit; rejected.");
                return false;
            }
            if (!item.TryGetUInt64(out ulong nodeId) || nodeId == 0)
            {
                errors.Add($"Story graph {context} '{key}' must list nonzero node ids; rejected.");
                return false;
            }
            nodeIds.Add(nodeId);
        }
        return true;
    }
}
