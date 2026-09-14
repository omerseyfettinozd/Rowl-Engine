using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using RowlEngine.Editor.ViewModels;

namespace RowlEngine.Editor.Services.Search;

/// <summary>Optional hard filters combined with the text query (filter bar).</summary>
public sealed class NodeSearchFilter
{
    public string? NodeKind { get; set; }
    public string? Speaker { get; set; }
    public string? ColorTag { get; set; }

    public bool IsEmpty =>
        string.IsNullOrWhiteSpace(NodeKind) &&
        string.IsNullOrWhiteSpace(Speaker) &&
        string.IsNullOrWhiteSpace(ColorTag);
}

/// <summary>One search execution: ranked hits plus timing metadata.</summary>
public sealed class NodeSearchResult
{
    public NodeSearchResult(
        IReadOnlyList<NodeSearchHit> hits,
        int totalCount,
        HashSet<ulong> matchingNodeIds,
        double elapsedMs)
    {
        Hits = hits;
        TotalCount = totalCount;
        MatchingNodeIds = matchingNodeIds;
        ElapsedMs = elapsedMs;
    }

    /// <summary>Top-ranked hits (capped for list UI).</summary>
    public IReadOnlyList<NodeSearchHit> Hits { get; }

    /// <summary>All matches before the display cap.</summary>
    public int TotalCount { get; }

    /// <summary>Every matching node id (uncapped; drives canvas dimming).</summary>
    public HashSet<ulong> MatchingNodeIds { get; }

    public double ElapsedMs { get; }
}

/// <summary>
/// Faz 4 Dilim 2 — global search index over node snapshots. Pure value
/// search (no Avalonia types): documents are built by the caller, queries
/// scan snapshot strings with ordinal-ignore-case matching. Content
/// subscriptions live in <see cref="NodeSearchService"/>, never here.
/// </summary>
public sealed class NodeSearchIndex
{
    /// <summary>Display cap: the list shows the best hits, never the graph.</summary>
    public const int MaxResults = 200;

    private static readonly NodeSearchField[] AnyFieldOrder =
    {
        NodeSearchField.Title,
        NodeSearchField.NodeId,
        NodeSearchField.Speaker,
        NodeSearchField.DialogueText,
        NodeSearchField.ChoiceOption,
        NodeSearchField.ContentId,
        NodeSearchField.Variable,
        NodeSearchField.Condition,
        NodeSearchField.Asset,
        NodeSearchField.Script,
        NodeSearchField.ColorTag,
    };

    private readonly List<NodeSearchDocument> _documents = new();
    private readonly Dictionary<NodeViewModel, NodeSearchDocument> _byNode = new();

    public int Count => _documents.Count;

    public void Clear()
    {
        _documents.Clear();
        _byNode.Clear();
    }

    public void Upsert(NodeSearchDocument document)
    {
        if (document is null)
            return;
        if (_byNode.TryGetValue(document.Node, out NodeSearchDocument? existing))
        {
            int at = _documents.IndexOf(existing);
            if (at >= 0) _documents[at] = document;
            _byNode[document.Node] = document;
        }
        else
        {
            _byNode[document.Node] = document;
            _documents.Add(document);
        }
    }

    public bool Remove(NodeViewModel node)
    {
        if (node is null || !_byNode.TryGetValue(node, out NodeSearchDocument? document))
            return false;
        _byNode.Remove(node);
        _documents.Remove(document);
        return true;
    }

    public NodeSearchResult Search(string? rawQuery, NodeSearchFilter? filter = null)
    {
        var stopwatch = Stopwatch.StartNew();
        NodeSearchQuery query = NodeSearchQuery.Parse(rawQuery);
        var ranked = new List<(NodeSearchHit hit, int weight)>();
        var matchingIds = new HashSet<ulong>();

        if (!query.IsEmpty || (filter is not null && !filter.IsEmpty))
        {
            foreach (NodeSearchDocument document in _documents)
            {
                if (!PassesFilter(document, filter))
                    continue;
                if (TryMatch(document, query, out NodeSearchHit? hit, out int weight))
                {
                    matchingIds.Add(document.NodeId);
                    ranked.Add((hit!, weight));
                }
                else if (query.IsEmpty)
                {
                    // Filter-only query: every passing node matches; the hit
                    // surfaces the filtered area for the badge/snippet.
                    matchingIds.Add(document.NodeId);
                    ranked.Add((FilterHit(document, filter!), 1));
                }
            }
        }

        ranked.Sort((a, b) =>
        {
            int byWeight = a.weight.CompareTo(b.weight);
            return byWeight != 0 ? byWeight : a.hit.NodeId.CompareTo(b.hit.NodeId);
        });

        var hits = new List<NodeSearchHit>(Math.Min(ranked.Count, MaxResults));
        for (int i = 0; i < ranked.Count && i < MaxResults; i++)
            hits.Add(ranked[i].hit);

        stopwatch.Stop();
        return new NodeSearchResult(hits, ranked.Count, matchingIds, stopwatch.Elapsed.TotalMilliseconds);
    }

    private static bool PassesFilter(NodeSearchDocument document, NodeSearchFilter? filter)
    {
        if (filter is null || filter.IsEmpty)
            return true;
        if (!string.IsNullOrWhiteSpace(filter.NodeKind) &&
            !document.Kinds.Contains(filter.NodeKind.Trim()))
            return false;
        if (!string.IsNullOrWhiteSpace(filter.Speaker) &&
            document.Speakers.IndexOf(filter.Speaker.Trim(), StringComparison.OrdinalIgnoreCase) < 0)
            return false;
        if (!string.IsNullOrWhiteSpace(filter.ColorTag))
        {
            string wanted = NodeColorTags.Normalize(filter.ColorTag);
            if (document.ColorTags.IndexOf(wanted, StringComparison.OrdinalIgnoreCase) < 0)
                return false;
        }
        return true;
    }

