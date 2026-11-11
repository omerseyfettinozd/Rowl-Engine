using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace RowlEngine.Editor.Services.Extras;

/// <summary>
/// Reads and writes a project's <c>Assets/json/extras.json</c> (D07-3). Saving is atomic
/// (temporary file + move, the same path every other project document uses) and refuses a
/// catalog the native engine would partly ignore.
/// </summary>
public static partial class ExtrasCatalogStore
{
    private static readonly object CacheGate = new();
    private static string? _cachedPath;
    private static long _cachedLength = -1;
    private static DateTime _cachedWriteUtc;
    private static ExtrasLoadResult? _cachedResult;

    /// <summary><c>&lt;assets&gt;/json/extras.json</c>.</summary>
    public static string PathOf(string assetsPath) =>
        Path.Combine(assetsPath, "json", ExtrasRules.CatalogFileName);

    /// <summary>Reads the catalog. A missing file is an empty model (normal: a game without extras).</summary>
    public static ExtrasLoadResult Load(string assetsPath)
    {
        string path = PathOf(assetsPath);
        try
        {
            if (!File.Exists(path))
                return new ExtrasLoadResult(new ExtrasCatalogModel(), false, null);
            var info = new FileInfo(path);
            if (info.Length > ExtrasRules.MaxFileBytes)
                return new ExtrasLoadResult(null, true, "FileTooLarge");
            return ExtrasCatalogModel.Parse(File.ReadAllText(path, Encoding.UTF8));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return new ExtrasLoadResult(null, true, "Unreadable");
        }
    }

    /// <summary>
    /// The catalog for read-only consumers (the unlock component's id list). Cached by file
    /// length and write time, so a property getter may call it freely. Null when there is no
    /// usable catalog (no file, or one the engine would ignore).
    /// </summary>
    public static ExtrasCatalogModel? ReadShared(string assetsPath)
    {
        string path = PathOf(assetsPath);
        long length = -1;
        DateTime written = default;
        try
        {
            if (File.Exists(path))
            {
                var info = new FileInfo(path);
                length = info.Length;
                written = info.LastWriteTimeUtc;
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            length = -1;
        }

        lock (CacheGate)
        {
            if (_cachedResult is null || _cachedPath != path || _cachedLength != length || _cachedWriteUtc != written)
            {
                _cachedResult = Load(assetsPath);
                _cachedPath = path;
                _cachedLength = length;
                _cachedWriteUtc = written;
            }
            var model = _cachedResult.Model;
            return model is null || model.IsEmpty ? null : model;
        }
    }

    /// <summary>Forgets the cached read (call after the file changed by other means than <see cref="Save"/>).</summary>
    public static void Invalidate()
    {
        lock (CacheGate) _cachedResult = null;
    }

    /// <summary>
    /// Writes the catalog atomically when it passes <see cref="ExtrasCatalogModel.Validate"/>.
    /// Returns the issues that stopped the save (empty = saved).
    /// </summary>
    public static IReadOnlyList<ExtrasIssue> Save(string assetsPath, ExtrasCatalogModel model)
    {
        var issues = model.Validate();
        if (issues.Count > 0) return issues;
        ProjectFileSystem.WriteAllTextAtomically(PathOf(assetsPath), model.ToJson());
        Invalidate();
        RaiseSaved();
        return Array.Empty<ExtrasIssue>();
    }
}
