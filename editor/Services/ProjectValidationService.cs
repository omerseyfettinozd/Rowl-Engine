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
        // Faz 1 Dilim 1: disk dizini bir kez çıkarılır; hem çakışma denetimi hem de
        // birebir (Ordinal) referans çözümleme aynı dizini kullanır. Dosya sistemi
        // büyük/küçük harf duyarsız olsa bile karşılaştırma Ordinal yapılır, çünkü
        // Linux runtime ve .rowlpkg araması case-sensitive'dir.
        var diskIndex = BuildDiskIndex(assetsPath);
        foreach (var collision in FindCaseCollisions(diskIndex.ExactPaths))
            issues.Add(new(true, collision, null, null));

        foreach (var node in nodeList.Where(node => reachable.Contains(node.Id)))
        foreach (var component in node.AllComponents.Where(component => component.IsEnabled))
        foreach (var pair in component.Serialize().Where(pair => AssetKeys.Contains(pair.Key) && pair.Value is string))
        {
            var asset = (string)pair.Value;
            if (string.IsNullOrWhiteSpace(asset)) continue;
            ValidateSingleAssetReference(node.Id, component.DisplayName, asset, diskIndex, issues);
        }
        return issues;
    }

    private sealed record DiskIndex(HashSet<string> ExactPaths, Dictionary<string, string> InsensitivePaths);

    private static DiskIndex BuildDiskIndex(string assetsPath)
    {
        var exact = new HashSet<string>(StringComparer.Ordinal);
        var insensitive = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        try
        {
            if (string.IsNullOrWhiteSpace(assetsPath) || !Directory.Exists(assetsPath))
                return new(exact, insensitive);
            string root = Path.GetFullPath(assetsPath);
            foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                string rel = Path.GetRelativePath(root, file).Replace('\\', '/');
                exact.Add(rel);
                insensitive.TryAdd(rel, rel);
            }
        }
        catch { }
        return new(exact, insensitive);
    }

    private static IEnumerable<string> FindCaseCollisions(HashSet<string> exactPaths)
    {
        var groups = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        foreach (string rel in exactPaths)
        {
            if (!groups.TryGetValue(rel, out var list)) groups[rel] = list = new();
            list.Add(rel);
        }
        foreach (var group in groups.Values)
        {
            var distinct = group.Distinct(StringComparer.Ordinal).OrderBy(x => x, StringComparer.Ordinal).ToList();
            if (distinct.Count > 1)
                yield return $"Asset name collision: {string.Join(", ", distinct.Select(x => $"'{x}'"))} differ only by letter case. Rename them so every Assets-relative path is unique across platforms.";
        }
    }

    private static void ValidateSingleAssetReference(ulong nodeId, string componentName, string asset, DiskIndex disk, List<ProjectValidationIssue> issues)
    {
        if (IsOutsideProjectPath(asset))
        {
            issues.Add(new(true, $"Node #{nodeId}: asset '{asset}' points outside the project Assets directory ({componentName}). Use an Assets-relative path.", nodeId, asset));
            return;
        }

        string ext = Path.GetExtension(asset.Replace('\\', '/'));
        if (MediaFormatCatalog.IsConverterPendingExtension(ext))
        {
            issues.Add(new(true, $"Node #{nodeId}: asset '{asset}' ({componentName}) {ConverterHint(ext)}", nodeId, asset));
            return;
        }
        if (MediaFormatCatalog.IsKnownUnsupportedMediaExtension(ext))
        {
            issues.Add(new(true, $"Node #{nodeId}: asset '{asset}' ({componentName}) uses unsupported format '{ext.ToLowerInvariant()}'. Accepted: PNG/JPEG/BMP/TGA, WAV/OGG, TTF/OTF.", nodeId, asset));
            return;
        }

        string normalized = asset.Replace('\\', '/');
        string[] candidates = { normalized, "images/" + normalized, "audio/" + normalized, "fonts/" + normalized, "scripts/" + normalized };
        if (candidates.Any(disk.ExactPaths.Contains)) return;
        string? insensitiveHit = candidates.Select(c => disk.InsensitivePaths.TryGetValue(c, out var actual) ? actual : null)
            .FirstOrDefault(hit => hit is not null);
        if (insensitiveHit is not null)
            issues.Add(new(true, $"Node #{nodeId}: asset '{asset}' does not match any file exactly (found '{insensitiveHit}' with different letter case). Asset paths are case-sensitive; fix the reference.", nodeId, asset));
        else
            issues.Add(new(true, $"Node #{nodeId}: missing asset '{asset}' ({componentName}).", nodeId, asset));
    }

    private static string ConverterHint(string ext)
        => $"uses '{ext.ToLowerInvariant()}', which needs the Faz 5 media converter (MP3/FLAC -> OGG, WebP -> PNG) and is rejected until then. Accepted: PNG/JPEG/BMP/TGA, WAV/OGG, TTF/OTF.";

    private static bool IsOutsideProjectPath(string asset)
    {
        if (Path.IsPathFullyQualified(asset)) return true;
        // Windows sürücü kökü (C:\...) Linux hostta da proje dışı sayılır.
        if (asset.Length >= 3 && char.IsLetter(asset[0]) && asset[1] == ':'
            && (asset[2] == '\\' || asset[2] == '/'))
            return true;
        string normalized = asset.Replace('\\', '/');
        if (normalized.StartsWith('/')) return true;
        foreach (string segment in normalized.Split('/'))
            if (segment == "..") return true;
        return false;
    }
}
