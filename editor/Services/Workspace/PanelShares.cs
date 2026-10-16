using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D5 — last known size of closed side panels, so a panel that is opened
/// again returns to the share the user had dragged it to (instead of the
/// default). A share is the fraction of its parent split's area the panel took
/// (0 &lt; share &lt; 1, along the split's axis). Immutable with value equality
/// (sorted by panel id) so <see cref="EditorWorkspaceState"/> still compares
/// structurally and a no-op change keeps the state equal.
/// </summary>
public sealed class PanelShares : IReadOnlyCollection<KeyValuePair<string, double>>, IEquatable<PanelShares>
{
    private readonly KeyValuePair<string, double>[] _items;

    public static readonly PanelShares Empty = new(Array.Empty<KeyValuePair<string, double>>());

    /// <summary>Panels whose share is remembered (the ones that close and reopen as canonical side panels).</summary>
    public static readonly IReadOnlyList<string> Remembered = new[] { PanelIds.Hierarchy, PanelIds.Inspector, PanelIds.Bottom };

    private PanelShares(KeyValuePair<string, double>[] sorted) => _items = sorted;

    public int Count => _items.Length;

    public bool TryGet(string panelId, out double share)
    {
        foreach (KeyValuePair<string, double> item in _items)
        {
            if (string.Equals(item.Key, panelId, StringComparison.Ordinal))
            {
                share = item.Value;
                return true;
            }
        }
        share = 0;
        return false;
    }

    /// <summary>Same instance when the value is unchanged.</summary>
    public PanelShares With(string panelId, double share)
    {
        if (TryGet(panelId, out double current) && current == share)
            return this;
        return new(_items.Where(i => !string.Equals(i.Key, panelId, StringComparison.Ordinal))
            .Append(new KeyValuePair<string, double>(panelId, share))
            .OrderBy(i => i.Key, StringComparer.Ordinal)
            .ToArray());
    }

    public IEnumerator<KeyValuePair<string, double>> GetEnumerator() =>
        ((IEnumerable<KeyValuePair<string, double>>)_items).GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    public bool Equals(PanelShares? other) =>
        other is not null && _items.SequenceEqual(other._items);

    public override bool Equals(object? obj) => Equals(obj as PanelShares);

    public override int GetHashCode()
    {
        var hash = new HashCode();
        foreach (KeyValuePair<string, double> item in _items)
        {
            hash.Add(item.Key, StringComparer.Ordinal);
            hash.Add(item.Value);
        }
        return hash.ToHashCode();
    }

    public override string ToString() =>
        "{" + string.Join(", ", _items.Select(i => i.Key + "=" + i.Value.ToString(CultureInfo.InvariantCulture))) + "}";
}
