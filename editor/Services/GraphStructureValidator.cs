using System.Collections.Generic;
using System.Linq;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Validates the Graph vNext structural contract: duplicate ids, dangling node
/// references, subgraph entry/exit membership, disjoint subgraph membership,
/// entry/exit port discipline, entry-to-exit reachability, orphan node chapter
/// ids and subgraph call-graph cycles. Mirrors the native contract in
/// story_graph_vnext.cpp; both sides must report the same violations.
/// </summary>
internal static class GraphStructureValidator
{
    public static IReadOnlyList<ProjectValidationIssue> Validate(
        IReadOnlyList<NodeViewModel> nodes,
        IReadOnlyList<ConnectionViewModel> connections,
        GraphStructureDocument structure)
    {
        var issues = new List<ProjectValidationIssue>();
        if (structure.IsEmpty)
            return issues;

        var nodeIds = new HashSet<ulong>(nodes.Select(node => node.Id));

        CheckDuplicateIds(structure.Groups.Select(group => group.Id), "group", issues);
        CheckDuplicateIds(structure.Subgraphs.Select(subgraph => subgraph.Id), "subgraph", issues);
        CheckDuplicateIds(structure.Chapters.Select(chapter => chapter.Id), "chapter", issues);
        if (issues.Count != 0)
            return issues;

        foreach (var group in structure.Groups)
        {
            foreach (ulong nodeId in group.NodeIds)
            {
                if (!nodeIds.Contains(nodeId))
                    issues.Add(Error($"Story graph group '{group.Id}' references missing node #{nodeId}; rejected.", nodeId));
            }
            if (group.NodeIds.Count == 0)
                issues.Add(Warning($"Story graph group '{group.Id}' has no members."));
        }

        var owner = new Dictionary<ulong, SubgraphDefinition>();
        foreach (var subgraph in structure.Subgraphs)
        {
            string context = $"Story graph subgraph '{subgraph.Id}'";
            if (!nodeIds.Contains(subgraph.EntryNodeId))
                issues.Add(Error($"{context} entry references missing node #{subgraph.EntryNodeId}; rejected.", subgraph.EntryNodeId));
            foreach (ulong nodeId in subgraph.NodeIds)
            {
                if (!nodeIds.Contains(nodeId))
                    issues.Add(Error($"{context} references missing node #{nodeId}; rejected.", nodeId));
            }
            foreach (ulong exitId in subgraph.ExitNodeIds)
            {
                if (!nodeIds.Contains(exitId))
                    issues.Add(Error($"{context} exit references missing node #{exitId}; rejected.", exitId));
            }
            var members = new HashSet<ulong>(subgraph.NodeIds);
            if (nodeIds.Contains(subgraph.EntryNodeId) && !members.Contains(subgraph.EntryNodeId))
                issues.Add(Error($"{context} entry node #{subgraph.EntryNodeId} is not a member; rejected.", subgraph.EntryNodeId));
            foreach (ulong exitId in subgraph.ExitNodeIds)
            {
                if (nodeIds.Contains(exitId) && !members.Contains(exitId))
                    issues.Add(Error($"{context} exit node #{exitId} is not a member; rejected.", exitId));
            }
            foreach (ulong memberId in members)
            {
                if (owner.TryGetValue(memberId, out var first))
                    issues.Add(Error($"Story graph node #{memberId} belongs to subgraphs '{first.Id}' and '{subgraph.Id}'; rejected.", memberId));
                else
                    owner[memberId] = subgraph;
            }
            if (subgraph.NodeIds.Count == 0)
                issues.Add(Warning($"{context} has no members."));
        }
        if (issues.Any(issue => issue.IsError))
            return issues;

        var adjacency = BuildAdjacency(connections);

        // Port discipline: flow crosses a subgraph boundary only through ports.
        foreach (var node in nodes)
        {
            owner.TryGetValue(node.Id, out var sourceOwner);
            if (!adjacency.TryGetValue(node.Id, out var targets))
                continue;
            foreach (ulong targetId in targets)
            {
                owner.TryGetValue(targetId, out var targetOwner);
                if (ReferenceEquals(sourceOwner, targetOwner))
                    continue;
                if (sourceOwner is not null && !sourceOwner.ExitNodeIds.Contains(node.Id))
                    issues.Add(Error($"Story graph edge #{node.Id} -> #{targetId} leaves subgraph '{sourceOwner.Id}' from a non-exit node; rejected.", node.Id));
                if (targetOwner is not null && targetId != targetOwner.EntryNodeId)
                    issues.Add(Error($"Story graph edge #{node.Id} -> #{targetId} enters subgraph '{targetOwner.Id}' at a non-entry node; rejected.", targetId));
            }
        }
        if (issues.Any(issue => issue.IsError))
            return issues;

        // Entry-to-exit reachability inside each member set.
        foreach (var subgraph in structure.Subgraphs)
        {
            var members = new HashSet<ulong>(subgraph.NodeIds);
            var reached = new HashSet<ulong> { subgraph.EntryNodeId };
            var frontier = new Stack<ulong>();
            frontier.Push(subgraph.EntryNodeId);
            while (frontier.Count != 0)
            {
                ulong current = frontier.Pop();
                if (!adjacency.TryGetValue(current, out var targets))
                    continue;
                foreach (ulong targetId in targets)
                {
                    if (members.Contains(targetId) && reached.Add(targetId))
                        frontier.Push(targetId);
                }
            }
            foreach (ulong exitId in subgraph.ExitNodeIds)
            {
                if (!reached.Contains(exitId))
                    issues.Add(Error($"Story graph subgraph '{subgraph.Id}' exit node #{exitId} is unreachable from its entry; rejected.", exitId));
            }
        }

        // Orphan chapter references and chapter entry hints.
        var chapterIds = new HashSet<string>(structure.Chapters.Select(chapter => chapter.Id));
        foreach (var node in nodes)
        {
            if (!string.IsNullOrEmpty(node.ChapterId) && !chapterIds.Contains(node.ChapterId))
                issues.Add(Error($"Story graph node #{node.Id} references unknown chapter '{node.ChapterId}'; rejected.", node.Id));
        }
        foreach (var chapter in structure.Chapters)
        {
            if (chapter.StartNodeId is { } startNodeId && !nodeIds.Contains(startNodeId))
                issues.Add(Error($"Story graph chapter '{chapter.Id}' start references missing node #{startNodeId}; rejected.", startNodeId));
            if (!nodes.Any(node => node.ChapterId == chapter.Id))
                issues.Add(Warning($"Story graph chapter '{chapter.Id}' has no nodes assigned."));
        }

        // Subgraph call-graph cycles: direct member-to-member boundary
        // crossings are calls, and a call cycle has no call-stack semantics.
        var calls = new Dictionary<string, HashSet<string>>();
        foreach (var node in nodes)
        {
            if (!owner.TryGetValue(node.Id, out var sourceOwner))
                continue;
            if (!adjacency.TryGetValue(node.Id, out var targets))
                continue;
            foreach (ulong targetId in targets)
            {
                if (owner.TryGetValue(targetId, out var targetOwner) &&
                    !ReferenceEquals(sourceOwner, targetOwner))
                {
                    if (!calls.TryGetValue(sourceOwner.Id, out var callees))
                        calls[sourceOwner.Id] = callees = new HashSet<string>();
                    callees.Add(targetOwner.Id);
                }
            }
        }
        string? cycle = FindCallCycle(structure.Subgraphs.Select(subgraph => subgraph.Id), calls);
        if (cycle is not null)
            issues.Add(Error($"Story graph subgraph call cycle detected: {cycle}; rejected.", null));

        return issues;
    }

