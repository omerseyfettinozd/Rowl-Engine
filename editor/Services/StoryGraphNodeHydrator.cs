using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;
using System.Text.Json;

namespace RowlEngine.Editor.Services;

/// <summary>Creates the stable editor-facing shell for one persisted graph node.</summary>
internal static class StoryGraphNodeHydrator
{
    public static NodeViewModel CreateShell(JsonElement nodeJson, int nodeIndex)
    {
        ulong nodeId = nodeJson.TryGetProperty("id", out var id) ? id.GetUInt64() : 0;
        string title = nodeJson.TryGetProperty("title", out var titleValue)
            ? titleValue.GetString() ?? $"Node #{nodeId}"
            : $"Node #{nodeId}";
        double x = nodeJson.TryGetProperty("editor_x", out var xValue)
            ? xValue.GetDouble()
            : 60 + (nodeIndex % 5) * 280;
        double y = nodeJson.TryGetProperty("editor_y", out var yValue)
            ? yValue.GetDouble()
            : 80 + (nodeIndex / 5) * 220;
        return new NodeViewModel(nodeId, title, x, y, bare: true);
    }

    public static void EnsureDefaultObjects(NodeViewModel node)
    {
        if (node.Objects.Count != 0) return;
        node.CreateObject("Background").AddComponent<BackgroundComponentViewModel>();
        node.CreateObject("Evelyn").AddComponent<CharacterComponentViewModel>();
        node.CreateObject("Dialogue Box").AddComponent<DialogueComponentViewModel>();
        node.CreateObject("Audio").AddComponent<AudioComponentViewModel>();
    }

    public static void PopulateLegacyFields(NodeViewModel node, JsonElement value)
    {
        var dialogue = node.CreateObject("Dialogue Box").AddComponent<DialogueComponentViewModel>();
        dialogue.Speaker = Text(value, "speaker", "Evelyn");
        dialogue.DialogueText = Text(value, "dialogue", string.Empty);
        dialogue.X = Number(value, "dialogue_box_x", dialogue.X); dialogue.Y = Number(value, "dialogue_box_y", dialogue.Y);
        dialogue.Width = Number(value, "dialogue_box_width", dialogue.Width); dialogue.Height = Number(value, "dialogue_box_height", dialogue.Height);
        dialogue.TypewriterSound = Text(value, "voice_blip_sound", dialogue.TypewriterSound);
        dialogue.VoiceBlipPitch = Number(value, "voice_blip_pitch", dialogue.VoiceBlipPitch);
        dialogue.VoiceBlipVariance = Number(value, "voice_blip_variance", dialogue.VoiceBlipVariance);
        dialogue.VoiceBlipCadence = value.TryGetProperty("voice_blip_cadence", out var vbc) && vbc.TryGetInt32(out var vbci) ? vbci : dialogue.VoiceBlipCadence;
        dialogue.VoiceBlipSkipPunctuation = value.TryGetProperty("voice_blip_skip_punctuation", out var vbsp) && vbsp.ValueKind == JsonValueKind.False ? false : dialogue.VoiceBlipSkipPunctuation;
        dialogue.VoiceBlipVolume = Number(value, "voice_blip_volume", dialogue.VoiceBlipVolume);
        dialogue.VoiceBlipChannel = value.TryGetProperty("voice_blip_channel", out var vbch) && vbch.TryGetInt32(out var vbchi) && vbchi == 2 ? "Sfx" : "Voice";

        var background = node.CreateObject("Background").AddComponent<BackgroundComponentViewModel>();
        background.Texture = Text(value, "background", "bg_beach_sunset.png");
        background.X = Number(value, "background_x", background.X); background.Y = Number(value, "background_y", background.Y);
        background.Width = Number(value, "background_width", background.Width); background.Height = Number(value, "background_height", background.Height);
        background.Rotation = Number(value, "background_rotation", background.Rotation);
        background.ParallaxFactorX = Number(value, "background_parallax_x", background.ParallaxFactorX);
        background.ParallaxFactorY = Number(value, "background_parallax_y", background.ParallaxFactorY);
        background.Opacity = Number(value, "background_opacity", background.Opacity);

        var character = node.CreateObject("Evelyn").AddComponent<CharacterComponentViewModel>();
        character.Sprite = Text(value, "character", "spr_evelyn.png");
        character.Position = Text(value, "character_pos", "Right");
        character.X = Number(value, "character_x", character.X); character.Y = Number(value, "character_y", character.Y);
        character.Width = Number(value, "character_width", character.Width); character.Height = Number(value, "character_height", character.Height);
        character.Scale = Number(value, "character_scale", character.Scale);
        character.Rotation = Number(value, "character_rotation", character.Rotation);
        character.ScaleX = Number(value, "character_scale_x", character.ScaleX);
        character.ScaleY = Number(value, "character_scale_y", character.ScaleY);
        character.VoiceBlipSound = Text(value, "character_voice_blip_sound", character.VoiceBlipSound);
        character.VoiceBlipPitch = Number(value, "character_voice_blip_pitch", character.VoiceBlipPitch);
        character.VoiceBlipVariance = Number(value, "character_voice_blip_variance", character.VoiceBlipVariance);
        character.VoiceBlipCadence = value.TryGetProperty("character_voice_blip_cadence", out var cvbc) && cvbc.TryGetInt32(out var cvbci) ? cvbci : character.VoiceBlipCadence;

        node.CreateObject("Audio").AddComponent<AudioComponentViewModel>().DspFilter = Text(value, "dsp", "Normal");
    }

    private static string Text(JsonElement value, string property, string fallback) =>
        value.TryGetProperty(property, out var item) ? item.GetString() ?? fallback : fallback;

    private static double Number(JsonElement value, string property, double fallback) =>
        value.TryGetProperty(property, out var item) && item.TryGetDouble(out var result) ? result : fallback;
}
