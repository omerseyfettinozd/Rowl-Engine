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
    internal static readonly string[] AssetKeys = { "texture", "sprite", "bgm_track", "sfx_track", "path", "typewriter_sound", "custom_box_texture" };

    public static IReadOnlyList<ProjectValidationIssue> Validate(IEnumerable<NodeViewModel> nodes, IEnumerable<ConnectionViewModel> connections, string assetsPath, ulong? startNodeId = null)
        => Validate(nodes, connections, assetsPath, startNodeId, structure: null);

    /// <summary>
    /// Validates the graph plus the optional Graph vNext structure contract
    /// (groups, subgraphs, chapters). A null structure skips structure checks.
    /// </summary>
    public static IReadOnlyList<ProjectValidationIssue> Validate(IEnumerable<NodeViewModel> nodes, IEnumerable<ConnectionViewModel> connections, string assetsPath, ulong? startNodeId, GraphStructureDocument? structure)
    {
        // Faz 4 Dilim 5 — the disk is scanned once here; the shared-index
        // overload below lets the batch linter reuse the same scan.
        var diskIndex = BuildDiskIndex(assetsPath);
        return Validate(nodes, connections, assetsPath, startNodeId, structure, diskIndex);
    }

    /// <summary>
    /// Core validation over a caller-supplied disk index. Same rules as the
    /// public overload; <see cref="ProjectLintService"/> passes the index it
    /// already built so Validate+Lint share a single directory scan.
    /// </summary>
    internal static IReadOnlyList<ProjectValidationIssue> Validate(IEnumerable<NodeViewModel> nodes, IEnumerable<ConnectionViewModel> connections, string assetsPath, ulong? startNodeId, GraphStructureDocument? structure, DiskIndex diskIndex)
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
        // Faz 2 Dilim 1: persistent dialogue content identity. Duplicate or
        // malformed content_id values are build-blocking errors; empty ids
        // (pre-migration v4 content) are valid and ignored here.
        issues.AddRange(CheckContentIds(nodeList, reachable));
        // Faz 1 Dilim 1: the caller-supplied disk index backs both the
        // collision check and the Ordinal reference resolution. Comparison
        // stays Ordinal even on case-insensitive filesystems, because the
        // Linux runtime and .rowlpkg lookup are case-sensitive.
        if (diskIndex.ScanError is { } scanError)
            issues.Add(new(false, $"Asset disk scan incomplete: {scanError} Some asset references may be reported as missing.", null, null));
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
        // Faz 5 Dilim 3: katmanlı karakter assetleri (layers + expressions)
        // iç-içe anahtarlardadır; missing-file sahipliği buradadır (error).
        // Linter aynı dosyaya yalnızca advisory warning verir.
        foreach (var node in nodeList.Where(node => reachable.Contains(node.Id)))
        foreach (var component in node.AllComponents.Where(component => component.IsEnabled))
        {
            Dictionary<string, object> data;
            try
            {
                data = component.Serialize();
            }
            catch (Exception)
            {
                continue;
            }
            IReadOnlyList<CharacterLayerAssetRef> refs;
            try
            {
                refs = CharacterLayersService.CollectAssetRefs(data);
            }
            catch (Exception)
            {
                continue;
            }
            // Aynı component'in üst-seviye anahtarıyla aynı dosya iki kez
            // raporlanmaz (legacy sprite == layers.body durumu).
            var covered = new HashSet<string>(StringComparer.Ordinal);
            foreach (var pair in data)
            {
                if (AssetKeys.Contains(pair.Key) && pair.Value is string top &&
                    !string.IsNullOrWhiteSpace(top))
                    covered.Add(top);
            }
            foreach (var assetRef in refs)
            {
                if (string.IsNullOrWhiteSpace(assetRef.Asset)) continue;
                if (!covered.Add(assetRef.Asset)) continue;
                ValidateSingleAssetReference(node.Id, component.DisplayName, assetRef.Asset, diskIndex, issues);
            }
        }
        // Faz 1 Dilim 5: Graph vNext structure contract. Connections are
        // re-materialized from the validated adjacency above so the structure
        // validator sees exactly the edges the graph checks accepted.
        if (structure is not null && !structure.IsEmpty)
        {
            var structureConnections = new List<ConnectionViewModel>();
            foreach (var node in nodeList)
            {
                foreach (ulong targetId in adjacency[node.Id])
                    structureConnections.Add(new ConnectionViewModel(node, byId[targetId]));
            }
            issues.AddRange(GraphStructureValidator.Validate(nodeList, structureConnections, structure));
        }
        return issues;
    }

    private static IEnumerable<ProjectValidationIssue> CheckContentIds(
        List<NodeViewModel> nodeList, HashSet<ulong> reachable)
    {
        var seen = new Dictionary<string, ulong>(StringComparer.OrdinalIgnoreCase);
        foreach (var node in nodeList.Where(node => reachable.Contains(node.Id)))
        {
            foreach (var dialogue in ContentIdService.EnumerateDialogues(node))
            {
                if (!dialogue.IsEnabled)
                    continue;
                string raw = dialogue.ContentId ?? string.Empty;
                if (string.IsNullOrWhiteSpace(raw))
                    continue;
                string? normalized = ContentIdService.Normalize(raw);
                if (normalized is null)
                {
                    yield return new(true,
                        $"Node #{node.Id}: dialogue component '{dialogue.ComponentId}' has an invalid content_id '{raw}'; expected UUID form (8-4-4-4-12). Run content migration or assign a fresh id.",
                        node.Id);
                    continue;
                }
                if (seen.TryGetValue(normalized, out ulong firstNodeId))
                {
                    yield return new(true,
                        $"Story graph contains a duplicate content_id '{normalized}' (nodes #{firstNodeId} and #{node.Id}); rejected.",
                        node.Id);
                }
                else
                {
                    seen[normalized] = node.Id;
                }
            }
        }
    }

    internal sealed record DiskIndex(HashSet<string> ExactPaths, Dictionary<string, string> InsensitivePaths, string? ScanError);

    internal static DiskIndex BuildDiskIndex(string assetsPath)
    {
        var exact = new HashSet<string>(StringComparer.Ordinal);
        var insensitive = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        if (string.IsNullOrWhiteSpace(assetsPath) || !Directory.Exists(assetsPath))
            return new(exact, insensitive, null);
        try
        {
            string root = Path.GetFullPath(assetsPath);
            foreach (string file in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories))
            {
                string rel = Path.GetRelativePath(root, file).Replace('\\', '/');
                exact.Add(rel);
                insensitive.TryAdd(rel, rel);
            }
        }
        catch (Exception error)
        {
            // Surface the failure instead of returning a silently partial
            // index: callers add it as a validation warning (see Validate).
            return new(exact, insensitive, $"'{assetsPath}' could not be fully scanned ({error.GetType().Name}: {error.Message}).");
        }
        return new(exact, insensitive, null);
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
            // Faz 5 Dilim 5: dönüştürülebilir kaynaklar build'i engellemez
            // (kabul-dönüştürerek). Dönüştürülmüş çıktı diskteyse sessiz;
            // yoksa import sırasında üretileceğini söyleyen advisory warning.
            foreach (string candidate in MediaConverterService.ConvertedOutputCandidates(asset))
            {
                if (disk.ExactPaths.Contains(candidate))
                    return;
            }
            issues.Add(new(false, $"Node #{nodeId}: asset '{asset}' ({componentName}) uses '{ext.ToLowerInvariant()}', which is converted to {ConvertedOutputHint(ext)} on import; no converted output found on disk yet.", nodeId, asset));
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

    private static string ConvertedOutputHint(string ext)
        => MediaConverterService.TryGetConversionTarget(
                ext, out string outExt, out string outDir, out _)
            ? $"'{outExt}' under '{outDir}/' (Faz 5 converter: MP3/FLAC -> OGG, WebP -> PNG)"
            : "its converted form";

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
