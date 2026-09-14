using System;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services.Search;

/// <summary>
/// Faz 4 Dilim 2 — parsed search query. Bare tokens match any field;
/// <c>field:value</c> tokens restrict one field. All tokens combine with AND.
/// Unknown prefixes degrade to literal text (never fail-closed).
/// Supported fields: title, id, speaker, text/dialogue/choice/option,
/// content_id (contentid/cid), variable (var), condition (cond),
/// asset (bg/portrait/sound/bgm/sfx), script (lua/code), tag (color/colour),
/// type (kind).
/// </summary>
public sealed class NodeSearchQuery
{
    public static readonly NodeSearchQuery Empty = new(new List<NodeSearchToken>());

    public IReadOnlyList<NodeSearchToken> Tokens { get; }

    public bool IsEmpty => Tokens.Count == 0;

    private NodeSearchQuery(List<NodeSearchToken> tokens) => Tokens = tokens;

    public static NodeSearchQuery Parse(string? raw)
    {
        var tokens = new List<NodeSearchToken>();
        if (string.IsNullOrWhiteSpace(raw))
            return Empty;
        foreach (string part in SplitRespectingQuotes(raw))
        {
            if (part.Length == 0)
                continue;
            int colon = part.IndexOf(':');
            if (colon > 0 && colon < part.Length - 1)
            {
                string prefix = part.Substring(0, colon).ToLowerInvariant();
                string value = Unquote(part.Substring(colon + 1));
                if (value.Length == 0)
                    continue;
                if (TryMapField(prefix, out NodeSearchField field))
                {
                    // text: spans every player-readable string (dialogue +
                    // choice options); dialogue:/choice: stay single-area.
                    tokens.Add(new NodeSearchToken(field, value, matchChoiceToo: prefix == "text"));
                    continue;
                }
            }
            else if (colon == part.Length - 1)
            {
                continue; // "speaker:" with no value: ignore, not literal.
            }
            tokens.Add(new NodeSearchToken(null, Unquote(part)));
        }
        return new NodeSearchQuery(tokens);
    }

    private static bool TryMapField(string prefix, out NodeSearchField field)
    {
        switch (prefix)
        {
            case "title": field = NodeSearchField.Title; return true;
            case "id": field = NodeSearchField.NodeId; return true;
            case "speaker": field = NodeSearchField.Speaker; return true;
            case "text":
            case "dialogue":
            case "dialog": field = NodeSearchField.DialogueText; return true;
            case "choice":
            case "option": field = NodeSearchField.ChoiceOption; return true;
            case "content_id":
            case "contentid":
            case "cid": field = NodeSearchField.ContentId; return true;
            case "variable":
            case "var": field = NodeSearchField.Variable; return true;
            case "condition":
            case "cond": field = NodeSearchField.Condition; return true;
            case "asset":
            case "bg":
            case "portrait":
            case "sound":
            case "bgm":
            case "sfx": field = NodeSearchField.Asset; return true;
            case "script":
            case "lua":
            case "code": field = NodeSearchField.Script; return true;
            case "tag":
            case "color":
            case "colour": field = NodeSearchField.ColorTag; return true;
            case "type":
            case "kind": field = NodeSearchField.NodeKind; return true;
            default: field = default; return false;
        }
    }

    private static string Unquote(string value)
    {
        value = value.Trim();
        if (value.Length >= 2 && value[0] == '"' && value[^1] == '"')
            return value.Substring(1, value.Length - 2);
        return value;
    }

    private static IEnumerable<string> SplitRespectingQuotes(string raw)
    {
        int start = -1;
        bool inQuotes = false;
        for (int i = 0; i < raw.Length; i++)
        {
            char c = raw[i];
            if (c == '"')
            {
                inQuotes = !inQuotes;
                if (start < 0) start = i;
            }
            else if (char.IsWhiteSpace(c) && !inQuotes)
            {
                if (start >= 0)
                {
                    yield return raw.Substring(start, i - start);
                    start = -1;
                }
            }
            else if (start < 0)
            {
                start = i;
            }
        }
        if (start >= 0)
            yield return raw.Substring(start);
    }
}

public sealed class NodeSearchToken
{
    public NodeSearchToken(NodeSearchField? field, string value, bool matchChoiceToo = false)
    {
        Field = field;
        Value = value ?? string.Empty;
        MatchChoiceToo = matchChoiceToo;
    }

    /// <summary>Null means any field.</summary>
    public NodeSearchField? Field { get; }

    public string Value { get; }

    /// <summary>True for the text: prefix (dialogue + choice options).</summary>
    public bool MatchChoiceToo { get; }

    public override string ToString() =>
        Field.HasValue ? $"{Field.Value}:{Value}" : Value;
}
