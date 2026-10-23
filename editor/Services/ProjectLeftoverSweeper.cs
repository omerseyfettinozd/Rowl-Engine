using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;

namespace RowlEngine.Editor.Services;

/// <summary>
/// C04 — kill/çökme sonrası kalan, editörün kendi ürettiği geçici girdileri
/// proje ağacı içinde temizler. YALNIZ tam üretim kalıpları (32 küçük hex guid)
/// silinir; kullanıcı dosyaları (notes.part, song.ogg.backup...) asla.
/// Kalıplar: WriteAllTextAtomically <c>.ad.guid.tmp</c>, dönüştürücü
/// <c>hedef.guid.part</c> (+ <c>.rowlconv.json</c>), içe aktarma
/// <c>.ad.rowlimport.part</c>, derleme <c>.ad.guid.building</c> (dizin),
/// Farklı Kaydet <c>hedef.guid.partial</c> (dizin). Sembolik bağlar izlenmez.
/// Yalnız kök (özyinelemesiz) ve <c>Assets/</c> ağacı taranır: geliştirmede kök
/// depo köküne düşebilir; build/, .git/ gibi büyük ağaçlar her açılışta gezilmez.
/// </summary>
internal static class ProjectLeftoverSweeper
{
    private const string Hex = "[0-9a-f]{32}";
    private static readonly Regex[] FilePatterns =
    {
        new($@"^\..+\.{Hex}\.tmp$", RegexOptions.CultureInvariant),
        new($@"^.+\.{Hex}\.part$", RegexOptions.CultureInvariant),
        new($@"^.+\.{Hex}\.part\.rowlconv\.json$", RegexOptions.CultureInvariant),
        new(@"^\..+\.rowlimport\.part$", RegexOptions.CultureInvariant),
    };
    private static readonly Regex[] DirectoryPatterns =
    {
        new($@"^\..+\.{Hex}\.building$", RegexOptions.CultureInvariant),
        new($@"^.+\.{Hex}\.partial$", RegexOptions.CultureInvariant),
    };

    /// <summary>Silinen yolların listesini döner; hata yutulur (proje açılışı bozulmaz).</summary>
    public static IReadOnlyList<string> Sweep(string projectRoot, Action<string>? log = null)
    {
        var removed = new List<string>();
        try
        {
            if (!Directory.Exists(projectRoot)) return removed;
            Walk(projectRoot, removed, log, recurse: false);
            string assets = Path.Combine(projectRoot, "Assets");
            if (Directory.Exists(assets)
                && (File.GetAttributes(assets) & FileAttributes.ReparsePoint) == 0)
                Walk(assets, removed, log, recurse: true);
        }
        catch (Exception) { }
        return removed;
    }

    private static void Walk(string dir, List<string> removed, Action<string>? log, bool recurse)
    {
        string[] entries;
        try { entries = Directory.GetFileSystemEntries(dir); }
        catch (Exception) { return; }

        foreach (string path in entries)
        {
            try
            {
                var attrs = File.GetAttributes(path);
                if ((attrs & FileAttributes.ReparsePoint) != 0) continue;
                string name = Path.GetFileName(path);
                if ((attrs & FileAttributes.Directory) != 0)
                {
                    if (Matches(DirectoryPatterns, name))
                    {
                        Directory.Delete(path, recursive: true);
                        Record(path, removed, log);
                    }
                    else if (recurse)
                    {
                        Walk(path, removed, log, recurse: true);
                    }
                }
                else if (Matches(FilePatterns, name))
                {
                    File.Delete(path);
                    Record(path, removed, log);
                }
            }
            catch (Exception) { }
        }
    }

    private static bool Matches(Regex[] patterns, string name)
    {
        foreach (var p in patterns)
            if (p.IsMatch(name)) return true;
        return false;
    }

    private static void Record(string path, List<string> removed, Action<string>? log)
    {
        removed.Add(path);
        log?.Invoke($"[TEMİZLİK] Yarım kalmış geçici girdi silindi: {path}");
    }
}
