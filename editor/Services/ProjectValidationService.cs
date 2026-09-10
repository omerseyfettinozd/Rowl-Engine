using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

public sealed record ProjectValidationIssue(bool IsError, string Message, ulong? NodeId = null, string? AssetPath = null);

/// <summary>Pure graph checks shared by export and the editor's issue panel.</summary>
internal static class ProjectValidationService
{
    private static readonly string[] AssetKeys = { "texture", "sprite", "bgm_track", "sfx_track", "path", "typewriter_sound", "custom_box_texture" };

    public static IReadOnlyList<ProjectValidationIssue> Validate(IEnumerable<NodeViewModel> nodes, IEnumerable<ConnectionViewModel> connections, string assetsPath, ulong? startNodeId = null)
    {
        var nodeList = nodes.ToList(); var issues = new List<ProjectValidationIssue>();
        if (nodeList.Count == 0) { issues.Add(new(true, "Graph has no nodes.")); return issues; }
        var byId = nodeList.ToDictionary(node => node.Id);
        var adjacency = nodeList.ToDictionary(node => node.Id, _ => new List<ulong>());
        foreach (var connection in connections)
        {
            if (connection.SourceNode is null || connection.TargetNode is null || !byId.ContainsKey(connection.SourceNode.Id) || !byId.ContainsKey(connection.TargetNode.Id))
            { issues.Add(new(true, "A graph connection has a missing source or target node.")); continue; }
            adjacency[connection.SourceNode.Id].Add(connection.TargetNode.Id);
        }
        var start = startNodeId is { } requested && byId.TryGetValue(requested, out var configured)
            ? configured : nodeList.FirstOrDefault(node => node.IsStartNode) ?? nodeList.MinBy(node => node.Id)!;
        if (startNodeId is { } missing && start.Id != missing) issues.Add(new(true, $"Configured start node #{missing} does not exist.", missing));
        var reachable = new HashSet<ulong> { start.Id }; var pending = new Queue<ulong>(); pending.Enqueue(start.Id);
        while (pending.TryDequeue(out var id)) foreach (var target in adjacency[id]) if (reachable.Add(target)) pending.Enqueue(target);
        foreach (var node in nodeList.Where(node => !reachable.Contains(node.Id))) issues.Add(new(false, $"Node #{node.Id} ('{node.Title}') is unreachable from the start node.", node.Id));
        foreach (var node in nodeList.Where(node => reachable.Contains(node.Id) && adjacency[node.Id].Count == 0 && node.Id != start.Id)) issues.Add(new(false, $"Node #{node.Id} ('{node.Title}') is a terminal node.", node.Id));

        // Iterative colour DFS visits each edge once and cannot overflow on a long story chain.
        var colours = new Dictionary<ulong, byte>(); bool hasCycle = false;
        var stack = new Stack<(ulong id, int next)>(); stack.Push((start.Id, 0)); colours[start.Id] = 1;
        while (stack.Count > 0)
        {
            var (id, next) = stack.Pop();
            if (next >= adjacency[id].Count) { colours[id] = 2; continue; }
            stack.Push((id, next + 1)); var target = adjacency[id][next];
            if (!reachable.Contains(target)) continue;
            colours.TryGetValue(target, out var colour);
            if (colour == 1) { hasCycle = true; continue; }
            if (colour == 0) { colours[target] = 1; stack.Push((target, 0)); }
        }
        if (hasCycle) issues.Add(new(false, "Reachable story graph contains a cycle; confirm it has an intentional exit.", start.Id));
        foreach (var node in nodeList.Where(node => reachable.Contains(node.Id)))
        foreach (var component in node.AllComponents.Where(component => component.IsEnabled))
        foreach (var pair in component.Serialize().Where(pair => AssetKeys.Contains(pair.Key) && pair.Value is string))
        {
            var asset = (string)pair.Value;
            if (!string.IsNullOrWhiteSpace(asset) && !ExistsInsideAssets(assetsPath, asset))
                issues.Add(new(true, $"Node #{node.Id}: missing asset '{asset}' ({component.DisplayName}).", node.Id, asset));
        }
        return issues;
    }

    private static bool ExistsInsideAssets(string assetsPath, string asset)
    {
        var root = Path.GetFullPath(assetsPath);
        foreach (var candidate in new[] { asset, Path.Combine("images", asset), Path.Combine("audio", asset), Path.Combine("fonts", asset), Path.Combine("scripts", asset) })
        {
            var path = Path.GetFullPath(Path.Combine(root, candidate));
            if (path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.Ordinal) && File.Exists(path)) return true;
        }
        return false;
    }
}
