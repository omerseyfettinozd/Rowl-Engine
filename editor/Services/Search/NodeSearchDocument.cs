using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using RowlEngine.Editor.ViewModels;
using RowlEngine.Editor.ViewModels.Components;

namespace RowlEngine.Editor.Services.Search;

/// <summary>
/// Faz 4 Dilim 2 — memory-friendly per-node search snapshot. Built once per
/// node (rebuilt only when that node reports a content change), so a query
/// over 2.000 nodes scans plain strings instead of walking the component
/// tree. Original casing is kept: matching uses ordinal-ignore-case scans
/// and snippets window the original text.
/// </summary>
public sealed class NodeSearchDocument
{
    public NodeViewModel Node { get; }

    public ulong NodeId => Node.Id;

    public string Title { get; private set; } = string.Empty;

    public string NodeIdText { get; private set; } = string.Empty;

    public string Speakers { get; private set; } = string.Empty;

    public string DialogueTexts { get; private set; } = string.Empty;

    public string ChoiceTexts { get; private set; } = string.Empty;

    public string ContentIds { get; private set; } = string.Empty;

    public string Variables { get; private set; } = string.Empty;

    public string Conditions { get; private set; } = string.Empty;

    public string Assets { get; private set; } = string.Empty;

    public string Scripts { get; private set; } = string.Empty;

    public string ColorTags { get; private set; } = string.Empty;

    /// <summary>Semantic node kinds (dialogue/choice/condition/.../jump).</summary>
    public HashSet<string> Kinds { get; } = new(StringComparer.OrdinalIgnoreCase);

    public string KindsText { get; private set; } = string.Empty;

    private NodeSearchDocument(NodeViewModel node) => Node = node;

    public static NodeSearchDocument Build(NodeViewModel node, int outgoingEdges = 0)
    {
        var document = new NodeSearchDocument(node);
        document.Refresh(outgoingEdges);
        return document;
    }

