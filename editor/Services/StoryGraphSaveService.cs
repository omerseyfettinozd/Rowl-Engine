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

    public sealed class SnapshotNode
    {
        public ulong Id { get; init; }
        public string Title { get; init; } = string.Empty;
        public double X { get; init; }
        public double Y { get; init; }
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
        NodeViewModel? activeNode)
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
            ActiveNode = active
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
        var graph = new
        {
            format_version = 4,
            start_node_id = snapshot.StartNodeId,
            nodes = snapshot.Nodes.Select(n => new
            {
                id = n.Id,
                title = n.Title,
                editor_x = n.X,
                editor_y = n.Y,
                objects = n.Objects.Select(o => new
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
                next_nodes = n.NextNodes.Select(x => new
                {
                    id = x.Id,
                    label = x.Label,
                    option_id = x.OptionId
                }).ToArray(),
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
                character_rotation = n.CharacterRotation,
                character_scale_x = n.CharacterScaleX,
                character_scale_y = n.CharacterScaleY,
                dialogue_box_x = n.DialogueBoxX,
                dialogue_box_y = n.DialogueBoxY,
                dialogue_box_width = n.DialogueBoxWidth,
                dialogue_box_height = n.DialogueBoxHeight
            }).ToArray()
        };
        return JsonSerializer.Serialize(graph, IndentedOptions);
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
            return sequence == latestSequence();
        }
        catch (Exception ex)
        {
            log?.Invoke($"⚠️ Failed to save story graph: {ex.Message}");
            return false;
        }
    }
}