    private static bool TryMatch(
        NodeSearchDocument document, NodeSearchQuery query,
        out NodeSearchHit? hit, out int weight)
    {
        hit = null;
        weight = 0;
        if (query.IsEmpty)
            return false;

        NodeSearchField displayField = NodeSearchField.Title;
        int displayIndex = -1;
        int bestWeight = int.MaxValue;

        foreach (NodeSearchToken token in query.Tokens)
        {
            if (!MatchToken(document, token, out NodeSearchField matchedField, out int matchIndex))
                return false; // AND semantics.
            int tokenWeight = NodeSearchFieldLabels.Weight(matchedField);
            if (tokenWeight < bestWeight)
            {
                bestWeight = tokenWeight;
                displayField = matchedField;
                displayIndex = matchIndex;
            }
        }

        string source = document.FieldText(displayField);
        hit = new NodeSearchHit(document.Node, displayField, Snippet(source, displayIndex));
        weight = bestWeight;
        return true;
    }

    private static bool MatchToken(
        NodeSearchDocument document, NodeSearchToken token,
        out NodeSearchField matchedField, out int matchIndex)
    {
        matchedField = NodeSearchField.Title;
        matchIndex = -1;
        if (token.Value.Length == 0)
            return true;

        if (token.Field.HasValue)
        {
            if (token.Field.Value == NodeSearchField.NodeKind)
                return MatchKind(document, token.Value, out matchedField, out matchIndex);
            string source = document.FieldText(token.Field.Value);
            int at = source.IndexOf(token.Value, StringComparison.OrdinalIgnoreCase);
            if (at >= 0)
            {
                matchedField = token.Field.Value;
                matchIndex = at;
                return true;
            }
            // text: also spans choice options (single display field wins).
            if (token.MatchChoiceToo && token.Field.Value == NodeSearchField.DialogueText)
            {
                int optionAt = document.ChoiceTexts.IndexOf(token.Value, StringComparison.OrdinalIgnoreCase);
                if (optionAt >= 0)
                {
                    matchedField = NodeSearchField.ChoiceOption;
                    matchIndex = optionAt;
                    return true;
                }
            }
            return false;
        }

        foreach (NodeSearchField field in AnyFieldOrder)
        {
            string source = document.FieldText(field);
            if (source.Length == 0)
                continue;
            int at = source.IndexOf(token.Value, StringComparison.OrdinalIgnoreCase);
            if (at >= 0)
            {
                matchedField = field;
                matchIndex = at;
                return true;
            }
        }
        return false;
    }

    private static bool MatchKind(
        NodeSearchDocument document, string value,
        out NodeSearchField matchedField, out int matchIndex)
    {
        matchedField = NodeSearchField.NodeKind;
        matchIndex = -1;
        string wanted = value.Trim();
        if (wanted.Length == 0)
            return true;
        foreach (string kind in document.Kinds)
        {
            if (string.Equals(kind, wanted, StringComparison.OrdinalIgnoreCase))
            {
                matchIndex = document.KindsText.IndexOf(kind, StringComparison.OrdinalIgnoreCase);
                return true;
            }
        }
        return false;
    }

    private static NodeSearchHit FilterHit(NodeSearchDocument document, NodeSearchFilter filter)
    {
        if (!string.IsNullOrWhiteSpace(filter.NodeKind))
            return new NodeSearchHit(document.Node, NodeSearchField.NodeKind,
                Snippet(document.KindsText, document.KindsText.IndexOf(
                    filter.NodeKind.Trim(), StringComparison.OrdinalIgnoreCase)));
        if (!string.IsNullOrWhiteSpace(filter.Speaker))
            return new NodeSearchHit(document.Node, NodeSearchField.Speaker,
                Snippet(document.Speakers, document.Speakers.IndexOf(
                    filter.Speaker.Trim(), StringComparison.OrdinalIgnoreCase)));
        return new NodeSearchHit(document.Node, NodeSearchField.ColorTag,
            Snippet(document.ColorTags, 0));
    }

    /// <summary>Single-line excerpt (~±30 chars) around the match; never throws.</summary>
    public static string Snippet(string source, int matchIndex, int matchLength = 1)
    {
        if (string.IsNullOrEmpty(source))
            return string.Empty;
        string flat = source.Replace('\n', ' ').Replace('\r', ' ');
        const int maxLength = 120;
        if (flat.Length <= maxLength && matchIndex < 0)
            return flat;
        if (matchIndex < 0)
            return flat.Length > maxLength ? flat.Substring(0, maxLength) + "…" : flat;
        int start = Math.Max(0, matchIndex - 30);
        int end = Math.Min(flat.Length, matchIndex + matchLength + 30);
        // Grow small windows toward the cap without overshooting.
        while (end - start < maxLength && (start > 0 || end < flat.Length))
        {
            if (start > 0) start = Math.Max(0, start - 10);
            else if (end < flat.Length) end = Math.Min(flat.Length, end + 10);
            else break;
        }
        string excerpt = flat.Substring(start, end - start);
        if (start > 0) excerpt = "…" + excerpt;
        if (end < flat.Length) excerpt += "…";
        return excerpt;
    }
}
