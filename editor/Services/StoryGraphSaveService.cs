using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Detached save snapshot. Captured on the UI thread (the only place that may
/// touch live view models); serialization and disk I/O run on a worker thread.
/// Contains zero live references by construction.
/// </summary>
internal sealed class StoryGraphSaveSnapshot
{
    public ulong StartNodeId { get; init; }
    public List<SnapshotNode> Nodes { get; init; } = new();
    public SnapshotNode? ActiveNode { get; init; }

    /// <summary>
    /// Faz 4 Dilim 3 — vNext structure (groups / subgraphs / chapters).
    /// Empty by default so tagless/chapterless graphs stay byte-stable v4.
    /// </summary>
    public GraphStructureDocument Structure { get; init; } = new();

    public sealed class SnapshotNode
    {
        public ulong Id { get; init; }
        public string Title { get; init; } = string.Empty;
        public double X { get; init; }
        public double Y { get; init; }
        /// <summary>Owning chapter id (empty = unassigned, v4 case).</summary>
        public string ChapterId { get; init; } = string.Empty;
        /// <summary>Visual color tag (empty = none).</summary>
        public string ColorTag { get; init; } = string.Empty;
        /// <summary>Free-form category labels.</summary>
        public List<string> Tags { get; init; } = new();
        public string Speaker { get; init; } = string.Empty;
        public string DialogueText { get; init; } = string.Empty;
        public string BackgroundTexture { get; init; } = string.Empty;
        public double BackgroundX { get; init; }
        public double BackgroundY { get; init; }
        public double BackgroundWidth { get; init; }
        public double BackgroundHeight { get; init; }
        public double BackgroundRotation { get; init; }
        public double BackgroundParallaxX { get; init; }
        public double BackgroundParallaxY { get; init; }
        public double BackgroundOpacity { get; init; }
        public string CharacterSprite { get; init; } = string.Empty;
        public string CharacterPosition { get; init; } = string.Empty;
        public double CharacterX { get; init; }
        public double CharacterY { get; init; }
        public double CharacterWidth { get; init; }
        public double CharacterHeight { get; init; }
        public double CharacterScale { get; init; }
        public double CharacterRotation { get; init; }
        public double CharacterScaleX { get; init; }
        public double CharacterScaleY { get; init; }
        public double DialogueBoxX { get; init; }
        public double DialogueBoxY { get; init; }
        public double DialogueBoxWidth { get; init; }
        public double DialogueBoxHeight { get; init; }
        public string VoiceBlipSound { get; init; } = string.Empty;
        public double VoiceBlipPitch { get; init; }
        public double VoiceBlipVariance { get; init; }
        public int VoiceBlipCadence { get; init; }
        public bool VoiceBlipSkipPunctuation { get; init; }
        public double VoiceBlipVolume { get; init; }
        public string VoiceBlipChannel { get; init; } = string.Empty;
        public string DspFilter { get; init; } = string.Empty;
        public List<SnapshotObject> Objects { get; init; } = new();
        public List<SnapshotNext> NextNodes { get; init; } = new();
    }

    public sealed class SnapshotObject
    {
        public string Id { get; init; } = string.Empty;
        public string Name { get; init; } = string.Empty;
        public bool IsActive { get; init; }
        public List<SnapshotComponent> Components { get; init; } = new();
    }

    public sealed class SnapshotComponent
    {
        public string Type { get; init; } = string.Empty;
        public string Id { get; init; } = string.Empty;
        public bool Enabled { get; init; }
        public Dictionary<string, object> Data { get; init; } = new();
    }

    public sealed class SnapshotNext
    {
        public ulong Id { get; init; }
        public string Label { get; init; } = string.Empty;
        public string OptionId { get; init; } = string.Empty;
    }
}

/// <summary>
/// MS-2 save path: UI thread captures a detached snapshot; worker thread
/// serializes and writes. A monotonic sequence number guarantees an older
/// save can never overwrite a newer one.
/// </summary>
internal static class StoryGraphSaveService
{
    private static readonly JsonSerializerOptions IndentedOptions = new()
    {
        WriteIndented = true,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    /// <summary>
    /// Reads live view models. MUST run on the UI thread.
    /// </summary>
    public static StoryGraphSaveSnapshot Capture(
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong startNodeId,
        NodeViewModel? activeNode,
        GraphStructureDocument? structure = null)
    {
        var nodeList = nodes.ToList();
        var connectionList = connections.ToList();
        var byId = nodeList.ToDictionary(n => n.Id);
        var snapshotNodes = nodeList.Select(n => CaptureNode(n, connectionList)).ToList();

        StoryGraphSaveSnapshot.SnapshotNode? active = null;
        var activeSource = activeNode ?? nodeList.FirstOrDefault();
        if (activeSource != null && byId.TryGetValue(activeSource.Id, out var match))
            active = snapshotNodes.FirstOrDefault(n => n.Id == match.Id);

        return new StoryGraphSaveSnapshot
        {
            StartNodeId = startNodeId,
            Nodes = snapshotNodes,
            ActiveNode = active,
            Structure = structure ?? new GraphStructureDocument(),
        };
    }

    private static StoryGraphSaveSnapshot.SnapshotNode CaptureNode(
        NodeViewModel node, IReadOnlyCollection<ConnectionViewModel> connections)
    {
        var outgoing = connections
            .Where(c => c.SourceNode == node && c.TargetNode != null)
            .ToList();
        var choiceComponents = node.Objects
            .SelectMany(o => o.Components)
            .OfType<ChoiceComponentViewModel>()
            .ToList();
        var choiceOptions = choiceComponents
            .Where(c => c.IsEnabled && c.OwnerObject?.IsActive != false)
            .SelectMany(c => c.Options)
            .ToList();
        var next = (choiceComponents.Count > 0
            ? choiceOptions
                .Where(o => o.IsEnabled && o.TargetNodeId != 0)
                .Select(o => new StoryGraphSaveSnapshot.SnapshotNext
                {
                    Id = o.TargetNodeId,
                    Label = o.Text,
                    OptionId = o.OptionId
                })
            : outgoing.Select(c => new StoryGraphSaveSnapshot.SnapshotNext
            {
                Id = c.TargetNode!.Id,
                Label = string.Empty,
                OptionId = c.OptionId
            })).ToList();

        return new StoryGraphSaveSnapshot.SnapshotNode
        {
            Id = node.Id,
            Title = node.Title,
            X = node.X,
            Y = node.Y,
            ChapterId = node.ChapterId ?? string.Empty,
            ColorTag = node.ColorTag ?? string.Empty,
            Tags = node.Tags.ToList(),
            Speaker = node.Speaker,
            DialogueText = node.DialogueText,
            BackgroundTexture = node.BackgroundTexture,
            BackgroundX = node.BackgroundX,
            BackgroundY = node.BackgroundY,
            BackgroundWidth = node.BackgroundWidth,
            BackgroundHeight = node.BackgroundHeight,
            BackgroundRotation = node.BackgroundRotation,
            BackgroundParallaxX = node.BackgroundParallaxX,
            BackgroundParallaxY = node.BackgroundParallaxY,
            BackgroundOpacity = node.BackgroundOpacity,
            CharacterSprite = node.CharacterSprite,
            CharacterPosition = node.CharacterPosition,
            CharacterX = node.CharacterX,
            CharacterY = node.CharacterY,
            CharacterWidth = node.CharacterWidth,
            CharacterHeight = node.CharacterHeight,
            CharacterScale = node.CharacterScale,
            CharacterRotation = node.CharacterRotation,
            CharacterScaleX = node.CharacterScaleX,
            CharacterScaleY = node.CharacterScaleY,
            DialogueBoxX = node.DialogueBoxX,
            DialogueBoxY = node.DialogueBoxY,
            DialogueBoxWidth = node.DialogueBoxWidth,
            DialogueBoxHeight = node.DialogueBoxHeight,
            VoiceBlipSound = node.VoiceBlipSound,
            VoiceBlipPitch = node.VoiceBlipPitch,
            VoiceBlipVariance = node.VoiceBlipVariance,
            VoiceBlipCadence = node.VoiceBlipCadence,
            VoiceBlipSkipPunctuation = node.VoiceBlipSkipPunctuation,
            VoiceBlipVolume = node.VoiceBlipVolume,
            VoiceBlipChannel = node.VoiceBlipChannel,
            DspFilter = node.DspFilter,
            Objects = node.Objects.Select(o => new StoryGraphSaveSnapshot.SnapshotObject
            {
                Id = o.Id,
                Name = o.Name,
                IsActive = o.IsActive,
                Components = o.Components.Select(c => new StoryGraphSaveSnapshot.SnapshotComponent
                {
                    Type = c.TypeKey,
                    Id = c.ComponentId,
                    Enabled = c.IsEnabled,
                    Data = new Dictionary<string, object>(c.Serialize())
                }).ToList()
            }).ToList(),
            NextNodes = next
        };
    }

    public static string SerializeFullGraph(StoryGraphSaveSnapshot snapshot)
    {
        // v5 is written only when the document carries vNext structure or
        // chapter assignments; otherwise output stays byte-stable v4.
        var structure = snapshot.Structure ?? new GraphStructureDocument();
        bool hasStructure = !structure.IsEmpty;
        bool hasChapterAssignments = snapshot.Nodes.Any(n => !string.IsNullOrEmpty(n.ChapterId));
        int formatVersion = hasStructure || hasChapterAssignments
            ? GraphStructureLimits.CurrentVersion
            : GraphStructureLimits.LegacyVersion;
        var graph = new Dictionary<string, object?>
        {
            ["format_version"] = formatVersion,
            ["start_node_id"] = snapshot.StartNodeId,
            ["nodes"] = snapshot.Nodes.Select(RenderSnapshotNode).ToArray(),
        };
        if (hasStructure)
        {
            if (structure.Groups.Count != 0)
                graph["groups"] = structure.Groups.Select(RenderGroup).ToArray();
            if (structure.Subgraphs.Count != 0)
                graph["subgraphs"] = structure.Subgraphs.Select(RenderSubgraph).ToArray();
            if (structure.Chapters.Count != 0)
                graph["chapters"] = structure.Chapters.Select(RenderChapter).ToArray();
        }
        return JsonSerializer.Serialize(graph, IndentedOptions);
    }

    private static object RenderGroup(CanvasGroup group) => new
    {
        id = group.Id,
        title = group.Title,
        color = group.Color,
        x = group.X,
        y = group.Y,
        width = group.Width,
        height = group.Height,
        node_ids = group.NodeIds.ToArray()
    };

    private static object RenderSubgraph(SubgraphDefinition subgraph) => new
    {
        id = subgraph.Id,
        title = subgraph.Title,
        entry_node_id = subgraph.EntryNodeId,
        exit_node_ids = subgraph.ExitNodeIds.ToArray(),
        node_ids = subgraph.NodeIds.ToArray()
    };

    private static object RenderChapter(ChapterDefinition chapter)
    {
        var rendered = new Dictionary<string, object?>
        {
            ["id"] = chapter.Id,
            ["title"] = chapter.Title,
            ["order"] = chapter.Order,
            ["summary"] = chapter.Summary
        };
        if (chapter.StartNodeId is { } startNodeId)
            rendered["start_node_id"] = startNodeId;
        return rendered;
    }

    private static object RenderSnapshotNode(StoryGraphSaveSnapshot.SnapshotNode n)
    {
        // Dictionary so chapter_id / metadata are omitted (not null) when
        // unassigned — mirroring StoryGraphSerializer node-for-node.
        var rendered = new Dictionary<string, object?>
        {
            ["id"] = n.Id,
            ["title"] = n.Title,
            ["editor_x"] = n.X,
            ["editor_y"] = n.Y,
            ["objects"] = n.Objects.Select(o => new
            {
                id = o.Id,
                name = o.Name,
                is_active = o.IsActive,
                components = o.Components.Select(c => new
                {
                    type = c.Type,
                    id = c.Id,
                    enabled = c.Enabled,
                    data = c.Data
                }).ToArray()
            }).ToArray(),
            ["next_nodes"] = n.NextNodes.Select(x => new
            {
                id = x.Id,
                label = x.Label,
                option_id = x.OptionId
            }).ToArray(),
            ["speaker"] = n.Speaker,
            ["dialogue"] = n.DialogueText,
            ["background"] = n.BackgroundTexture,
            ["background_x"] = n.BackgroundX,
            ["background_y"] = n.BackgroundY,
            ["background_width"] = n.BackgroundWidth,
            ["background_height"] = n.BackgroundHeight,
            ["background_rotation"] = n.BackgroundRotation,
            ["background_parallax_x"] = n.BackgroundParallaxX,
            ["background_parallax_y"] = n.BackgroundParallaxY,
            ["background_opacity"] = n.BackgroundOpacity,
            ["character"] = n.CharacterSprite,
            ["character_pos"] = n.CharacterPosition,
            ["character_x"] = n.CharacterX,
            ["character_y"] = n.CharacterY,
            ["character_width"] = n.CharacterWidth,
            ["character_height"] = n.CharacterHeight,
            ["character_scale"] = n.CharacterScale,
            ["character_rotation"] = n.CharacterRotation,
            ["character_scale_x"] = n.CharacterScaleX,
            ["character_scale_y"] = n.CharacterScaleY,
            ["dialogue_box_x"] = n.DialogueBoxX,
            ["dialogue_box_y"] = n.DialogueBoxY,
            ["dialogue_box_width"] = n.DialogueBoxWidth,
            ["dialogue_box_height"] = n.DialogueBoxHeight,
        };
        if (!string.IsNullOrEmpty(n.ChapterId))
            rendered["chapter_id"] = n.ChapterId;
        // Same omission rule as StoryGraphSerializer: untagged nodes carry no
        // metadata key, so legacy v4 documents stay byte-stable.
        var cleanTags = (n.Tags ?? new List<string>())
            .Select(tag => Services.Search.NodeColorTags.NormalizeListTag(tag))
            .Where(tag => tag is not null)
            .Distinct()
            .Take(Services.Search.NodeColorTags.MaxTagsPerNode)
            .ToArray();
        bool hasColorTag = !string.IsNullOrEmpty(n.ColorTag);
        if (hasColorTag || cleanTags.Length > 0)
        {
            var metadata = new Dictionary<string, object?>();
            if (hasColorTag)
                metadata["color_tag"] = n.ColorTag;
            if (cleanTags.Length > 0)
                metadata["tags"] = cleanTags;
            rendered["metadata"] = metadata;
        }
        return rendered;
    }

    public static string? SerializeActiveStory(StoryGraphSaveSnapshot snapshot)
    {
        var n = snapshot.ActiveNode;
        if (n == null) return null;
        var activeNode = new
        {
            format_version = 2,
            node_id = n.Id,
            components = n.Objects.Where(o => o.IsActive).SelectMany(o => o.Components.Select(c => new
            {
                type = c.Type,
                id = c.Id,
                enabled = c.Enabled,
                data = c.Data
            })).ToArray(),
            speaker = n.Speaker,
            dialogue = n.DialogueText,
            background = n.BackgroundTexture,
            background_x = n.BackgroundX,
            background_y = n.BackgroundY,
            background_width = n.BackgroundWidth,
            background_height = n.BackgroundHeight,
            background_rotation = n.BackgroundRotation,
            background_parallax_x = n.BackgroundParallaxX,
            background_parallax_y = n.BackgroundParallaxY,
            background_opacity = n.BackgroundOpacity,
            character = n.CharacterSprite,
            character_pos = n.CharacterPosition,
            character_x = n.CharacterX,
            character_y = n.CharacterY,
            character_width = n.CharacterWidth,
            character_height = n.CharacterHeight,
            character_scale = n.CharacterScale,
            dialogue_box_x = n.DialogueBoxX,
            dialogue_box_y = n.DialogueBoxY,
            dialogue_box_width = n.DialogueBoxWidth,
            dialogue_box_height = n.DialogueBoxHeight,
            voice_blip_sound = n.VoiceBlipSound,
            voice_blip_pitch = n.VoiceBlipPitch,
            voice_blip_variance = n.VoiceBlipVariance,
            voice_blip_cadence = n.VoiceBlipCadence,
            voice_blip_skip_punctuation = n.VoiceBlipSkipPunctuation,
            voice_blip_volume = n.VoiceBlipVolume,
            voice_blip_channel = n.VoiceBlipChannel == "Sfx" ? 2 : 1,
            dsp = n.DspFilter
        };
        return JsonSerializer.Serialize(activeNode, IndentedOptions);
    }

    /// <summary>
    /// Serializes and writes on ANY thread. Aborts without touching disk when
    /// this sequence is stale (a newer save was scheduled meanwhile).
    /// </summary>
    public static bool TryWriteSnapshot(
        StoryGraphSaveSnapshot snapshot,
        string assetsJsonPath,
        long sequence,
        Func<long> latestSequence,
        Action<string>? log = null)
    {
        try
        {
            string fullGraphJson = SerializeFullGraph(snapshot);
            string? activeJson = SerializeActiveStory(snapshot);

            if (sequence != latestSequence())
                return false;

            Directory.CreateDirectory(assetsJsonPath);
            ProjectFileSystem.WriteAllTextAtomically(
                Path.Combine(assetsJsonPath, "full_story_graph.json"), fullGraphJson);

            if (sequence != latestSequence())
                return false;

            if (activeJson != null)
            {
                ProjectFileSystem.WriteAllTextAtomically(
                    Path.Combine(assetsJsonPath, "active_story.json"), activeJson);
            }

            RemoveStaleLegacyCopy(assetsJsonPath, log);
            return sequence == latestSequence();
        }
        catch (Exception ex)
        {
            log?.Invoke($"Failed to save story graph: {ex.Message}");
            return false;
        }
    }

    /// <summary>
    /// Best-effort removal of a stale `Assets/full_story_graph.json` left by
    /// pre-MS-2 writers. Writes go to the canonical `Assets/json/` address
    /// only; a lingering legacy copy must never shadow it. Mirrors the
    /// cleanup in <see cref="StoryGraphDocumentWriter"/> for the sync path.
    /// </summary>
    private static void RemoveStaleLegacyCopy(string assetsJsonPath, Action<string>? log)
    {
        try
        {
            if (!string.Equals(Path.GetFileName(assetsJsonPath), "json", StringComparison.OrdinalIgnoreCase))
                return;
            string? assetsPath = Path.GetDirectoryName(assetsJsonPath);
            if (string.IsNullOrEmpty(assetsPath))
                return;
            string legacyPath = Path.Combine(assetsPath, "full_story_graph.json");
            string canonicalPath = Path.Combine(assetsJsonPath, "full_story_graph.json");
            if (!string.Equals(legacyPath, canonicalPath, StringComparison.OrdinalIgnoreCase) &&
                File.Exists(legacyPath))
            {
                File.Delete(legacyPath);
            }
        }
        catch (Exception ex)
        {
            log?.Invoke($"Legacy graph copy could not be removed: {ex.Message}");
        }
    }
}