    private static void CheckDuplicateIds(
        IEnumerable<string> ids, string kind, List<ProjectValidationIssue> issues)
    {
        var seen = new HashSet<string>();
        foreach (string id in ids)
        {
            if (!seen.Add(id))
                issues.Add(Error($"Story graph contains a duplicate {kind} id '{id}'; rejected.", null));
        }
    }

    private static Dictionary<ulong, List<ulong>> BuildAdjacency(
        IReadOnlyList<ConnectionViewModel> connections)
    {
        var adjacency = new Dictionary<ulong, List<ulong>>();
        foreach (var connection in connections)
        {
            if (connection.SourceNode is null || connection.TargetNode is null)
                continue;
            if (!adjacency.TryGetValue(connection.SourceNode.Id, out var targets))
                adjacency[connection.SourceNode.Id] = targets = new List<ulong>();
            targets.Add(connection.TargetNode.Id);
        }
        return adjacency;
    }

    private static string? FindCallCycle(
        IEnumerable<string> ids, Dictionary<string, HashSet<string>> calls)
    {
        var visit = new Dictionary<string, int>();
        var stack = new List<string>();
        string? cycle = null;
        bool Visit(string id)
        {
            visit[id] = 1;
            stack.Add(id);
            if (calls.TryGetValue(id, out var callees))
            {
                foreach (string callee in callees.OrderBy(name => name))
                {
                    visit.TryGetValue(callee, out int state);
                    if (state == 1)
                    {
                        int start = stack.IndexOf(callee);
                        cycle = string.Join(" -> ", stack.Skip(start).Select(name => $"'{name}'")) + $" -> '{callee}'";
                        return false;
                    }
                    if (state == 0 && !Visit(callee))
                        return false;
                }
            }
            stack.RemoveAt(stack.Count - 1);
            visit[id] = 2;
            return true;
        }
        foreach (string id in ids.OrderBy(name => name))
        {
            visit.TryGetValue(id, out int state);
            if (state == 0 && !Visit(id))
                return cycle;
        }
        return null;
    }

    private static ProjectValidationIssue Error(string message, ulong? nodeId) =>
        new(true, message, nodeId, null);

    private static ProjectValidationIssue Warning(string message) =>
        new(false, message, null, null);
}
