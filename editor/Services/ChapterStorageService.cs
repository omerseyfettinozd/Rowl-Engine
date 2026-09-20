using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace RowlEngine.Editor.Services
{
    /// <summary>
    /// Faz 4 Dilim 3 — chapter boundary file architecture. A large project
    /// no longer has to live in one giant JSON: <see cref="Split"/> writes
    /// one file per chapter plus a <c>chapter_index.json</c> manifest, and
    /// <see cref="Merge"/> joins them back into a single full story graph.
    /// <para>
    /// Layout under <c>&lt;chaptersDir&gt;/</c>:
    /// <c>chapter_index.json</c> (<c>format_version</c>, <c>start_node_id</c>,
    /// <c>node_order</c> global id list, <c>chapters</c>, <c>groups</c>,
    /// <c>subgraphs</c>) plus one <c>&lt;chapter_id&gt;.json</c> per chapter
    /// (<c>chapter_id</c> + <c>nodes</c> array with full node payloads).
    /// </para>
    /// <para>
    /// Backward compatibility: a legacy single-file v4 graph (no chapters)
    /// keeps loading as one implicit default chapter; <see cref="Split"/>
    /// promotes it to an explicit <c>"default"</c> chapter (one-way, recorded
    /// in the manifest). Cross-chapter edges stay legal because node ids are
    /// global: <c>next_nodes</c> entries always reference global node ids and
    /// merge restores the exact global node order via <c>node_order</c>.
    /// </para>
    /// </summary>
    public sealed class ChapterStorageService
    {
        /// <summary>Chapter used for unassigned nodes on split.</summary>
        public const string DefaultChapterId = "default";

        /// <summary>Title of the implicit default chapter.</summary>
        public const string DefaultChapterTitle = "Ana Bölüm";

        public const string IndexFileName = "chapter_index.json";

        private readonly List<ChapterDefinition> _definitions = new();

        public IReadOnlyList<ChapterDefinition> Definitions => _definitions;

        /// <summary>Replaces the known chapter definitions (project load).</summary>
        public void LoadDefinitions(IEnumerable<ChapterDefinition> definitions)
        {
            _definitions.Clear();
            _definitions.AddRange(definitions);
        }

        public void Clear() => _definitions.Clear();

        /// <summary>Session chapters as v5 persistence records.</summary>
        public List<ChapterDefinition> ToRecords() => _definitions.ToList();

        /// <summary>
        /// Effective chapter list for a node set: defined chapters plus the
        /// implicit default chapter when at least one node is unassigned (or
        /// when nothing is defined yet and nodes exist).
        /// </summary>
        public IReadOnlyList<ChapterDefinition> EffectiveChapters(
            IEnumerable<string?> nodeChapterIds)
        {
            var result = _definitions
                .OrderBy(c => c.Order)
                .ThenBy(c => c.Id, StringComparer.Ordinal)
                .ToList();
            bool hasUnassigned = nodeChapterIds.Any(string.IsNullOrEmpty);
            if (hasUnassigned &&
                !result.Any(c => string.Equals(c.Id, DefaultChapterId, StringComparison.Ordinal)))
            {
                result.Insert(0, new ChapterDefinition(
                    DefaultChapterId, DefaultChapterTitle, -1, string.Empty, null));
            }
            return result;
        }

        /// <summary>Nodes of one chapter (empty chapterId = unassigned).</summary>
        public static IReadOnlyList<ulong> NodeIdsOfChapter(
            IReadOnlyList<(ulong id, string chapterId)> nodes, string chapterId) =>
            nodes.Where(n => string.Equals(n.chapterId ?? string.Empty, chapterId, StringComparison.Ordinal))
                .Select(n => n.id)
                .ToList();

        // Split / merge (pure JSON, no view models)

        /// <summary>
        /// Splits a full story graph JSON document into per-chapter files.
        /// Unassigned nodes land in the implicit <c>"default"</c> chapter.
        /// Returns the written chapter ids in manifest order.
        /// </summary>
        public static IReadOnlyList<string> Split(string fullGraphJson, string chaptersDir)
        {
            JsonNode? root = JsonNode.Parse(fullGraphJson)
                ?? throw new InvalidOperationException("Empty story graph document; rejected.");
            JsonArray nodes = root["nodes"]?.AsArray()
                ?? throw new InvalidOperationException("Story graph does not contain a nodes array; rejected.");

            var chapters = root["chapters"]?.AsArray();
            var byId = new Dictionary<string, JsonObject>(StringComparer.Ordinal);
            var order = new List<string>();
            if (chapters is not null)
            {
                foreach (var item in chapters)
                {
                    if (item is JsonObject obj &&
                        obj["id"]?.GetValue<string>() is { } id && id.Length > 0 &&
                        !byId.ContainsKey(id))
                    {
                        byId[id] = obj;
                        order.Add(id);
                    }
                }
            }

            // Bucket nodes (preserve document order inside each bucket).
            var buckets = new Dictionary<string, List<JsonNode>>(StringComparer.Ordinal);
            var nodeOrder = new List<ulong>();
            foreach (var node in nodes)
            {
                if (node is not JsonObject obj)
                    continue;
                if (!obj.TryGetPropertyValue("id", out var idNode) ||
                    !idNode!.GetValueKind().Equals(JsonValueKind.Number))
                    throw new InvalidOperationException("Story graph node without a numeric id; rejected.");
                ulong nodeId = idNode.GetValue<ulong>();
                nodeOrder.Add(nodeId);
                string chapterId = obj["chapter_id"]?.GetValue<string>() ?? string.Empty;
                if (chapterId.Length == 0)
                    chapterId = DefaultChapterId;
                if (!byId.ContainsKey(chapterId))
                {
                    if (!buckets.ContainsKey(chapterId))
                    {
                        byId[chapterId] = new JsonObject
                        {
                            ["id"] = chapterId,
                            ["title"] = chapterId == DefaultChapterId ? DefaultChapterTitle : chapterId,
                            ["order"] = byId.Count,
                            ["summary"] = string.Empty,
                        };
                        order.Add(chapterId);
                    }
                }
                if (!buckets.TryGetValue(chapterId, out var bucket))
                {
                    bucket = new List<JsonNode>();
                    buckets[chapterId] = bucket;
                }
                bucket.Add(node);
            }
            // Chapters without nodes still get a (possibly empty) file.
            foreach (string id in order)
                if (!buckets.ContainsKey(id))
                    buckets[id] = new List<JsonNode>();

            Directory.CreateDirectory(chaptersDir);

            // Per-chapter node files.
            foreach (var (chapterId, bucket) in buckets)
            {
                var file = new JsonObject
                {
                    ["format_version"] = GraphStructureLimits.CurrentVersion,
                    ["chapter_id"] = chapterId,
                    ["nodes"] = new JsonArray(bucket.Select(Clone).ToArray()),
                };
                ProjectFileSystem.WriteAllTextAtomically(
                    Path.Combine(chaptersDir, SanitizeFileName(chapterId) + ".json"),
                    file.ToJsonString(Indented));
            }

            // Manifest: everything needed to rebuild the full document.
            var manifest = new JsonObject
            {
                ["format_version"] = GraphStructureLimits.CurrentVersion,
                ["node_order"] = new JsonArray(nodeOrder.Select(id => JsonValue.Create(id)).ToArray()),
                ["chapters"] = new JsonArray(order.Select(id => Clone(byId[id])).ToArray()),
            };
            if (root["start_node_id"] is { } start)
                manifest["start_node_id"] = Clone(start);
            if (root["groups"] is { } groups)
                manifest["groups"] = Clone(groups);
            if (root["subgraphs"] is { } subgraphs)
                manifest["subgraphs"] = Clone(subgraphs);
            ProjectFileSystem.WriteAllTextAtomically(
                Path.Combine(chaptersDir, IndexFileName),
                manifest.ToJsonString(Indented));

            return order;
        }

        /// <summary>
        /// Joins per-chapter files back into a single full story graph JSON
        /// document. The global node order from the manifest is restored, so
        /// <c>Merge(Split(x))</c> is a byte-stable round-trip modulo key
        /// order and insignificant whitespace (compare parsed, not raw).
        /// </summary>
        public static string Merge(string chaptersDir)
        {
            string indexPath = Path.Combine(chaptersDir, IndexFileName);
            if (!File.Exists(indexPath))
                throw new InvalidOperationException(
                    $"Chapter manifest '{IndexFileName}' not found; rejected.");
            JsonNode? manifest = JsonNode.Parse(File.ReadAllText(indexPath))
                ?? throw new InvalidOperationException("Empty chapter manifest; rejected.");

            var byId = new Dictionary<ulong, JsonNode>();
            foreach (string file in Directory.EnumerateFiles(chaptersDir, "*.json"))
            {
                if (string.Equals(Path.GetFileName(file), IndexFileName, StringComparison.OrdinalIgnoreCase))
                    continue;
                JsonNode? doc = JsonNode.Parse(File.ReadAllText(file));
                var fileNodes = doc?["nodes"]?.AsArray();
                if (fileNodes is null)
                    continue;
                foreach (var node in fileNodes)
                {
                    if (node is JsonObject obj &&
                        obj.TryGetPropertyValue("id", out var idNode) &&
                        idNode is JsonValue idValue &&
                        idValue.TryGetValue<ulong>(out ulong nodeId))
                    {
                        if (byId.ContainsKey(nodeId))
                            throw new InvalidOperationException(
                                $"Duplicate node id {nodeId} across chapter files; rejected.");
                        byId[nodeId] = node;
                    }
                }
            }

            var merged = new JsonArray();
            var order = manifest["node_order"]?.AsArray();
            if (order is not null)
            {
                foreach (var entry in order)
                {
                    if (entry is JsonValue value && value.TryGetValue<ulong>(out ulong nodeId) &&
                        byId.TryGetValue(nodeId, out var node))
                    {
                        merged.Add(Clone(node));
                        byId.Remove(nodeId);
                    }
                }
            }
            // Files may carry nodes the manifest never listed (hand-added);
            // append them deterministically instead of dropping user data.
            foreach (ulong extra in byId.Keys.OrderBy(id => id))
                merged.Add(Clone(byId[extra]));

            var full = new JsonObject
            {
                ["nodes"] = merged,
            };
            bool hasStructure = false;
            if (manifest["chapters"] is { } chapters)
            {
                full["chapters"] = Clone(chapters);
                hasStructure = true;
            }
            if (manifest["groups"] is { } groups)
            {
                full["groups"] = Clone(groups);
                hasStructure = true;
            }
            if (manifest["subgraphs"] is { } subgraphs)
            {
                full["subgraphs"] = Clone(subgraphs);
                hasStructure = true;
            }
            bool hasAssignments = merged.OfType<JsonObject>()
                .Any(n => n["chapter_id"]?.GetValue<string>() is { Length: > 0 });
            full["format_version"] = hasStructure || hasAssignments
                ? GraphStructureLimits.CurrentVersion
                : GraphStructureLimits.LegacyVersion;
            if (manifest["start_node_id"] is { } start)
                full["start_node_id"] = Clone(start);

            // Canonical key order: version, start, nodes, then structure.
            var ordered = new JsonObject();
            foreach (string key in new[] { "format_version", "start_node_id", "nodes", "groups", "subgraphs", "chapters" })
                if (full.TryGetPropertyValue(key, out var value) && value is not null)
                    ordered[key] = Clone(value);
            return ordered.ToJsonString(Indented);
        }

        /// <summary>Reads one per-chapter file (definition echo + node payloads).</summary>
        public static (string chapterId, JsonArray nodes) LoadChapterFile(string path)
        {
            JsonNode? doc = JsonNode.Parse(File.ReadAllText(path))
                ?? throw new InvalidOperationException($"Empty chapter file '{path}'; rejected.");
            string chapterId = doc["chapter_id"]?.GetValue<string>() ?? string.Empty;
            if (chapterId.Length == 0)
                throw new InvalidOperationException($"Chapter file '{path}' has no chapter_id; rejected.");
            JsonArray nodes = doc["nodes"]?.AsArray() ?? new JsonArray();
            return (chapterId, (JsonArray)Clone(nodes));
        }

        private static string SanitizeFileName(string chapterId)
        {
            char[] invalid = Path.GetInvalidFileNameChars();
            var chars = chapterId.Select(c =>
                invalid.Contains(c) || c == '/' || c == '\\' ? '_' : c).ToArray();
            string name = new string(chars).Trim();
            return name.Length > 0 ? name : DefaultChapterId;
        }

        private static JsonNode Clone(JsonNode node) =>
            JsonNode.Parse(node.ToJsonString())!;

        private static readonly JsonSerializerOptions Indented = new()
        {
            WriteIndented = true,
            Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        };
    }
}
