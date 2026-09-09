using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

internal sealed record ProjectValidationIssue(bool IsError, string Message);

/// <summary>Pure, non-mutating checks run before export and reusable by editor tooling.</summary>
internal static class ProjectValidationService
{
    private static readonly string[] AssetKeys = { "texture", "sprite", "bgm_track", "sfx_track", "path", "typewriter_sound", "custom_box_texture" };

    public static IReadOnlyList<ProjectValidationIssue> Validate(
        IEnumerable<NodeViewModel> nodes, IEnumerable<ConnectionViewModel> connections, string assetsPath,
        ulong? startNodeId = null)
    {
        var nodeList = nodes.ToList();
        var issues = new List<ProjectValidationIssue>();
        if (nodeList.Count == 0) { issues.Add(new(true, "Graph has no nodes.")); return issues; }
        var ids = nodeList.Select(node => node.Id).ToHashSet();
        foreach (var connection in connections)
        {
            if (connection.SourceNode is null || connection.TargetNode is null ||
                !ids.Contains(connection.SourceNode.Id) || !ids.Contains(connection.TargetNode.Id))
                issues.Add(new(true, "A graph connection has a missing source or target node."));
        }
        var start = startNodeId is { } requestedStart
            ? nodeList.FirstOrDefault(node => node.Id == requestedStart)
            : nodeList.FirstOrDefault(node => node.IsStartNode);
        start ??= nodeList.MinBy(node => node.Id)!;
        if (startNodeId is { } missingStart && start.Id != missingStart)
            issues.Add(new(true, $"Configured start node #{missingStart} does not exist."));
        var reachable = new HashSet<ulong> { start.Id };
        var pending = new Queue<ulong>(); pending.Enqueue(start.Id);
        while (pending.TryDequeue(out var id))
            foreach (var target in connections.Where(c => c.SourceNode?.Id == id).Select(c => c.TargetNode?.Id ?? 0))
                if (target != 0 && reachable.Add(target)) pending.Enqueue(target);
        foreach (var node in nodeList.Where(node => !reachable.Contains(node.Id)))
            issues.Add(new(false, $"Node #{node.Id} ('{node.Title}') is unreachable from the start node."));

        foreach (var node in nodeList.Where(node => reachable.Contains(node.Id)))
        {
            var outgoing = connections.Where(connection => connection.SourceNode?.Id == node.Id)
                .Select(connection => connection.TargetNode?.Id ?? 0)
                .Where(id => ids.Contains(id)).ToList();
            if (outgoing.Count == 0 && node.Id != start.Id)
                issues.Add(new(false, $"Node #{node.Id} ('{node.Title}') is a terminal node."));
        }

        var visiting = new HashSet<ulong>();
        var visited = new HashSet<ulong>();
        bool HasCycle(ulong id)
        {
            if (!reachable.Contains(id)) return false;
            if (!visiting.Add(id)) return true;
            foreach (var target in connections.Where(connection => connection.SourceNode?.Id == id)
                         .Select(connection => connection.TargetNode?.Id ?? 0))
                if (target != 0 && HasCycle(target)) return true;
            visiting.Remove(id);
            visited.Add(id);
            return false;
        }
        if (HasCycle(start.Id))
            issues.Add(new(false, "Reachable story graph contains a cycle; confirm it has an intentional exit."));

        foreach (var node in nodeList)
        {
        // Unreachable nodes are reported above but do not block export for
        // legacy drafts that are intentionally disconnected from the release path.
        if (!reachable.Contains(node.Id)) continue;
        foreach (var component in node.AllComponents.Where(component => component.IsEnabled))
        {
            foreach (var pair in component.Serialize().Where(pair => AssetKeys.Contains(pair.Key) && pair.Value is string))
            {
                var asset = (string)pair.Value;
                if (string.IsNullOrWhiteSpace(asset)) continue;
                if (!ExistsInsideAssets(assetsPath, asset))
                    issues.Add(new(true, $"Node #{node.Id}: missing asset '{asset}' ({component.DisplayName})."));
            }
        }
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
