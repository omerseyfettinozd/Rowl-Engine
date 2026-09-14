namespace RowlEngine.Editor.Services.Search;

/// <summary>
/// Faz 4 Dilim 2 — searchable node field taxonomy. Every value maps to one
/// snapshot string inside <see cref="NodeSearchDocument"/>; the label is the
/// Turkish badge shown in the result list.
/// </summary>
public enum NodeSearchField
{
    Title,
    NodeId,
    Speaker,
    DialogueText,
    ChoiceOption,
    ContentId,
    Variable,
    Condition,
    Asset,
    Script,
    ColorTag,
    NodeKind,
}

public static class NodeSearchFieldLabels
{
    public static string Label(NodeSearchField field) => field switch
    {
        NodeSearchField.Title => "Başlık",
        NodeSearchField.NodeId => "Kimlik",
        NodeSearchField.Speaker => "Konuşmacı",
        NodeSearchField.DialogueText => "Diyalog",
        NodeSearchField.ChoiceOption => "Seçenek",
        NodeSearchField.ContentId => "İçerik Kimliği",
        NodeSearchField.Variable => "Değişken",
        NodeSearchField.Condition => "Koşul",
        NodeSearchField.Asset => "Asset",
        NodeSearchField.Script => "Script",
        NodeSearchField.ColorTag => "Renk Etiketi",
        NodeSearchField.NodeKind => "Tür",
        _ => field.ToString(),
    };

    /// <summary>Lower weight wins: title/id matches outrank body matches.</summary>
    public static int Weight(NodeSearchField field) => field switch
    {
        NodeSearchField.Title => 0,
        NodeSearchField.NodeId => 0,
        NodeSearchField.Speaker => 1,
        NodeSearchField.ContentId => 1,
        NodeSearchField.ColorTag => 1,
        NodeSearchField.NodeKind => 1,
        NodeSearchField.DialogueText => 2,
        NodeSearchField.ChoiceOption => 2,
        NodeSearchField.Variable => 3,
        NodeSearchField.Condition => 3,
        NodeSearchField.Asset => 4,
        NodeSearchField.Script => 4,
        _ => 5,
    };
}
