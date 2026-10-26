using System;
using System.Text;

namespace RowlEngine.Editor.Services.Extras;

/// <summary>
/// The rules the native engine applies to <c>Assets/json/extras.json</c> (D07-1,
/// <c>engine/src/state/extras_catalog.cpp</c>), restated for the editor so a catalog the
/// editor accepts is a catalog the player loads in full. A parity test
/// (<c>EditorD073ExtrasAuthoringTests</c>) feeds the same documents to both sides.
/// </summary>
public static class ExtrasRules
{
    public const int Format = 1;
    /// <summary>Largest accepted extras.json in bytes; a bigger file is ignored as a whole.</summary>
    public const int MaxFileBytes = 256 * 1024;
    /// <summary>Per list; surplus items are skipped by the engine.</summary>
    public const int MaxItemsPerList = 256;
    public const int MaxIdLength = 64;
    public const int MaxTextBytes = 512;
    public const int MaxLanguageTagBytes = 16;
    public const int MaxPathBytes = 260;

    public const string KindCg = "cg";
    public const string KindMusic = "music";
    public const string KindAchievement = "achievement";

    /// <summary>The catalog path inside a project: <c>Assets/json/extras.json</c>.</summary>
    public const string CatalogFileName = "extras.json";

    /// <summary>True for <c>[a-z0-9_.-]{1,64}</c>.</summary>
    public static bool IsValidId(string? id)
    {
        if (string.IsNullOrEmpty(id) || id.Length > MaxIdLength) return false;
        foreach (char c in id)
        {
            bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    /// <summary>
    /// A VFS-relative media path: non-empty, at most 260 UTF-8 bytes, no control bytes, not
    /// absolute (leading <c>/</c> or <c>\</c>, drive letter), no <c>..</c> segment.
    /// </summary>
    public static bool IsSafeMediaPath(string? path)
    {
        if (string.IsNullOrEmpty(path)) return false;
        if (Encoding.UTF8.GetByteCount(path) > MaxPathBytes) return false;
        foreach (char c in path)
        {
            if (c < 0x20 || c == 0x7f) return false;
        }
        if (path[0] == '/' || path[0] == '\\') return false;
        if (path.Length >= 2 && path[1] == ':') return false;
        foreach (string segment in path.Replace('\\', '/').Split('/'))
        {
            if (segment == "..") return false;
        }
        return true;
    }

    public static bool IsTextWithinLimit(string text) => Encoding.UTF8.GetByteCount(text) <= MaxTextBytes;

    public static bool IsLanguageTagWithinLimit(string tag) => Encoding.UTF8.GetByteCount(tag) <= MaxLanguageTagBytes;

    /// <summary>
    /// The form a media path is compared by (the same as the native <c>canonicalMediaPath</c>,
    /// D07-1): <c>\</c> becomes <c>/</c>, ASCII letters are lowercased, leading <c>./</c> and
    /// <c>/</c> go, then one <c>Assets/</c> and one <paramref name="stripPrefix"/>
    /// (<c>images/</c> for CG, <c>audio/</c> for music) are dropped. So <c>bogaz.jpg</c>,
    /// <c>images/bogaz.jpg</c> and <c>Assets/images/Bogaz.JPG</c> are one asset.
    /// </summary>
    public static string CanonicalMediaPath(string? path, string stripPrefix)
    {
        if (string.IsNullOrEmpty(path)) return string.Empty;
        var builder = new StringBuilder(path.Length);
        foreach (char c in path)
        {
            char mapped = c == '\\' ? '/' : c;
            builder.Append(mapped >= 'A' && mapped <= 'Z' ? (char)(mapped - 'A' + 'a') : mapped);
        }
        string text = builder.ToString();
        int start = 0;
        while (text.Length - start >= 2 && text[start] == '.' && text[start + 1] == '/') start += 2;
        while (start < text.Length && text[start] == '/') start++;
        string view = text[start..];

        static string Strip(string value, string prefix) =>
            prefix.Length > 0 && value.Length > prefix.Length && value.StartsWith(prefix, StringComparison.Ordinal)
                ? value[prefix.Length..]
                : value;

        view = Strip(view, "assets/");
        view = Strip(view, stripPrefix.ToLowerInvariant());
        return view;
    }

    /// <summary>Maps a JSON <c>kind</c> to its canonical-prefix argument; null for an unknown kind.</summary>
    public static bool IsKnownKind(string? kind) =>
        kind == KindCg || kind == KindMusic || kind == KindAchievement;
}
