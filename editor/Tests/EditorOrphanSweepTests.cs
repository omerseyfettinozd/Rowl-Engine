using System;
using System.IO;
using System.Linq;
using RowlEngine.Editor.Services;
using Xunit;

namespace RowlEngine.Editor.Tests;

/// <summary>
/// C04 — kill/çökme sonrası kalan üretilmiş geçici girdiler proje açılışında
/// temizlenir; yalnız tam kalıba uyanlar (guid biçimli) silinir.
/// </summary>
public sealed class EditorOrphanSweepTests : IDisposable
{
    private readonly string _root = Path.Combine(Path.GetTempPath(), "RowlOrphan_" + Guid.NewGuid().ToString("N"));
    private static string G() => Guid.NewGuid().ToString("N");

    public EditorOrphanSweepTests() => Directory.CreateDirectory(_root);

    public void Dispose()
    {
        try { Directory.Delete(_root, recursive: true); } catch (Exception) { }
    }

    private string Touch(string rel)
    {
        string p = Path.Combine(_root, rel);
        Directory.CreateDirectory(Path.GetDirectoryName(p)!);
        File.WriteAllText(p, "x");
        return p;
    }

    [Fact]
    public void Sweep_RemovesOnlyGeneratedLeftovers()
    {
        string g = G();
        var generated = new[]
        {
            Touch($"Assets/json/.full_story_graph.json.{g}.tmp"),
            Touch($"Assets/images/hero.webp.{g}.part"),
            Touch($"Assets/images/hero.webp.{g}.part.rowlconv.json"),
            Touch("Assets/images/.bg.png.rowlimport.part"),
        };
        string building = Path.Combine(_root, $".Game.{g}.building");
        Directory.CreateDirectory(building);
        File.WriteAllText(Path.Combine(building, "a.bin"), "x");
        string partial = Path.Combine(_root, $"Proj.{g}.partial");
        Directory.CreateDirectory(partial);

        var keep = new[]
        {
            Touch("notes.part"),
            Touch("song.ogg.backup"),
            Touch($"Assets/hero.webp.{g}.partx"),
            Touch($"Assets/readme.{g.ToUpperInvariant()}.part"),   // büyük harf: bizim üretimimiz değil
            Touch($"Assets/short.abc123.part"),
            Touch($"x.{g}.tmp"),                                    // noktasız ad: WriteAllTextAtomically üretmez
            Touch("Assets/hero.webp.rowlconv.json"),
            Touch("project.rowlproj"),
            // Kök dışı alt ağaçlar (build/, .git/) gezilmez: geliştirmede kök depo köküdür.
            Touch($"build/obj/.x.json.{g}.tmp"),
        };
        string keepDir = Path.Combine(_root, "Saves.partial");
        Directory.CreateDirectory(keepDir);

        var removed = ProjectLeftoverSweeper.Sweep(_root);

        Assert.All(generated, p => Assert.False(File.Exists(p), p));
        Assert.False(Directory.Exists(building));
        Assert.False(Directory.Exists(partial));
        Assert.All(keep, p => Assert.True(File.Exists(p), p));
        Assert.True(Directory.Exists(keepDir));
        Assert.Equal(generated.Length + 2, removed.Count);
    }

    [Fact]
    public void Sweep_LogsEachRemoval_AndMissingRootIsNoOp()
    {
        Touch($"Assets/.a.json.{G()}.tmp");
        var log = new System.Collections.Generic.List<string>();
        var removed = ProjectLeftoverSweeper.Sweep(_root, log.Add);
        Assert.Single(removed);
        Assert.Single(log);
        Assert.Empty(ProjectLeftoverSweeper.Sweep(Path.Combine(_root, "nope")));
    }

    [Fact]
    public void Sweep_DoesNotFollowSymlinks()
    {
        string outside = Path.Combine(Path.GetTempPath(), "RowlOrphanOut_" + G());
        Directory.CreateDirectory(outside);
        string victim = Path.Combine(outside, $".v.{G()}.tmp");
        File.WriteAllText(victim, "x");
        try
        {
            Directory.CreateSymbolicLink(Path.Combine(_root, "link"), outside);
        }
        catch (Exception) { Directory.Delete(outside, true); return; } // sembolik bağ kurulamadı
        ProjectLeftoverSweeper.Sweep(_root);
        Assert.True(File.Exists(victim));
        Directory.Delete(outside, true);
    }
}
