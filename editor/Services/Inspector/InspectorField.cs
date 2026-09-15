using System.Collections.Generic;

namespace RowlEngine.Editor.Services.Inspector
{
    /// <summary>
    /// Faz 4 Dilim 4 — widget kind for one inspectable field. The kind
    /// decides which editor control the Inspector materializes; validation
    /// keys (<see cref="InspectorFieldDescriptor.Key"/>) stay stable even
    /// when the widget changes.
    /// </summary>
    public enum InspectorFieldKind
    {
        /// <summary>Single-line text.</summary>
        Text,
        /// <summary>Multi-line text.</summary>
        MultilineText,
        /// <summary>Slider + numeric box with min/max/step.</summary>
        Range,
        /// <summary>Boolean toggle.</summary>
        Toggle,
        /// <summary>Fixed option list.</summary>
        Enum,
        /// <summary>Hex color + preset swatches.</summary>
        Color,
        /// <summary>Project-relative asset path + browse + existence check.</summary>
        Asset,
        /// <summary>Chapter assignment dropdown (chapters come from the session).</summary>
        Chapter,
    }

    /// <summary>Asset family accepted by an <see cref="InspectorFieldKind.Asset"/> field.</summary>
    public enum InspectorAssetKind
    {
        Image,
        Audio,
        Script,
        Font,
    }

    /// <summary>One fixed option of an <see cref="InspectorFieldKind.Enum"/> field.</summary>
    public sealed record InspectorFieldOption(string Value, string Label);

    /// <summary>
    /// Faz 4 Dilim 4 — static description of one inspectable field:
    /// which widget to show, which limits apply, which validation key it
    /// reports under. Descriptors carry no values; values stay on the
    /// view models they describe.
    /// </summary>
    public sealed record InspectorFieldDescriptor(
        string Key,
        string Label,
        InspectorFieldKind Kind,
        string ComponentType = "node",
        double Min = 0,
        double Max = 100,
        double Step = 1,
        string Hint = "",
        InspectorAssetKind AssetKind = InspectorAssetKind.Image,
        IReadOnlyList<InspectorFieldOption>? Options = null)
    {
        /// <summary>Well-known validation/chat field keys (stable contract).</summary>
        public static class Keys
        {
            public const string NodeTitle = "node.title";
            public const string NodeChapter = "node.chapter";
            public const string NodePorts = "node.ports";
            public const string DialogueSpeaker = "dialogue.speaker";
            public const string DialogueText = "dialogue.dialogue_text";
            public const string DialogueContentId = "dialogue.content_id";
            public const string DialogueSpeakerFontSize = "dialogue.speaker_font_size";
            public const string DialogueFontSize = "dialogue.font_size";
            public const string DialogueSpeakerColor = "dialogue.speaker_color";
            public const string DialogueTextColor = "dialogue.text_color";
            public const string DialogueBoxColor = "dialogue.box_color";
            public const string DialogueBoxOpacity = "dialogue.box_opacity";
            public const string DialogueTextSpeed = "dialogue.text_speed";
            public const string DialogueBlipPitch = "dialogue.blip_pitch";
            public const string DialogueBlipVolume = "dialogue.blip_volume";
            public const string DialogueBlipSound = "dialogue.blip_sound";
            public const string BackgroundTexture = "background.texture";
            public const string BackgroundOpacity = "background.opacity";
            public const string BackgroundScale = "background.scale";
            public const string CharacterSprite = "character.sprite";
            public const string CharacterVoiceBlip = "character.voice_blip";
            public const string AudioBgmTrack = "audio.bgm_track";
            public const string AudioSfxTrack = "audio.sfx_track";
            public const string ChoiceOptionTarget = "choice.option_target";
            public const string ChoiceOptionText = "choice.option_text";
            public const string ScriptPath = "script.script_path";
        }
    }
}
