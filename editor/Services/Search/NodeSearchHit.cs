using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services.Search;

/// <summary>
/// Faz 4 Dilim 2 — one ranked search hit. <see cref="Field"/> names the
/// matched area (shown as a badge) and <see cref="Snippet"/> carries a short
/// excerpt of the matching text.
/// </summary>
public sealed class NodeSearchHit
{
    public NodeSearchHit(NodeViewModel node, NodeSearchField field, string snippet)
    {
        Node = node;
        Field = field;
        Snippet = snippet;
    }

    public NodeViewModel Node { get; }

    public NodeSearchField Field { get; }

    public string FieldLabel => NodeSearchFieldLabels.Label(Field);

    public string Snippet { get; }

    public ulong NodeId => Node.Id;

    public string NodeTitle => Node.Title;
}
