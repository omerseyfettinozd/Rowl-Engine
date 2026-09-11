using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Owns the JSON representation of editable story data. Keeping this logic out
/// of the main window view model makes the on-disk schema independently testable
/// and prevents UI concerns from spreading into save/build paths.
/// </summary>
internal static class StoryGraphSerializer
{
    private static readonly JsonSerializerOptions IndentedOptions = new()
    {
        WriteIndented = true,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
    };

    public static string SerializeFullStoryGraph(
        IEnumerable<NodeViewModel> nodes,
        IEnumerable<ConnectionViewModel> connections,
        ulong startNodeId)
    {
        var connectionList = connections.ToList();
        var graph = new
        {
            format_version = 4,
            start_node_id = startNodeId,
            nodes = nodes.Select(node => SerializeNode(node, connectionList)).ToArray()
        };

        return JsonSerializer.Serialize(graph, IndentedOptions);
    }

    public static string SerializeActiveStory(NodeViewModel node)
    {
        var components = node.AllComponents.Select(component => new
        {
            type = component.TypeKey,
            id = component.ComponentId,
            enabled = component.IsEnabled,
            data = component.Serialize()
        }).ToArray();

        var activeNode = new
        {
            format_version = 2,
            node_id = node.Id,
            components,
            speaker = node.Speaker,
            dialogue = node.DialogueText,
            background = node.BackgroundTexture,
            background_x = node.BackgroundX,
            background_y = node.BackgroundY,
            background_width = node.BackgroundWidth,
            background_height = node.BackgroundHeight,
            background_rotation = node.BackgroundRotation,
            background_parallax_x = node.BackgroundParallaxX,
            background_parallax_y = node.BackgroundParallaxY,
            background_opacity = node.BackgroundOpacity,
            character = node.CharacterSprite,
            character_pos = node.CharacterPosition,
            character_x = node.CharacterX,
            character_y = node.CharacterY,
            character_width = node.CharacterWidth,
            character_height = node.CharacterHeight,
            character_scale = node.CharacterScale,
            dialogue_box_x = node.DialogueBoxX,
            dialogue_box_y = node.DialogueBoxY,
            dialogue_box_width = node.DialogueBoxWidth,
            dialogue_box_height = node.DialogueBoxHeight,
            voice_blip_sound = node.VoiceBlipSound,
            voice_blip_pitch = node.VoiceBlipPitch,
            voice_blip_variance = node.VoiceBlipVariance,
            voice_blip_cadence = node.VoiceBlipCadence,
            voice_blip_skip_punctuation = node.VoiceBlipSkipPunctuation,
            voice_blip_volume = node.VoiceBlipVolume,
            voice_blip_channel = node.VoiceBlipChannel == "Sfx" ? 2 : 1,
            dsp = node.DspFilter
        };

        return JsonSerializer.Serialize(activeNode, IndentedOptions);
    }

    /// <summary>Serializes the enabled scene components for the native live preview.</summary>
    public static string SerializePreviewComponents(NodeViewModel node)
    {
        var components = node.AllComponents
            .Where(component => component.IsEnabled)
            .Select(component => new
            {
                type = component.TypeKey,
                id = component.ComponentId,
                enabled = component.IsEnabled,
                data = component.Serialize()
            }).ToArray();

        return JsonSerializer.Serialize(components, new JsonSerializerOptions
        {
            Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
        });
    }

    private static object SerializeNode(NodeViewModel node, IReadOnlyCollection<ConnectionViewModel> connections)
    {
        var outgoingConnections = connections
            .Where(connection => connection.SourceNode == node && connection.TargetNode != null)
            .ToList();
        var choiceComponents = node.Objects
            .SelectMany(frameObject => frameObject.Components)
            .OfType<ChoiceComponentViewModel>()
            .ToList();
        var choiceOptions = choiceComponents
            .Where(choice => choice.IsEnabled && choice.OwnerObject?.IsActive != false)
            .SelectMany(choice => choice.Options)
            .ToList();
        var nextNodes = choiceComponents.Count > 0
            ? choiceOptions.Where(option => option.IsEnabled && option.TargetNodeId != 0).Select(option => new
            {
                id = option.TargetNodeId,
                label = option.Text,
                option_id = option.OptionId
            }).ToArray()
            : outgoingConnections.Select(connection => new
            {
                id = connection.TargetNode!.Id,
                label = string.Empty,
                option_id = connection.OptionId
            }).ToArray();

        var objects = node.Objects.Select(frameObject => new
        {
            id = frameObject.Id,
            name = frameObject.Name,
            is_active = frameObject.IsActive,
            components = frameObject.Components.Select(component => new
            {
                type = component.TypeKey,
                id = component.ComponentId,
                enabled = component.IsEnabled,
                data = component.Serialize()
            }).ToArray()
        }).ToArray();

        return new
        {
            id = node.Id,
            title = node.Title,
            editor_x = node.X,
            editor_y = node.Y,
            objects,
            next_nodes = nextNodes,
            speaker = node.Speaker,
            dialogue = node.DialogueText,
            background = node.BackgroundTexture,
            background_x = node.BackgroundX,
            background_y = node.BackgroundY,
            background_width = node.BackgroundWidth,
            background_height = node.BackgroundHeight,
            background_rotation = node.BackgroundRotation,
            background_parallax_x = node.BackgroundParallaxX,
            background_parallax_y = node.BackgroundParallaxY,
            background_opacity = node.BackgroundOpacity,
            character = node.CharacterSprite,
            character_pos = node.CharacterPosition,
            character_x = node.CharacterX,
            character_y = node.CharacterY,
            character_width = node.CharacterWidth,
            character_height = node.CharacterHeight,
            character_scale = node.CharacterScale,
            character_rotation = node.CharacterRotation,
            character_scale_x = node.CharacterScaleX,
            character_scale_y = node.CharacterScaleY,
            dialogue_box_x = node.DialogueBoxX,
            dialogue_box_y = node.DialogueBoxY,
            dialogue_box_width = node.DialogueBoxWidth,
            dialogue_box_height = node.DialogueBoxHeight
        };
    }
}
