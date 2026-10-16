using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;

namespace RowlEngine.Editor.Services.Workspace;

/// <summary>
/// Faz 5 D4 — immutable set of panel ids with value equality (kept sorted
/// ordinally), so a record holding it (<see cref="EditorWorkspaceState.Pinned"/>)
/// compares structurally and a no-op change keeps the state equal.
/// </summary>
public sealed class PanelSet : IReadOnlyCollection<string>, IEquatable<PanelSet>
{
    private readonly string[] _ids;

    public static readonly PanelSet Empty = new(Array.Empty<string>());

    private PanelSet(string[] sortedDistinct) => _ids = sortedDistinct;

    public static PanelSet Of(IEnumerable<string> ids) =>
        new(ids.Distinct(StringComparer.Ordinal).OrderBy(id => id, StringComparer.Ordinal).ToArray());

    public int Count => _ids.Length;

    public bool Contains(string? id) => id is not null && Array.BinarySearch(_ids, id, StringComparer.Ordinal) >= 0;

    public PanelSet With(string id) => Contains(id) ? this : Of(_ids.Append(id));

    public PanelSet Without(string id) => Contains(id) ? new(_ids.Where(v => !string.Equals(v, id, StringComparison.Ordinal)).ToArray()) : this;

    /// <summary>Only the ids also in <paramref name="keep"/>; the same instance when nothing is dropped.</summary>
    public PanelSet Intersect(IReadOnlyCollection<string> keep)
    {
        if (_ids.All(id => keep.Contains(id, StringComparer.Ordinal)))
            return this;
        return new(_ids.Where(id => keep.Contains(id, StringComparer.Ordinal)).ToArray());
    }

    public IEnumerator<string> GetEnumerator() => ((IEnumerable<string>)_ids).GetEnumerator();

    IEnumerator IEnumerable.GetEnumerator() => GetEnumerator();

    public bool Equals(PanelSet? other) =>
        other is not null && _ids.AsSpan().SequenceEqual(other._ids, StringComparer.Ordinal);

    public override bool Equals(object? obj) => Equals(obj as PanelSet);

    public override int GetHashCode()
    {
        var hash = new HashCode();
        foreach (string id in _ids)
            hash.Add(id, StringComparer.Ordinal);
        return hash.ToHashCode();
    }

    public override string ToString() => "[" + string.Join(", ", _ids) + "]";
}
