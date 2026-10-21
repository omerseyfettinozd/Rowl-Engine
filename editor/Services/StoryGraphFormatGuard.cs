using System;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Text.Json;
using RowlEngine.Editor.Services.Strings;

namespace RowlEngine.Editor.Services;

/// <summary>
/// C04 — daha yeni bir Rowl sürümünün yazdığı hikâye grafiğini (format_version &gt;
/// mevcut) korur: yükleme reddedilir, kayıt yolları dosyaya dokunmaz.
/// Dosya akış okuyucuyla taranır (DOM yok); (uzunluk, zaman) önbelleği
/// otomatik kaydın her turunda yeniden taramayı önler.
/// </summary>
internal static class StoryGraphFormatGuard
{
    private static readonly ConcurrentDictionary<string, (long Length, long Ticks, int Version)> Cache = new();

    public static string Message(int version) => string.Format(
        CultureInfo.CurrentCulture,
        EditorStrings.T("StoryGraph.Load.NewerFormat"),
        version.ToString(CultureInfo.InvariantCulture));

    /// <summary>Dosyanın biçim sürümü mevcut sürümden yeniyse true.</summary>
    public static bool IsNewerFile(string path, out int version)
    {
        version = 0;
        try
        {
            var info = new FileInfo(path);
            if (!info.Exists) return false;
            string key = info.FullName;
            if (Cache.TryGetValue(key, out var hit) && hit.Length == info.Length && hit.Ticks == info.LastWriteTimeUtc.Ticks)
            {
                version = hit.Version;
                return version > GraphStructureLimits.CurrentVersion;
            }
            version = ReadVersion(path);
            Cache[key] = (info.Length, info.LastWriteTimeUtc.Ticks, version);
            return version > GraphStructureLimits.CurrentVersion;
        }
        catch (Exception)
        {
            version = 0;
            return false;
        }
    }

    /// <summary>Kanonik (Assets/json) ve eski (Assets/) kopyadan biri yeniyse true.</summary>
    public static bool IsNewerOnDisk(string assetsPath, string assetsJsonPath, out int version)
    {
        if (IsNewerFile(Path.Combine(assetsJsonPath, "full_story_graph.json"), out version)) return true;
        return !string.IsNullOrEmpty(assetsPath)
            && IsNewerFile(Path.Combine(assetsPath, "full_story_graph.json"), out version);
    }

    /// <summary>Yalnız json dizini bilinen kayıt yolları için: üst dizin eski kopyayı tutar.</summary>
    public static bool IsNewerOnDisk(string assetsJsonPath, out int version)
    {
        string? assetsPath = string.Equals(Path.GetFileName(assetsJsonPath), "json", StringComparison.OrdinalIgnoreCase)
            ? Path.GetDirectoryName(assetsJsonPath)
            : null;
        return IsNewerOnDisk(assetsPath ?? string.Empty, assetsJsonPath, out version);
    }

    private static int ReadVersion(string path)
    {
        var reader = new Utf8JsonReader(File.ReadAllBytes(path));
        if (!reader.Read() || reader.TokenType != JsonTokenType.StartObject) return 0;
        while (reader.Read() && reader.TokenType == JsonTokenType.PropertyName)
        {
            bool match = reader.ValueTextEquals("format_version");
            if (!reader.Read()) break;
            if (match)
                return reader.TokenType == JsonTokenType.Number && reader.TryGetInt32(out int v) ? v : 0;
            reader.Skip();
        }
        return 0;
    }
}
