using System;
using System.Collections.Generic;

namespace RowlEngine.Editor.Services;

/// <summary>
/// Faz 4 Dilim 1 — uniform-grid spatial index over canvas rectangles.
/// Backs viewport culling: only nodes (and their incident wires) inside
/// the visible rect get Avalonia controls. Grid lookup keeps pan/zoom
/// filtering proportional to visible items instead of the whole graph.
/// Pure value math (no Avalonia/VM types) so the full contract is
/// headless-testable.
/// </summary>
public sealed class CanvasSpatialIndex
{
    /// <summary>Cell edge in canvas pixels (a node card is ~300 px wide).</summary>
    public const double CellSize = 512.0;

    private readonly struct Entry
    {
        public readonly ulong Id;
        public readonly double X;
        public readonly double Y;
        public readonly double Width;
        public readonly double Height;

        public Entry(ulong id, double x, double y, double width, double height)
        {
            Id = id;
            X = x;
            Y = y;
            Width = width;
            Height = height;
        }
    }

    private readonly Dictionary<ulong, Entry> _entries = new();
    private readonly Dictionary<(long, long), HashSet<ulong>> _cells = new();

    /// <summary>Number of indexed rectangles.</summary>
    public int Count => _entries.Count;

    /// <summary>Adds or replaces one rectangle (non-finite inputs are rejected).</summary>
    public bool Add(ulong id, double x, double y, double width, double height)
    {
        if (!IsFiniteRect(x, y, width, height) || width <= 0 || height <= 0)
            return false;
        Remove(id);
        var entry = new Entry(id, x, y, width, height);
        _entries[id] = entry;
        foreach (var cell in CoveredCells(entry))
        {
            if (!_cells.TryGetValue(cell, out var bucket))
            {
                bucket = new HashSet<ulong>();
                _cells[cell] = bucket;
            }
            bucket.Add(id);
        }
        return true;
    }

    /// <summary>Moves an indexed rectangle (no-op for unknown ids).</summary>
    public bool Move(ulong id, double x, double y, double width, double height) =>
        _entries.ContainsKey(id) && Add(id, x, y, width, height);

    /// <summary>Removes one rectangle (no-op for unknown ids).</summary>
    public bool Remove(ulong id)
    {
        if (!_entries.TryGetValue(id, out var entry))
            return false;
        _entries.Remove(id);
        foreach (var cell in CoveredCells(entry))
        {
            if (_cells.TryGetValue(cell, out var bucket))
            {
                bucket.Remove(id);
                if (bucket.Count == 0)
                    _cells.Remove(cell);
            }
        }
        return true;
    }

    public void Clear()
    {
        _entries.Clear();
        _cells.Clear();
    }

    /// <summary>
    /// Returns ids whose rectangles intersect the query rect (touching
    /// edges count as visible so cards are never clipped by a pixel).
    /// Non-finite or empty queries yield an empty list, never throw.
    /// </summary>
    public List<ulong> Query(double x, double y, double width, double height)
    {
        var result = new List<ulong>();
        if (!IsFiniteRect(x, y, width, height) || width <= 0 || height <= 0)
            return result;
        double maxX = x + width;
        double maxY = y + height;
        var seen = new HashSet<ulong>();
        foreach (var cell in CoveredCells(x, y, maxX, maxY))
        {
            if (!_cells.TryGetValue(cell, out var bucket))
                continue;
            foreach (ulong id in bucket)
            {
                if (!seen.Add(id) || !_entries.TryGetValue(id, out var entry))
                    continue;
                if (entry.X <= maxX && entry.X + entry.Width >= x &&
                    entry.Y <= maxY && entry.Y + entry.Height >= y)
                    result.Add(id);
            }
        }
        return result;
    }

    private static bool IsFiniteRect(double x, double y, double width, double height) =>
        double.IsFinite(x) && double.IsFinite(y) &&
        double.IsFinite(width) && double.IsFinite(height);

    private static IEnumerable<(long, long)> CoveredCells(Entry entry) =>
        CoveredCells(entry.X, entry.Y, entry.X + entry.Width, entry.Y + entry.Height);

    private static IEnumerable<(long, long)> CoveredCells(
        double minX, double minY, double maxX, double maxY)
    {
        long cellMinX = (long)Math.Floor(minX / CellSize);
        long cellMaxX = (long)Math.Floor(maxX / CellSize);
        long cellMinY = (long)Math.Floor(minY / CellSize);
        long cellMaxY = (long)Math.Floor(maxY / CellSize);
        for (long cx = cellMinX; cx <= cellMaxX; cx++)
            for (long cy = cellMinY; cy <= cellMaxY; cy++)
                yield return (cx, cy);
    }
}