    public void Refresh(int outgoingEdges = 0)
    {
        Title = Node.Title ?? string.Empty;
        NodeIdText = Node.Id.ToString();

        var speakers = new List<string>();
        var dialogues = new List<string>();
        var choices = new List<string>();
        var contentIds = new List<string>();
        var variables = new List<string>();
        var conditions = new List<string>();
        var assets = new List<string>();
        var scripts = new List<string>();

        bool hasDialogue = false;
        bool hasChoiceOptions = false;
        bool hasCondition = false;
        bool hasVariable = false;
        bool hasScript = false;
        bool hasTransition = false;
        bool hasCamera = false;
        bool hasAudioTrack = false;
        bool hasDialogueText = false;

        foreach (NodeComponentViewModel component in Node.AllComponents)
        {
            switch (component)
            {
                case DialogueComponentViewModel dialogue:
                    hasDialogue = true;
                    Add(speakers, dialogue.Speaker);
                    Add(dialogues, dialogue.DialogueText);
                    Add(contentIds, dialogue.ContentId);
                    Add(assets, dialogue.TypewriterSound);
                    Add(assets, dialogue.CustomBoxTexture);
                    if (!string.IsNullOrWhiteSpace(dialogue.DialogueText))
                        hasDialogueText = true;
                    break;
                case ChoiceComponentViewModel choice:
                    foreach (ChoiceOptionViewModel option in choice.Options)
                    {
                        Add(choices, option.Text);
                        Add(conditions, option.Condition);
                        Add(assets, option.BackgroundImage);
                        Add(assets, option.NormalImage);
                        Add(assets, option.HoverImage);
                        Add(assets, option.PressedImage);
                        Add(assets, option.DisabledImage);
                        if (!string.IsNullOrWhiteSpace(option.Text))
                            hasChoiceOptions = true;
                    }
                    break;
                case ConditionComponentViewModel condition:
                    hasCondition = true;
                    Add(conditions, condition.Expression);
                    Add(conditions, condition.FailTargetNodeId);
                    break;
                case VariableComponentViewModel variable:
                    hasVariable = true;
                    Add(variables, variable.Key);
                    Add(variables, variable.Value);
                    Add(variables, variable.Operation);
                    break;
                case ScriptComponentViewModel script:
                    hasScript = true;
                    Add(scripts, script.ScriptPath);
                    Add(scripts, script.InlineCode);
                    break;
                case BackgroundComponentViewModel background:
                    Add(assets, background.Texture);
                    break;
                case CharacterComponentViewModel character:
                    Add(assets, character.Sprite);
                    Add(assets, character.VoiceBlipSound);
                    break;
                case AudioComponentViewModel audio:
                    Add(assets, audio.BgmTrack);
                    Add(assets, audio.SfxTrack);
                    if (!string.IsNullOrWhiteSpace(audio.BgmTrack) ||
                        !string.IsNullOrWhiteSpace(audio.SfxTrack))
                        hasAudioTrack = true;
                    break;
                case TransitionComponentViewModel:
                    hasTransition = true;
                    break;
                case CameraComponentViewModel:
                    hasCamera = true;
                    break;
            }
        }

        Speakers = Join(speakers);
        DialogueTexts = Join(dialogues);
        ChoiceTexts = Join(choices);
        ContentIds = Join(contentIds);
        Variables = Join(variables);
        Conditions = Join(conditions);
        Assets = Join(assets);
        Scripts = Join(scripts);

        var tags = new List<string>();
        if (!string.IsNullOrEmpty(Node.ColorTag))
            tags.Add(Node.ColorTag);
        foreach (string tag in Node.Tags)
        {
            string? normalized = NodeColorTags.NormalizeListTag(tag);
            if (normalized is not null && !tags.Contains(normalized))
                tags.Add(normalized);
        }
        ColorTags = Join(tags);

        Kinds.Clear();
        if (hasDialogue) Kinds.Add("dialogue");
        if (hasChoiceOptions || Node.HasChoices) Kinds.Add("choice");
        if (hasCondition) Kinds.Add("condition");
        if (hasVariable) Kinds.Add("variable");
        if (hasScript) Kinds.Add("script");
        if (hasTransition) Kinds.Add("transition");
        if (hasCamera) Kinds.Add("camera");
        if (hasAudioTrack) Kinds.Add("audio");
        // Jump = pure routing node: it forwards flow elsewhere without
        // player-facing content of its own (no dialogue text, no choices).
        if (outgoingEdges > 0 && !hasChoiceOptions && !Node.HasChoices && !hasDialogueText)
            Kinds.Add("jump");
        KindsText = Kinds.Count == 0 ? string.Empty : string.Join(" ", Kinds.OrderBy(k => k));
    }

    public string FieldText(NodeSearchField field) => field switch
    {
        NodeSearchField.Title => Title,
        NodeSearchField.NodeId => NodeIdText,
        NodeSearchField.Speaker => Speakers,
        NodeSearchField.DialogueText => DialogueTexts,
        NodeSearchField.ChoiceOption => ChoiceTexts,
        NodeSearchField.ContentId => ContentIds,
        NodeSearchField.Variable => Variables,
        NodeSearchField.Condition => Conditions,
        NodeSearchField.Asset => Assets,
        NodeSearchField.Script => Scripts,
        NodeSearchField.ColorTag => ColorTags,
        NodeSearchField.NodeKind => KindsText,
        _ => string.Empty,
    };

    private static void Add(List<string> sink, string? value)
    {
        if (!string.IsNullOrWhiteSpace(value))
            sink.Add(value);
    }

    private static string Join(List<string> values)
    {
        if (values.Count == 0)
            return string.Empty;
        if (values.Count == 1)
            return values[0];
        var builder = new StringBuilder();
        for (int i = 0; i < values.Count; i++)
        {
            if (i > 0) builder.Append(" | ");
            builder.Append(values[i]);
        }
        return builder.ToString();
    }
}
